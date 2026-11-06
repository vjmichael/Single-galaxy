#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto db=(fs::temp_directory_path()/("sg_playerperson_v98_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 try{
  Simulation seed(20898);
  const auto ship=seed.add_ship(7,{0,0},100);
  const auto foreign=seed.add_ship(8,{5000,0},100);
  const auto before_material=seed.total_ore();
  std::string durable;
  {
    PersistentWorld world(db,std::move(seed));
    CHECK(!world.register_player_person(8,ship));
    CHECK(!world.register_player_person(7,foreign));
    CHECK(world.world().player_people().empty());
    const auto before=world.world().canonical_state();
    bool failed=false;
    try{(void)world.register_player_person(7,ship,FailPoint::AfterSnapshotWrite);}
    catch(const std::exception&){failed=true;}
    CHECK(failed);
    CHECK(world.world().canonical_state()==before);
    CHECK(world.register_player_person(7,ship));
    CHECK(!world.register_player_person(7,ship));
    CHECK(world.world().player_people().size()==1);
    const auto& person=world.world().player_people().at(7);
    CHECK(person.owner==7 && person.home_ship==ship &&
          person.physical_ship==ship && person.custodian==0 &&
          person.state==PlayerPersonState::AboardOwnShip &&
          person.last_receipt==0);
    CHECK(world.world().total_ore()==before_material);
    durable=world.world().canonical_state();
    CHECK(durable.rfind("galaxy-kernel-v98|",0)==0);
    CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
    // Player identity must not be recreated by mutating a vessel owner
    // or by forging the character's physical position in a snapshot.
    auto forged=durable;
    const auto begin=forged.find("playerperson|7|");
    CHECK(begin!=std::string::npos);
    const auto end=forged.find('\n',begin);
    CHECK(end!=std::string::npos);
    forged.replace(begin,end-begin,
      "playerperson|7|"+std::to_string(ship)+"|"+
      std::to_string(foreign)+"|0|0|0|0|0");
    bool rejected=false;
    try{(void)Simulation::from_snapshot(forged);}
    catch(const std::exception&){rejected=true;}
    CHECK(rejected);
  }
  {
    PersistentWorld reopened(db,Simulation(20898));
    CHECK(reopened.world().canonical_state()==durable);
    CHECK(reopened.world().player_people().at(7).physical_ship==ship);
    CHECK(reopened.world().total_ore()==before_material);
  }
  fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
  std::cout<<"PASS NPC-RESP-02: distinct player person, v98 receipt, forged location rejection and native SQLite restart\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL NPC-RESP-02: "<<e.what()<<'\n';
  fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
  return 1;
 }
}
