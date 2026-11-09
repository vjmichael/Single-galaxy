#pragma once
// FP3D-ENV-01: deterministic native swept ship/asteroid obstacle geometry.
// Original C++ ship motion owns collision; Godot only displays the outcome.
// All inputs are bounded integral millimeters, no floating point or client coordinates.
#include <cstdint>
namespace galaxy::native_collision {
struct Delta3 {std::int64_t x=0,y=0,z=0;};
constexpr std::int64_t kPrototypeShipRadiusMm=1000;
constexpr std::int64_t kMaxRockRadiusMm=350000;
constexpr std::int64_t kMaxSpeedPerAxisMm=20000;
// True if a segment from ship_origin to ship_origin+motion enters the solid
// sphere centered at rock_relative_to_ship. One-millimeter conservative skin.
// A ship already inside and moving AWAY is allowed to escape.
inline bool swept_ship_rock(Delta3 rock_relative_to_ship,Delta3 motion,
                            std::int64_t rock_radius_mm) noexcept {
    if(rock_radius_mm<1 || rock_radius_mm>kMaxRockRadiusMm ||
       motion.x < -kMaxSpeedPerAxisMm || motion.x > kMaxSpeedPerAxisMm ||
       motion.y < -kMaxSpeedPerAxisMm || motion.y > kMaxSpeedPerAxisMm ||
       motion.z < -kMaxSpeedPerAxisMm || motion.z > kMaxSpeedPerAxisMm)
        return false; // the caller separately validates physical-world bounds
    const auto r=rock_radius_mm+kPrototypeShipRadiusMm+1;
    const auto cap=r+kMaxSpeedPerAxisMm;
    const auto q=rock_relative_to_ship;
    if(q.x< -cap || q.x>cap || q.y< -cap || q.y>cap ||
       q.z< -cap || q.z>cap)return false;
    const std::int64_t speed2=motion.x*motion.x+motion.y*motion.y+motion.z*motion.z;
    if(!speed2)return false;
    const std::int64_t dot=q.x*motion.x+q.y*motion.y+q.z*motion.z;
    const auto start2=q.x*q.x+q.y*q.y+q.z*q.z;
    const auto radius2=r*r;
    if(start2<=radius2 && dot<=0)return false; // escaping old penetration
    if(dot<=0)return start2<=radius2;
    if(dot>=speed2){
        const auto dx=q.x-motion.x,dy=q.y-motion.y,dz=q.z-motion.z;
        return dx*dx+dy*dy+dz*dz<=radius2;
    }
    // The exact closest time is dot/speed2 in [0,1]. Integer residual
    // rounding is bounded to <= 1mm/axis; the +1mm skin is deliberate.
    const auto rx=q.x-(motion.x*dot/speed2);
    const auto ry=q.y-(motion.y*dot/speed2);
    const auto rz=q.z-(motion.z*dot/speed2);
    return rx*rx+ry*ry+rz*rz<=radius2;
}
} // namespace galaxy::native_collision
