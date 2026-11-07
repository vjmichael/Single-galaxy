#include "galaxy/Navigation.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace galaxy {
namespace {
// v119 may travel beyond the legacy local XY box. Never subtract signed
// galactic positions or abs(INT64_MIN) in NPC navigation decisions.
std::uint64_t gap(std::int64_t a,std::int64_t b) noexcept {
    const auto x=static_cast<std::uint64_t>(a);
    const auto y=static_cast<std::uint64_t>(b);
    return a>=b ? x-y : y-x;
}
std::int64_t offset_checked(std::int64_t position,std::int64_t delta){
    if((delta>0 && position>std::numeric_limits<std::int64_t>::max()-delta) ||
       (delta<0 && position<std::numeric_limits<std::int64_t>::min()-delta))
        throw std::overflow_error("NPC 3D waypoint outside representable local coordinate");
    return position+delta;
}
int axis(std::int64_t position,std::int64_t velocity,std::int64_t target) {
    const int heading=(target>position)-(target<position);
    if(!heading && !velocity)return 0;
    const auto a=static_cast<std::uint64_t>(Simulation::kAccelerationMmPerTick2);
    const auto magnitude=gap(velocity,0);
    const auto steps=magnitude/a;
    // A physical ship has bounded velocity. Overflow-safe saturation is
    // defensive for calls with malformed/non-authoritative entities.
    const long double stop_distance=
        static_cast<long double>(steps)*static_cast<long double>(steps>0?steps-1:0)*a/2;
    if(velocity && (heading==0 || (velocity>0?1:-1)!=heading ||
        static_cast<long double>(gap(target,position))<=stop_distance+a))
        return velocity>0?-1:1;
    return heading;
}
} // namespace

NavigationDecision assist_to_object(const Entity& ship,const Entity& target,
                                    std::int64_t stand_off_mm) {
    if(ship.type!=EntityType::Ship || ship.dock_state!=DockState::InSpace ||
       ship.jump_state!=ShipJumpState::Ready ||
       target.id==ship.id || ship.system_id!=target.system_id ||
       stand_off_mm<0 || stand_off_mm>Simulation::kWorldBoundMm)
        throw std::invalid_argument("invalid physical object approach");
    // Original object and ship are both authoritative Simulation entities.
    // The caller must still prove same local frame and pilot authorization.
    NavigationDecision decision;
    decision.waypoint=target.position;
    decision.waypoint_z_mm=target.position_z_mm;
    if(gap(target.position.x,ship.position.x)>750 ||
       target.position.y==ship.position.y)
        decision.waypoint.x=offset_checked(target.position.x,
            target.position.x>ship.position.x ? -stand_off_mm : stand_off_mm);
    else
        decision.waypoint.y=offset_checked(target.position.y,
            target.position.y>ship.position.y ? -stand_off_mm : stand_off_mm);
    const auto vx=ship.velocity.x-target.velocity.x;
    const auto vy=ship.velocity.y-target.velocity.y;
    const auto vz=ship.velocity_z_mm_per_tick-target.velocity_z_mm_per_tick;
    decision.arrived=
        gap(decision.waypoint.x,ship.position.x)<=250 &&
        gap(decision.waypoint.y,ship.position.y)<=250 &&
        gap(decision.waypoint_z_mm,ship.position_z_mm)<=250 &&
        vx==0 && vy==0 && vz==0;
    if(!decision.arrived){
        decision.thrust_x=axis(ship.position.x,vx,decision.waypoint.x);
        decision.thrust_y=axis(ship.position.y,vy,decision.waypoint.y);
        decision.thrust_z=axis(ship.position_z_mm,vz,decision.waypoint_z_mm);
    }
    return decision;
}

NavigationDecision assist_to_station(const Entity& ship,Vector2 station) {
    if(ship.type!=EntityType::Ship||ship.dock_state!=DockState::InSpace)
        throw std::invalid_argument("navigation assist needs an undocked ship");
    NavigationDecision decision;
    // Existing station adapter: original non-ship station plane is Z=0.
    decision.waypoint_z_mm=0;
    if(gap(station.x,ship.position.x)>750 ||
       station.y==ship.position.y)
        decision.waypoint={
            offset_checked(station.x,
                station.x>ship.position.x?-1500:1500),station.y};
    else
        decision.waypoint={station.x,
            offset_checked(station.y,
                station.y>ship.position.y?-1500:1500)};
    decision.arrived=
        gap(decision.waypoint.x,ship.position.x)<=250 &&
        gap(decision.waypoint.y,ship.position.y)<=250 &&
        gap(decision.waypoint_z_mm,ship.position_z_mm)<=250 &&
        ship.velocity.x==0 && ship.velocity.y==0 &&
        ship.velocity_z_mm_per_tick==0;
    if(!decision.arrived){
        decision.thrust_x=axis(ship.position.x,ship.velocity.x,
                               decision.waypoint.x);
        decision.thrust_y=axis(ship.position.y,ship.velocity.y,
                               decision.waypoint.y);
        decision.thrust_z=axis(ship.position_z_mm,
                               ship.velocity_z_mm_per_tick,0);
    }
    return decision;
}
} // namespace galaxy
