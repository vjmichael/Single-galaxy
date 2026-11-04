#pragma once
// PGF-01B.2: one authoritative candidate geography projection.
// All terrain, oceans, waterways and valleys come from WatershedMap.
// The previous free-standing region/landscape surveys are compatibility
// prototypes; do not use them for authoritative world placement.
#include "galaxy/PlanetaryWatershed.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace galaxy::planetary {
struct GeographicSite {
    PlanetaryRegionKey key{};
    Landscape landscape{};
    int downstream_latitude=-1, downstream_longitude=-1;
    std::uint64_t catchment_runoff=0; // climate score, NOT liquid kg
    std::int32_t spill_height=0;
    bool inland_terminal=false;
};
inline Watercourse watercourse_from_drainage(DrainageKind d) {
    switch(d) {
        case DrainageKind::Arroyo: return Watercourse::DryArroyo;
        case DrainageKind::SeasonalStream: return Watercourse::SeasonalStream;
        case DrainageKind::PerennialRiver: return Watercourse::PerennialRiver;
        default: return Watercourse::None;
    }
}
inline GeographicSite survey_geographic_site(
    const WatershedMap& map,const PlanetaryWorldKey& key,
    const PlanetaryEnvelope& envelope,int latitude,int longitude) {
    validate_planetary_envelope(key,envelope);
    if(key.galaxy_seed!=map.key.galaxy_seed ||
       key.system!=map.key.system || key.body!=map.key.body ||
       key.generator_revision!=map.key.generator_revision ||
       envelope.climate!=map.climate ||
       envelope.water_coverage!=map.water_coverage ||
       envelope.average_celsius!=map.average_celsius)
        throw std::invalid_argument("watershed world identity mismatch");
    const auto& source=map.at(latitude,longitude);
    const auto profile=derive_region_profile(key,envelope);
    GeographicSite site;
    site.key={key.body,key.generator_revision,
              std::uint16_t(latitude),std::uint16_t(longitude)};
    site.landscape=survey_landscape(profile,std::uint16_t(latitude),
                                   std::uint16_t(longitude));
    auto& land=site.landscape;
    auto& r=land.region;
    // Replace independently generated environmental elevations and waterways
    // with the graph's single-source data.
    r.elevation=std::int16_t(std::clamp(source.elevation,-32768,32767));
    r.surface_water=source.ocean;
    // Reject independent cave/groundwater guesses in the authoritative view.
    land.underground_light=false;
    land.groundwater_potential=false;
    land.watercourse=source.ocean?Watercourse::None:
                     watercourse_from_drainage(source.flow);
    site.catchment_runoff=source.drainage;
    site.spill_height=source.spill_height;
    site.inland_terminal=!source.ocean&&source.downstream<0;
    if(source.downstream>=0) {
        site.downstream_latitude=source.downstream/kWatershedColumns;
        site.downstream_longitude=source.downstream%kWatershedColumns;
    }
    const bool arid=envelope.climate==WorldClimate::Arid;
    const bool sterile=envelope.climate==WorldClimate::AirlessRock;
    const bool frozen=envelope.climate==WorldClimate::Frozen;
    const bool active_water=land.watercourse==Watercourse::SeasonalStream ||
                            land.watercourse==Watercourse::PerennialRiver;
    // Candidate river corridors cannot generate ecological benefits if this
    // world is airless or frozen; seasonally frozen water is PGF-01C.
    const bool river_valley=active_water&&!sterile&&!frozen;
    if(source.ocean) {
        // An ocean basin on an ice world is covered ice, NOT liquid sea.
        r.terrain=frozen?Terrain::Ice:Terrain::ShallowSea; // bathymetry later
        land.landform=Landform::OpenWater;
        land.shelter=Shelter::Open;
        r.farming_possible=false;
        r.fertility=0;
        r.vegetation=Vegetation::None;
        land.vegetation_density=0;
        land.local_water_potential=frozen?0:100;
    } else if(sterile) {
        r.terrain=Terrain::BareRock;
        r.farming_possible=false;
        r.fertility=0;
        r.vegetation=Vegetation::None;
        land.landform=Landform::Other;
        land.shelter=Shelter::Open;
        land.vegetation_density=0;
        land.local_water_potential=0;
    } else if(frozen) {
        // Keep surface ice, while recording graph potential for a future thaw
        // solver. A frozen potential river is not a flowing liquid river.
        r.terrain=Terrain::Ice;
        r.farming_possible=false;
        r.fertility=0;
        r.vegetation=Vegetation::None;
        land.landform=Landform::Other;
        land.shelter=Shelter::Open;
        land.local_water_potential=0;
        land.vegetation_density=0;
        land.watercourse=Watercourse::None;
    } else {
        const bool high=source.elevation>=160;
        const bool mountain=source.elevation>=360;
        const bool dry=arid||r.moisture<35;
        // Clear old independently generated canyon/forest/river artifacts.
        land.shelter=Shelter::Open;
        if(river_valley) {
            // The graph owns river placement. Gorge potential depends on
            // relief compared with its downstream site, not arbitrary longitude.
            const bool canyon=source.downstream>=0 &&
                 source.elevation-map.cells[source.downstream].elevation>=80;
            land.landform=canyon?Landform::ForestedCanyon:Landform::RiverValley;
            if(canyon) land.shelter=Shelter::Canyon;
            r.terrain=canyon?Terrain::Forest:Terrain::Wetland;
            land.local_water_potential=land.watercourse==Watercourse::PerennialRiver?90:60;
            // Ecosystem expression is conditional on independently seeded life.
            land.vegetation_density=std::uint8_t(
                std::max(int(land.vegetation_density),canyon?72:48));
            r.fertility=std::uint8_t(std::max(int(r.fertility),55));
        } else if(mountain) {
            land.landform=Landform::Mountain;
            r.terrain=Terrain::Mountain;
            land.local_water_potential=std::uint8_t(
                land.watercourse==Watercourse::DryArroyo?25:std::min(45,int(r.moisture)/2));
        } else if(dry) {
            land.landform=high?Landform::HighDesert:
                         r.moisture>=18?Landform::AridPrairie:Landform::LowDesert;
            r.terrain=high?Terrain::Dryland:
                      r.moisture>=18?Terrain::Grassland:Terrain::Desert;
            if(land.watercourse==Watercourse::DryArroyo)
                land.local_water_potential=25;
        } else {
            land.landform=Landform::Other;
            land.local_water_potential=std::uint8_t(std::min(45,int(r.moisture)/2));
        }
        if(envelope.biosphere_present) {
            if(river_valley) r.vegetation=Vegetation::Woodland;
            else if(land.landform==Landform::AridPrairie)
                r.vegetation=Vegetation::DesertGrass;
        } else {
            r.vegetation=Vegetation::None;
            r.dormant_seed_bank=false;
            r.episodic_bloom_potential=false;
            land.vegetation_density=0;
        }
        r.farming_possible=r.fertility>=40 && r.temperature_celsius>=0 &&
                           r.temperature_celsius<=46 &&
                           r.terrain!=Terrain::Mountain;
    }
    // World without indigenous biosphere can still have technically arable
    // physical ground, but no free grown vegetation.
    if(!envelope.biosphere_present) {
        r.vegetation=Vegetation::None;
        r.dormant_seed_bank=false;
        r.episodic_bloom_potential=false;
        land.vegetation_density=0;
    }
    return site;
}
} // namespace galaxy::planetary
