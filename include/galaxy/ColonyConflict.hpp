#pragma once
// CONFLICT-01A: motivations and pre-attack plans, NOT weapon or hostility facts.
// An observed foreign ship is never automatically an enemy. Offensive intent
// may arise from stable leadership agendas, scarcity or security concerns.
#include "galaxy/ColonyDefense.hpp"
#include "galaxy/ColonySafety.hpp"
#include <cstdint>

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
struct Decision {
    EntityId source=0,target_ship=0;
    PlayerId target_owner=0;
    std::uint64_t leader_id=0,contact_event_id=0;
    Agenda agenda=Agenda::Cooperative;
    Motive motive=Motive::None;
    Posture posture=Posture::Observe;
    colony_safety::Risk source_risk=colony_safety::Risk::Safe;
};
// Deterministic bounded strategic choice using ACTUAL locally observed contact
// and ACTUAL live resources. It does not submit a combat command, move an NPC,
// confer guilt, seize materials, or spend fabricated offensive ammunition.
Decision evaluate(const Simulation& world,EntityId colony,
                  std::uint64_t leader,Agenda agenda,
                  const colony_defense::Contact& observed,
                  std::uint64_t contact_event_id);
}
