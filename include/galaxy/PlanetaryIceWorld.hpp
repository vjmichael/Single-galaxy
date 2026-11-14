#pragma once
// PLAY-13D.2D: cold-world environmental and possible biosphere survey.
// Surface temperatures are distinct from insulated subsurface microclimates.
// Pure deterministic metadata: this does NOT create water, organisms or energy.
#include "galaxy/PlanetaryRegions.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace galaxy::planetary {
enum class IceHabitat {
    SterileExposedIce, SnowField, IceCoveredSea, BrinePocket,
    GeothermalVent, SubglacialWater, ShelteredIceCrevice
};
enum class IceLifePotential {
    None, DormantMicrobes, LiquidBrineMicrobes, SubglacialAquatic,
    ChemosyntheticCommunity
};
struct IceWorldProfile {
    PlanetProfile planet{};
    std::int16_t tropical_max_celsius=0; // 32 F by default
    std::int16_t polar_min_celsius=-55; // about -67 F by default
    bool has_internal_heat=true;
    bool has_subsurface_water=true;
    bool has_salts=true;
};
struct IceRegion {
    Region surface{};
    std::int16_t surface_celsius=0;
    IceHabitat habitat=IceHabitat::SterileExposedIce;
    IceLifePotential life=IceLifePotential::None;
    bool persistent_liquid_niche=false;
    bool photosynthetic_surface_vegetation=false;
    std::uint8_t candidate_heat=0;
};
inline IceRegion survey_ice_region(const IceWorldProfile& ice,
                                   std::uint16_t lat,std::uint16_t lon) {
    if(ice.planet.climate!=WorldClimate::Frozen ||
       ice.tropical_max_celsius>0 ||
       ice.polar_min_celsius>ice.tropical_max_celsius ||
       ice.polar_min_celsius< -150)
        throw std::invalid_argument("invalid ice-world climate");
    IceRegion out;
    out.surface=survey_region(ice.planet,lat,lon);
    // 36 belts, with each hemisphere having equator-to-pole gradient.
    // Avoid enforcing permanent biological activity based on temperature alone.
    const int dist=lat<18?17-int(lat):int(lat)-18;
    const auto seed=astronomy_mix(ice.planet.seed^
       astronomy_mix(ice.planet.body + std::uint64_t(lat)*73ULL +
                                  std::uint64_t(lon)*131ULL));
    const int span=int(ice.tropical_max_celsius)-int(ice.polar_min_celsius);
    const int baseline=int(ice.tropical_max_celsius)-(span*dist)/17;
    // local anomaly always stays within global specified thermal envelope
    const int variation=int(seed%9)-4;
    out.surface_celsius=std::int16_t(std::clamp(
        baseline+variation,int(ice.polar_min_celsius),
        int(ice.tropical_max_celsius)));
    out.surface.temperature_celsius=out.surface_celsius;
    out.surface.terrain=Terrain::Ice;
    out.surface.vegetation=Vegetation::None;
    out.surface.farming_possible=false;
    out.surface.fertility=0;
    out.surface.dormant_seed_bank=false;
    out.surface.episodic_bloom_potential=false;
    out.photosynthetic_surface_vegetation=false;
    out.candidate_heat=std::uint8_t(seed%100);
    const bool water=ice.has_subsurface_water;
    const bool heat=ice.has_internal_heat && out.candidate_heat>=86;
    const bool brine=water && ice.has_salts && ((seed>>8)%13==0);
    const bool buried=water && ((seed>>16)%17==0);
    const bool crevice=((seed>>24)%11==0);
    if(heat && water) {
        out.habitat=IceHabitat::GeothermalVent;
        out.life=IceLifePotential::ChemosyntheticCommunity;
        out.persistent_liquid_niche=true;
    } else if(buried) {
        out.habitat=IceHabitat::SubglacialWater;
        out.life=IceLifePotential::SubglacialAquatic;
        out.persistent_liquid_niche=true;
    } else if(brine) {
        out.habitat=IceHabitat::BrinePocket;
        out.life=IceLifePotential::LiquidBrineMicrobes;
        out.persistent_liquid_niche=true;
    } else if(crevice) {
        out.habitat=IceHabitat::ShelteredIceCrevice;
        out.life=IceLifePotential::DormantMicrobes;
    } else if(out.surface.surface_water) {
        out.habitat=IceHabitat::IceCoveredSea;
    } else if(dist>=12) {
        out.habitat=IceHabitat::SterileExposedIce;
    } else {
        out.habitat=IceHabitat::SnowField;
    }
    // This is a *potential* ecological niche; biosphere existence is an
    // independent world-genesis decision, not guaranteed by a wet hot spot.
    return out;
}
} // namespace galaxy::planetary
