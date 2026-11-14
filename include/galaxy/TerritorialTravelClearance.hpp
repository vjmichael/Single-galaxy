#pragma once
// DISPOSITION-03C: denial of services at genuinely colony-controlled access
// points. A claim is NOT ownership of every gate, lane, or ship in a region.
#include "galaxy/TerritorialHailing.hpp"
#include <cstdint>

namespace galaxy::colony_disposition {

enum class TerritorialService : std::uint8_t {
    ColonyApproach=1, Docking=2, FoldGate=3, HyperlaneEntry=4,
    ManagedRouteReservation=5
};
enum class ClearanceDecision : std::uint8_t {
    Allow=0, DenyPendingHail=1, DenyTerritorialStanding=2,
    DenyNoInvitation=3, NotAuthorizedToDecide=4
};
struct ControlledAccessRequest {
    PlayerId requesting_owner=0;
    EntityId ship=0;
    EntityId controlling_colony=0;
    // An actual owner/operating authority must have proved this asset
    // belongs to the controlling colony. UI names or nearby coordinates
    // do not establish jurisdiction.
    bool infrastructure_control_verified=false;
    bool service_requires_invitation=false;
    TerritorialService service=TerritorialService::Docking;
};
struct ClearanceResult {
    ClearanceDecision decision=ClearanceDecision::NotAuthorizedToDecide;
    std::uint64_t related_notice=0;
    bool can_block_physical_flight=false;
};
// Called BEFORE using a controlled installation or authorizing an approach.
// Existing travel, docking and route executors must call this with their
// authenticated issuer and true infrastructure owner: displaying a warning
// in the UI alone never enforces a restriction.
inline ClearanceResult territorial_service_clearance(
    const PersistentWorld& world,const TerritoryJournal& journal,
    const ControlledAccessRequest& request) {
    ClearanceResult result{};
    const auto* ship=world.world().find(request.ship);
    const auto* controller=world.world().find(request.controlling_colony);
    if(!request.requesting_owner || !ship || !controller ||
       ship->owner_id!=request.requesting_owner ||
       !controller->owner_id || !request.infrastructure_control_verified)
        return result;
    // Only the original colony can determine the use of its actually
    // controlled service. This decision cannot stop free flight, ship
    // propulsion, emergency exit or another owner's public infrastructure.
    const auto standing=journal.standing(world,request.controlling_colony,
        request.ship);
    result.related_notice=standing.warning_event;
    if(request.requesting_owner==controller->owner_id) {
        result.decision=ClearanceDecision::Allow;
        return result;
    }
    if(standing.standing==npc_response::VisitorStanding::Expelled ||
       standing.trespass_recorded) {
        result.decision=ClearanceDecision::DenyTerritorialStanding;
        return result;
    }
    if(request.service_requires_invitation && !standing.invited) {
        result.decision=ClearanceDecision::DenyNoInvitation;
        return result;
    }
    // Acknowledging a guard's leave-warning does NOT automatically grant
    // travel privileges. Only a proper invitation can override restricted
    // access; a normal non-restricted service requires completed customs
    // hailing once the colony has issued a warning.
    if(!standing.invited && (!standing.warned || !standing.notice_received)) {
        result.decision=ClearanceDecision::DenyPendingHail;
        return result;
    }
    result.decision=ClearanceDecision::Allow;
    return result;
}
} // namespace galaxy::colony_disposition
