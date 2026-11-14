#pragma once
// Stage 7 POP-08: named residents make bounded choices using ACTUAL WO4
// shortages, original WO5 physical freight and original constitutional law.
// A resident decides WHAT to request, never creates inventory, a ship,
// transport, payment, signed owner consent, court judgment or a second ledger.
#include "galaxy/Stage07CivicPolicy.hpp"
#include "galaxy/WO4LocalAllocation.hpp"
#include "galaxy/WO5IntercolonyBooking.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::stage7 {

inline bool authentic_resident(const Simulation& world,PlayerId owner,
        EntityId home,std::uint64_t npc_id,EntityId observed_place) {
    const auto* ship=world.find(home);
    const auto it=world.npc_records().find(npc_id);
    return ship && ship->owner_id==owner && owner!=0 &&
        owner!=Simulation::kServerAuthority && it!=world.npc_records().end() &&
        it->second.home_ship==home &&
        it->second.legal_residence().id==home &&
        it->second.location==observed_place &&
        !it->second.transport_id &&
        !world.npc_casualties().count(npc_id);
}

struct ResidentVitalDecision {
    PlayerId owner=0;
    EntityId original_home=0;
    std::uint64_t resident=0,generation=0,tick=0,world_hash=0;
    economy04::AllocationPolicy reserves{};
    economy04::Grant original_grant{};
};

// A real resident aboard the original colony can nominate the FIRST WO4
// critical-window/reserve allocation to an occupied physical habitat/farm.
// Stable native WO4 ordering determines the finite quantity and destination.
inline std::optional<ResidentVitalDecision> decide_resident_vital(
        const PersistentWorld& persistent,PlayerId owner,EntityId home,
        std::uint64_t resident,
        economy04::AllocationPolicy policy={{4,1},true,true}) {
    const auto& sim=persistent.world();
    if(!resident || !policy.owner_enabled ||
       !authentic_resident(sim,owner,home,resident,home))
        return std::nullopt;
    const auto seen=economy04::observe_owner_world(
        sim,owner,home,policy.reserves);
    if(!seen || !seen->still_current(sim))return std::nullopt;
    const auto plan=economy04::plan_local_vitals(*seen,sim,policy);
    if(!plan || !plan->still_current(sim))return std::nullopt;
    for(const auto& grant:plan->grants){
        if(grant.source_ship!=home)continue; // real outpost consumer in WO4
        return ResidentVitalDecision{owner,home,resident,
            persistent.generation(),sim.tick(),sim.state_hash(),policy,grant};
    }
    return std::nullopt;
}

// This is a real native material-moving command, not a shadow stock update.
// The original submit/step and receipt must succeed before reporting Applied.
inline economy04::CommitResult execute_resident_vital(
        PersistentWorld& persistent,const ResidentVitalDecision& decision,
        PlayerId authenticated_owner,std::uint64_t command_id,
        bool independently_approved,FailPoint fail=FailPoint::None) {
    economy04::CommitResult denied{};
    denied.command_id=command_id;
    const auto& sim=persistent.world();
    if(!independently_approved || authenticated_owner!=decision.owner ||
       !persistent.generation() ||
       persistent.generation()!=decision.generation ||
       sim.tick()!=decision.tick || sim.state_hash()!=decision.world_hash)
        return denied;
    const auto fresh=decide_resident_vital(
        persistent,decision.owner,decision.original_home,
        decision.resident,decision.reserves);
    if(!fresh || fresh->original_grant.destination_location!=
                   decision.original_grant.destination_location ||
       fresh->original_grant.vital!=decision.original_grant.vital ||
       fresh->original_grant.quantity_kg!=decision.original_grant.quantity_kg ||
       fresh->original_grant.priority!=decision.original_grant.priority)
        return denied;
    return economy04::commit_owner_approved_local_vital(
        persistent,decision.original_grant,decision.reserves,command_id,
        true,fail);
}

// An intercolony economic choice requires a DIFFERENT physically occupied
// receiving colony, native surplus at an actually equipped planetary source,
// a real third carrier ship, and the original carrier's posting preflight.
// No named NPC or trade algorithm may synthesize a freight-capable ship.
struct ResidentFreightDecision {
    PlayerId owner=0;
    EntityId home=0;
    std::uint64_t resident=0,generation=0,tick=0,world_hash=0;
    logistics05::IntercolonyBookingOffer original_contract{};
};

inline std::optional<ResidentFreightDecision> decide_resident_freight(
        const PersistentWorld& persistent,PlayerId owner,EntityId home,
        std::uint64_t resident,EntityId carrier,std::uint64_t native_order_id,
        economy04::Policy reserves={4,1}) {
    const auto& sim=persistent.world();
    if(!resident || !authentic_resident(sim,owner,home,resident,home))
        return std::nullopt;
    const auto board=economy04::observe_regional_distribution(
        sim,owner,reserves);
    if(!board || !board->still_current(sim))return std::nullopt;
    // First affordable real WO4 need, in source-stable order.
    // The original third-party freight code has NO monetary or reciprocal
    // exchange authority: these are physical supply contracts, not sales.
    for(const auto& demand:board->unbooked_carrier_requests) {
        if(demand.destination.source_ship!=home ||
           demand.destination.kind!=economy04::CompartmentKind::ColonyShip ||
           demand.proposed_kg<=0)continue;
        const auto quantity=std::min<std::int64_t>(1000,demand.proposed_kg);
        const auto native=logistics05::inspect_intercolony_booking(
            sim,owner,demand,carrier,native_order_id,quantity,reserves);
        if(!native)continue;
        return ResidentFreightDecision{owner,home,resident,
            persistent.generation(),sim.tick(),sim.state_hash(),*native};
    }
    return std::nullopt;
}

inline logistics05::BookingResult execute_resident_freight(
        PersistentWorld& persistent,const ResidentFreightDecision& offer,
        PlayerId authenticated_owner,bool independently_approved,
        FailPoint fail=FailPoint::None) {
    logistics05::BookingResult denied{};
    denied.original_order_id=offer.original_contract.native_local_order_id;
    if(!independently_approved || !offer.generation ||
       authenticated_owner!=offer.owner ||
       persistent.generation()!=offer.generation ||
       persistent.world().tick()!=offer.tick ||
       persistent.world().state_hash()!=offer.world_hash)
        return denied;
    const auto fresh=decide_resident_freight(
        persistent,offer.owner,offer.home,offer.resident,
        offer.original_contract.carrier_ship,
        offer.original_contract.native_local_order_id,
        offer.original_contract.reserves);
    if(!fresh || fresh->original_contract.origin_ship!=
                   offer.original_contract.origin_ship ||
       fresh->original_contract.destination_ship!=
                   offer.original_contract.destination_ship ||
       fresh->original_contract.kilograms!=
                   offer.original_contract.kilograms ||
       fresh->original_contract.cargo!=offer.original_contract.cargo ||
       fresh->original_contract.pickup_site!=
                   offer.original_contract.pickup_site)
        return denied;
    return logistics05::book_approved_intercolony_pickup(
        persistent,offer.original_contract,true,fail);
}

// An actually short-supplied original colony's recorded personal outlook can
// petition for a NONCOERCIVE policy review. Material scarcity alone never
// constitutes a crime, a judicial verdict, or a lawmaking authorization.
inline std::optional<CivicMotion> propose_scarcity_review(
        const PersistentWorld& persistent,PlayerId owner,
        EntityId home,std::uint64_t resident,
        economy04::Policy reserves={4,1}) {
    if(!authentic_resident(persistent.world(),owner,home,resident,home))
        return std::nullopt;
    const auto observed=economy04::observe_owner_world(
        persistent.world(),owner,home,reserves);
    if(!observed || !observed->still_current(persistent.world()) ||
       observed->compartments.empty())return std::nullopt;
    const auto& needs=observed->compartments.front().needs;
    const bool hardship=std::any_of(needs.begin(),needs.end(),
        [](const economy04::Need& n){
            return n.pressure==economy04::Pressure::CriticalCurrentWindow ||
                   n.pressure==economy04::Pressure::BelowReserve;
        });
    if(!hardship)return std::nullopt;
    return propose_civic_reconciliation(persistent,owner,home,resident);
}

inline std::optional<colony_conflict::GovernanceEpoch>
approve_scarcity_review(PersistentWorld& persistent,const CivicMotion& motion,
        PlayerId authenticated_owner,std::uint64_t legal_event_id,
        bool independent_owner_approval,
        economy04::Policy reserves={4,1}) {
    if(!independent_owner_approval)return std::nullopt;
    const auto current=propose_scarcity_review(
        persistent,authenticated_owner,motion.colony,motion.observed_npc,reserves);
    if(!current || current->personal_event!=motion.personal_event ||
       current->source_generation!=motion.source_generation ||
       current->source_tick!=motion.source_tick ||
       current->source_hash!=motion.source_hash ||
       current->prior_governance_event!=motion.prior_governance_event)
        return std::nullopt;
    // The native one-writer governance epoch takes effect only on the next
    // world generation; no NPC is granted a vote or unilateral lawmaking.
    return approve_civic_motion(persistent,motion,authenticated_owner,
                               legal_event_id,true);
}
} // namespace galaxy::stage7
