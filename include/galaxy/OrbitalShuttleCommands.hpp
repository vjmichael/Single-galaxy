#pragma once
// PLAY-13D.4C.6: desktop-capable native shuttle command boundary.
// No HTTP, browser protocol, second NPC roster or shadow resource inventory.
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <optional>

namespace galaxy {

enum class OrbitalShuttleAction : std::uint8_t {
    Status, Build, AssignPilot, Load, Unload, Descend, Ascend
};

struct OrbitalShuttleCommand {
    PlayerId owner=0;
    EntityId ship=0;
    PlanetarySiteKey site{};
    OrbitalShuttleAction action=OrbitalShuttleAction::Status;
    std::uint64_t npc=0;
    std::uint64_t request_id=0;
    bool water=true;
    std::int64_t kilograms=0;
};

struct OrbitalShuttleReply {
    bool accepted=false;
    bool mutated=false;
    std::uint64_t generation=0;
    bool present=false;
    PlanetaryShuttleState shuttle{};
};

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
        case OrbitalShuttleAction::Load:
        case OrbitalShuttleAction::Unload:
            if(cmd.npc || cmd.request_id || cmd.kilograms<1 ||
               cmd.kilograms>1000)break;
            accepted=cmd.action==OrbitalShuttleAction::Load?
                world.load_planetary_shuttle(cmd.owner,cmd.ship,cmd.site,
                    cmd.water,cmd.kilograms):
                world.unload_planetary_shuttle(cmd.owner,cmd.ship,cmd.site,
                    cmd.water,cmd.kilograms);
            break;
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
    }
    return reply;
}
} // namespace galaxy
