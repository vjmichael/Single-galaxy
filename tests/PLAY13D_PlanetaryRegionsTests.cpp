#include "galaxy/PlanetaryRegions.hpp"
#include <set>
#include <stdexcept>
#include <iostream>
using namespace galaxy::planetary;
#define CHECK(x) do{if(!(x)) throw std::runtime_error("PLAY13D2A failed: " #x);}while(false)
int main(){
    PlanetProfile temperate{42,919,WorldClimate::LifeSupporting,45,22};
    std::set<int> terrains;
    bool fertile=false, infertile=false, warm=false, cold=false;
    for(int y=0;y<36;++y)for(int x=0;x<72;++x){
        const auto a=survey_region(temperate,y,x), b=survey_region(temperate,y,x);
        CHECK(a.terrain==b.terrain && a.fertility==b.fertility && a.temperature_celsius==b.temperature_celsius);
        terrains.insert(int(a.terrain));
        fertile |= a.farming_possible; infertile |= !a.farming_possible;
        warm |= a.temperature_celsius>=15; cold |= a.temperature_celsius<0;
    }
    CHECK(terrains.size()>=3 && fertile && infertile && warm && cold);
    PlanetProfile ocean{43,919,WorldClimate::Ocean,96,18};
    bool shallow=false,deep=false;
    for(int y=0;y<36;++y)for(int x=0;x<72;++x){
        auto r=survey_region(ocean,y,x);
        shallow |= r.terrain==Terrain::ShallowSea;
        deep |= r.terrain==Terrain::DeepOcean;
    }
    CHECK(shallow && deep);
    // An arid living planet contains drought-adapted plants without becoming
    // uniformly fertile; rain pulses awaken some dormant wildflower communities.
    PlanetProfile arid{45,919,WorldClimate::Arid,8,27};
    bool desert_life=false,dormant=false,bloom=false,sterile=false;
    for(int y=0;y<36;++y)for(int x=0;x<72;++x){
        auto r=survey_region(arid,y,x);
        if(r.terrain==Terrain::Desert || r.terrain==Terrain::Dryland){
            desert_life |= r.vegetation!=Vegetation::None && !r.farming_possible;
            dormant |= r.dormant_seed_bank;
            bloom |= arid_bloom_active(r,1,80) && !arid_bloom_active(r,1,10);
            sterile |= r.vegetation==Vegetation::None;
        }
    }
    CHECK(desert_life && dormant && bloom && sterile);
    PlanetProfile rock{44,919,WorldClimate::AirlessRock,0,-5};
    for(int y=0;y<36;++y)for(int x=0;x<72;++x){
        auto r=survey_region(rock,y,x);
        CHECK(!r.farming_possible && r.fertility==0 && r.terrain==Terrain::BareRock);
        CHECK(r.vegetation==Vegetation::None && !r.episodic_bloom_potential);
    }
    bool reject=false;try{(void)survey_region(temperate,36,0);}catch(const std::invalid_argument&){reject=true;}
    CHECK(reject);
    std::cout<<"PASS PLAY13D2A regional climate, ecological diversity, oceans, sterile worlds\n";
}
