#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C6C ")+#x+" line "+std::to_string(__LINE__));}while(false)
static bool rejects(const std::string& value){
    try{(void)Simulation::from_snapshot(value);return false;}
    catch(const std::exception&){return true;}
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play13c6c_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  fs::create_directories(dir);
  std::uint32_t system=0;std::uint64_t body=0;EntityId ship=0;
  for(std::uint32_t i=1;i<=2000&&!body;++i){
    Simulation sample(6013,i);
    sample.ensure_physical_celestial_systems();
    for(const auto& b:sample.physical_celestial_systems().at(i))
      if(sample.celestial_oasis_prospect(b.id)->candidate){system=i;body=b.id;break;}
  }
  CHECK(body && system);
  const auto db=(dir/"oasis.sqlite").string();
  std::int64_t origin=0;
  std::string snapshot;
  {
    Simulation seed(613,system);
    ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.provision_starter_exploration_scanner(ship);
    seed.add_asteroid({30000,0},50);
    seed.ensure_physical_celestial_systems();
    seed.provision_oasis_reserve_genesis(body,8000,4000);
    CHECK(seed.oasis_reserves().at(body).water_kg==8000);
    CHECK(seed.life_support(ship)->water_kg==2000);
    CHECK(seed.oasis_reserves().size()==1);
    origin=seed.total_ore();
    seed.ensure_ship_frame_registry();
    PersistentWorld world(db,std::move(seed));
    CHECK(world.world().total_ore()==origin);
    CHECK(!world.collect_oasis_resource(7,ship,body,true,1000)); // not physically there
    CHECK(world.begin_celestial_transit(7,ship,body,60001));
    world.steps(4);
    CHECK(world.complete_celestial_transit(60001));
    CHECK(world.world().canonical_state().rfind("galaxy-kernel-v75|",0)==0);
    const auto initial=world.world().canonical_state();
    CHECK(!world.collect_oasis_resource(8,ship,body,true,1000)); // wrong owner
    CHECK(!world.collect_oasis_resource(7,ship,body,true,500)); // fractional
    CHECK(!world.collect_oasis_resource(7,ship,body,true,9000)); // unavailable
    CHECK(world.world().canonical_state()==initial);
    bool rolled_back=false;
    try{(void)world.collect_oasis_resource(7,ship,body,true,1000,FailPoint::AfterSnapshotWrite);}
    catch(const std::exception&){rolled_back=true;}
    CHECK(rolled_back);
    CHECK(world.world().canonical_state()==initial);
    CHECK(world.collect_oasis_resource(7,ship,body,true,1000));
    CHECK(world.world().oasis_reserves().at(body).water_kg==7000);
    CHECK(world.world().life_support(ship)->water_kg==3000);
    CHECK(world.world().total_ore()==origin);
    CHECK(world.collect_oasis_resource(7,ship,body,false,1000));
    CHECK(world.world().oasis_reserves().at(body).nutrients_kg==3000);
    CHECK(world.world().life_support(ship)->nutrients_kg==3000);
    CHECK(world.world().total_ore()==origin);
    snapshot=world.world().canonical_state();
    CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
    auto altered=snapshot;
    const auto start=altered.find("oasisreserve|");
    CHECK(start!=std::string::npos);
    const auto end=altered.find('\n',start);
    CHECK(end!=std::string::npos);
    auto line=altered.substr(start,end-start);
    const auto water=line.find("|7000|");
    CHECK(water!=std::string::npos);
    line.replace(water,6,"|9000|"); // exceeds original source
    altered.replace(start,end-start,line);
    CHECK(rejects(altered));
    const auto m=world.world().oasis_reserves().at(body);
    CHECK(m.initial_water_kg==8000&&m.initial_nutrients_kg==4000);
  }
  {
    PersistentWorld world(db,Simulation(42));
    CHECK(world.world().canonical_state()==snapshot);
    CHECK(world.world().total_ore()==origin);
    CHECK(world.world().oasis_reserves().at(body).water_kg==7000);
    CHECK(world.collect_oasis_resource(7,ship,body,true,7000));
    CHECK(world.world().oasis_reserves().at(body).water_kg==0);
    CHECK(!world.collect_oasis_resource(7,ship,body,true,1000));
    CHECK(world.world().total_ore()==origin);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY13C6C finite genesis source, physical collection, rollback, depletion and SQLite recovery\n";
  return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL PLAY13C6C "<<e.what()<<'\n';
    std::error_code ec;fs::remove_all(dir,ec);return 1;
 }
}
