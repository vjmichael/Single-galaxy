#include "galaxy/Navigation.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
namespace galaxy {
namespace {
int axis(std::int64_t position,std::int64_t velocity,std::int64_t target) {
    const auto distance=target-position;
    const int heading=(distance>0)-(distance<0);
    if(!heading && !velocity)return 0;
    const std::int64_t a=Simulation::kAccelerationMmPerTick2;
    const auto steps=std::llabs(velocity)/a;
    const auto stop_distance=steps*(steps-1)/2*a;
    if(velocity && (heading==0 || (velocity>0?1:-1)!=heading ||
        std::llabs(distance)<=stop_distance+a))return velocity>0?-1:1;
    return heading;
}
}
NavigationDecision assist_to_station(const Entity& ship,Vector2 station) {
    if(ship.type!=EntityType::Ship||ship.dock_state!=DockState::InSpace)
        throw std::invalid_argument("navigation assist needs an undocked ship");
    const auto dx=station.x-ship.position.x;
    const auto dy=station.y-ship.position.y;
    NavigationDecision decision;
    // The stand-off waypoint is inside the docking radius, leaving the ship
    // at rest rather than jumping/teleporting or clipping into the station.
    if(std::llabs(dx)>750 || dy==0)
        decision.waypoint={station.x-(dx>0?1500:-1500),station.y};
    else decision.waypoint={station.x,station.y-(dy>0?1500:-1500)};
    const auto rx=decision.waypoint.x-ship.position.x;
    const auto ry=decision.waypoint.y-ship.position.y;
    decision.arrived=std::llabs(rx)<=250 && std::llabs(ry)<=250 &&
        ship.velocity.x==0 && ship.velocity.y==0;
    if(!decision.arrived) {
        decision.thrust_x=axis(ship.position.x,ship.velocity.x,decision.waypoint.x);
        decision.thrust_y=axis(ship.position.y,ship.velocity.y,decision.waypoint.y);
    }
    return decision;
}
} // namespace galaxy
