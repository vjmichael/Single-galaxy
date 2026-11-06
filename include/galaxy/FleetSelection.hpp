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
        for(const auto& item:list(world)) {
            if(item.ship!=ship || !item.ready)return false;
            piloted_=ship;
            return true;
        }
        return false;
    }

    // Death/destruction/crew reassignment cannot preserve a stale cockpit.
    bool reconcile(const Simulation& world) {
        if(piloted_==home_ || is_ready(world,piloted_))return false;
        piloted_=home_;
        return true;
    }
    bool is_ready(const Simulation& world,EntityId ship) const {
        for(const auto& item:list(world))
            if(item.ship==ship)return item.ready;
        return false;
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
            // Require a real named pilot physically assigned to this hull,
            // not merely a nonzero aggregate from a stale build receipt.
            bool aboard=false;
            for(const auto& [id,n]:world.npc_records()) {
                (void)id;
                if(n.home_ship==ship && n.location==ship &&
                   n.assigned_site==ship && n.role==NpcRole::Ship &&
                   !n.transport_id){aboard=true;break;}
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
