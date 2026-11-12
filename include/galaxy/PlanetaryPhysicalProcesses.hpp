#pragma once
// S6-D / v128. Physical subaccounts of an ALREADY funded original S6-C
// regional source, not a second inventory or a water/soil genesis engine.
// Forecast weather determines bounded transfer RATES only.
#include "galaxy/PlanetarySeasonalClimate.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
namespace galaxy::planetary {
constexpr std::uint64_t kPhysicalEnvironmentCycleTicks=20;
enum class PhysicalHazard : std::uint8_t {None=0,Cold=1,Heat=2,Flood=3,Drought=4};
struct PhysicalEnvironment {
    // Surface liquid, surface ice, aquifer, retained local atmospheric vapor.
    std::array<std::int64_t,4> water_kg{};
    // Stable topsoil and retained sediment (no second geological source).
    std::array<std::int64_t,2> soil_kg{};
    std::uint64_t updated_tick=0;
    PhysicalHazard hazard=PhysicalHazard::None;
};
inline void verify_physical_environment(
    const PhysicalEnvironment& x,std::int64_t water,std::int64_t soil) {
    if(water<0||water>100000000||soil<0||soil>100000000||unsigned(x.hazard)>4)
        throw std::invalid_argument("invalid planetary physical budget");
    std::int64_t w=0,s=0;
    for(const auto v:x.water_kg) {
        if(v<0||v>water)throw std::invalid_argument("invalid physical water phase");
        w+=v;
    }
    for(const auto v:x.soil_kg) {
        if(v<0||v>soil)throw std::invalid_argument("invalid physical soil phase");
        s+=v;
    }
    if(w!=water||s!=soil)
        throw std::invalid_argument("planetary physical mass does not conserve");
}
inline PhysicalEnvironment initial_physical_environment(
    std::int64_t water,std::int64_t soil,std::int16_t temp,
    std::uint64_t tick) {
    PhysicalEnvironment result;
    result.water_kg[temp<=0?1:0]=water;
    result.soil_kg[0]=soil;
    result.updated_tick=tick;
    verify_physical_environment(result,water,soil);
    return result;
}
inline void advance_physical_environment(
    PhysicalEnvironment& x,const SeasonalCell& weather,bool ocean,
    std::uint64_t tick,std::int64_t water,std::int64_t soil) {
    verify_physical_environment(x,water,soil);
    if(tick<=x.updated_tick || tick%kPhysicalEnvironmentCycleTicks)
        throw std::invalid_argument("invalid planetary environmental cycle");
    auto transfer=[](std::int64_t& from,std::int64_t& to,std::int64_t limit) {
        const auto n=std::min(from,std::max<std::int64_t>(limit,0));
        from-=n;to+=n;
    };
    auto& liquid=x.water_kg[0];
    auto& ice=x.water_kg[1];
    auto& groundwater=x.water_kg[2];
    auto& atmosphere=x.water_kg[3];
    const int t=weather.temperature_c;
    if(t<=0) {
        transfer(liquid,ice,100+std::int64_t(std::min(-t,100))*12);
        transfer(atmosphere,ice,std::int64_t(weather.snowfall)+10);
    } else {
        transfer(ice,liquid,100+std::int64_t(std::min(t,100))*12);
        // Precipitation cannot create matter, only condense held atmosphere.
        transfer(atmosphere,liquid,weather.precipitation);
        if(!ocean)transfer(liquid,groundwater,
                          5+std::int64_t(std::min(weather.precipitation,100U)));
        if(!ocean&&!weather.precipitation)transfer(groundwater,liquid,20);
        transfer(liquid,atmosphere,std::min(weather.evaporation,200U));
    }
    if(!ocean && t>0 && liquid>0 && weather.environmental_risk==3)
        transfer(x.soil_kg[0],x.soil_kg[1],
                 std::min<std::int64_t>(100,liquid/100+1));
    else if(!ocean && t>0 && !weather.environmental_risk)
        transfer(x.soil_kg[1],x.soil_kg[0],10);
    x.hazard=t<=-35?PhysicalHazard::Cold:
        t>=48?PhysicalHazard::Heat:
        !ocean && liquid>0 && weather.environmental_risk==3?
          PhysicalHazard::Flood:
        !ocean && t>25 && liquid<100 && groundwater<100 &&
          !weather.precipitation?PhysicalHazard::Drought:PhysicalHazard::None;
    x.updated_tick=tick;
    verify_physical_environment(x,water,soil);
}
} // namespace galaxy::planetary
