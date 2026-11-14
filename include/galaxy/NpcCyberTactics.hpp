#pragma once
// SHIP-STATUS-04: bridge ORIGINAL durable colony political intentions
// to ORIGINAL physically fitted NPC cyber equipment and command executor.
// A raid intention is not a lawful arrest/attack warrant or crime finding.
#include "galaxy/ColonyDispositionCoordinator.hpp"
#include <cstdint>
#include <stdexcept>

namespace galaxy::npc_cyber {

enum class DispatchStatus : std::uint8_t {
    Invalid=0,NoOriginalRaidIntent=1,NoPhysicalCapability=2,
    Refused=3,Queued=4
};
struct Dispatch {
    DispatchStatus status=DispatchStatus::Invalid;
    colony_conflict::Posture posture=colony_conflict::Posture::Observe;
    std::uint64_t recorded_intent=0,command_id=0;
    bool legal_attack_warrant=false,criminal_verdict=false;
};
// Only the trusted colony governance/tactical host may call this. In
// particular, client-supplied owner IDs or contact IDs are never credentials.
//
// This permits an independently predatory NPC colony to ATTEMPT an attack
// when its *original* governance already chooses PrepareRaid. Whether that
// aggression is criminal is adjudicated independently, AFTER the event.
inline Dispatch dispatch_governed_hack(
    PersistentWorld& world,EntityId colony,
    std::uint64_t witnessed_contact_event,
    std::uint64_t decision_event_id,
    std::uint64_t pilot_npc,std::uint64_t observer_npc,
    std::uint64_t attack_command_id,
    FailPoint fail=FailPoint::None) {
    Dispatch out{};
    if(!colony || !witnessed_contact_event || !decision_event_id ||
       !attack_command_id || !pilot_npc || !observer_npc ||
       witnessed_contact_event==decision_event_id ||
       witnessed_contact_event==attack_command_id ||
       decision_event_id==attack_command_id)return out;
    const auto* ship=world.world().find(colony);
    if(!ship || ship->type!=EntityType::Ship || !ship->owner_id)
        return out;
    // Reconstruct original dated political/leadership choice from the
    // original contact evidence, rather than inventing an "attack permit".
    const auto evaluated=colony_disposition::assess(
        world,colony,witnessed_contact_event);
    out.posture=evaluated.decision.posture;
    if(evaluated.decision.source!=colony ||
       evaluated.decision.target_owner==ship->owner_id ||
       !evaluated.decision.target_ship)return out;
    if(evaluated.decision.posture!=colony_conflict::Posture::PrepareRaid){
        out.status=DispatchStatus::NoOriginalRaidIntent;
        return out;
    }
    if(!ship->fitting.intrusion_emitter()){
        out.status=DispatchStatus::NoPhysicalCapability;
        return out;
    }
    const auto recorded=world.record_colony_diplomatic_intent(
        decision_event_id,colony,witnessed_contact_event);
    if(recorded.source!=colony ||
       recorded.target_ship!=evaluated.decision.target_ship ||
       recorded.posture!=colony_conflict::Posture::PrepareRaid ||
       recorded.leader_id!=evaluated.decision.leader_id ||
       recorded.governance_event_id!=evaluated.decision.governance_event_id)
        throw std::runtime_error("original colony raid decision changed");
    out.recorded_intent=decision_event_id;
    // Strategic intent is its own immutable record, even if the physical
    // attack fails because target flees or equipment is out of range.
    const auto submitted=world.submit_npc_cyber_attack(
        ship->owner_id,colony,pilot_npc,observer_npc,
        evaluated.decision.target_ship,attack_command_id,fail);
    out.status=submitted?DispatchStatus::Queued:DispatchStatus::Refused;
    if(submitted)out.command_id=attack_command_id;
    return out;
}
} // namespace galaxy::npc_cyber
