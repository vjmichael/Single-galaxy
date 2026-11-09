// PLAY-STATION-04A: original simulation 3D asteroid source, not a UI dummy.
#include "galaxy/SpaceMiningContact.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <limits>
using namespace galaxy;
using namespace galaxy::space_mining_view;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(std::string("04A line ")+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
 try {
   Simulation w(20261108);
   const auto ship=w.add_ship(7,{0,0},24);
   const auto asteroid=w.add_geological_asteroid({12500,0},AsteroidClass::Metallic,2,5000);
   const auto distant=w.add_asteroid({74000,0},9);
   w.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
   auto contact=nearest_original_asteroid(w,7,ship);
   REQUIRE(contact && contact->rock==asteroid);
   REQUIRE(contact->ore_tonnes==w.find(asteroid)->ore && contact->ore_tonnes>0);
   REQUIRE(contact->x_mm==12500 && contact->y_mm==0 && contact->z_mm==0);
   REQUIRE(contact->radius_mm==2000 && contact->taxonomy==2);
   REQUIRE(!contact->mining_tool_online); // no default laser / free mining module
   REQUIRE(contact->cargo_tonnes==0 && contact->cargo_capacity_tonnes==24);
   REQUIRE(!nearest_original_asteroid(w,8,ship));
   REQUIRE(!nearest_original_asteroid(w,7,distant));
   REQUIRE(!nearest_original_asteroid(w,7,std::numeric_limits<EntityId>::max()));
   const galactic::Axis edge{123,galactic::kSectorEdgeMm-5};
   const galactic::Axis next{124,5};
   const auto across=nearby_axis_delta(edge,next,30000);
   const auto reverse=nearby_axis_delta(next,edge,30000);
   REQUIRE(across && *across==10 && reverse && *reverse==-10);
   REQUIRE(!nearby_axis_delta(edge,galactic::Axis{125,5},30000));
   REQUIRE(!nearby_axis_delta(galactic::Axis{std::numeric_limits<std::int64_t>::max(),5},
                             galactic::Axis{std::numeric_limits<std::int64_t>::min(),5},30000));
   const auto before=w.canonical_state();
   REQUIRE(nearest_original_asteroid(w,7,ship) && w.canonical_state()==before);
   std::cout<<"PLAY-STATION-04A PASS: original 3D asteroid range/projection, finite ore, owner and local-frame boundaries, no free mining equipment, sector seam, read-only snapshot\n";
   return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
