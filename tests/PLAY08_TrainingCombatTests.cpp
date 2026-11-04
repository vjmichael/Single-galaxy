#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY08: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command fire(PersistentWorld& world,std::uint64_t id,EntityId actor,EntityId target,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=world.world().tick()+1;
 c.issuer=issuer;c.actor=actor;c.target=target;c.action=Action::FireTrainingWeapon;return c;
}
static Outcome execute(PersistentWorld& world,Command cmd){
 CHECK(world.submit(cmd));world.step();
 CHECK(world.world().events().back().command_id==cmd.id);
 return world.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play08_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try {
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string();
  const auto copy=(dir/"recovered.sqlite").string();
  EntityId ship=0,drone=0,farDrone=0,behindDrone=0;
  {
   Simulation genesis(808);ship=genesis.add_colony_ship(7,{0,0},60,24);
   drone=genesis.add_training_drone({8000,0});
   farDrone=genesis.add_training_drone({80000,0});
   behindDrone=genesis.add_training_drone({-8000,0});
   CHECK(genesis.find(ship)->weapon_charges==12);
   CHECK(genesis.find(drone)->hull==45);
   PersistentWorld w(db,std::move(genesis));
   CHECK(execute(w,fire(w,100,ship,farDrone))==Outcome::OutOfRange);
   CHECK(execute(w,fire(w,101,ship,behindDrone))==Outcome::OutOfRange);
   CHECK(w.world().find(ship)->weapon_charges==12);
   CHECK(execute(w,fire(w,1,ship,drone,8))==Outcome::Unauthorized);
   CHECK(w.world().find(ship)->weapon_charges==12);
   CHECK(execute(w,fire(w,2,ship,ship))==Outcome::InvalidTarget);
   CHECK(execute(w,fire(w,3,ship,drone))==Outcome::Applied);
   CHECK(w.world().find(ship)->weapon_charges==11);
   CHECK(w.world().find(drone)->hull==30);
   CHECK(w.world().events().back().transferred==15);
   CHECK(execute(w,fire(w,4,ship,drone))==Outcome::InTransition);
   CHECK(w.world().find(ship)->weapon_charges==11);
  }
  {
   PersistentWorld w(db,Simulation(2));
   CHECK(w.world().find(ship)->weapon_charges==11);
   CHECK(w.world().find(drone)->hull==30);
   CHECK(execute(w,fire(w,5,ship,drone))==Outcome::Applied);
   CHECK(w.world().find(drone)->hull==15);
   CHECK(execute(w,fire(w,6,ship,drone))==Outcome::InTransition);
   CHECK(execute(w,fire(w,7,ship,drone))==Outcome::Applied);
   CHECK(w.world().find(drone)->type==EntityType::Wreck);
   CHECK(w.world().find(drone)->hull==0);
   CHECK(w.world().find(ship)->weapon_charges==9);
   CHECK(execute(w,fire(w,8,ship,drone))==Outcome::InvalidTarget);
   CHECK(w.world().find(ship)->weapon_charges==9);
   PersistentWorld::recover_copy(db,copy);
  }
  {
   PersistentWorld w(copy,Simulation(3));
   CHECK(w.world().find(ship)->weapon_charges==9);
   CHECK(w.world().find(drone)->type==EntityType::Wreck);
   CHECK(w.world().find(drone)->owner_id==8);
   CHECK(w.world().find(ship)->hull==100);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-08 training weapon, owner, cooldown, finite charges, wreck, restart\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-08 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
