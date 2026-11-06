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

NavigationDecision assist_to_object(const Entity& ship,const Entity& target,
                                    std::int64_t stand_off_mm) {
    if(ship.type!=EntityType::Ship || ship.dock_state!=DockState::InSpace ||
       ship.jump_state!=ShipJumpState::Ready ||
       target.id==ship.id || ship.system_id!=target.system_id ||
       stand_off_mm<0 || stand_off_mm>Simulation::kWorldBoundMm)
        throw std::invalid_argument("invalid physical object approach");
    // The caller must additionally verify local-frame custody and target
    // visibility/authorization. A target's velocity is not ship authority.
    const auto dx=target.position.x-ship.position.x;
    const auto dy=target.position.y-ship.position.y;
    NavigationDecision decision;
    // Stand off along the major relative axis, never through a target.
    // For stationary stations this preserves FLIGHT-02's docking waypoint.
    decision.waypoint=target.position;
    if(std::llabs(dx)>750 || dy==0)
        decision.waypoint.x-=(dx>0?stand_off_mm:-stand_off_mm);
    else decision.waypoint.y-=(dy>0?stand_off_mm:-stand_off_mm);
    const auto rx=decision.waypoint.x-ship.position.x;
    const auto ry=decision.waypoint.y-ship.position.y;
    const auto vx=ship.velocity.x-target.velocity.x;
    const auto vy=ship.velocity.y-target.velocity.y;
    decision.arrived=std::llabs(rx)<=250 && std::llabs(ry)<=250 &&
                     vx==0 && vy==0;
    if(!decision.arrived) {
        decision.thrust_x=axis(ship.position.x,vx,decision.waypoint.x);
        decision.thrust_y=axis(ship.position.y,vy,decision.waypoint.y);
    }
    return decision;
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
