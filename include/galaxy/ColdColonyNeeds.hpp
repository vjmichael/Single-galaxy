#pragma once
// ECON-03c2 — event-driven population-needs accounting for distant colonies.
// This is a separate, opt-in kernel; do not skip active Simulation::step() with
// it until every other relevant event (factory, attack, power, etc.) is routed.
#include "galaxy/MacroNeeds.hpp"
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <vector>

namespace galaxy::cold_colony {
struct State {
    std::uint64_t processed_tick=0;
    std::uint64_t population=0;
    ColonyLifeSupport habitat{}; // consumer's REAL local stock
    ColonyLifeSupport depot{};   // separately stored, REAL stock
};
struct Event {
    enum class Kind { DeliveredFromDepot, PopulationChanged };
    std::uint64_t tick=0;
    Kind kind=Kind::DeliveredFromDepot;
    // For deliveries, all non-negative quantities move out of this cohort's
    // existing depot; upstream NPC-haul authorization remains the host's job.
    std::int64_t food_kg=0, water_kg=0, oxygen_kg=0;
    // Used ONLY by PopulationChanged; never creates an individual NPC record.
    std::uint64_t new_population=0;
};
struct AdvanceResult {
    std::uint64_t periods=0;
    std::uint64_t shortage_periods=0;
    std::uint64_t events=0;
};
// Advance arbitrary numbers of logical ticks without a tick/person loop.
// Events must be in nondecreasing tick order and strictly after processed_tick.
// All events at a given tick execute BEFORE that tick's needs deadline.
// Atomic: on any invalid/overflowing event, state remains unchanged.
// Supports pause/resume: the State fields are the exact checkpoint boundary.
AdvanceResult advance(State& state, std::uint64_t target_tick,
                      const std::vector<Event>& events={},
                      std::uint64_t cooldown_ticks=macro_needs::default_cooldown_ticks,
                      macro_needs::Ration ration={});
} // namespace galaxy::cold_colony
