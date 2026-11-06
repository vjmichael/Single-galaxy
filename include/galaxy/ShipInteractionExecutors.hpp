#pragma once
// FLIGHT-04B — ship-to-ship entrypoints that reuse the ORIGINAL durable
// executors. Never use this header as a replacement cargo/passenger engine.
// The host must still authenticate each action and obtain its original plan.
#include "galaxy/Persistence.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"

namespace galaxy::ship_interaction {

// The original intercolony freight executor is the ONLY stock-mutation path.
// Existing celestial flight, sealed goods, receiving stock and source-owner
// checks still apply. A rendezvous receipt does not waive any of them.
inline bool deliver_freight_after_rendezvous(
    PersistentWorld& persistent,PlayerId owner,std::uint64_t freight_order,
    std::uint64_t rendezvous_order,FailPoint fail=FailPoint::None) {
    const auto& world=persistent.world();
    const auto leg=world.intercolony_freight().find(freight_order);
    if(leg==world.intercolony_freight().end() ||
       leg->second.owner!=owner ||
       leg->second.stage!=IntercolonyFreightStage::Loaded)
        return false;
    const auto physical=rendezvous_preflight(world,owner,
        leg->second.carrier_ship,leg->second.destination_ship,rendezvous_order);
    if(physical!=Readiness::Ready)return false;
    // Native executor rechecks stock and current physical geometry and
    // commits any accepted debit/credit in its original SQLite transaction.
    return persistent.deliver_intercolony_freight(owner,freight_order,fail);
}

// Host-only original ferry evacuation. A player cannot pass an ordinary
// command here as a new authority: the original trusted host executor
// requires its reserved issuer and complete real manifest and voyage.
inline bool submit_passenger_rescue_after_rendezvous(
    PersistentWorld& persistent,PlayerId owner,std::uint64_t rescue_ferry,
    std::uint64_t original_ferry,std::uint64_t rendezvous_order,
    const Command& evacuation,FailPoint fail=FailPoint::None) {
    if(evacuation.action!=Action::HostEvacuatePassengers ||
       evacuation.issuer!=Simulation::kServerAuthority ||
       evacuation.actor!=original_ferry ||
       evacuation.target!=rescue_ferry ||
       evacuation.passenger_ids.empty())return false;
    if(rendezvous_preflight(persistent.world(),owner,rescue_ferry,
         original_ferry,rendezvous_order)!=Readiness::Ready)return false;
    return persistent.submit_host_passenger_action(evacuation,fail);
}

// Explicit dependent-family rescue: retain the existing complete guardian
// roster, family/region revisions, trusted SQLite custody reconciliation,
// voyage identity and failpoint behavior. No new custody bookkeeping.
inline bool execute_dependent_rescue_after_rendezvous(
    PersistentWorld& persistent,PlayerId owner,
    std::uint64_t rendezvous_order,const DependentRescuePlan& plan,
    FailPoint fail=FailPoint::None) {
    if(!plan.original_ferry || !plan.rescue_ferry ||
       plan.original_ferry==plan.rescue_ferry ||
       !plan.command_id || !plan.rescue_transport ||
       !plan.original_transport || plan.child_ids.empty())
        return false;
    if(rendezvous_preflight(persistent.world(),owner,plan.rescue_ferry,
         plan.original_ferry,rendezvous_order)!=Readiness::Ready)
        return false;
    // Existing authoritative executor may reject/throw on stale plans.
    // Deliberately propagate it rather than silently report a rescue.
    persistent.step_with_dependent_rescue(plan,fail);
    return true;
}

// Ordinary dependent boarding/disembarkation is not ship-to-ship by default:
// the original route may have a station/colony as its endpoint. To avoid
// accidentally treating that destination as another ship, require an
// explicit receiving ship identity AND the original plan's endpoint match.
inline bool execute_dependent_arrival_after_rendezvous(
    PersistentWorld& persistent,PlayerId owner,EntityId receiving_ship,
    std::uint64_t rendezvous_order,const DependentTravelPlan& plan,
    FailPoint fail=FailPoint::None) {
    if(plan.boarding || !receiving_ship || plan.ferry==receiving_ship ||
       plan.destination!=receiving_ship || !plan.transport_id ||
       plan.child_ids.empty())return false;
    if(rendezvous_preflight(persistent.world(),owner,plan.ferry,
         receiving_ship,rendezvous_order)!=Readiness::Ready)return false;
    persistent.step_with_dependent_transport(plan,fail);
    return true;
}
} // namespace galaxy::ship_interaction
