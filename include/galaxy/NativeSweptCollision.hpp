#pragma once
// FP3D-ENV-01D: original native ship/asteroid conservative swept contact.
// Ship hull on this graybox is ~8m long with wings ~6m wide. Until true mesh
// collision arrives, enclose that *whole* geometry in a 5.2m bounding sphere;
// this is conservative, not a claim of detailed triangle or capsule contact.
// Integer fraction and first safe millimetres are deterministic on MSVC/Linux.
#include <cstdint>
#include <optional>
namespace galaxy::native_collision {
struct Delta3 {std::int64_t x=0,y=0,z=0;};
constexpr std::int64_t kShipBoundingRadiusMm=5200;
constexpr std::int64_t kPrototypeShipRadiusMm=kShipBoundingRadiusMm; // compatibility
constexpr std::int64_t kContactSkinMm=1;
constexpr std::int64_t kMaxRockRadiusMm=350000;
constexpr std::int64_t kMaxSpeedPerAxisMm=20000;
constexpr std::uint32_t kSubtickScale=1u<<20;
struct Contact {
    Delta3 safe_delta{}; // deepest representable safe motion before contact
    std::uint32_t first_contact_subtick=0; // in [0,kSubtickScale]
};
// All products are bounded by the validated 20,000 mm/tick motion and nearby
// <=375,201mm coordinate deltas; 64-bit intermediate operations cannot overflow.
inline std::optional<Contact> first_swept_ship_rock_contact(
        Delta3 offset,Delta3 motion,std::int64_t rock_radius_mm) noexcept {
    if(rock_radius_mm<1 || rock_radius_mm>kMaxRockRadiusMm ||
       motion.x < -kMaxSpeedPerAxisMm || motion.x > kMaxSpeedPerAxisMm ||
       motion.y < -kMaxSpeedPerAxisMm || motion.y > kMaxSpeedPerAxisMm ||
       motion.z < -kMaxSpeedPerAxisMm || motion.z > kMaxSpeedPerAxisMm)
        return std::nullopt; // caller rejects unbounded simulation data
    const auto r=rock_radius_mm+kShipBoundingRadiusMm+kContactSkinMm;
    const auto cap=r+kMaxSpeedPerAxisMm;
    if(offset.x < -cap || offset.x>cap || offset.y < -cap ||
       offset.y>cap || offset.z < -cap || offset.z>cap)
        return std::nullopt;
    const std::int64_t speed2=motion.x*motion.x+
        motion.y*motion.y+motion.z*motion.z;
    if(!speed2)return std::nullopt;
    const std::int64_t dot=offset.x*motion.x+offset.y*motion.y+
        offset.z*motion.z;
    const std::int64_t radius2=r*r;
    const auto inside=[&](Delta3 moved) {
        const auto dx=offset.x-moved.x,dy=offset.y-moved.y,
                   dz=offset.z-moved.z;
        return dx*dx+dy*dy+dz*dz<=radius2;
    };
    const bool started_inside=inside({});
    if(started_inside){
        // An old save or an undocking error can start in a collider. Do not
        // trap a ship that is moving outward from it. Inward intent is stopped.
        if(dot<=0)return std::nullopt;
        return Contact{{},0};
    }
    if(dot<=0)return std::nullopt; // receding without starting in a rock
    const auto closest=dot>=speed2?kSubtickScale:
        static_cast<std::uint32_t>(dot*kSubtickScale/speed2);
    const auto moved=[&](std::uint32_t t)->Delta3 {
        return {motion.x*static_cast<std::int64_t>(t)/kSubtickScale,
                motion.y*static_cast<std::int64_t>(t)/kSubtickScale,
                motion.z*static_cast<std::int64_t>(t)/kSubtickScale};
    };
    // The closest approach may be a grazing tangent. Test the integer path
    // there before binary searching the *entry* side, never the exit side.
    if(!inside(moved(closest)))return std::nullopt;
    std::uint32_t safe=0,hit=closest;
    while(hit-safe>1){
        const auto middle=safe+(hit-safe)/2;
        if(inside(moved(middle)))hit=middle;
        else safe=middle;
    }
    return Contact{moved(safe),hit};
}
inline bool swept_ship_rock(Delta3 offset,Delta3 motion,
                            std::int64_t rock_radius_mm) noexcept {
    return first_swept_ship_rock_contact(offset,motion,rock_radius_mm).has_value();
}
} // namespace galaxy::native_collision
