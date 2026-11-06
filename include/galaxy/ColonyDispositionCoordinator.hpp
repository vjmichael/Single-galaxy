#pragma once
// DISPOSITION-01: one bridge from existing durable CONFLICT-01/GRSM policy
// to original NPC-RESP-01 physical flight. No parallel personality engine.
#include "galaxy/ColonyThreatPolicy.hpp"
#include "galaxy/NpcResponseDispatch.hpp"
#include <cstdint>
#include <stdexcept>

namespace galaxy::colony_disposition {

struct Evaluation {
    colony_conflict::Decision decision{};
    colony_conflict::GovernanceEpoch governance{};
    npc_response::ThreatAssessment threat{};
    // Threat assessment is strategic advice; not authority to arrest or shoot.
    bool movement_warranted=false;
};

inline npc_response::ColonyCharacter character_for(
        colony_conflict::Agenda agenda) {
    switch(agenda){
    case colony_conflict::Agenda::Cooperative:
        return npc_response::ColonyCharacter::Cooperative;
    case colony_conflict::Agenda::Isolationist:
        return npc_response::ColonyCharacter::Territorial;
    case colony_conflict::Agenda::Expansionist:
        return npc_response::ColonyCharacter::Militant;
    case colony_conflict::Agenda::Predatory:
        return npc_response::ColonyCharacter::Criminal;
    }
    throw std::invalid_argument("invalid original colony agenda");
}
// This method consumes the ORIGINAL validated durable contact, previous-epoch
// diplomatic reputation, current effective governance and actual shortages.
// It accepts no caller-supplied scores, colony personality or crime verdict.
inline Evaluation assess(const PersistentWorld& world,
                         EntityId colony,std::uint64_t contact_event) {
    if(!colony || !contact_event)
        throw std::invalid_argument("disposition requires source and observed contact");
    Evaluation result{};
    result.governance=world.colony_governance(colony);
    result.decision=world.assess_colony_diplomatic_intent(colony,contact_event);
    const auto& d=result.decision;
    if(d.source!=colony || !d.target_ship || !d.target_owner ||
       d.governance_event_id!=result.governance.event_id ||
       d.leader_id!=result.governance.leader_id ||
       d.agenda!=result.governance.agenda ||
       d.diplomatic_policy!=result.governance.policy)
        throw std::runtime_error("colony disposition governance is not current");
    npc_response::ThreatContext context{};
    context.colony=character_for(d.agenda);
    context.relation=colony_conflict::attitude_for_score(d.relation_score);
    context.urgent_shortage=
        d.source_risk==colony_safety::Risk::Critical ||
        d.source_risk==colony_safety::Risk::Emergency;
    // No invitations, claimed regions or attacks exist merely because a
    // contact was seen. These require separate original signed receipts.
    context.standing=npc_response::VisitorStanding::Unknown;
    context.in_claimed_region=false;
    context.observed_attack=false;
    result.threat=npc_response::assess_threat(context);
    // The mature CONFLICT-01 dated strategy is decisive where this new
    // optional explanatory projection would otherwise guess visitor policy.
    switch(d.posture) {
    case colony_conflict::Posture::Observe:
        result.threat.intent=npc_response::ThreatIntent::Ignore;
        result.threat.basis=npc_response::Basis::None;
        break;
    case colony_conflict::Posture::Warn:
        result.threat.intent=npc_response::ThreatIntent::WarnLeave;
        result.threat.basis=d.motive==colony_conflict::Motive::Territory?
            npc_response::Basis::Territorial:npc_response::Basis::Diplomatic;
        break;
    case colony_conflict::Posture::RequestAid:
        result.threat.intent=npc_response::ThreatIntent::Hail;
        result.threat.basis=npc_response::Basis::None;
        break;
    case colony_conflict::Posture::DemandSupplies:
        result.threat.intent=npc_response::ThreatIntent::Threaten;
        result.threat.basis=npc_response::Basis::Predatory;
        break;
    case colony_conflict::Posture::PrepareRaid:
        result.threat.intent=npc_response::ThreatIntent::PrepareAmbush;
        result.threat.basis=npc_response::Basis::Predatory;
        break;
    default:
        throw std::runtime_error("invalid original colony strategic posture");
    }
    result.movement_warranted=d.posture!=colony_conflict::Posture::Observe;
    // Strategic intent must NEVER silently grant an attack, conviction,
    // arrest, seizure, or legal jurisdiction.
    if(result.threat.legal_crime_finding || result.threat.can_attack ||
       result.threat.can_arrest || result.threat.can_seize_goods)
        throw std::logic_error("colony intent improperly escalated authority");
    return result;
}

struct DispatchResult {
    Evaluation evaluation{};
    npc_response::Status status=npc_response::Status::Refused;
    bool intent_recorded=false;
};
// Two separate durable records: CONFLICT-01 dated strategic intent and the
// original physical NPC flight order. The caller must use distinct event IDs.
// A failed flight does not retroactively erase an observed strategic intent.
// Never mistake the journaled intent for actual pursuit or weapon discharge.
inline DispatchResult dispatch_witnessed_response(
    PersistentWorld& world,EntityId colony,std::uint64_t contact_event,
    std::uint64_t decision_event,std::uint64_t flight_order,
    std::uint64_t observer_npc,std::uint64_t pilot_npc=0,
    FailPoint fail=FailPoint::None) {
    if(!decision_event || !flight_order ||
       decision_event==flight_order || !observer_npc)
        throw std::invalid_argument("response requires distinct evidence and flight identities");
    DispatchResult result{};
    result.evaluation=assess(world,colony,contact_event);
    if(!result.evaluation.movement_warranted)return result;
    const auto& d=result.evaluation.decision;
    // Reconstruct live named-watch evidence; do not reuse an old contact's
    // coordinates after a player flees or after a guard loses authority.
    const auto current=world.local_colony_watch_contacts(colony,observer_npc);
    const auto found=std::find_if(current.begin(),current.end(),
        [&](const colony_defense::Contact& c){
            return c.source==colony && c.foreign_ship==d.target_ship &&
                   c.foreign_owner==d.target_owner &&
                   c.observer_npc==observer_npc &&
                   c.world_tick==world.world().tick();
        });
    if(found==current.end()){
        result.status=npc_response::Status::NoAuthority;
        return result;
    }
    const auto* source=world.world().find(colony);
    if(!source || !source->owner_id){
        result.status=npc_response::Status::WrongOwner;
        return result;
    }
    // A strategic decision is recorded through CONFLICT-01's native immutable
    // ledger, not a replacement personality or diplomacy table.
    const auto recorded=world.record_colony_diplomatic_intent(
        decision_event,colony,contact_event);
    if(recorded.source!=d.source || recorded.target_ship!=d.target_ship ||
       recorded.leader_id!=d.leader_id ||
       recorded.governance_event_id!=d.governance_event_id ||
       recorded.posture!=d.posture ||
       recorded.relation_event_id!=d.relation_event_id)
        throw std::runtime_error("recorded original disposition changed during dispatch");
    result.intent_recorded=true;
    // This issues ONLY a normal Patrol flight from the original qualified
    // NPC navigator. An ambush posture cannot shoot or seize goods.
    result.status=npc_response::dispatch_observed_patrol(
        world,source->owner_id,colony,pilot_npc,
        *found,flight_order,fail);
    return result;
}
} // namespace galaxy::colony_disposition
