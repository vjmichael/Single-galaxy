#pragma once
// ECON-03c1: deterministic population-scale consumption and shortage horizon.
// One operation per resource compartment/cohort, NOT per NPC. Units are game kg
// and committed simulation ticks. The scheduler can collapse unchanged periods;
// callers must process intervening production, transfers and policy events.
#include <cstdint>

namespace galaxy {
struct ColonyLifeSupport;
namespace macro_needs {
constexpr std::uint64_t default_cooldown_ticks=20;
struct Ration {
    std::uint64_t food_kg=1, water_kg=1, oxygen_kg=1;
};
struct Result {
    std::int64_t food_kg=0, water_kg=0, oxygen_kg=0;
    bool food_supplied=true, water_supplied=true, oxygen_supplied=true;
    bool fully_supplied() const noexcept {
        return food_supplied && water_supplied && oxygen_supplied;
    }
};
struct Runway {
    std::uint64_t food_periods=0,water_periods=0,oxygen_periods=0;
    std::uint64_t next_due_tick=0,first_unserved_tick=0;
};
// Count boundaries in (previous_tick, target_tick] without ticking each person
// or each missed interval. Both ticks are in the same persistent world clock.
std::uint64_t periods_due(std::uint64_t previous_tick,std::uint64_t target_tick,
                          std::uint64_t cooldown_ticks=default_cooldown_ticks);
std::uint64_t next_due(std::uint64_t now,
                       std::uint64_t cooldown_ticks=default_cooldown_ticks);
// Deduct actual supplies immediately for the due windows; return corresponding
// waste to their originating compartment. A shortage never fabricates matter.
// Inputs can represent trillions of residents, but each stock must fit int64.
Result consume(ColonyLifeSupport& stock,std::uint64_t population,
               std::uint64_t due_periods=1,Ration ration={});
Runway runway(const ColonyLifeSupport& stock,std::uint64_t population,
              std::uint64_t now,std::uint64_t cooldown_ticks=default_cooldown_ticks,
              Ration ration={});
} // namespace macro_needs
} // namespace galaxy
