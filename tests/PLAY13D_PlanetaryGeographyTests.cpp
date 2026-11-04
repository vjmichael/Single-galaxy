#include "galaxy/PlanetaryGeography.hpp"
#include <iostream>
#include <stdexcept>
using namespace galaxy::planetary;
#define CHECK(x) do {if(!(x))throw std::runtime_error("PGF01B2: " #x);}while(false)
static void check(const PlanetaryWorldKey& key,const PlanetaryEnvelope& env){
    const auto map=generate_watersheds(key,env);
    std::uint64_t rivers=0,coasts=0,validated=0;
    for(int y=0;y<kWatershedRows;++y)for(int x=0;x<kWatershedColumns;++x){
        const auto a=survey_geographic_site(map,key,env,y,x);
        const auto b=survey_geographic_site(map,key,env,y,x);
        const auto& cell=map.at(y,x);
        CHECK(a.key.body==key.body && a.key.revision==key.generator_revision);
        CHECK(a.landscape.region.elevation==cell.elevation);
        CHECK(a.landscape.region.surface_water==cell.ocean);
        CHECK(a.landscape.landform==b.landscape.landform);
        CHECK(a.catchment_runoff==cell.drainage);
        CHECK(a.spill_height==cell.spill_height);
        const auto projected=cell.ocean?Watercourse::None:watercourse_from_drainage(cell.flow);
        if(env.climate!=WorldClimate::Frozen)
            CHECK(a.landscape.watercourse==projected);
        else CHECK(a.landscape.watercourse==Watercourse::None);
        if(cell.downstream>=0){
            CHECK(a.downstream_latitude==cell.downstream/kWatershedColumns);
            CHECK(a.downstream_longitude==cell.downstream%kWatershedColumns);
            CHECK(map.at(a.downstream_latitude,a.downstream_longitude).spill_height<=a.spill_height);
        } else CHECK(a.downstream_latitude==-1 && a.downstream_longitude==-1);
        if(cell.ocean){
            ++coasts;
            CHECK(a.landscape.landform==Landform::OpenWater);
            CHECK(!a.landscape.region.farming_possible);
        }
        if(a.landscape.watercourse!=Watercourse::None)++rivers;
        if(env.climate==WorldClimate::AirlessRock){
            CHECK(a.landscape.region.vegetation==Vegetation::None);
            CHECK(a.landscape.local_water_potential==0);
        }
        if(!env.biosphere_present){
            CHECK(a.landscape.region.vegetation==Vegetation::None);
            CHECK(a.landscape.vegetation_density==0);
        }
        ++validated;
    }
    CHECK(validated==kWatershedCells && coasts==map.ocean_count);
    std::cout<<"Integrated world cells="<<validated<<" channels="<<rivers<<" ocean="<<coasts<<"\n";
}
int main(){
    PlanetaryWorldKey key{613,7,9001,1};
    check(key,{WorldClimate::LifeSupporting,45,22,true});
    check(key,{WorldClimate::Arid,8,27,true});
    check(key,{WorldClimate::Frozen,75,-25,false});
    check(key,{WorldClimate::AirlessRock,0,-20,false});
    const auto env=PlanetaryEnvelope{WorldClimate::Arid,8,27,true};
    const auto map=generate_watersheds(key,env);
    auto wrong=key;wrong.body++;
    bool reject=false;
    try{(void)survey_geographic_site(map,wrong,env,17,17);}
    catch(const std::invalid_argument&){reject=true;}
    CHECK(reject);
    std::cout<<"PASS PGF-01B.2 shared terrain, runoff and river geography\n";
}
