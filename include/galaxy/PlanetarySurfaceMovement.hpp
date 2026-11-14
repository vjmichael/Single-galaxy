#pragma once
// S6-F5: deterministic, fixed-point native CONTACT from the ONE original
// watershed survey and the ONE persisted physical-environment account.
// This is a coarse landing-site movement domain, not Godot ArrayMesh physics,
// arbitrary lat/lon teleport, landscape generation, or additional geology.
#include "galaxy/PlanetaryGeography.hpp"
#include "galaxy/PlanetaryPhysicalProcesses.hpp"
#include <cstdint>
#include <optional>

namespace galaxy::planetary::surface_motion {
constexpr std::int64_t kPadExtentMm=32000;      // local site ±32 m
constexpr std::int64_t kPlayerStepMm=300;       // maximum displacement per committed tick
constexpr std::int64_t kNpcStepMm=250;
constexpr std::int64_t kShuttleHatchRadiusMm=1500;
constexpr std::int64_t kActorClearanceMm=500;
// Geographic watershed elevation is a survey ordinal, not calibrated
// mountain altitude. Adopt a bounded deterministic collision displacement
// on this local pad (1 survey elevation point = 1m of contact height).
// Wider terrain geometry, hills and coastline boundaries remain S6-F6 work.
constexpr std::int64_t kGroundMmPerSurveyUnit=1000;
struct Contact {
    std::int64_t ground_y_mm=0;
    PhysicalHazard hazard=PhysicalHazard::None;
    bool safe_to_walk=false;
};
inline bool radial_within(std::int64_t x,std::int64_t z,
                          std::int64_t radius) noexcept {
    if(x< -kPadExtentMm || x>kPadExtentMm ||
       z< -kPadExtentMm || z>kPadExtentMm)return false;
    return x*x+z*z<=radius*radius;
}
inline bool near_shuttle_hatch(std::int64_t x,std::int64_t z) noexcept {
    return radial_within(x,z,kShuttleHatchRadiusMm);
}
inline bool within_step(std::int64_t dx,std::int64_t dz,
                        bool player) noexcept {
    const auto max=player?kPlayerStepMm:kNpcStepMm;
    return (dx || dz) && dx>=-max && dx<=max &&
           dz>=-max && dz<=max && dx*dx+dz*dz<=max*max;
}
inline bool native_surface_structure_clear(
    std::int64_t x,std::int64_t z,bool housing,bool utility) noexcept {
    // Conservative occupied *equipment pads*, active only after the ORIGINAL
    // installed construction is complete. Exact building meshes/doors need
    // native building footprints in a later construction geometry pass.
    if(housing && x>=8000 && x<=14000 && z>=-3500 && z<=3500)return false;
    if(utility && x<=-8000 && x>=-14000 && z>=-3500 && z<=3500)return false;
    return true;
}
inline std::optional<Contact> original_ground_contact(
    const GeographicSite& survey,const PhysicalEnvironment* physical,
    std::int64_t x_mm,std::int64_t z_mm) {
    if(x_mm< -kPadExtentMm || x_mm>kPadExtentMm ||
       z_mm< -kPadExtentMm || z_mm>kPadExtentMm ||
       survey.landscape.region.surface_water)
        return std::nullopt;
    const auto elevation=survey.landscape.region.elevation;
    Contact contact{};
    contact.ground_y_mm=std::int64_t(elevation)*kGroundMmPerSurveyUnit;
    contact.hazard=physical?physical->hazard:PhysicalHazard::None;
    contact.safe_to_walk=
        contact.hazard!=PhysicalHazard::Cold &&
        contact.hazard!=PhysicalHazard::Heat &&
        contact.hazard!=PhysicalHazard::Flood;
    return contact;
}
} // namespace galaxy::planetary::surface_motion
