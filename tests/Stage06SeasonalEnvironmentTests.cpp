// STAGE6-ENV01 source-staged native seasonal climate, geography and
// conservation regression. Execute under full exact-head Windows/Linux CTest
// during consolidated acceptance; this file is not proof of a native pass.
#include "galaxy/PlanetarySeasonalClimate.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace galaxy::planetary;
#define CHECK(x) do{if(!(x))throw std::runtime_error("STAGE6-ENV01 line "+std::to_string(__LINE__)+": " #x);}while(false)
static void verify(const PlanetaryWorldKey& key,
                   const PlanetaryEnvelope& env) {
    const auto terrain=generate_watersheds(key,env);
    const auto a=generate_seasonal_climate(terrain,key,env);
    const auto b=generate_seasonal_climate(terrain,key,env);
    CHECK(a.cells.size()==kPlanetaryQuarters*kWatershedCells);
    CHECK(a.model_revision==kSeasonalModelRevision);
    CHECK(a.annual_precipitation==b.annual_precipitation);
    CHECK(a.annual_evaporation==b.annual_evaporation);
    CHECK(a.annual_routed_runoff==b.annual_routed_runoff);
    CHECK(a.end_snowpack==b.end_snowpack);
    CHECK(a.annual_precipitation==
          a.annual_evaporation+a.annual_routed_runoff+a.end_snowpack);
    std::array<std::uint64_t,4> rainfall{}, evaporated{}, runoff{}, terminal{};
    std::uint64_t dry_ice=0, differentiated=0;
    for(int y=0;y<kWatershedRows;++y) {
        for(int x=0;x<kWatershedColumns;++x) {
            auto site=survey_seasonal_site(terrain,a,key,env,y,x);
            CHECK(site.key.body==key.body);
            CHECK(site.key.revision==key.generator_revision);
            std::uint32_t annual=0,annual_evap=0,annual_snow=0;
            for(unsigned q=0;q<kPlanetaryQuarters;++q) {
                const auto& c=a.at(q,y,x);
                const auto& d=b.at(q,y,x);
                CHECK(c.temperature_c==d.temperature_c);
                CHECK(c.precipitation==d.precipitation);
                CHECK(c.catchment_discharge==d.catchment_discharge);
                CHECK(c.snowpack_end==d.snowpack_end);
                CHECK(c.candidate_liquid==d.candidate_liquid);
                CHECK(c.sea_ice==d.sea_ice);
                CHECK(c.environmental_risk==d.environmental_risk);
                CHECK(c.local_runoff<=c.catchment_discharge);
                CHECK(c.evaporation<=c.precipitation);
                CHECK(c.snowfall<=c.precipitation);
                CHECK(!c.candidate_liquid || c.temperature_c>0);
                CHECK(!c.sea_ice || terrain.at(y,x).ocean);
                CHECK(!c.sea_ice || c.temperature_c<=-2);
                CHECK(!c.sea_ice || !c.candidate_liquid);
                const auto previous=q?site.quarters[q-1].snowpack_end:0U;
                CHECK(previous+c.snowfall>=c.snowmelt);
                CHECK(previous+c.snowfall-c.snowmelt==c.snowpack_end);
                CHECK(c.precipitation+previous==
                      c.evaporation+c.local_runoff+c.snowpack_end);
                CHECK(c.environmental_risk<=3);
                annual+=c.precipitation;annual_evap+=c.evaporation;
                annual_snow=c.snowpack_end;
                rainfall[q]+=c.precipitation;
                evaporated[q]+=c.evaporation;
                runoff[q]+=c.local_runoff;
                if(terrain.at(y,x).downstream<0)
                    terminal[q]+=c.catchment_discharge;
                CHECK(site.quarters[q].catchment_discharge==
                      c.catchment_discharge);
                if(c.sea_ice)++dry_ice;
            }
            const auto annual_source=env.climate==WorldClimate::AirlessRock ||
                                     !env.water_coverage?0U:
                                     terrain.at(y,x).rainfall;
            CHECK(annual==annual_source);
            CHECK(annual==annual_evap+annual_snow+
                  (site.quarters[0].local_runoff+
                   site.quarters[1].local_runoff+
                   site.quarters[2].local_runoff+
                   site.quarters[3].local_runoff));
            CHECK(site.annual_precipitation==annual);
            CHECK(site.annual_evaporation==annual_evap);
            CHECK(site.snowpack_end==annual_snow);
            if(site.quarters[0].temperature_c!=site.quarters[2].temperature_c)
                ++differentiated;
            if(env.climate==WorldClimate::AirlessRock || !env.water_coverage) {
                CHECK(annual==0);
                CHECK(!site.potential_open_water);
                CHECK(!site.has_warm_growing_quarter ||
                      !terrain.at(y,x).ocean);
            }
        }
    }
    CHECK(differentiated>0);
    std::uint64_t all_rain=0,all_evap=0,all_runoff=0;
    for(unsigned q=0;q<kPlanetaryQuarters;++q) {
        CHECK(terminal[q]==runoff[q]);
        all_rain+=rainfall[q];all_evap+=evaporated[q];
        all_runoff+=runoff[q];
    }
    CHECK(a.annual_precipitation==all_rain);
    CHECK(a.annual_evaporation==all_evap);
    CHECK(a.annual_routed_runoff==all_runoff);
    if(env.climate==WorldClimate::AirlessRock || !env.water_coverage) {
        CHECK(a.annual_precipitation==0);
        CHECK(a.annual_routed_runoff==0);
        CHECK(a.end_snowpack==0);
    }
    auto wrong_key=key;wrong_key.body++;
    bool refused=false;
    try{(void)generate_seasonal_climate(terrain,wrong_key,env);}
    catch(const std::invalid_argument&){refused=true;}
    CHECK(refused);
    auto wrong_env=env;wrong_env.water_coverage=
        env.water_coverage==100?99:env.water_coverage+1;
    refused=false;
    try{(void)generate_seasonal_climate(terrain,key,wrong_env);}
    catch(const std::invalid_argument&){refused=true;}
    CHECK(refused);
    refused=false;
    try{(void)survey_seasonal_site(terrain,a,key,wrong_env,17,17);}
    catch(const std::invalid_argument&){refused=true;}
    CHECK(refused);
    refused=false;
    try{(void)a.at(4,17,17);}
    catch(const std::out_of_range&){refused=true;}
    CHECK(refused);
    std::cout<<"STAGE6-ENV01 body="<<key.body
             <<" model_rain="<<all_rain
             <<" routed="<<all_runoff
             <<" evaporated="<<all_evap
             <<" stored_snow="<<a.end_snowpack
             <<" ice_quarters="<<dry_ice<<'\n';
}
int main() {
    try{
        const PlanetaryWorldKey key{613,7,9001,1};
        verify(key,{WorldClimate::LifeSupporting,45,22,true});
        verify(key,{WorldClimate::Arid,8,27,true});
        verify(key,{WorldClimate::Frozen,75,-25,false});
        verify(key,{WorldClimate::Ocean,88,16,false});
        verify(key,{WorldClimate::AirlessRock,0,50,false});
        verify(key,{WorldClimate::Arid,0,27,false});
        std::cout<<"STAGE6-ENV01 native seasonal budget source PASS\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"STAGE6-ENV01 staged CTest failed: "<<ex.what()<<'\n';
        return 1;
    }
}
