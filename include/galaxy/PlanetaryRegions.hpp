#pragma once
// PLAY-13D.2A — deterministic regional planetary environments.
// Survey metadata only: not yet host-authoritative construction or farming.
#include "galaxy/Astronomy.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace galaxy::planetary {
enum class WorldClimate { LifeSupporting, Ocean, Arid, AirlessRock, Frozen };
enum class Terrain { DeepOcean, ShallowSea, Coast, Wetland, Grassland, Forest, Dryland, Desert, Mountain, Ice, BareRock };
struct PlanetProfile {
    std::uint64_t body=0, seed=0;
    WorldClimate climate=WorldClimate::LifeSupporting;
    std::uint8_t water_coverage=60; // 0..100, global trend not a universal biome
    std::int16_t average_celsius=12; // degree C; regional offsets below
};
struct Region {
    std::uint16_t latitude_cell=0, longitude_cell=0;
    std::int16_t temperature_celsius=0;
    std::uint8_t moisture=0, fertility=0;
    std::int16_t elevation=0; // abstract signed relief; negative under sea
    Terrain terrain=Terrain::BareRock;
    bool surface_water=false;
    bool farming_possible=false;
};
// 36 latitude rows (-90 to +90), 72 longitude columns; stable bounded cost.
// This smooth, reproducible regional approximation is not a climate simulator.
inline Region survey_region(const PlanetProfile& p,std::uint16_t lat,std::uint16_t lon) {
    if(!p.body || p.water_coverage>100 || lat>=36 || lon>=72)
        throw std::invalid_argument("invalid planetary region request");
    // Coarse regional noise + fine relief; adjacent tiles share their broad
    // climate signal. Seam is wrapped at the longitude boundary.
    const auto coarse_lat=lat/6, coarse_lon=(lon/9)%8;
    const auto noise=[&](std::uint64_t salt,std::uint64_t x,std::uint64_t y) {
        return astronomy_mix(p.seed ^ astronomy_mix(p.body+salt) ^
                             astronomy_mix((x+1)*0x9e3779b97f4a7c15ULL ^
                                           (y+1)*0xbf58476d1ce4e5b9ULL));
    };
    const int lat_distance=lat<18?17-int(lat):int(lat)-18;
    const int polar_cooling=lat_distance*3;
    const int regional_heat=int(noise(1,coarse_lat,coarse_lon)%19)-9;
    const int temp=int(p.average_celsius)-polar_cooling+regional_heat;
    const int moisture_base=int(noise(2,coarse_lat,coarse_lon)%71)+15;
    const int moisture=std::clamp(moisture_base - (p.climate==WorldClimate::Arid?45:0),0,100);
    const int relief=int(noise(3,coarse_lat,coarse_lon)%141)-70
                    +int(noise(4,lat,lon)%41)-20;
    // Ocean coverage influences waterline, not an all-or-nothing biome.
    const int waterline=(int(p.water_coverage)-50)*2;
    const bool ocean=relief<waterline && p.water_coverage>0;
    const bool airless=p.climate==WorldClimate::AirlessRock;
    const bool frozen=p.climate==WorldClimate::Frozen;
    Region r;
    r.latitude_cell=lat;r.longitude_cell=lon;
    r.temperature_celsius=std::int16_t(std::clamp(temp,-150,100));
    r.moisture=std::uint8_t(moisture);
    r.elevation=std::int16_t(relief-waterline);
    r.surface_water=ocean;
    if(airless) r.terrain=Terrain::BareRock;
    else if(ocean) r.terrain=r.elevation < -45?Terrain::DeepOcean:Terrain::ShallowSea;
    else if(temp<=-12 || (frozen && temp<1)) r.terrain=Terrain::Ice;
    else if(relief>=75) r.terrain=Terrain::Mountain;
    else if(r.elevation<=8 && p.water_coverage>0) r.terrain=Terrain::Coast;
    else if(moisture>=76) r.terrain=Terrain::Wetland;
    else if(moisture>=58) r.terrain=Terrain::Forest;
    else if(moisture>=35) r.terrain=Terrain::Grassland;
    else if(moisture>=18) r.terrain=Terrain::Dryland;
    else r.terrain=Terrain::Desert;
    if(airless) r.terrain=Terrain::BareRock;
    r.fertility=std::uint8_t(airless || ocean || frozen || temp<0 || temp>46?0:
        std::clamp(moisture + (temp>=8&&temp<=30?15:0) -
                   (relief>55?35:0),0,100));
    r.farming_possible=!airless&&!ocean&&!frozen&&temp>=0&&temp<=46&&r.fertility>=40;
    return r;
}
} // namespace galaxy::planetary
