#pragma once
// PLAY-13D.2C: arid-landscape survey prototype, not a hydrology solver.
// Does not create liquid water, forest biomass, cargo or durable player sites.
#include "galaxy/PlanetaryRegions.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace galaxy::planetary {
enum class Watercourse { None, DryArroyo, SeasonalStream, PerennialRiver };
enum class Shelter { Open, Canyon, CavernEntrance };
enum class Landform { OpenWater, LowDesert, HighDesert, AridPrairie,
                      Mountain, RiverValley, ForestedCanyon, CavernForest, Other };
struct Landscape {
    Region region{};
    Landform landform=Landform::Other;
    Watercourse watercourse=Watercourse::None;
    Shelter shelter=Shelter::Open;
    std::uint8_t vegetation_density=0, local_water_potential=0;
    bool underground_light=false; // entrance/skylight required for normal photosynthesis
    bool groundwater_potential=false;
};
inline Landscape survey_landscape(const PlanetProfile& p,
                                  std::uint16_t latitude,std::uint16_t longitude) {
    Landscape out;
    out.region=survey_region(p,latitude,longitude);
    auto& r=out.region;
    const bool atmospheric=p.climate!=WorldClimate::AirlessRock;
    const bool active=p.climate==WorldClimate::Arid || p.climate==WorldClimate::LifeSupporting;
    const bool land=!r.surface_water && atmospheric && r.terrain!=Terrain::Ice;
    const auto noise=[&](std::uint64_t salt) {
        return astronomy_mix(astronomy_mix(p.seed^p.body^salt) ^
            (std::uint64_t(latitude+1)*0x9e3779b97f4a7c15ULL) ^
            (std::uint64_t(longitude+1)*0xbf58476d1ce4e5b9ULL));
    };
    const bool high=r.elevation>=35;
    const bool mountain=r.elevation>=80 || r.terrain==Terrain::Mountain;
    const bool dry=p.climate==WorldClimate::Arid || r.moisture<35;
    // Flow corridor runs through a contiguous longitude belt with a gradual
    // latitude-dependent meander. No validated upstream/downstream topology yet.
    const int center=22+int(latitude/9)+int((astronomy_mix(p.seed^p.body^
        std::uint64_t(latitude/3+7))%3))-1;
    const int d=std::abs(int(longitude)-center);
    const bool valley=land&&active&&d<=2;
    const bool gorge=valley&&r.elevation>=-25&&
        ((noise(17)%4)!=0);
    if(valley) {
        if(d==0 && (noise(20)%5)!=0) out.watercourse=Watercourse::PerennialRiver;
        else if(d<=1 && (noise(21)%3)!=0) out.watercourse=Watercourse::SeasonalStream;
        else out.watercourse=Watercourse::DryArroyo;
    } else if(land&&dry&&(noise(22)%19==0)) {
        out.watercourse=Watercourse::DryArroyo;
    }
    if(gorge) out.shelter=Shelter::Canyon;
    // Some sheltered entrances have groundwater and skylights/entrance light;
    // a closed dark cave is NOT automatically capable of a green forest.
    const bool cave=land&&active&&high&&(noise(23)%13==0);
    if(cave) {
        out.shelter=Shelter::CavernEntrance;
        out.underground_light=(noise(24)%4)!=0;
        out.groundwater_potential=(noise(25)%3)!=0;
    }
    if(r.surface_water) out.landform=Landform::OpenWater;
    else if(!land) out.landform=Landform::Other;
    else if(cave&&out.underground_light&&out.groundwater_potential)
        out.landform=Landform::CavernForest;
    else if(gorge&&(out.watercourse==Watercourse::PerennialRiver ||
                    out.watercourse==Watercourse::SeasonalStream))
        out.landform=Landform::ForestedCanyon;
    else if(valley) out.landform=Landform::RiverValley;
    else if(mountain) out.landform=Landform::Mountain;
    else if(dry && high) out.landform=Landform::HighDesert;
    else if(dry && r.moisture>=18) out.landform=Landform::AridPrairie;
    else if(dry) out.landform=Landform::LowDesert;
    else out.landform=Landform::Other;
    out.local_water_potential=std::uint8_t(r.surface_water?100:
        out.watercourse==Watercourse::PerennialRiver?90:
        out.groundwater_potential?75:
        out.watercourse==Watercourse::SeasonalStream?60:
        out.watercourse==Watercourse::DryArroyo?25:
        std::min(45,int(r.moisture)/2));
    int density=land?int(r.moisture):0;
    if(out.landform==Landform::AridPrairie) density=std::max(density,24);
    if(out.landform==Landform::RiverValley) density=std::max(density,48);
    if(out.landform==Landform::ForestedCanyon) density=std::max(density,72);
    if(out.landform==Landform::CavernForest) density=std::max(density,65);
    out.vegetation_density=std::uint8_t(std::clamp(density,0,100));
    // Report sheltered ecology even if the surface regional mean is dry.
    if(out.landform==Landform::ForestedCanyon ||
       out.landform==Landform::CavernForest) {
        r.vegetation=Vegetation::Woodland;
        r.fertility=std::uint8_t(std::max(55,int(r.fertility)));
    } else if(out.landform==Landform::AridPrairie) {
        r.vegetation=Vegetation::DesertGrass;
    }
    return out;
}
} // namespace galaxy::planetary
