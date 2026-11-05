#pragma once
// PLAY-13D PGF-01A: versioned deterministic adapter to existing region surveys.
// This is survey metadata only, not a water-flow solver, climate simulation,
// actual biosphere, authoritative resource genesis, or a host game command.
#include "galaxy/PlanetaryRegions.hpp"
#include "galaxy/PlanetaryLandscape.hpp"
#include "galaxy/PlanetaryIceWorld.hpp"
#include <cstdint>
#include <stdexcept>

namespace galaxy::planetary {
struct PlanetaryWorldKey {
    std::uint64_t galaxy_seed=0;
    std::uint32_t system=0;
    std::uint64_t body=0;
    std::uint32_t generator_revision=1;
};
struct PlanetaryEnvelope {
    WorldClimate climate=WorldClimate::LifeSupporting;
    std::uint8_t water_coverage=60;
    std::int16_t average_celsius=12;
    bool biosphere_present=false; // independent world-genesis decision
};
struct PlanetaryRegionKey {
    std::uint64_t body=0;
    std::uint32_t revision=1;
    std::uint16_t latitude=0, longitude=0;
};
struct PlanetarySurvey {
    PlanetaryRegionKey key{};
    Region surface{};
    bool biosphere_present=false;
    bool land_supports_farming=false;
    bool environment_might_support_life=false;
    bool has_candidate_liquid_water=false; // not a consumable inventory
};
inline void validate_planetary_envelope(
    const PlanetaryWorldKey& k,const PlanetaryEnvelope& e) {
    if(!k.galaxy_seed || !k.system || !k.body || !k.generator_revision ||
       e.water_coverage>100 || e.average_celsius< -150 ||
       e.average_celsius>100)
        throw std::invalid_argument("invalid planetary world key or envelope");
    if(e.climate==WorldClimate::AirlessRock && e.biosphere_present)
        throw std::invalid_argument("airless sterile world cannot host surface biosphere");
}
inline PlanetProfile derive_region_profile(
    const PlanetaryWorldKey& k,const PlanetaryEnvelope& e) {
    validate_planetary_envelope(k,e);
    // Stable domain separation from astronomical system, body and revision.
    const auto s=astronomy_mix(k.galaxy_seed ^
           astronomy_mix(std::uint64_t(k.system)*0x9e3779b97f4a7c15ULL) ^
           astronomy_mix(k.body) ^
           astronomy_mix(std::uint64_t(k.generator_revision)*0xbf58476d1ce4e5b9ULL));
    return PlanetProfile{k.body,s,e.climate,e.water_coverage,e.average_celsius};
}
inline PlanetarySurvey survey_planetary_world(
    const PlanetaryWorldKey& k,const PlanetaryEnvelope& e,
    std::uint16_t latitude,std::uint16_t longitude) {
    auto p=derive_region_profile(k,e);
    if(latitude>=36 || longitude>=72)
        throw std::invalid_argument("invalid planetary survey location");
    PlanetarySurvey survey;
    survey.key={k.body,k.generator_revision,latitude,longitude};
    survey.surface=survey_region(p,latitude,longitude);
    survey.biosphere_present=e.biosphere_present;
    survey.land_supports_farming=survey.surface.farming_possible;
    // Existing Frozen survey is a coarse surface proxy. More detailed
    // subsurface niche potential requires the separate ice-world adapter.
    if(e.climate==WorldClimate::Frozen) {
        IceWorldProfile ice{};
        ice.planet=p;
        const auto ice_region=survey_ice_region(ice,latitude,longitude);
        survey.surface=ice_region.surface;
        survey.land_supports_farming=false;
        survey.has_candidate_liquid_water=ice_region.persistent_liquid_niche;
        survey.environment_might_support_life=ice_region.life!=IceLifePotential::None;
    } else {
        // A region may be ecologically viable even if no life ever evolved.
        survey.environment_might_support_life=
           e.climate!=WorldClimate::AirlessRock &&
           (survey.surface.surface_water || survey.surface.vegetation!=Vegetation::None ||
            survey.surface.farming_possible);
        survey.has_candidate_liquid_water=survey.surface.surface_water;
    }
    // Report biology as observationally present only when this planet's
    // persisted biosphere flag is true; candidate climate alone is not life.
    if(!e.biosphere_present) {
        survey.surface.vegetation=Vegetation::None;
        survey.surface.dormant_seed_bank=false;
        survey.surface.episodic_bloom_potential=false;
    }
    return survey;
}
} // namespace galaxy::planetary
