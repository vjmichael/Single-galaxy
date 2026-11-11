// GROUP3-B genuine original C++ finite asteroid observer; no client ore.
#include "galaxy/SpaceMiningSurvey.hpp"
#include <stdexcept>
#include <iostream>
#include <limits>
#include <string>
using namespace galaxy;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(std::string("GROUP3B line ")+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
 try{
    Simulation w(0x330B);
    const auto owned=w.add_colony_ship(7,{0,0},24,3);
    const auto rock=w.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
    const auto far=w.add_geological_asteroid({1450000,0},AsteroidClass::Silicate,8,1100);
    const auto beyond=w.add_geological_asteroid({8800000,0},AsteroidClass::Carbonaceous,7,1100);
    w.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    const auto original=w.canonical_state();
    const auto rows=mining_survey::nearby_original_asteroids(w,7,owned);
    REQUIRE(rows.size()==2);
    REQUIRE(rows[0].rock==rock && rows[1].rock==far);
    REQUIRE(rows[0].x_mm==25000 && rows[1].x_mm==1450000);
    REQUIRE(rows[0].ore_tonnes==-1 && rows[0].taxonomy==3);
    REQUIRE(!rows[0].geological_detail);
    REQUIRE(w.find(rock)->ore>0 && w.find(far)->ore>0);
    REQUIRE(rows[0].tick==w.tick());
    REQUIRE(!mining_survey::nearby_original_asteroids(w,8,owned).size());
    REQUIRE(mining_survey::nearby_original_asteroids(w,7,beyond).empty());
    REQUIRE(mining_survey::nearby_original_asteroids(w,7,owned,0).empty());
    REQUIRE(mining_survey::nearby_original_asteroids(w,7,owned,17).empty());
    REQUIRE(w.canonical_state()==original);
    const galactic::Axis edge{123,galactic::kSectorEdgeMm-5};
    const galactic::Axis next{124,5};
    const auto across=space_mining_view::nearby_axis_delta(edge,next,mining_survey::kOpticalRangeMm);
    REQUIRE(across && *across==10);
    REQUIRE(!space_mining_view::nearby_axis_delta(
        edge,galactic::Axis{130,5},mining_survey::kOpticalRangeMm));
    REQUIRE(!space_mining_view::nearby_axis_delta(
        galactic::Axis{std::numeric_limits<std::int64_t>::max(),5},
        galactic::Axis{std::numeric_limits<std::int64_t>::min(),5},
        mining_survey::kOpticalRangeMm));
    std::cout<<"GROUP3-B PASS: original 1.45km source silhouette, no 8.8km fake contact, owner isolation, scanner geology redacted, finite original ore preserved, bounded sector seam, canonical state immutable\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
