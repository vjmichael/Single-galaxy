#pragma once
// DEF-01A: real local crew watch; an unidentified foreign contact is NOT
// evidence of weapons fire, aggression, criminal intent or player hostility.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::colony_defense {
inline constexpr std::int64_t kLocalWatchRangeMm = 5000;
struct Contact {
    EntityId source=0,foreign_ship=0;
    std::uint64_t observer_npc=0,world_tick=0,distance_squared=0;
    // Trusted host-private authorization provenance, NOT a remotely
    // identified pilot. Never publish foreign_owner as a sensor reading.
    PlayerId foreign_owner=0;
    std::uint32_t system=0;
    std::uint8_t sensor_detail=0;
    std::string observed_description; // repeatable but not necessarily unique
    bool authenticated_hail=false; // NEVER inferred by passive scan
};
// Bounded local watch requires a physically present and duty-eligible named
// colony NPC. No omniscient server listing or speculative hostility marker.
std::vector<Contact> observe_local_contacts(const Simulation& world,
                                              EntityId colony_ship,
                                              std::uint64_t observer_npc);
}
