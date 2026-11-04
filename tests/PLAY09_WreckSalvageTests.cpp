#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY09: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command cmd(PersistentWorld& w,std::uint64_t id,EntityId ship,
                   EntityId target,Action action,std::int64_t amount=0,PlayerId owner=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.actor=ship;c.issuer=owner;c.target=target;c.action=action;c.amount=amount;return c;
}
static Outcome run(PersistentWorld& w,const Command& c){
 CHECK(w.submit(c));w.step();
 CHECK(w.world().events().back().command_id==c.id);
 return w.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play09_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try {
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string(),recovered=(dir/"recovered.sqlite").string();
  EntityId ship=0,drone=0,farDrone=0;
  {
   Simulation seed(909);
   ship=seed.add_colony_ship(7,{0,0},60,24);
   drone=seed.add_training_drone({2000,0});
   farDrone=seed.add_training_drone({80000,0});
   CHECK(seed.find(drone)->cargo==6);
   CHECK(seed.total_ore()==12);
   PersistentWorld w(db,std::move(seed));
   CHECK(run(w,cmd(w,1,ship,drone,Action::SalvageWreck,6))==Outcome::InvalidTarget);
   for(auto id:{2ULL,4ULL,6ULL}){
    CHECK(run(w,cmd(w,id,ship,drone,Action::FireTrainingWeapon))==Outcome::Applied);
   }
   CHECK(w.world().find(drone)->type==EntityType::Wreck);
   CHECK(w.world().find(drone)->cargo==6);
   CHECK(w.world().total_ore()==12);
   CHECK(run(w,cmd(w,7,ship,drone,Action::SalvageWreck,6,8))==Outcome::Unauthorized);
   CHECK(run(w,cmd(w,8,ship,farDrone,Action::SalvageWreck,6))==Outcome::InvalidTarget);
   CHECK(run(w,cmd(w,9,ship,drone,Action::SalvageWreck,4))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==4);
   CHECK(w.world().find(ship)->cargo==4);
   CHECK(w.world().find(drone)->cargo==2);
   CHECK(w.world().total_ore()==12);
  }
  {
   PersistentWorld w(db,Simulation(1));
   CHECK(w.world().find(ship)->cargo==4);
   CHECK(w.world().find(drone)->cargo==2);
   CHECK(run(w,cmd(w,10,ship,drone,Action::SalvageWreck,6))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==2);
   CHECK(w.world().find(ship)->cargo==6);
   CHECK(w.world().find(drone)->cargo==0);
   CHECK(w.world().find(drone)->type==EntityType::Wreck);
   CHECK(run(w,cmd(w,11,ship,drone,Action::SalvageWreck,1))==Outcome::NoResources);
   CHECK(w.world().total_ore()==12);
   PersistentWorld::recover_copy(db,recovered);
  }
  {
   PersistentWorld w(recovered,Simulation(2));
   CHECK(w.world().find(ship)->cargo==6);
   CHECK(w.world().find(drone)->cargo==0);
   CHECK(w.world().find(drone)->type==EntityType::Wreck);
   CHECK(w.world().find(farDrone)->cargo==6);
   CHECK(w.world().total_ore()==12);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-09 conserved, authorized, persistent wreck material recovery\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-09 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
