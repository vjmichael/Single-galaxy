#pragma once
// S6-E/v130 ecological production rules. Actual material is owned by the
// original planetary site and S6-C/D physical region, not this assessment.
// These policies NEVER turn weather scores, precipitation or sunlight into kg.
#include "galaxy/PlanetaryPhysicalProcesses.hpp"
#include <cstdint>
#include <stdexcept>

namespace galaxy::planetary {
enum class CropMode : std::uint8_t {Blocked=0, Field=1, Controlled=2};
struct CultivationBudget {
    CropMode mode=CropMode::Blocked;
    PhysicalHazard hazard=PhysicalHazard::None;
    std::int64_t water_kg=0,nutrients_kg=0,food_kg=0,wastewater_kg=0;
    std::int64_t electricity_units=0; // service, NOT mass
};
inline PhysicalHazard site_physical_hazard(const PhysicalEnvironment* state){
    return state?state->hazard:PhysicalHazard::None;
}
inline std::int64_t habitat_hvac_energy(PhysicalHazard risk) {
    return risk==PhysicalHazard::Cold || risk==PhysicalHazard::Heat ? 3 : 1;
}
inline bool surface_work_hazard(PhysicalHazard risk){
    return risk==PhysicalHazard::Flood;
}
inline bool can_harvest_open_field(const PhysicalEnvironment* state,
                                   bool biosphere) {
    return state && biosphere && state->hazard==PhysicalHazard::None &&
        state->soil_kg[0]>=100 &&
        (state->water_kg[0]>=100 || state->water_kg[2]>=100);
}
inline CultivationBudget cultivation_budget(
    const PhysicalEnvironment* state,bool native_biosphere,
    bool controlled_facility_ready,std::int64_t stored_electricity,
    bool contained_original_soil=false) {
    const auto risk=site_physical_hazard(state);
    if(surface_work_hazard(risk))return {};
    if(can_harvest_open_field(state,native_biosphere))
        return {CropMode::Field,risk,10,5,12,3,0};
    // Hydroponics is an explicitly POWERED original installed farm, not
    // a claim that an uninhabited world spontaneously grows plants.
    if(!controlled_facility_ready)return {};
    const auto stressed=(risk==PhysicalHazard::Cold ||
                         risk==PhysicalHazard::Heat ||
                         risk==PhysicalHazard::Drought);
    // The exact physically extracted site-local soil bed is a reusable
    // medium for this installed farm; it is not consumed or minted as a crop.
    const auto base_power=stressed?3:2;
    const auto power=base_power-(contained_original_soil?1:0);
    if(stored_electricity<power)return {};
    // Stress converts LESS of the same original inputs into usable food.
    // The withheld mass remains in the site's existing wastewater inventory.
    const auto food=(stressed?10:11)+(contained_original_soil?1:0);
    const CultivationBudget result{CropMode::Controlled,risk,10,5,food,
                                  15-food,power};
    if(result.water_kg+result.nutrients_kg!=
       result.food_kg+result.wastewater_kg)
        throw std::logic_error("ecological production generated matter");
    return result;
}
} // namespace galaxy::planetary
