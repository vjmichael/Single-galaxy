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
// S6-D cross-region physical flux: bounded SOURCE-OWNED matter, not rainfall.
// The original planetresource region stock is moved in lockstep with its
// v128 physical phase subaccount. Each edge is chosen by the authoritative
// canonical watershed. Input budgets are immutable during a planning pass;
// the original world STEP commits the complete deterministic resulting map.
struct NeighborPhysicalFlux {
    std::int64_t liquid_kg=0;
    std::int64_t aquifer_kg=0;
    std::int64_t sediment_kg=0;
    bool empty() const noexcept {
        return !liquid_kg && !aquifer_kg && !sediment_kg;
    }
};
inline NeighborPhysicalFlux propose_neighbor_flux(
    const PhysicalEnvironment& upstream,const PhysicalEnvironment& downstream,
    const std::array<std::int64_t,4>& from_stock,
    const std::array<std::int64_t,4>& to_stock,
    bool canonical_downstream,bool downstream_ocean) {
    if(!canonical_downstream)return {};
    verify_physical_environment(upstream,from_stock[0],from_stock[1]);
    verify_physical_environment(downstream,to_stock[0],to_stock[1]);
    // Absolute rate caps prevent one clock tick from teleporting lakes or
    // creating huge erosion in a single 36x72 climate/region cell.
    const auto liquid=std::min<std::int64_t>(
        {upstream.water_kg[0]/20,50,100000000-to_stock[0]});
    const auto aquifer=upstream.water_kg[2]>downstream.water_kg[2]+40?
        std::min<std::int64_t>({(upstream.water_kg[2]-
                 downstream.water_kg[2])/20,10,100000000-to_stock[0]-liquid}):0;
    const auto sediment=upstream.hazard==PhysicalHazard::Flood ||
                        downstream_ocean?
        std::min<std::int64_t>(
            {upstream.soil_kg[1]/20,10,100000000-to_stock[1]}):0;
    return {std::max<std::int64_t>(0,liquid),
            std::max<std::int64_t>(0,aquifer),
            std::max<std::int64_t>(0,sediment)};
}
inline void apply_neighbor_flux(
    PhysicalEnvironment& upstream,PhysicalEnvironment& downstream,
    std::array<std::int64_t,4>& from_stock,
    std::array<std::int64_t,4>& to_stock,
    const NeighborPhysicalFlux& flux) {
    if(flux.liquid_kg<0 || flux.aquifer_kg<0 || flux.sediment_kg<0 ||
       flux.liquid_kg>upstream.water_kg[0] ||
       flux.aquifer_kg>upstream.water_kg[2] ||
       flux.sediment_kg>upstream.soil_kg[1] ||
       flux.liquid_kg+flux.aquifer_kg>100000000-to_stock[0] ||
       flux.sediment_kg>100000000-to_stock[1])
        throw std::invalid_argument("physical interregion flux overdraw");
    const auto before_water=from_stock[0]+to_stock[0];
    const auto before_soil=from_stock[1]+to_stock[1];
    upstream.water_kg[0]-=flux.liquid_kg;
    downstream.water_kg[0]+=flux.liquid_kg;
    upstream.water_kg[2]-=flux.aquifer_kg;
    downstream.water_kg[2]+=flux.aquifer_kg;
    upstream.soil_kg[1]-=flux.sediment_kg;
    downstream.soil_kg[1]+=flux.sediment_kg;
    from_stock[0]-=flux.liquid_kg+flux.aquifer_kg;
    to_stock[0]+=flux.liquid_kg+flux.aquifer_kg;
    from_stock[1]-=flux.sediment_kg;
    to_stock[1]+=flux.sediment_kg;
    verify_physical_environment(upstream,from_stock[0],from_stock[1]);
    verify_physical_environment(downstream,to_stock[0],to_stock[1]);
    if(from_stock[0]+to_stock[0]!=before_water ||
       from_stock[1]+to_stock[1]!=before_soil)
        throw std::logic_error("physical regional exchange minted water or soil");
}
} // namespace galaxy::planetary
