#pragma once
// CONFLICT-01A: motivations and pre-attack plans, NOT weapon or hostility facts.
// An observed foreign ship is never automatically an enemy. Offensive intent
// may arise from stable leadership agendas, scarcity or security concerns.
#include "galaxy/ColonyDefense.hpp"
#include "galaxy/ColonySafety.hpp"
#include <cstdint>
#include <vector>

namespace galaxy::colony_conflict {
enum class Agenda : int {
    Cooperative=0, Isolationist=1, Expansionist=2, Predatory=3
};
enum class Motive : int {
    None=0, Security=1, Territory=2, Scarcity=3, Predation=4
};
enum class Posture : int {
    Observe=0, Warn=1, RequestAid=2, DemandSupplies=3,
    PrepareRaid=4
};
// CONFLICT-01B3: published governance changes require a real resident leader.
enum class DiplomaticPolicy : int {
    Pragmatic=0, Reconciliatory=1, SecurityFirst=2
};
enum class TransitionKind : int {
    Appointment=0, EmergencyVacancy=1, PolicyRevision=2
};
struct GovernanceEpoch {
    std::uint64_t event_id=0,prior_event_id=0,ordinal=0;
    std::uint64_t founding_event_id=0;
    EntityId colony=0;
    std::uint64_t leader_id=0;
    Agenda agenda=Agenda::Cooperative;
    DiplomaticPolicy policy=DiplomaticPolicy::Pragmatic;
    TransitionKind kind=TransitionKind::Appointment;
};
// CONFLICT-01B1: a colony's subjective attitude toward an identifiable
// foreign owner; distinct from any criminal verdict or physical attack fact.
enum class Attitude : int {
    Hostile=-2, Wary=-1, Neutral=0, Friendly=1, Allied=2
};
struct RelationRevision {
    std::uint64_t event_id=0,prior_event_id=0,revision=0;
    std::uint64_t knowledge_id=0,leader_id=0,incident_generation=0;
    EntityId colony=0;
    PlayerId target_owner=0;
    int old_score=0,delta=0,new_score=0;
    Attitude attitude=Attitude::Neutral;
    bool subjective=true;
};
// Transmitted (never fabricated firsthand) GRSM interpretations may change
// one colony's diplomatic attitude differently from another's. The observer
// chain is checked independently by existing knowledge and incident journals.
int relationship_delta(int fidelity,int valence,int confidence);
Attitude attitude_for_score(int score);
struct Decision {
    EntityId source=0,target_ship=0;
    PlayerId target_owner=0;
    std::uint64_t leader_id=0,contact_event_id=0;
    Agenda agenda=Agenda::Cooperative;
    Motive motive=Motive::None;
    Posture posture=Posture::Observe;
    colony_safety::Risk source_risk=colony_safety::Risk::Safe;
    // CONFLICT-01B2: the historically frozen, previous-generation diplomatic
    // belief applied to this intent; zero is neutral/no published revision.
    std::uint64_t relation_event_id=0;
    int relation_score=0;
    // Zero denotes the founding leader. The archived agenda_event in the
    // durable B2 decision identifies the governance epoch if nonzero.
    std::uint64_t governance_event_id=0;
    DiplomaticPolicy diplomatic_policy=DiplomaticPolicy::Pragmatic;
};
// Adjust only nonviolent strategic posture using previously committed opinions.
// A hostile belief is neither a crime finding nor permission to shoot.
Decision evaluate_diplomatic(Decision base,int historical_score,
                            std::uint64_t historical_revision_event,
                            DiplomaticPolicy policy=DiplomaticPolicy::Pragmatic);
// Deterministic bounded strategic choice using ACTUAL locally observed contact
// and ACTUAL live resources. It does not submit a combat command, move an NPC,
// confer guilt, seize materials, or spend fabricated offensive ammunition.
Decision evaluate(const Simulation& world,EntityId colony,
                  std::uint64_t leader,Agenda agenda,
                  const colony_defense::Contact& observed,
                  std::uint64_t contact_event_id);
}
