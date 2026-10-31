#pragma once
#include "galaxy/Galaxy.hpp"
#include "galaxy/Simulation.hpp"
#include <map>
#include <utility>

namespace galaxy {
// B6.4 original seven-system test region. Security and sovereignty are
// independent; this is NOT an EVE star map or asset-derived geography.
struct RegionState {
    GalaxyMap map;
    std::map<std::pair<std::uint32_t,std::uint32_t>,EntityId> directed_gates;
    std::map<std::uint32_t,EntityId> stations;
    std::map<std::uint32_t,EntityId> asteroids;
    EntityId gate(std::uint32_t from,std::uint32_t to) const;
    GalaxyMap operational_routes(const Simulation& world) const;
};
// Requires the existing B6.3 demo fixtures in systems 1 and 2.
// Adds five systems and eight additional bidirectional links, preserving
// the B6.3 home/frontier gate and initial trade journey.
RegionState build_lumen_reach(Simulation& world,EntityId station_home,
    EntityId station_frontier,EntityId rock_home,EntityId rock_frontier,
    EntityId gate_home_to_frontier,EntityId gate_frontier_to_home);
} // namespace galaxy
