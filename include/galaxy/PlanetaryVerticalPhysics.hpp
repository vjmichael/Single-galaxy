#pragma once
// S6-F5-C foundation: fixed-point radial motion from VERIFIED original
// ground and physical gravity sources. No Godot acceleration, damage,
// atmosphere, oxygen supply, or suit protection is invented here.
#include <cstdint>
#include <optional>
#include <limits>
#include "galaxy/PlanetaryPhysicalProcesses.hpp"
namespace galaxy::planetary::vertical {
struct State {
    std::int64_t height_mm=0;
    std::int64_t radial_speed_mm_s=0;
    std::int64_t oxygen_ml=0; // passed from a VERIFIED native supply
    std::int64_t exposure_ms=0;
    bool grounded=true;
};
struct Source {
    std::int64_t ground_height_mm=0;
    // This must be an ORIGINAL physical-celestial gravity observation.
    // No default g or guessed density is admitted.
    std::int64_t gravity_mm_s2=0;
    PhysicalHazard hazard=PhysicalHazard::None;
    bool breathable_atmosphere=false;
    bool verified_oxygen_supply=false;
    bool verified_thermal_protection=false;
    // When false, caller handles native whole-kg oxygen conservation itself.
    // This permits vertical dynamics without inventing atmospheric breathability.
    bool evaluate_oxygen=true;
    std::int64_t oxygen_use_ml_s=0;
};
struct Result {
    State next{};
    bool impact=false;
    std::int64_t impact_speed_mm_s=0;
    bool oxygen_exhausted=false;
    bool thermal_unprotected=false;
};
constexpr std::int64_t kMaxHeightMm=100000000;
constexpr std::int64_t kMaxSpeedMmS=60000;
constexpr std::int64_t kJumpSpeedMmS=4500;
constexpr std::int64_t kHardImpactMmS=7000;
inline std::optional<Result> advance(State before,const Source& original,
                                    std::int64_t step_ms,bool jump) noexcept {
    if(step_ms<20||step_ms>250||
       original.gravity_mm_s2<=0||original.gravity_mm_s2>50000||
       original.ground_height_mm< -kMaxHeightMm||
       original.ground_height_mm>kMaxHeightMm||
       before.height_mm< -kMaxHeightMm||
       before.height_mm>kMaxHeightMm||
       before.radial_speed_mm_s< -kMaxSpeedMmS||
       before.radial_speed_mm_s>kMaxSpeedMmS||
       before.oxygen_ml<0 || before.exposure_ms<0 ||
       original.oxygen_use_ml_s<0 || original.oxygen_use_ml_s>10000)
        return std::nullopt;
    // An established contact cannot jump onto an arbitrary much higher
    // ledge simply because a new Godot triangle was observed.
    if(before.grounded &&
       (before.height_mm<original.ground_height_mm-300 ||
        before.height_mm>original.ground_height_mm+300))
        return std::nullopt;
    Result out{};
    out.next=before;
    if(before.grounded){
        out.next.height_mm=original.ground_height_mm;
        out.next.radial_speed_mm_s=jump?kJumpSpeedMmS:0;
        out.next.grounded=!jump;
    }
    if(!out.next.grounded){
        const auto previous=out.next.radial_speed_mm_s;
        const auto dv=original.gravity_mm_s2*step_ms/1000;
        const auto now=previous-dv;
        const auto moved=(previous+now)*step_ms/2000;
        out.next.radial_speed_mm_s=now< -kMaxSpeedMmS?-kMaxSpeedMmS:now;
        out.next.height_mm+=moved;
        if(out.next.height_mm<=original.ground_height_mm){
            out.impact=true;
            out.impact_speed_mm_s=out.next.radial_speed_mm_s<0?
                -out.next.radial_speed_mm_s:0;
            out.next.height_mm=original.ground_height_mm;
            out.next.radial_speed_mm_s=0;
            out.next.grounded=true;
        } else if(out.next.height_mm>kMaxHeightMm) return std::nullopt;
    }
    if(original.evaluate_oxygen && !original.breathable_atmosphere){
        if(!original.verified_oxygen_supply) return std::nullopt;
        const auto use=(original.oxygen_use_ml_s*step_ms+999)/1000;
        out.next.oxygen_ml=before.oxygen_ml<use?0:before.oxygen_ml-use;
        out.oxygen_exhausted=out.next.oxygen_ml==0;
    }
    const bool hazard=original.hazard==PhysicalHazard::Heat ||
        original.hazard==PhysicalHazard::Cold ||
        original.hazard==PhysicalHazard::Flood;
    out.thermal_unprotected=hazard&&!original.verified_thermal_protection;
    if(out.thermal_unprotected){
        if(before.exposure_ms>std::numeric_limits<std::int64_t>::max()-step_ms)
            return std::nullopt;
        out.next.exposure_ms+=step_ms;
    }
    return out;
}
} // namespace galaxy::planetary::vertical
