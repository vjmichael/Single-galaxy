#pragma once

// WORK-01B.2: read-only, canonical worker qualification and physical
// shipyard capacity. This is a planning index, not a workforce ledger,
// entitlement to consent, a factory simulator or durable material escrow.
#include "galaxy/Simulation.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>

namespace galaxy::work {

enum class Qualification : std::uint8_t {
    Qualified=0,MissingNpc=1,ForeignColony=2,WrongRole=3,
    WrongStation=4,PassengerInTransit=5,ResidentElsewhere=6,
    MentorDuty=7,NativeDutyUnavailable=8,ExistingNativeAssignment=9
};

struct NativeCapacityContext {
    // These point to the SAME Simulation maps used by real shipyard,
    // freighter and NPC authorization. No copied mutable job state.
    const std::map<std::uint64_t,ShipyardConstruction>* yards=nullptr;
    const std::map<std::uint64_t,FreighterBuildOrder>* builds=nullptr;
    // Production binds Simulation::npc_available_for_duty, which checks
    // actual medical fitness and existing shuttle/passenger bookings.
    std::function<bool(std::uint64_t)> native_duty_available{};
};

struct CapacityIndex {
    std::set<std::uint64_t> occupied_slips; // one per real unfinished build
    std::set<std::uint64_t> working_npcs;   // real active jobs only
};

inline CapacityIndex index_native_capacity(const NativeCapacityContext& ctx) {
    CapacityIndex out{};
    if(ctx.yards){
        for(const auto& [id,yard]:*ctx.yards){
            (void)id;
            // A yard under construction occupies its two explicitly
            // assigned builders; the same people cannot commission a hull.
            if(!yard.operational())
                for(auto member:yard.workers)if(member)
                    out.working_npcs.insert(member);
        }
    }
    if(ctx.builds){
        for(const auto& [id,build]:*ctx.builds){
            (void)id;
            if(build.finished())continue;
            out.occupied_slips.insert(build.yard_order);
            if(!ctx.yards)continue;
            const auto yard=ctx.yards->find(build.yard_order);
            if(yard==ctx.yards->end())continue;
            for(auto member:yard->second.workers)if(member)
                out.working_npcs.insert(member);
        }
    }
    return out;
}

// An actual shipboard worker must be physically at the post and under
// this colony's original custody. No generic experience-score/license
// fabrication: no such credential exists yet for shipyard or commissioning.
inline Qualification qualify_ship_worker(
    std::uint64_t id,EntityId home,
    const std::map<std::uint64_t,NpcRecord>& npcs,
    const NativeCapacityContext& ctx,const CapacityIndex& index,
    bool commissioning) {
    const auto it=npcs.find(id);
    if(!id || it==npcs.end())return Qualification::MissingNpc;
    const auto& n=it->second;
    if(n.home_ship!=home)return Qualification::ForeignColony;
    if(n.role!=NpcRole::Ship)return Qualification::WrongRole;
    if(n.location!=home || n.assigned_site!=home)
        return Qualification::WrongStation;
    if(n.transport_id)return Qualification::PassengerInTransit;
    if(commissioning){
        if(n.residence_kind!=transport::LocationKind::ColonyShip ||
           (n.residence_id && n.residence_id!=home))
            return Qualification::ResidentElsewhere;
        if(n.mentor_id)return Qualification::MentorDuty;
        if(index.working_npcs.count(id))
            return Qualification::ExistingNativeAssignment;
    }
    if(ctx.native_duty_available && !ctx.native_duty_available(id))
        return Qualification::NativeDutyUnavailable;
    return Qualification::Qualified;
}

enum class YardCapacityStatus : std::uint8_t {
    Available=0,MissingOrForeignYard=1,NotBuilt=2,
    BusySlip=3,UnqualifiedBuilders=4
};
struct YardCapacity {
    std::uint64_t yard_id=0;
    std::uint8_t physical_slips=0,occupied_slips=0,qualified_builders=0;
    YardCapacityStatus status=YardCapacityStatus::MissingOrForeignYard;
    std::array<std::uint64_t,2> approved_builders{};
    bool usable() const noexcept {
        return status==YardCapacityStatus::Available;
    }
};

// A supplier's actual installed yard owns exactly one physical slip.
// A named worker is qualified for that yard only when assigned in the
// canonical construction record AND currently qualified for native duty.
inline YardCapacity inspect_shipyard(
    const ShipyardCustomerOrder& contract,
    const std::map<std::uint64_t,NpcRecord>& npcs,
    const NativeCapacityContext& ctx,const CapacityIndex& index) {
    YardCapacity capacity{};
    capacity.yard_id=contract.yard_order;
    if(!ctx.yards)return capacity;
    const auto it=ctx.yards->find(contract.yard_order);
    if(it==ctx.yards->end() ||
       it->second.owner!=contract.supplier_owner ||
       it->second.colony_ship!=contract.supplier_ship)
        return capacity;
    capacity.physical_slips=1;
    capacity.approved_builders=it->second.workers;
    if(!it->second.operational()){
        capacity.status=YardCapacityStatus::NotBuilt;
        return capacity;
    }
    if(index.occupied_slips.count(contract.yard_order)){
        capacity.occupied_slips=1;
        capacity.status=YardCapacityStatus::BusySlip;
        return capacity;
    }
    for(auto id:it->second.workers){
        if(qualify_ship_worker(id,it->second.colony_ship,
            npcs,ctx,index,false)==Qualification::Qualified)
            ++capacity.qualified_builders;
    }
    capacity.status=capacity.qualified_builders==2 &&
                    it->second.workers[0]!=it->second.workers[1]?
        YardCapacityStatus::Available:YardCapacityStatus::UnqualifiedBuilders;
    return capacity;
}

} // namespace galaxy::work
