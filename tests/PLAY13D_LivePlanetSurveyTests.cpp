#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::string("PGF01B2LIVE ")+#x); }while(false)
int main() {
  const auto db=fs::temp_directory_path()/
    ("sg_pgf_live_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
  try {
    EntityId ship=0;std::uint64_t planet=0;
    planetary::GeographicSite original{};
    {
      Simulation seed(777,1);
      ship=seed.add_colony_ship(7,{0,0},50,5);
      seed.provision_starter_exploration_scanner(ship);
      PersistentWorld world(db.string(),std::move(seed));
      world.ensure_physical_celestial_systems();
      for(const auto& body:world.world().physical_celestial_systems().at(1))
        if(body.kind==CelestialKind::Planet){planet=body.id;break;}
      CHECK(planet!=0);
      CHECK(!world.world().scan_planetary_geography(8,ship,planet,17,22));
      CHECK(!world.world().scan_planetary_geography(7,ship,planet,36,22));
      CHECK(!world.world().scan_planetary_geography(7,ship,planet,17,72));
      CHECK(!world.world().scan_planetary_geography(7,ship,planet+500000,17,22));
      const auto first=world.world().scan_planetary_geography(7,ship,planet,17,22);
      CHECK(first.has_value());
      original=*first;
      CHECK(original.key.body==planet);
      CHECK(original.key.revision==1);
      const auto again=world.world().scan_planetary_geography(7,ship,planet,17,22);
      CHECK(again.has_value());
      CHECK(again->landscape.region.elevation==original.landscape.region.elevation);
      CHECK(again->landscape.watercourse==original.landscape.watercourse);
      CHECK(again->catchment_runoff==original.catchment_runoff);
    }
    {
      PersistentWorld world(db.string(),Simulation(777,1));
      const auto recovered=world.world().scan_planetary_geography(7,ship,planet,17,22);
      CHECK(recovered.has_value());
      CHECK(recovered->landscape.region.elevation==original.landscape.region.elevation);
      CHECK(recovered->landscape.region.surface_water==original.landscape.region.surface_water);
      CHECK(recovered->landscape.region.terrain==original.landscape.region.terrain);
      CHECK(recovered->landscape.watercourse==original.landscape.watercourse);
      CHECK(recovered->catchment_runoff==original.catchment_runoff);
      CHECK(recovered->downstream_latitude==original.downstream_latitude);
      CHECK(recovered->downstream_longitude==original.downstream_longitude);
      CHECK(!world.world().scan_planetary_geography(9,ship,planet,17,22));
    }
    fs::remove(db);
    std::cout<<"PASS PGF-01B.2 real Simulation survey ownership and restart parity\n";
  } catch(const std::exception& e) {
    std::error_code ec;fs::remove(db,ec);
    std::cerr<<"FAIL PGF-01B.2 "<<e.what()<<'\n';
    return 1;
  }
}
