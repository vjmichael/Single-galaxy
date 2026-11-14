#pragma once
// WO4-B: deterministic, bounded, owner-scoped RESOURCE PLANNING ONLY.
// Source and destination are the original WO4-A physical inventory rows.
// No reservation, transfer, dispatch, approval, or material is created.
#include "galaxy/WO4ResourceDemand.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace galaxy::economy04 {
enum class Priority : std::uint8_t {
    VitalCurrentWindow=0, VitalReserve=1,
    Maintenance=2, Production=3, Discretionary=4
};
enum class TransferScope : std::uint8_t {
    OriginalLocalCommand=0, RequiresPhysicalCarrier=1
};
struct AllocationPolicy {
    // Original residents' four-window reserve remains protected.
    // This is a planning limit, not automatic authority to release stock.
    Policy reserves{};
    bool permit_local_life_support_offers=true;
    // A declared owner choice is necessary even to prepare a life-support
    // offer. Production and discretion can NEVER override vital reserves.
    bool owner_enabled=false;
};
struct Grant {
    PlayerId owner=0;
    EntityId source_ship=0,source_location=0,destination_location=0;
    CompartmentKind destination_kind=CompartmentKind::OutpostHabitat;
    Vital vital=Vital::Food;
    Priority priority=Priority::VitalCurrentWindow;
    TransferScope scope=TransferScope::OriginalLocalCommand;
    std::int64_t quantity_kg=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
};
struct AllocationPlan {
    PlayerId owner=0;
    EntityId source_ship=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    AllocationPolicy policy{};
    std::vector<Grant> grants;
    bool still_current(const Simulation& world) const {
        return observed_tick==world.tick() &&
               observed_state_hash==world.state_hash();
    }
};
// Only physically instantiated original outpost bins are eligible for the
// local native command. Planetary shuttles and intership logistics belong
// to WO5; this planner refuses to treat proximity as transported goods.
inline std::optional<AllocationPlan> plan_local_vitals(
    const Snapshot& observed,const Simulation& world,
    AllocationPolicy policy) {
    if(!policy.owner_enabled || !policy.permit_local_life_support_offers ||
       !observed.still_current(world) ||
       observed.compartments.empty() ||
       observed.compartments.front().kind!=CompartmentKind::ColonyShip ||
       observed.compartments.front().physical_location!=observed.source_ship ||
       observed.policy.reserve_windows!=policy.reserves.reserve_windows ||
       observed.policy.kg_per_person_per_window!=
           policy.reserves.kg_per_person_per_window ||
       !colony03::original_owner(world,observed.owner,observed.source_ship))
        return std::nullopt;
    // The snapshot is caller-supplied data. Merely copying the current
    // state hash onto a forged compartment/need is NOT physical proof.
    // Reconstruct all inputs from the original world before even offering
    // a proposal; the commit route independently does the same again.
    const auto authoritative=observe_owner_world(
        world,observed.owner,observed.source_ship,observed.policy);
    if(!authoritative ||
       authoritative->compartments.size()!=observed.compartments.size())
        return std::nullopt;
    for(std::size_t i=0;i<observed.compartments.size();++i){
        const auto& got=observed.compartments[i];
        const auto& real=authoritative->compartments[i];
        if(got.kind!=real.kind || got.owner!=real.owner ||
           got.source_ship!=real.source_ship ||
           got.physical_location!=real.physical_location ||
           !(got.planetary_key==real.planetary_key) ||
           got.current_consumers!=real.current_consumers)
            return std::nullopt;
        for(std::size_t v=0;v<3;++v) {
            const auto& a=got.needs[v];
            const auto& b=real.needs[v];
            if(a.vital!=b.vital || a.pressure!=b.pressure ||
               a.physically_present_kg!=b.physically_present_kg ||
               a.one_window_kg!=b.one_window_kg ||
               a.reserve_target_kg!=b.reserve_target_kg ||
               a.reserve_deficit_kg!=b.reserve_deficit_kg ||
               a.buffer_above_target_kg!=b.buffer_above_target_kg ||
               a.complete_windows_covered!=b.complete_windows_covered)
                return std::nullopt;
        }
    }
    AllocationPlan plan{};
    plan.owner=observed.owner;plan.source_ship=observed.source_ship;
    plan.observed_tick=observed.observed_tick;
    plan.observed_state_hash=observed.observed_state_hash;
    plan.policy=policy;
    const auto& ship=observed.compartments.front();
    for(std::size_t vital_index=0;vital_index<3;++vital_index) {
        const auto& ship_need=ship.needs[vital_index];
        std::int64_t available=ship_need.buffer_above_target_kg;
        if(available<=0)continue;
        // Pass one: meet a current window of vital consumption for all
        // urgent outpost compartments before filling anyone's full buffer.
        // Pass two: restore the desired reserve in stable original order.
        std::vector<std::int64_t> assigned(observed.compartments.size(),0);
        for(unsigned pass=0;pass<2;++pass) {
            for(std::size_t i=1;i<observed.compartments.size();++i) {
                const auto& destination=observed.compartments[i];
                if(destination.kind!=CompartmentKind::OutpostHabitat &&
                   destination.kind!=CompartmentKind::OutpostFarm)continue;
                if(destination.owner!=plan.owner ||
                   destination.source_ship!=plan.source_ship ||
                   destination.physical_location==plan.source_ship ||
                   !colony03::original_site(world,plan.source_ship,
                                            destination.physical_location))
                    return std::nullopt;
                if(destination.kind==CompartmentKind::OutpostHabitat &&
                   !world.habitat(destination.physical_location))
                    return std::nullopt;
                if(destination.kind==CompartmentKind::OutpostFarm &&
                   !world.farm(destination.physical_location))
                    return std::nullopt;
                const auto& need=destination.needs[vital_index];
                if(!destination.current_consumers ||
                   need.reserve_deficit_kg<=0)continue;
                const auto goal=pass==0?
                    need.one_window_kg:need.reserve_target_kg;
                const auto currently=need.physically_present_kg+assigned[i];
                const auto shortage=std::max<std::int64_t>(0,goal-currently);
                // Native habitat/farm stock-transfer command caps each bin
                // at 100,000,000 kg. Never promise an engine-denied transfer.
                constexpr std::int64_t kOriginalBinCap=100000000;
                const auto remaining_capacity=std::max<std::int64_t>(0,
                    kOriginalBinCap-currently);
                const auto take=std::min({available,shortage,remaining_capacity});
                if(take<=0)continue;
                Grant g{};
                g.owner=plan.owner;g.source_ship=plan.source_ship;
                g.source_location=plan.source_ship;
                g.destination_location=destination.physical_location;
                g.destination_kind=destination.kind;
                g.vital=static_cast<Vital>(vital_index);
                g.priority=pass==0?Priority::VitalCurrentWindow:
                                   Priority::VitalReserve;
                g.quantity_kg=take;g.observed_tick=plan.observed_tick;
                g.observed_state_hash=plan.observed_state_hash;
                plan.grants.push_back(g);
                assigned[i]+=take;available-=take;
                if(!available)break;
            }
            if(!available)break;
        }
    }
    return plan;
}
// The deterministic result may contain more than one candidate per resource
// or location. They are alternatives from ONE immutable observed snapshot,
// NOT jointly reserved work. The host must re-observe after each original
// committed command; remaining stale offers cannot be replayed.
} // namespace galaxy::economy04
