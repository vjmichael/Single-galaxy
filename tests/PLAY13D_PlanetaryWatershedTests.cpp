#include "galaxy/PlanetaryWatershed.hpp"
#include <iostream>
#include <stdexcept>
#include <array>
using namespace galaxy::planetary;
#define CHECK(x) do {if(!(x)) throw std::runtime_error("PGF01B: " #x);}while(false)
static void check(const PlanetaryWorldKey& key,const PlanetaryEnvelope& envelope) {
    const auto a=generate_watersheds(key,envelope);
    const auto b=generate_watersheds(key,envelope);
    CHECK(a.ocean_count==b.ocean_count && a.total_rainfall==b.total_rainfall);
    CHECK(a.terminal_discharge==a.total_rainfall);
    std::uint64_t precipitation=0;
    unsigned channels=0,outlets=0,checked=0;
    for(int i=0;i<kWatershedCells;++i){
        const auto& c=a.cells[i];
        const auto& identical=b.cells[i];
        CHECK(c.elevation==identical.elevation &&
              c.spill_height==identical.spill_height &&
              c.downstream==identical.downstream &&
              c.drainage==identical.drainage &&
              c.flow==identical.flow);
        precipitation+=c.rainfall;
        channels+=c.flow==DrainageKind::Arroyo ||
                  c.flow==DrainageKind::SeasonalStream ||
                  c.flow==DrainageKind::PerennialRiver;
        if(c.downstream==-1) {++outlets;continue;}
        CHECK(c.downstream>=0 && c.downstream<kWatershedCells);
        CHECK(c.spill_height>=a.cells[c.downstream].spill_height);
        CHECK(c.drainage>=c.rainfall);
        CHECK(a.cells[c.downstream].drainage>=c.drainage);
        // All routes must terminate at sea or a designated dry inland basin.
        int walk=i,steps=0;
        while(a.cells[walk].downstream>=0) {
            walk=a.cells[walk].downstream;
            CHECK(++steps<=kWatershedCells);
        }
        CHECK(a.cells[walk].ocean || a.ocean_count==0);
        ++checked;
    }
    CHECK(precipitation==a.total_rainfall && outlets>=1 && checked>0);
    if(envelope.water_coverage) CHECK(a.ocean_count==kWatershedCells*envelope.water_coverage/100);
    else CHECK(a.ocean_count==0);
    // Coarse terrain must have some spatial variability; no guaranteed
    // particular number of biomes or water sources.
    CHECK(a.cells[0].elevation!=a.cells[kWatershedCells/2].elevation);
    std::cout<<"PGF01B checked cells="<<checked<<" channels="<<channels
             <<" rainfall="<<a.total_rainfall<<" outlets="<<outlets<<"\n";
}
int main(){
    const PlanetaryWorldKey key{613,7,9001,1};
    check(key,PlanetaryEnvelope{WorldClimate::LifeSupporting,45,22,true});
    check(key,PlanetaryEnvelope{WorldClimate::Arid,8,27,true});
    check(key,PlanetaryEnvelope{WorldClimate::Frozen,75,-25,false});
    check(key,PlanetaryEnvelope{WorldClimate::AirlessRock,0,-20,false});
    auto newer=key;++newer.generator_revision;
    const auto a=generate_watersheds(key,PlanetaryEnvelope{WorldClimate::Arid,8,27,true});
    const auto b=generate_watersheds(newer,PlanetaryEnvelope{WorldClimate::Arid,8,27,true});
    bool changed=false;
    for(int i=0;i<kWatershedCells;++i) changed|=a.cells[i].elevation!=b.cells[i].elevation;
    CHECK(changed);
    std::cout<<"PASS PGF01B deterministic elevation, catchments, downhill drainage, conserved rainfall\n";
}
