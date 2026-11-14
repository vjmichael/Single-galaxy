#pragma once
// Stage 7 POP-06: bounded original-NPC civic petition with a separately
// authorized immutable policy epoch. The AI/NPC may ASK, never enact a law.
// An opinion is neither criminal guilt, a permit, nor an attack mandate.
#include "galaxy/Stage07SocietyEconomy.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace galaxy::stage7 {
struct CivicMotion {
    std::uint64_t source_generation=0,source_tick=0,source_hash=0;
    PlayerId approving_owner=0;
    EntityId colony=0;
    std::uint64_t leader=0,prior_governance_event=0,prior_ordinal=0;
    std::uint64_t observed_npc=0,personal_event=0;
    colony_conflict::Agenda retained_agenda=colony_conflict::Agenda::Cooperative;
    colony_conflict::DiplomaticPolicy old_policy=
        colony_conflict::DiplomaticPolicy::Pragmatic;
    colony_conflict::DiplomaticPolicy proposed_policy=
        colony_conflict::DiplomaticPolicy::Reconciliatory;
};

// A real individual's durable, *previously recorded* local social outlook
// justifies putting a narrowly scoped noncoercive policy choice on the
// creator-owner's agenda. One NPC cannot magically hold a referendum.
inline std::optional<CivicMotion> propose_civic_reconciliation(
        const PersistentWorld& world,PlayerId authenticated_owner,
        EntityId source_colony,std::uint64_t observing_npc) {
    if(!observing_npc)return std::nullopt;
    const auto facts=assess_original_society(
        world,authenticated_owner,source_colony);
    const auto& sim=world.world();
    const auto person=sim.npc_records().find(observing_npc);
    const auto outlook=world.colony_personal_outlook(
        source_colony,observing_npc);
    if(person==sim.npc_records().end() ||
       person->second.home_ship!=source_colony ||
       person->second.location!=source_colony ||
       person->second.transport_id ||
       sim.npc_casualties().count(observing_npc) ||
       !outlook || !outlook->event_id ||
       outlook->colony!=source_colony || outlook->npc_id!=observing_npc ||
       outlook->world_generation>facts.generation ||
       outlook->world_tick>facts.tick ||
       facts.effective_governance.leader_id==observing_npc ||
       facts.effective_governance.policy==
          colony_conflict::DiplomaticPolicy::Reconciliatory)
        return std::nullopt;
    CivicMotion motion{};
    motion.source_generation=facts.generation;
    motion.source_tick=facts.tick;
    motion.source_hash=sim.state_hash();
    motion.approving_owner=authenticated_owner;
    motion.colony=source_colony;
    motion.leader=facts.effective_governance.leader_id;
    motion.prior_governance_event=facts.effective_governance.event_id;
    motion.prior_ordinal=facts.effective_governance.ordinal;
    motion.observed_npc=observing_npc;
    motion.personal_event=outlook->event_id;
    motion.retained_agenda=facts.effective_governance.agenda;
    motion.old_policy=facts.effective_governance.policy;
    return motion;
}

// Only an independently authenticated, explicit owner approval can create
// the original signed-by-source governance receipt. All physical residency,
// immutable event uniqueness and epoch consistency are ultimately rechecked
// by the existing PersistentWorld::Store under BEGIN IMMEDIATE.
// Same-generation proposals cannot be silently renewed after a new world tick.
inline std::optional<colony_conflict::GovernanceEpoch> approve_civic_motion(
        PersistentWorld& world,const CivicMotion& motion,
        PlayerId authenticated_owner,std::uint64_t event_id,
        bool independently_approved) {
    if(!independently_approved || authenticated_owner==0 ||
       authenticated_owner==Simulation::kServerAuthority ||
       authenticated_owner!=motion.approving_owner ||
       !event_id ||
       event_id>static_cast<std::uint64_t>(
           std::numeric_limits<std::int64_t>::max()) ||
       world.generation()!=motion.source_generation ||
       world.world().tick()!=motion.source_tick ||
       world.world().state_hash()!=motion.source_hash)
        return std::nullopt;
    const auto revision=propose_civic_reconciliation(
        world,authenticated_owner,motion.colony,motion.observed_npc);
    if(!revision ||
       revision->source_generation!=motion.source_generation ||
       revision->leader!=motion.leader ||
       revision->prior_governance_event!=motion.prior_governance_event ||
       revision->prior_ordinal!=motion.prior_ordinal ||
       revision->personal_event!=motion.personal_event ||
       revision->old_policy!=motion.old_policy ||
       revision->retained_agenda!=motion.retained_agenda ||
       revision->proposed_policy!=motion.proposed_policy)
        return std::nullopt;
    return world.record_colony_governance_transition(
        event_id,motion.colony,motion.leader,motion.retained_agenda,
        motion.proposed_policy,
        colony_conflict::TransitionKind::PolicyRevision,
        authenticated_owner);
}
} // namespace galaxy::stage7
