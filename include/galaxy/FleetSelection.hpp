#pragma once

#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <vector>

namespace galaxy {

// SHIP-01D: pilot selection is read-only control-plane state. It must NEVER
// change colony resource authority, NPC ownership, shipyard IDs or the durable
// simulation. Native movement commands still pass through Simulation::submit.
struct FleetShip {
    EntityId ship=0;
    std::uint64_t manufacture_order=0;
    bool home=false,ready=false,selected=false;
    std::int64_t crew=0;
    std::uint64_t anchor_body=0;
    std::int64_t food_kg=0,water_kg=0,oxygen_kg=0;
};

class FleetSelection {
public:
    FleetSelection(PlayerId owner,EntityId home)
        : owner_(owner),home_(home),piloted_(home) {}
    EntityId home() const noexcept {return home_;}
    EntityId piloted() const noexcept {return piloted_;}

    std::vector<FleetShip> list(const Simulation& world) const {
        std::vector<FleetShip> entries;
        if(owner_==0 || owner_==Simulation::kServerAuthority)return entries;
        const auto* home_entity=world.find(home_);
        if(home_entity && home_entity->type==EntityType::Ship &&
           home_entity->owner_id==owner_ &&
           world.colony_ship(home_) && world.life_support(home_))
            entries.push_back(describe(world,home_,0,true));
        // Ships have immutable manufacturing receipts. A stray, foreign,
        // destroyed, or simply uncrewed hull must never become a pilot handle.
        for(const auto& [id,order]:world.freighter_build_orders()) {
            if(order.owner!=owner_ || order.source_ship!=home_ ||
               !order.finished())continue;
            const auto* vessel=world.find(order.launched_ship);
            if(!vessel || vessel->owner_id!=owner_ ||
               vessel->manufactured_order_id!=id)continue;
            entries.push_back(describe(world,order.launched_ship,id,false));
        }
        return entries;
    }

    bool select(const Simulation& world,EntityId ship) {
        if(!is_ready(world,ship))return false;
        piloted_=ship;
        return true;
    }

    // Death/destruction/crew reassignment cannot preserve a stale cockpit.
    bool reconcile(const Simulation& world) {
        if(piloted_==home_ || is_ready(world,piloted_))return false;
        piloted_=home_;
        return true;
    }
    bool is_ready(const Simulation& world,EntityId ship) const {
        if(owner_==0 || owner_==Simulation::kServerAuthority)return false;
        const auto* e=world.find(ship);
        if(!e || e->owner_id!=owner_ || e->type!=EntityType::Ship)return false;
        if(ship==home_)
            return describe(world,home_,0,true).ready;
        // Do not rebuild the entire fleet (or scan all NPCs) every game tick.
        // The immutable hull receipt provides a direct indexed lookup.
        if(!e->manufactured_order_id)return false;
        const auto found=world.freighter_build_orders().find(
            e->manufactured_order_id);
        return found!=world.freighter_build_orders().end() &&
            found->second.owner==owner_ &&
            found->second.source_ship==home_ &&
            found->second.launched_ship==ship &&
            describe(world,ship,found->first,false).ready;
    }
private:
    FleetShip describe(const Simulation& world,EntityId ship,
                       std::uint64_t order,bool home) const {
        FleetShip f{};
        f.ship=ship;f.manufacture_order=order;f.home=home;
        f.selected=ship==piloted_;
        const auto* e=world.find(ship);
        const auto* colony=world.colony_ship(ship);
        const auto* stock=world.life_support(ship);
        if(colony)f.crew=colony->crew;
        if(stock) {
            f.food_kg=stock->food_kg;
            f.water_kg=stock->water_kg;
            f.oxygen_kg=stock->oxygen_kg;
        }
        if(const auto frame=world.ship_frames().find(ship);
           frame!=world.ship_frames().end())f.anchor_body=frame->second.anchor_body;
        if(!e || e->type!=EntityType::Ship ||
           e->owner_id!=owner_ || !colony || colony->crew<=0 ||
           !stock || !world.ship_frames().count(ship))
            return f;
        if(!home) {
            const auto build=world.freighter_build_orders().find(order);
            if(!e->fitting.exploration_scanner() ||
               build==world.freighter_build_orders().end() ||
               !build->second.commissioned() ||
               build->second.launched_ship!=ship ||
               build->second.source_ship!=home_ ||
               build->second.owner!=owner_)return f;
            // SHIP-01B.2 has two immutable *actual* commissioning NPC IDs.
            // Lookup is O(log NPCs) per pilot, not O(all NPCs) on every tick.
            // Future formal crew replacement will update a durable pilot-duty
            // receipt; do not silently accept a headcount without an identity.
            bool aboard=false;
            for(const auto id:build->second.commissioning_crew) {
                const auto n=world.npc_records().find(id);
                if(n!=world.npc_records().end() &&
                   n->second.home_ship==ship && n->second.location==ship &&
                   n->second.assigned_site==ship &&
                   n->second.role==NpcRole::Ship &&
                   !n->second.transport_id){aboard=true;break;}
            }
            if(!aboard)return f;
        }
        f.ready=true;
        return f;
    }

    PlayerId owner_=0;
    EntityId home_=0,piloted_=0;
};
} // namespace galaxy
