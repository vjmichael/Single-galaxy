#pragma once
// WO4-E: one-owner REGIONAL shortage/supply request board observed from the
// original authoritative Simulation. No regional CheckpointStore writes,
// carrier claims, cargo creation, booking, remote transport or trust escalation.
// Actual routing and physical custody remain WO5/RegionalHostCommands.
#include "galaxy/WO4Allocation.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace galaxy::economy04 {
struct RegionalLocation {
    EntityId source_ship=0;
    EntityId physical_location=0;
    CompartmentKind kind=CompartmentKind::ColonyShip;
    PlanetarySiteKey planetary_key{};
};
struct RegionalShortage {
    RegionalLocation destination{};
    Vital vital=Vital::Food;
    std::uint64_t consumers=0;
    std::int64_t current_kg=0;
    std::int64_t reserve_deficit_kg=0;
    Pressure pressure=Pressure::NoCurrentConsumers;
};
struct CarrierRequest {
    RegionalLocation source{},destination{};
    Vital vital=Vital::Food;
    Priority priority=Priority::VitalCurrentWindow;
    std::int64_t proposed_kg=0; // no reservation; source stock remains local
    std::uint64_t observed_tick=0,observed_state_hash=0;
    bool requires_original_carrier_and_authorization=true;
};
struct RegionalBoard {
    PlayerId owner=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    Policy policy{};
    std::vector<RegionalShortage> shortages;
    std::vector<CarrierRequest> unbooked_carrier_requests;
    bool still_current(const Simulation& world) const {
        return world.tick()==observed_tick &&
               world.state_hash()==observed_state_hash;
    }
};
inline std::optional<RegionalBoard> observe_regional_distribution(
    const Simulation& world,PlayerId owner,Policy policy={}) {
    if(!owner || owner==Simulation::kServerAuthority)
        return std::nullopt;
    RegionalBoard board{};
    board.owner=owner;board.observed_tick=world.tick();
    board.observed_state_hash=world.state_hash();
    board.policy=policy;
    struct Slot {RegionalLocation where;Compartment row;};
    std::vector<Slot> slots;
    // Source ship order (then each original outpost/planet map order) is
    // deterministic; no physically identified bin can belong to two ships.
    for(const auto& [ship_id,ship]:world.colony_ships()) {
        (void)ship;
        const auto* e=world.find(ship_id);
        if(!e || e->owner_id!=owner)continue;
        const auto observed=observe_owner_world(world,owner,ship_id,policy);
        if(!observed || !observed->still_current(world))
            return std::nullopt;
        for(const auto& row:observed->compartments){
            if(slots.size()>=1024)return std::nullopt;
            slots.push_back({{row.source_ship,row.physical_location,
                              row.kind,row.planetary_key},row});
        }
    }
    if(slots.empty())return std::nullopt;
    for(const auto& slot:slots)for(std::size_t v=0;v<3;++v){
        const auto& need=slot.row.needs[v];
        if(!need.reserve_deficit_kg)continue;
        RegionalShortage demand{};
        demand.destination=slot.where;
        demand.vital=static_cast<Vital>(v);
        demand.consumers=slot.row.current_consumers;
        demand.current_kg=need.physically_present_kg;
        demand.reserve_deficit_kg=need.reserve_deficit_kg;
        demand.pressure=need.pressure;
        board.shortages.push_back(demand);
    }
    if(board.shortages.size()>3072)return std::nullopt;
    // The same unit of real source stock must not fund two offered freight
    // requests. Use integer budgets, protected by the WO4-A reserve target.
    for(std::size_t v=0;v<3;++v){
        std::vector<std::int64_t> source_budget(slots.size(),0);
        std::vector<std::int64_t> dest_granted(slots.size(),0);
        for(std::size_t i=0;i<slots.size();++i)
            source_budget[i]=slots[i].row.needs[v].buffer_above_target_kg;
        // Stage 1: critical one-window survival; stage 2: reserve coverage.
        for(int stage=0;stage<2;++stage) {
            for(std::size_t d=0;d<slots.size();++d) {
                const auto& dest=slots[d];
                const auto& needed=dest.row.needs[v];
                if(!needed.reserve_deficit_kg || !dest.row.current_consumers)
                    continue;
                const auto goal=stage==0?
                    needed.one_window_kg:needed.reserve_target_kg;
                // dest_granted <= reserve_deficit, so no overflow here.
                auto missing=std::max<std::int64_t>(0,
                    goal-needed.physically_present_kg-dest_granted[d]);
                if(!missing)continue;
                for(std::size_t s=0;s<slots.size() && missing>0;++s){
                    if(s==d || source_budget[s]<=0)continue;
                    const auto& src=slots[s];
                    // Original intra-outpost ship -> habitat/farm commands
                    // are already in WO4-C; do not offer the same stock
                    // as both a local transfer and a scheduled freight load.
                    const bool original_local=
                        src.where.kind==CompartmentKind::ColonyShip &&
                        src.where.source_ship==dest.where.source_ship &&
                        (dest.where.kind==CompartmentKind::OutpostHabitat ||
                         dest.where.kind==CompartmentKind::OutpostFarm);
                    if(original_local)continue;
                    const auto quantity=std::min(missing,source_budget[s]);
                    if(quantity<=0)continue;
                    CarrierRequest request{};
                    request.source=src.where;
                    request.destination=dest.where;
                    request.vital=static_cast<Vital>(v);
                    request.priority=stage==0?Priority::VitalCurrentWindow:
                                             Priority::VitalReserve;
                    request.proposed_kg=quantity;
                    request.observed_tick=board.observed_tick;
                    request.observed_state_hash=board.observed_state_hash;
                    board.unbooked_carrier_requests.push_back(request);
                    if(board.unbooked_carrier_requests.size()>4096)
                        return std::nullopt;
                    source_budget[s]-=quantity;
                    dest_granted[d]+=quantity;
                    missing-=quantity;
                }
            }
        }
    }
    return board;
}
// In particular a RegionalBoard MUST NEVER be fed directly into the
// regional HostCommands API as though carrier_receipt or physical route
// were already validated; that is a separate host/WO5 operation.
} // namespace galaxy::economy04
