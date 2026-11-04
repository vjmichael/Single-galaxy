#pragma once
// PLAY-13C.1 — exact, reversible bridge between persistent cosmic millimeters
// and the existing short-range local-space coordinate system.
// No flight authority, ship movement, gravity, resource creation or world mutation.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy {
constexpr std::int64_t kLocalFrameLimitMm = 1000000000000LL; // 1000 km

struct LocalSpaceFrame {
    std::uint32_t system=0;
    std::uint64_t anchor_body_id=0; // durable celestial identity, never UI-selected coordinates
    CosmicPosition origin{};
};
struct LocalOffset3 { std::int64_t x_mm=0,y_mm=0,z_mm=0; };

inline std::optional<std::int64_t> checked_cosmic_add(std::int64_t a,std::int64_t b) {
    constexpr auto hi=std::numeric_limits<std::int64_t>::max();
    constexpr auto lo=std::numeric_limits<std::int64_t>::min();
    if((b>0 && a>hi-b)||(b<0 && a<lo-b)) return std::nullopt;
    return a+b;
}
inline std::optional<std::int64_t> checked_cosmic_sub(std::int64_t a,std::int64_t b) {
    constexpr auto hi=std::numeric_limits<std::int64_t>::max();
    constexpr auto lo=std::numeric_limits<std::int64_t>::min();
    if((b<0 && a>hi+b)||(b>0 && a<lo+b)) return std::nullopt;
    return a-b;
}
inline bool valid_local_offset(const LocalOffset3& p) {
    constexpr auto limit=kLocalFrameLimitMm;
    return p.x_mm>=-limit && p.x_mm<=limit &&
           p.y_mm>=-limit && p.y_mm<=limit &&
           p.z_mm>=-limit && p.z_mm<=limit;
}
inline std::optional<LocalOffset3> cosmic_to_local(
    const LocalSpaceFrame& frame,const CosmicPosition& world) {
    const auto x=checked_cosmic_sub(world.x_mm,frame.origin.x_mm);
    const auto y=checked_cosmic_sub(world.y_mm,frame.origin.y_mm);
    const auto z=checked_cosmic_sub(world.z_mm,frame.origin.z_mm);
    if(!x || !y || !z) return std::nullopt;
    const LocalOffset3 result{*x,*y,*z};
    return valid_local_offset(result)?std::optional<LocalOffset3>(result):std::nullopt;
}
inline std::optional<CosmicPosition> local_to_cosmic(
    const LocalSpaceFrame& frame,const LocalOffset3& local) {
    if(!valid_local_offset(local)) return std::nullopt;
    const auto x=checked_cosmic_add(frame.origin.x_mm,local.x_mm);
    const auto y=checked_cosmic_add(frame.origin.y_mm,local.y_mm);
    const auto z=checked_cosmic_add(frame.origin.z_mm,local.z_mm);
    if(!x || !y || !z) return std::nullopt;
    return CosmicPosition{*x,*y,*z};
}
// Existing Vector2 simulation cannot represent height. Refuse silent z loss.
inline std::optional<Vector2> local_to_legacy_plane(const LocalOffset3& local) {
    if(!valid_local_offset(local) || local.z_mm!=0) return std::nullopt;
    return Vector2{local.x_mm,local.y_mm};
}
} // namespace galaxy
