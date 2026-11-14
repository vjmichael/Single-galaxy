#pragma once
// STAGE6-ENV01 / PGF-01C: deterministic seasonal prospect projection on the
// ONE canonical PGF-01B.2 geography. All quantities here are climate MODEL
// UNITS, never liquid kg, biomass, inventory, extraction or weather authority.
// The original planet body/revision, discovery and SQLite site custody remain
// in Simulation/PersistentWorld. No new source entity is spawned here.
#include "galaxy/PlanetaryGeography.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

namespace galaxy::planetary {
constexpr std::uint32_t kSeasonalModelRevision=1;
constexpr unsigned kPlanetaryQuarters=4;
struct SeasonalCell {
    std::int16_t temperature_c=0;
    std::uint32_t precipitation=0; // subset of bounded annual *model* score
    std::uint32_t snowfall=0, snowmelt=0, evaporation=0;
    std::uint32_t snowpack_end=0, local_runoff=0;
    std::uint64_t catchment_discharge=0;
    bool sea_ice=false;
    bool candidate_liquid=false; // NOT harvestable, not a water entitlement
    std::uint8_t environmental_risk=0; // 0 none,1 cold,2 heat,3 high-flow
};
struct SeasonalClimateMap {
    PlanetaryWorldKey key{};
    PlanetaryEnvelope envelope{};
    std::uint32_t model_revision=kSeasonalModelRevision;
    std::vector<SeasonalCell> cells; // fixed 4*2592; heap, not 1MB stack
    std::uint64_t annual_precipitation=0, annual_evaporation=0;
    std::uint64_t annual_routed_runoff=0, end_snowpack=0;
    const SeasonalCell& at(unsigned quarter,int lat,int lon) const {
        if(quarter>=kPlanetaryQuarters || lat<0 || lat>=kWatershedRows ||
           lon<0 || lon>=kWatershedColumns ||
           cells.size()!=kPlanetaryQuarters*kWatershedCells)
            throw std::out_of_range("seasonal source cell");
        return cells[quarter*kWatershedCells+lat*kWatershedColumns+lon];
    }
};
struct SeasonalSite {
    PlanetaryRegionKey key{};
    std::uint32_t model_revision=kSeasonalModelRevision;
    std::array<SeasonalCell,kPlanetaryQuarters> quarters{};
    std::uint32_t annual_precipitation=0, annual_evaporation=0;
    std::uint32_t snowpack_end=0;
    bool potential_open_water=false, perennial_surface_ice=false;
    bool has_warm_growing_quarter=false;
    bool high_discharge_risk=false;
};

inline bool same_seasonal_envelope(const PlanetaryEnvelope& a,
                                  const PlanetaryEnvelope& b) {
    return a.climate==b.climate && a.water_coverage==b.water_coverage &&
           a.average_celsius==b.average_celsius &&
           a.biosphere_present==b.biosphere_present;
}

inline SeasonalClimateMap generate_seasonal_climate(
    const WatershedMap& map,const PlanetaryWorldKey& key,
    const PlanetaryEnvelope& envelope) {
    validate_planetary_envelope(key,envelope);
    if(map.key.galaxy_seed!=key.galaxy_seed ||
       map.key.system!=key.system || map.key.body!=key.body ||
       map.key.generator_revision!=key.generator_revision ||
       map.climate!=envelope.climate ||
       map.water_coverage!=envelope.water_coverage ||
       map.average_celsius!=envelope.average_celsius ||
       map.cells.size()!=std::size_t(kWatershedCells))
        throw std::invalid_argument("planetary seasonal source mismatch");

    SeasonalClimateMap result;
    result.key=key;result.envelope=envelope;
    result.cells.resize(kPlanetaryQuarters*kWatershedCells);
    std::array<std::uint32_t,kWatershedCells> snow{};
    for(unsigned quarter=0;quarter<kPlanetaryQuarters;++quarter) {
        std::vector<int> upstream(kWatershedCells,0);
        std::queue<int> ready;
        for(int i=0;i<kWatershedCells;++i) {
            const auto down=map.cells[i].downstream;
            if(down>=kWatershedCells || down==i || down< -1)
                throw std::invalid_argument("invalid seasonal watershed link");
            if(down>=0)++upstream[down];
        }
        for(int i=0;i<kWatershedCells;++i)
            if(!upstream[i])ready.push(i);
        for(int i=0;i<kWatershedCells;++i) {
            const auto y=i/kWatershedColumns,x=i%kWatershedColumns;
            const auto& geo=map.cells[i];
            auto& c=result.cells[quarter*kWatershedCells+i];
            const auto site=survey_geographic_site(map,key,envelope,y,x);
            const int dist=std::abs(y*2-(kWatershedRows-1));
            const int amplitude=3+(dist*12)/(kWatershedRows-1);
            // Quarter 0 northern winter/southern summer, quarter 2 inverse.
            const int hemisphere=y<kWatershedRows/2?-1:1;
            const int offset=(quarter==0?hemisphere*amplitude:
                              quarter==2?-hemisphere*amplitude:0);
            const int lapse=std::max(0,int(geo.elevation))/160;
            c.temperature_c=std::int16_t(std::clamp(
                int(site.landscape.region.temperature_celsius)+offset-lapse,
                -150,100));
            // Precipitation is never evidence of water on a sterile or
            // zero-water body. PGF-01B v1 may retain old rainfall scores
            // for those bodies; filter only the NEW seasonal projection,
            // without rewriting historically observed v1 geography.
            const auto annual=envelope.climate==WorldClimate::AirlessRock ||
                              !envelope.water_coverage?0U:geo.rainfall;
            // Deterministic fixed-point quarter partition: telescoping
            // integer fractions preserve every annual model unit.
            const std::array<std::uint32_t,4> weights =
                y<kWatershedRows/2 ? std::array<std::uint32_t,4>{{1,3,4,2}} :
                std::array<std::uint32_t,4>{{4,3,1,2}};
            std::uint32_t previous=0,total_weight=0;
            for(unsigned q=0;q<=quarter;++q) {
                if(q==quarter)previous=total_weight;
                total_weight+=weights[q];
            }
            c.precipitation=(annual*total_weight)/10-(annual*previous)/10;
            if(c.temperature_c<=0) {
                c.snowfall=c.precipitation;
            } else {
                // Evaporation is bounded by actual liquid precipitation,
                // while snowmelt is debited from previously stored snow.
                const int evap_fraction=std::clamp(
                    (int(c.temperature_c)+8)*2,0,60);
                c.evaporation=c.precipitation*
                    std::uint32_t(evap_fraction)/100;
                c.snowmelt=std::min(snow[i],
                    std::uint32_t(1+int(c.temperature_c)/2+
                                  int(c.precipitation)/2));
            }
            // Cold snowfall never grants immediate flowing liquid.
            snow[i]+=c.snowfall;
            if(c.snowmelt>snow[i])
                throw std::logic_error("negative planetary seasonal snowpack");
            snow[i]-=c.snowmelt;
            c.snowpack_end=snow[i];
            c.local_runoff=c.precipitation-c.snowfall-
                           c.evaporation+c.snowmelt;
            c.catchment_discharge=c.local_runoff;
            c.sea_ice=geo.ocean && c.temperature_c<=-2;
            c.candidate_liquid=c.temperature_c>0 &&
                (geo.ocean || c.local_runoff>0);
            result.annual_precipitation+=c.precipitation;
            result.annual_evaporation+=c.evaporation;
        }
        // Topological downstream routing uses the SAME original parent
        // links and follows every upstream cell exactly once. No new river
        // sources, hidden lakes, water inventory or cyclic flow are allowed.
        int processed=0;
        while(!ready.empty()) {
            const int i=ready.front();ready.pop();++processed;
            const auto down=map.cells[i].downstream;
            const auto flow=result.cells[quarter*kWatershedCells+i].
                catchment_discharge;
            if(down<0) {
                result.annual_routed_runoff+=flow;
            } else {
                auto& destination=result.cells[
                    quarter*kWatershedCells+down].catchment_discharge;
                if(destination>std::numeric_limits<std::uint64_t>::max()-flow)
                    throw std::overflow_error("planetary climate runoff overflow");
                destination+=flow;
                if(--upstream[down]==0)ready.push(down);
            }
        }
        if(processed!=kWatershedCells)
            throw std::invalid_argument("cyclic planetary watershed");
        // A candidate flow requires positive surface temperature. Even an
        // astronomical ocean basin may have continuous sea ice.
        for(int i=0;i<kWatershedCells;++i) {
            auto& c=result.cells[quarter*kWatershedCells+i];
            c.candidate_liquid=c.temperature_c>0 &&
                (map.cells[i].ocean || c.catchment_discharge>0);
            c.environmental_risk= c.temperature_c<=-35 ? 1 :
                c.temperature_c>=48 ? 2 :
                c.catchment_discharge>=3000 &&
                c.temperature_c>0 && !map.cells[i].ocean ? 3 : 0;
        }
    }
    for(const auto stored:snow)result.end_snowpack+=stored;
    // The only source is the climate model's annual precipitation.
    // Atmospheric loss, local runoff and retained snow close exactly.
    if(result.annual_precipitation!=result.annual_evaporation+
       result.annual_routed_runoff+result.end_snowpack)
        throw std::logic_error("seasonal water potential did not conserve");
    return result;
}

inline SeasonalSite survey_seasonal_site(
    const WatershedMap& map,const SeasonalClimateMap& climate,
    const PlanetaryWorldKey& key,const PlanetaryEnvelope& envelope,
    int latitude,int longitude) {
    validate_planetary_envelope(key,envelope);
    if(latitude<0||latitude>=kWatershedRows||
       longitude<0||longitude>=kWatershedColumns ||
       key.body!=map.key.body || key.system!=map.key.system ||
       key.galaxy_seed!=map.key.galaxy_seed ||
       key.generator_revision!=map.key.generator_revision ||
       climate.key.body!=key.body || climate.key.system!=key.system ||
       climate.key.galaxy_seed!=key.galaxy_seed ||
       climate.key.generator_revision!=key.generator_revision ||
       !same_seasonal_envelope(climate.envelope,envelope) ||
       climate.model_revision!=kSeasonalModelRevision ||
       map.climate!=envelope.climate ||
       map.water_coverage!=envelope.water_coverage ||
       map.average_celsius!=envelope.average_celsius)
        throw std::invalid_argument("seasonal site identity mismatch");
    SeasonalSite out;
    out.key={key.body,key.generator_revision,
             std::uint16_t(latitude),std::uint16_t(longitude)};
    const auto geo=survey_geographic_site(
        map,key,envelope,latitude,longitude);
    out.perennial_surface_ice=map.at(latitude,longitude).ocean;
    for(unsigned q=0;q<kPlanetaryQuarters;++q) {
        auto c=climate.at(q,latitude,longitude);
        out.quarters[q]=c;
        out.annual_precipitation+=c.precipitation;
        out.annual_evaporation+=c.evaporation;
        out.potential_open_water|=c.candidate_liquid;
        out.perennial_surface_ice&=c.sea_ice;
        out.has_warm_growing_quarter|=
            !geo.landscape.region.surface_water &&
            geo.landscape.region.fertility>=40 &&
            c.temperature_c>=5 && c.temperature_c<=38;
        out.high_discharge_risk|=c.environmental_risk==3;
    }
    out.snowpack_end=out.quarters.back().snowpack_end;
    return out;
}
} // namespace galaxy::planetary
