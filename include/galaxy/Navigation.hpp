#pragma once
#include "galaxy/Simulation.hpp"
namespace galaxy {
// A pure guidance decision: never teleports, transfers cargo or authorizes
// interaction. All motion is enacted by the original native Thrust commands.
struct NavigationDecision {
    int thrust_x=0, thrust_y=0, thrust_z=0;
    bool arrived=false;
    Vector2 waypoint{};
    std::int64_t waypoint_z_mm=0;
};
// FLIGHT-03: one target-neutral local approach solver. Position and velocity
// must be read from the same authoritative tick, with frame custody checked
// by the caller. stand_off_mm is the requested separation from target center;
// it is not permission to dock, mine, rescue or move inventory.
NavigationDecision assist_to_object(const Entity& ship, const Entity& target,
                                    std::int64_t stand_off_mm);
// Compatibility adapter for the original FLIGHT-02 station docking flow.
NavigationDecision assist_to_station(const Entity& ship, Vector2 station);
} // namespace galaxy
