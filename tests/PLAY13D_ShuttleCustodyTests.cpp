#include "galaxy/PlanetaryShuttle.hpp"
#include <stdexcept>
#include <string>
#include <iostream>
using namespace galaxy::planetary;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static ShuttleStock stock(ShuttleMaterial material,std::int64_t kg){
  ShuttleStock s{};s[static_cast<std::size_t>(material)]=kg;return s;
}
int main() {
  try {
    const ShuttleRegionKey a{71,12,35,1},b{72,12,35,1};
    ShuttleCustody world;
    world.seed_ship(9,stock(ShuttleMaterial::Food,100));
    world.seed_surface(a,stock(ShuttleMaterial::Water,50));
    world.seed_surface(b,ShuttleStock{});
    world.seed_npc(101,{ShuttleBerth::Orbit,9,{}});
    world.seed_npc(102,{ShuttleBerth::Orbit,9,{}});
    world.seed_npc(103,{ShuttleBerth::Surface,9,a});
    ShuttleVehicle vessel{};
    vessel.id=77;vessel.owner=5;vessel.ship=9;vessel.region=a;
    vessel.dry_mass_kg=20;vessel.cargo_limit_kg=30;vessel.seat_limit=2;
    vessel.fuel_limit_kg=10;vessel.fuel_kg=4;
    world.register_shuttle(vessel);
    auto initial=world.total_mobile_mass();
    world.validate();
    CHECK(!world.load(77,stock(ShuttleMaterial::Food,31),{101}));
    CHECK(!world.load(77,stock(ShuttleMaterial::Food,30),{101,101}));
    CHECK(!world.load(77,stock(ShuttleMaterial::Food,30),{103}));
    CHECK(world.load(77,stock(ShuttleMaterial::Food,25),{101,102}));
    CHECK(world.shuttle(77).manifest[static_cast<std::size_t>(ShuttleMaterial::Food)]==25);
    CHECK(world.npc_location(101).berth==ShuttleBerth::InFlight);
    CHECK(world.ship_stock(9)[static_cast<std::size_t>(ShuttleMaterial::Food)]==75);
    CHECK(!world.load(77,stock(ShuttleMaterial::Food,6),{}));
    CHECK(!world.load(77,ShuttleStock{}, {103}));
    CHECK(!world.unload(77,stock(ShuttleMaterial::Food,26),{}));
    CHECK(!world.unload(77,ShuttleStock{}, {101,101}));
    CHECK(world.total_mobile_mass()==initial);
    CHECK(world.test_arrive(77,ShuttleBerth::Surface));
    CHECK(!world.test_arrive(77,ShuttleBerth::Surface));
    CHECK(world.unload(77,stock(ShuttleMaterial::Food,25),{101,102}));
    CHECK(world.surface_stock(a)[static_cast<std::size_t>(ShuttleMaterial::Food)]==25);
    CHECK(world.npc_location(102).region==a);
    CHECK(world.total_mobile_mass()==initial);
    CHECK(!world.load(77,ShuttleStock{}, {101,102,103}));
    CHECK(world.load(77,stock(ShuttleMaterial::Water,10),{101}));
    CHECK(world.test_arrive(77,ShuttleBerth::Orbit));
    CHECK(world.unload(77,stock(ShuttleMaterial::Water,10),{101}));
    CHECK(world.ship_stock(9)[static_cast<std::size_t>(ShuttleMaterial::Water)]==10);
    CHECK(world.total_mobile_mass()==initial);
    world.validate();
    bool caught=false;
    try {world.seed_surface(a,ShuttleStock{});}catch(const std::invalid_argument&){caught=true;}
    CHECK(caught);
    caught=false;
    try {world.seed_ship(18,stock(ShuttleMaterial::Food,-1));}catch(const std::invalid_argument&){caught=true;}
    CHECK(caught);
    std::cout<<"PLAY13D4C1: finite shuttle custody and conservation OK\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
