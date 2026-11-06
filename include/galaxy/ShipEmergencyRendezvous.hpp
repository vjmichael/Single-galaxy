#pragma once
// FLIGHT-05: mission-specific adapter. Navigation clearance does NOT
// substitute for native emergency, full manifest, or passenger authority.
#include "galaxy/ShipInteractionPreflight.hpp"
#include "galaxy/Persistence.hpp"

namespace galaxy::ship_interaction {

// Permits dispatch to the existing native emergency executor *only* after
// a committed matching rendezvous. No new passenger or custody engine.
inline bool submit_emergency_rescue_after_rendezvous(
    PersistentWorld& persistent,PlayerId receiving_owner,
    std::uint64_t rendezvous_order,const Command& command,
    FailPoint fail=FailPoint::None) {
    if(command.action!=Action::HostEvacuatePassengers ||
       command.issuer!=Simulation::kServerAuthority ||
       !command.actor || !command.target ||
       command.actor==command.target ||
       command.passenger_ids.empty() ||
       command.transport_id==0 || command.amount<=0 ||
       command.execute_tick!=persistent.world().tick()+1)
        return false;
    if(rendezvous_preflight(persistent.world(),receiving_owner,
            command.target,command.actor,rendezvous_order)!=Readiness::Ready)
        return false;
    // Original host executor insists on critical/emergency source risk,
    // whole original voyage manifest, available seats, life support,
    // distinct non-reused rescue voyage and existing actual NPC identity.
    // Success means only scheduled: the committed step effects custody.
    return persistent.submit_host_emergency_ferry_rescue(command,fail);
}
} // namespace galaxy::ship_interaction
