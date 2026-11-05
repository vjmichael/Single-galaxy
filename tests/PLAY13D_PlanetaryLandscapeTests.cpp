#include "galaxy/PlanetaryLandscape.hpp"
#include <stdexcept>
#include <iostream>
#include <set>
using namespace galaxy::planetary;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("PLAY13D2C: " #x); } while(false)
int main(){
    PlanetProfile p{45,919,WorldClimate::Arid,8,27};
    std::set<Landform> forms;std::set<Watercourse> channels;
    bool shaded_green=false,cavern_green=false,high=false,low=false,prairie=false;
    for(unsigned y=0;y<36;++y)for(unsigned x=0;x<72;++x){
        const auto a=survey_landscape(p,y,x),b=survey_landscape(p,y,x);
        CHECK(a.landform==b.landform && a.watercourse==b.watercourse);
        forms.insert(a.landform);channels.insert(a.watercourse);
        shaded_green |= a.landform==Landform::ForestedCanyon && a.vegetation_density>=72;
        cavern_green |= a.landform==Landform::CavernForest && a.underground_light && a.groundwater_potential;
        high |= a.landform==Landform::HighDesert;
        low |= a.landform==Landform::LowDesert;
        prairie |= a.landform==Landform::AridPrairie;
        if(a.watercourse==Watercourse::DryArroyo) CHECK(a.local_water_potential<=75);
    }
    CHECK(high && low && prairie && shaded_green && cavern_green);
    CHECK(channels.count(Watercourse::DryArroyo));
    CHECK(channels.count(Watercourse::SeasonalStream));
    CHECK(channels.count(Watercourse::PerennialRiver));
    PlanetProfile rock{44,919,WorldClimate::AirlessRock,0,-5};
    for(unsigned y=0;y<36;++y)for(unsigned x=0;x<72;++x){
        auto a=survey_landscape(rock,y,x);
        CHECK(!a.underground_light && !a.groundwater_potential);
        CHECK(a.watercourse==Watercourse::None);
        CHECK(a.vegetation_density==0);
    }
    std::cout<<"PASS arid topography, rivers, arroyos, forested canyons, cavern niches\n";
}
