#pragma once
// SPACE-04B incremental foundation: presentation/near-field coordinates never
// become a second physical ship position or an invisible world boundary.
// Source of truth remains an explicitly supplied galactic::Point.
#include "galaxy/GalacticCoordinates.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::galactic {
struct Local3 {
    std::int64_t x_mm=0, y_mm=0, z_mm=0;
    bool operator==(const Local3& b) const {
        return x_mm==b.x_mm && y_mm==b.y_mm && z_mm==b.z_mm;
    }
};
struct FloatingFrame {
    Point origin{};
    // This limit is an accuracy/near-field window, never a motion clamp.
    std::int64_t radius_mm=1000000000LL;
    bool valid() const {return origin.valid() && radius_mm>0 &&
                               radius_mm<=kSectorEdgeMm;}
};
inline std::optional<Local3> local_from_galactic(
    const FloatingFrame& frame,Point ship){
    if(!frame.valid()||!ship.valid())return std::nullopt;
    const auto delta=nearby_displacement(ship,frame.origin);
    if(!delta)return std::nullopt;
    if(delta->x_mm > frame.radius_mm || delta->x_mm < -frame.radius_mm ||
       delta->y_mm > frame.radius_mm || delta->y_mm < -frame.radius_mm ||
       delta->z_mm > frame.radius_mm || delta->z_mm < -frame.radius_mm)
        return std::nullopt;
    return Local3{delta->x_mm,delta->y_mm,delta->z_mm};
}
inline std::optional<Point> galactic_from_local(
    const FloatingFrame& frame,Local3 local){
    if(!frame.valid() ||
       local.x_mm>frame.radius_mm || local.x_mm < -frame.radius_mm ||
       local.y_mm>frame.radius_mm || local.y_mm < -frame.radius_mm ||
       local.z_mm>frame.radius_mm || local.z_mm < -frame.radius_mm)
        return std::nullopt;
    return advance(frame.origin,
       Velocity{local.x_mm,local.y_mm,local.z_mm});
}
// Rebase the local origin to an exact canonical position. No world/ship
// position is modified, so no tick, velocity, cargo or ownership changes.
inline std::optional<FloatingFrame> rebase(
    FloatingFrame old_frame,Point new_origin){
    if(!old_frame.valid()||!new_origin.valid())return std::nullopt;
    return FloatingFrame{new_origin,old_frame.radius_mm};
}
// A local engine can return false when a target is out of its near-field
// range; this is not authority to delete the target or stop its flight.
inline bool locally_visible(FloatingFrame frame,Point target){
    return local_from_galactic(frame,target).has_value();
}
} // namespace galaxy::galactic
