#pragma once
// WO4-E: regional BUY-NOTHING goods requests. The user supplies a bounded
// material need; candidates draw ONLY from actual original ship cargo.
// These are NOT shipments, reserved stock, an economic trade or a route.
// The original WO5 carrier/ownership/receipt mechanism remains mandatory.
#include "galaxy/WO4MaterialAllocation.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace galaxy::economy04 {
struct MaterialNeed {
    EntityId destination=0; // actual existing owner ship or built outpost
    NativeMaterial material=NativeMaterial::Aluminum;
    Priority purpose=Priority::Production;
    std::int64_t requested_kg=0;
};
struct ProposedMaterialFreight {
    EntityId source_ship=0,destination=0;
    NativeMaterial material=NativeMaterial::Aluminum;
    Priority purpose=Priority::Production;
    std::int64_t proposed_kg=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    bool carrier_route_and_original_host_approval_required=true;
};
struct MaterialRequestBoard {
    PlayerId owner=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    std::vector<MaterialNeed> demands;
    std::vector<ProposedMaterialFreight> unbooked_requests;
    // Exact remaining need after this plan; it is never treated as sourced.
    std::vector<std::int64_t> uncovered_kg;
    bool still_current(const Simulation& world) const {
        return observed_tick==world.tick() &&
               observed_state_hash==world.state_hash();
    }
};
inline std::optional<MaterialRequestBoard> propose_regional_material_freight(
    const Simulation& world,PlayerId owner,
    const std::vector<MaterialNeed>& owner_declared_demands,
    const std::array<std::int64_t,4>& protect_in_each_ship_kg,
    bool trusted_host_authenticated_owner_and_approved_request) {
    if(!trusted_host_authenticated_owner_and_approved_request ||
       !owner || owner==Simulation::kServerAuthority ||
       owner_declared_demands.empty() ||
       owner_declared_demands.size()>512)
        return std::nullopt;
    for(const auto retain:protect_in_each_ship_kg)
        if(retain<0)return std::nullopt;
    MaterialRequestBoard board{};
    board.owner=owner;board.observed_tick=world.tick();
    board.observed_state_hash=world.state_hash();
    board.demands=owner_declared_demands;
    board.uncovered_kg.reserve(board.demands.size());
    for(const auto& demand:board.demands) {
        const auto* at=world.find(demand.destination);
        if(!at || at->owner_id!=owner ||
           demand.requested_kg<=0 ||
           demand.requested_kg>100000000 ||
           (demand.purpose!=Priority::Maintenance &&
            demand.purpose!=Priority::Production &&
            demand.purpose!=Priority::Discretionary) ||
           (at->type!=EntityType::Ship &&
            (at->type!=EntityType::Station ||
             !world.built_outposts().count(demand.destination))) ||
           (at->type==EntityType::Ship &&
             !world.colony_ship(demand.destination)))
            return std::nullopt;
        const auto idx=static_cast<int>(demand.material);
        if(idx<0 || idx>=4)return std::nullopt;
        board.uncovered_kg.push_back(demand.requested_kg);
    }
    // Stable, protected budget per actual source ship and native material.
    std::map<EntityId,std::array<std::int64_t,4>> budget;
    for(const auto& [ship,record]:world.colony_ships()) {
        (void)record;
        const auto* entity=world.find(ship);
        if(!entity || entity->owner_id!=owner)continue;
        const auto hold=world.ship_inventory_snapshot(ship);
        auto& available=budget[ship];
        for(std::size_t i=0;i<available.size();++i) {
            const auto physical=hold.amount(material_codes()[i]);
            if(physical<0)return std::nullopt;
            available[i]=std::max<std::int64_t>(
                0,physical-protect_in_each_ship_kg[i]);
        }
    }
    if(budget.empty())return std::nullopt;
    // A real source-to-its-own-built-storage allocation is already WO4-C,
    // so no duplicate remote freight promise can spend the same stock.
    // Higher owner-declared priority precedes lower, then stable demand
    // insertion order, then stable original source-ship order.
    for(const auto priority:
        {Priority::Maintenance,Priority::Production,Priority::Discretionary}) {
        for(std::size_t d=0;d<board.demands.size();++d) {
            const auto& demand=board.demands[d];
            if(demand.purpose!=priority)continue;
            auto& pending=board.uncovered_kg[d];
            const auto idx=static_cast<std::size_t>(
                static_cast<int>(demand.material));
            for(auto& [ship,available]:budget) {
                if(!pending)break;
                if(ship==demand.destination)continue;
                const auto destination=world.built_outposts().find(
                    demand.destination);
                if(destination!=world.built_outposts().end() &&
                   destination->second.source_colony_ship==ship)
                    continue; // original local physical transfer, not freight
                const auto take=std::min(pending,available[idx]);
                if(take<=0)continue;
                ProposedMaterialFreight request{};
                request.source_ship=ship;
                request.destination=demand.destination;
                request.material=demand.material;
                request.purpose=demand.purpose;
                request.proposed_kg=take;
                request.observed_tick=board.observed_tick;
                request.observed_state_hash=board.observed_state_hash;
                board.unbooked_requests.push_back(request);
                available[idx]-=take;
                pending-=take;
                if(board.unbooked_requests.size()>4096)
                    return std::nullopt;
            }
        }
    }
    return board;
}
} // namespace galaxy::economy04
