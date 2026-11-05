#pragma once
// PLAY-13D.4C.6: desktop-capable native shuttle command boundary.
// No HTTP, browser protocol, second NPC roster or shadow resource inventory.
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <optional>

namespace galaxy {

enum class OrbitalShuttleAction : std::uint8_t {
    Status, Build, AssignPilot, BookPassenger, ReleasePassenger, Disembark, Reboard, AssignVisitorDuty, ReleaseVisitorDuty, Load, Unload, Descend, Ascend
};

struct OrbitalShuttleCommand {
    PlayerId owner=0;
    EntityId ship=0;
    PlanetarySiteKey site{};
    OrbitalShuttleAction action=OrbitalShuttleAction::Status;
    std::uint64_t npc=0;
    std::uint64_t request_id=0;
    bool water=true; // legacy native callers: true water, false food
    std::int64_t kilograms=0;
    ShuttleCargo cargo=ShuttleCargo::Water; // explicit nutrients only
};

struct OrbitalShuttleReply {
    bool accepted=false;
    bool mutated=false;
    std::uint64_t generation=0;
    bool present=false;
    PlanetaryShuttleState shuttle{};
    std::optional<transport::Manifest> goods_manifest; // authoritative mixed cargo view
};

// A single authenticated, single-item pickup/delivery *step*. It does not
// teleport cargo or schedule a flight. The native vehicle must already be at
// the requested physical berth, and every mutation runs through the existing
// SQLite-authoritative world command. Passenger custody and resource stock
// remain owned exclusively by Simulation.
inline OrbitalShuttleReply execute_shuttle_transport_instruction(
    PersistentWorld& world,PlayerId authenticated,EntityId ship,
    const PlanetarySiteKey& key,const transport::Instruction& instruction) {
    OrbitalShuttleReply reply{};
    reply.generation=world.generation();
    if(!authenticated || authenticated==Simulation::kServerAuthority ||
       instruction.owner!=authenticated || !ship || instruction.goods.size()!=1)
        return reply;
    const auto site=world.world().planetary_sites().find(key);
    if(site==world.world().planetary_sites().end() ||
       site->second.owner!=authenticated || site->second.source_ship!=ship ||
       !world.world().colony_ship(ship))return reply;
    const auto manifest=world.world().planetary_transport_manifest(key);
    if(!manifest || !transport::serves(*manifest,instruction) ||
       manifest->in_transit)return reply;
    const auto& item=instruction.goods.front();
    const bool pickup=instruction.pickup==manifest->berth &&
                      instruction.delivery==manifest->other_end;
    const bool deliver=instruction.delivery==manifest->berth &&
                       instruction.pickup==manifest->other_end;
    if(!pickup && !deliver)return reply;
    bool accepted=false;
    const auto shuttle=world.world().planetary_shuttles().find(key);
    if(shuttle==world.world().planetary_shuttles().end())return reply;
    if(item.kind==transport::GoodKind::Resource) {
        if(item.amount>1000)return reply;
        galaxy::ShuttleCargo good{};
        switch(static_cast<transport::ResourceGood>(item.id)){
        case transport::ResourceGood::Water: good=ShuttleCargo::Water;break;
        case transport::ResourceGood::Food: good=ShuttleCargo::Food;break;
        case transport::ResourceGood::Nutrients: good=ShuttleCargo::Nutrients;break;
        default:return reply;
        }
        accepted=pickup?
            world.load_planetary_shuttle(authenticated,ship,key,good,
                static_cast<std::int64_t>(item.amount)):
            world.unload_planetary_shuttle(authenticated,ship,key,good,
                static_cast<std::int64_t>(item.amount));
    }else if(item.kind==transport::GoodKind::Npc){
        // No direct roster editing. Reuse named passenger booking / physical
        // disembarkation and their existing custody/guardian checks.
        if(shuttle->second.flight_phase==0 && pickup)
            accepted=world.book_planetary_shuttle_passenger(
                authenticated,ship,key,item.id,true);
        // Orbital cancellation is a separate RELEASE command, not proof
        // that a passenger completed travel back to the ship.
        else if(shuttle->second.flight_phase==2)
            accepted=world.transfer_planetary_shuttle_visitor(
                authenticated,ship,key,item.id,pickup);
    }
    // Equipment lines have a typed shape but no live physical vehicle adapter
    // yet. Fail closed rather than fabricate a piece of equipment.
    if(!accepted)return reply;
    reply.accepted=true;
    reply.mutated=true;
    reply.generation=world.generation();
    const auto current=world.world().planetary_shuttles().find(key);
    reply.present=current!=world.world().planetary_shuttles().end();
    if(reply.present) {
        reply.shuttle=current->second;
        reply.goods_manifest=world.world().planetary_transport_manifest(key);
    }
    return reply;
}

// Host-authenticated principal and ship are supplied by the trusted desktop
// process/transport, never inferred from user-provided NPC IDs or region text.
// The only write path is PersistentWorld's already journaled operations.
inline OrbitalShuttleReply execute_orbital_shuttle(
    PersistentWorld& world,const OrbitalShuttleCommand& cmd) {
    OrbitalShuttleReply reply{};
    reply.generation=world.generation();
    const auto [body,lat,lon,revision]=cmd.site;
    if(!cmd.owner || cmd.owner==Simulation::kServerAuthority ||
       !cmd.ship || !body || lat>=36 || lon>=72 || revision!=1)
        return reply;
    const auto& simulation=world.world();
    const auto site=simulation.planetary_sites().find(cmd.site);
    if(site==simulation.planetary_sites().end() ||
       site->second.owner!=cmd.owner || site->second.source_ship!=cmd.ship)
        return reply;
    if(cmd.action!=OrbitalShuttleAction::Status &&
       !simulation.colony_ship(cmd.ship))return reply;
    bool accepted=false;
    switch(cmd.action) {
        case OrbitalShuttleAction::Status:
            accepted=cmd.npc==0 && cmd.request_id==0 && cmd.kilograms==0;
            break;
        case OrbitalShuttleAction::Build:
            if(cmd.npc || cmd.request_id || cmd.kilograms)break;
            accepted=world.build_planetary_shuttle(cmd.owner,cmd.ship,cmd.site);
            break;
        case OrbitalShuttleAction::AssignPilot:
            if(!cmd.npc || cmd.request_id || cmd.kilograms)break;
            accepted=world.assign_planetary_shuttle_pilot(
                cmd.owner,cmd.ship,cmd.site,cmd.npc);
            break;
        case OrbitalShuttleAction::BookPassenger:
        case OrbitalShuttleAction::ReleasePassenger:
            if(!cmd.npc || cmd.request_id || cmd.kilograms)break;
            if(cmd.action==OrbitalShuttleAction::BookPassenger){
                const auto view=simulation.planetary_transport_manifest(cmd.site);
                if(!view || !view->vehicle)return reply;
                const transport::Instruction order{
                    world.generation()+1,cmd.owner,view->vehicle,
                    view->berth,view->other_end,
                    {{transport::GoodKind::Npc,cmd.npc,1,0}}};
                return execute_shuttle_transport_instruction(
                    world,cmd.owner,cmd.ship,cmd.site,order);
            }
            accepted=world.book_planetary_shuttle_passenger(
                cmd.owner,cmd.ship,cmd.site,cmd.npc,false);
            break;
        case OrbitalShuttleAction::Disembark:
        case OrbitalShuttleAction::Reboard:
            if(!cmd.npc || cmd.request_id || cmd.kilograms)break;
            {
                const auto view=simulation.planetary_transport_manifest(cmd.site);
                if(!view || !view->vehicle)return reply;
                const bool pickup=cmd.action==OrbitalShuttleAction::Reboard;
                const transport::Instruction order{
                    world.generation()+1,cmd.owner,view->vehicle,
                    pickup?view->berth:view->other_end,
                    pickup?view->other_end:view->berth,
                    {{transport::GoodKind::Npc,cmd.npc,1,0}}};
                return execute_shuttle_transport_instruction(
                    world,cmd.owner,cmd.ship,cmd.site,order);
            }
        case OrbitalShuttleAction::AssignVisitorDuty:
        case OrbitalShuttleAction::ReleaseVisitorDuty:
            if(!cmd.npc || cmd.request_id || cmd.kilograms)break;
            accepted=world.assign_planetary_shuttle_visitor_duty(
                cmd.owner,cmd.ship,cmd.site,cmd.npc,
                cmd.action==OrbitalShuttleAction::AssignVisitorDuty);
            break;
        case OrbitalShuttleAction::Load:
        case OrbitalShuttleAction::Unload:
            if(cmd.npc || cmd.request_id || cmd.kilograms<1 ||
               cmd.kilograms>1000)break;
            const auto kind=cmd.cargo==ShuttleCargo::Nutrients?
                ShuttleCargo::Nutrients:
                (cmd.water?ShuttleCargo::Water:ShuttleCargo::Food);
            {
                const auto view=simulation.planetary_transport_manifest(cmd.site);
                if(!view || !view->vehicle)return reply;
                const bool pickup=cmd.action==OrbitalShuttleAction::Load;
                const std::uint64_t resource=
                    kind==ShuttleCargo::Water?
                        static_cast<std::uint8_t>(transport::ResourceGood::Water):
                    kind==ShuttleCargo::Food?
                        static_cast<std::uint8_t>(transport::ResourceGood::Food):
                        static_cast<std::uint8_t>(transport::ResourceGood::Nutrients);
                const transport::Instruction order{
                    world.generation()+1,cmd.owner,view->vehicle,
                    pickup?view->berth:view->other_end,
                    pickup?view->other_end:view->berth,
                    {{transport::GoodKind::Resource,resource,
                      static_cast<std::uint64_t>(cmd.kilograms),
                      static_cast<std::uint64_t>(cmd.kilograms)}}};
                return execute_shuttle_transport_instruction(
                    world,cmd.owner,cmd.ship,cmd.site,order);
            }
        case OrbitalShuttleAction::Descend:
        case OrbitalShuttleAction::Ascend:
            if(cmd.npc || cmd.kilograms || !cmd.request_id)break;
            accepted=world.launch_planetary_shuttle(cmd.owner,cmd.ship,
                cmd.site,cmd.request_id,
                cmd.action==OrbitalShuttleAction::Ascend);
            break;
        default:
            return reply;
    }
    if(!accepted)return reply;
    reply.accepted=true;
    reply.mutated=cmd.action!=OrbitalShuttleAction::Status;
    reply.generation=world.generation();
    const auto current=world.world().planetary_shuttles().find(cmd.site);
    if(current!=world.world().planetary_shuttles().end()) {
        reply.present=true;
        reply.shuttle=current->second;
        reply.goods_manifest=world.world().planetary_transport_manifest(cmd.site);
    }
    return reply;
}
} // namespace galaxy
