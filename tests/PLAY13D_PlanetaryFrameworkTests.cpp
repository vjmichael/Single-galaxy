#include "galaxy/PlanetaryGenerationFramework.hpp"
#include <stdexcept>
#include <iostream>
using namespace galaxy::planetary;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("PGF01: " #x); } while(false)
int main() {
    const PlanetaryWorldKey key{613,7,9001,1};
    PlanetaryEnvelope arid{WorldClimate::Arid,8,27,true};
    auto profile=derive_region_profile(key,arid);
    CHECK(profile.body==9001);
    int dry=0,vegetated=0,wet=0;
    for(unsigned y=0;y<36;++y) for(unsigned x=0;x<72;++x) {
        const auto a=survey_planetary_world(key,arid,y,x);
        const auto b=survey_planetary_world(key,arid,y,x);
        CHECK(a.key.body==b.key.body && a.key.revision==b.key.revision);
        CHECK(a.surface.temperature_celsius==b.surface.temperature_celsius);
        CHECK(a.surface.terrain==b.surface.terrain);
        CHECK(a.surface.vegetation==b.surface.vegetation);
        dry+=a.surface.terrain==Terrain::Desert || a.surface.terrain==Terrain::Dryland;
        vegetated+=a.surface.vegetation!=Vegetation::None;
        wet+=a.surface.surface_water;
    }
    CHECK(dry>0 && vegetated>0);
    // No biome quota: a nearly sterile planet can remain mono-ecological.
    PlanetaryEnvelope rock{WorldClimate::AirlessRock,0,-20,false};
    for(unsigned y=0;y<36;++y)for(unsigned x=0;x<72;++x) {
       auto r=survey_planetary_world(key,rock,y,x);
       CHECK(!r.biosphere_present);
       CHECK(r.surface.vegetation==Vegetation::None);
       CHECK(!r.land_supports_farming);
    }
    // A potentially viable region on a lifeless world must not create plants.
    auto lifeless=arid;
    lifeless.biosphere_present=false;
    auto point=survey_planetary_world(key,lifeless,17,23);
    CHECK(!point.biosphere_present && point.surface.vegetation==Vegetation::None);
    // Independently versioned generation, not dependent on query order.
    auto v2=key; ++v2.generator_revision;
    CHECK(derive_region_profile(v2,arid).seed!=profile.seed);
    bool invalid=false;
    try{ auto e=rock;e.biosphere_present=true;(void)derive_region_profile(key,e); }
    catch(const std::invalid_argument&){invalid=true;}
    CHECK(invalid);
    invalid=false;
    try{(void)survey_planetary_world(key,arid,36,0);}
    catch(const std::invalid_argument&){invalid=true;}
    CHECK(invalid);
    // Ice survey envelope: polar colder than -51C, equatorial ceiling 0C.
    PlanetaryEnvelope ice{WorldClimate::Frozen,75,-25,false};
    bool polar=false,equator=false,subsurface=false;
    for(unsigned y=0;y<36;++y)for(unsigned x=0;x<72;++x){
        auto r=survey_planetary_world(key,ice,y,x);
        CHECK(r.surface.temperature_celsius<=0);
        polar|=(y==0 || y==35)&&r.surface.temperature_celsius<=-51;
        equator|=(y==17 || y==18)&&r.surface.temperature_celsius>=-4;
        subsurface|=r.has_candidate_liquid_water;
        CHECK(r.surface.vegetation==Vegetation::None);
    }
    CHECK(polar && equator && subsurface);
    std::cout<<"PASS PGF01A versioned planetary generator adapter and ecological bounds\n";
}
