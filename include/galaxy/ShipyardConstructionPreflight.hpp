#pragma once
// SHIP-01A.0 — READ-ONLY authorization and material/labor admission.
// This is not a shipyard, stock ledger, ship creator, or authority to spend
// materials. Only a later PersistentWorld command may commit construction.
#include "galaxy/Simulation.hpp"
#include <array>
#include <cstdint>
#include <set>

namespace galaxy::industry {
enum class ShipyardBlocker : std::uint8_t {
    None=0, InvalidRequest, InvalidColony, Unauthorized, MissingLifeSupport,
    InsufficientModules, MissingNamedWorkers, WorkerNotAvailable
};
struct ShipyardBuildPreflight {
    ShipyardBlocker blocker=ShipyardBlocker::InvalidRequest;
    EntityId colony=0;
    PlayerId owner=0;
    std::uint64_t order_id=0;
    std::int64_t existing_modules=0;
    std::int64_t required_modules=0;
    std::array<std::uint64_t,2> workers{};
    bool permitted() const noexcept { return blocker==ShipyardBlocker::None; }
};
// Fixed provisional stage-one bill: four REAL pre-fabricated construction
// modules. A future blueprint version will require additional factory-made
// structural components, a power connection, and orbital construction slips.
inline ShipyardBuildPreflight inspect_shipyard_build(
    const Simulation& world,PlayerId authenticated_owner,EntityId colony,
    std::uint64_t order_id,std::array<std::uint64_t,2> workers) {
    ShipyardBuildPreflight result{};
    result.colony=colony;result.owner=authenticated_owner;
    result.order_id=order_id;result.workers=workers;
    result.required_modules=4;
    if(!authenticated_owner || authenticated_owner==Simulation::kServerAuthority ||
       !colony || !order_id || !workers[0] || !workers[1] ||
       workers[0]==workers[1]){
        result.blocker=ShipyardBlocker::InvalidRequest;return result;
    }
    const auto* hull=world.find(colony);
    const auto* stock=world.colony_ship(colony);
    if(!hull || hull->type!=EntityType::Ship || !stock){
        result.blocker=ShipyardBlocker::InvalidColony;return result;
    }
    if(hull->owner_id!=authenticated_owner){
        result.blocker=ShipyardBlocker::Unauthorized;return result;
    }
    if(!world.life_support(colony)){
        result.blocker=ShipyardBlocker::MissingLifeSupport;return result;
    }
    result.existing_modules=stock->modules;
    if(stock->modules<result.required_modules){
        result.blocker=ShipyardBlocker::InsufficientModules;return result;
    }
    const auto& npcs=world.npc_records();
    if(npcs.empty()){
        result.blocker=ShipyardBlocker::MissingNamedWorkers;return result;
    }
    for(const auto id:workers){
        const auto it=npcs.find(id);
        if(it==npcs.end() || it->second.home_ship!=colony){
            result.blocker=ShipyardBlocker::MissingNamedWorkers;return result;
        }
        const auto& person=it->second;
        if(person.location!=colony || person.role!=NpcRole::Ship ||
           person.assigned_site!=colony || person.transport_id ||
           !world.npc_available_for_duty(id)){
            result.blocker=ShipyardBlocker::WorkerNotAvailable;return result;
        }
    }
    result.blocker=ShipyardBlocker::None;
    return result;
}
} // namespace galaxy::industry
