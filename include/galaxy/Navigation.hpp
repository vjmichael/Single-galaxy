#pragma once
#include "galaxy/Simulation.hpp"
namespace galaxy {
// Development navigation assist: ordinary thrust, never relocation or warp.
// Input is the actual committed world position/velocity and a station position.
// Commands still travel through Simulation's normal authorization and physics.
struct NavigationDecision { int thrust_x=0,thrust_y=0; bool arrived=false; Vector2 waypoint{}; };
NavigationDecision assist_to_station(const Entity& ship, Vector2 station);
} // namespace galaxy
