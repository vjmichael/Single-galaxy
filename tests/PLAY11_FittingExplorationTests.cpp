#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY11: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command issue(PersistentWorld& w,std::uint64_t id,EntityId ship,Action action,std::int64_t amount=0,PlayerId owner=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.actor=ship;c.issuer=owner;c.action=action;c.amount=amount;return c;
}
static Outcome run(PersistentWorld& w,const Command& c){
 CHECK(w.submit(c));w.step();CHECK(w.world().events().back().command_id==c.id);
 return w.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play11_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try {
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string(), copy=(dir/"recovered.sqlite").string();
  EntityId ship=0, rock=0;
  {
   Simulation seed(1111);ship=seed.add_colony_ship(7,{0,0},60,24);
   seed.provision_starter_exploration_scanner(ship);
   rock=seed.add_asteroid({2000,0},20);
   CHECK(seed.find(ship)->fitting.hardpoints.size()==2);
   CHECK(seed.find(ship)->fitting.support.size()==2);
   CHECK(seed.find(ship)->fitting.subsystems.size()==2);
   CHECK(seed.find(ship)->fitting.rigging.size()==1);
   CHECK(seed.find(ship)->fitting.exploration_scanner());
   CHECK(seed.total_ore()==22); // 20 asteroid + 2 embodied scanner
   PersistentWorld w(db,std::move(seed));
   CHECK(run(w,issue(w,1,ship,Action::SurveyLocal,0,8))==Outcome::Unauthorized);
   CHECK(run(w,issue(w,2,ship,Action::SurveyLocal))==Outcome::Applied);
   CHECK(w.world().discoveries().count({7,rock})==1);
   CHECK(run(w,issue(w,3,ship,Action::ConfigureExplorationScanner,-1))==Outcome::Applied);
   CHECK(!w.world().find(ship)->fitting.exploration_scanner());
   CHECK(w.world().colony_ship(ship)->modules==1);
   CHECK(w.world().total_ore()==22);
   CHECK(run(w,issue(w,4,ship,Action::SurveyLocal))==Outcome::NoResources);
   CHECK(run(w,issue(w,5,ship,Action::ConfigureExplorationScanner,-1))==Outcome::InvalidArgument);
   CHECK(run(w,issue(w,6,ship,Action::ConfigureExplorationScanner,1))==Outcome::Applied);
   CHECK(w.world().find(ship)->fitting.exploration_scanner());
   CHECK(w.world().colony_ship(ship)->modules==0);
   CHECK(w.world().total_ore()==22);
   CHECK(run(w,issue(w,7,ship,Action::ConfigureExplorationScanner,1))==Outcome::InvalidArgument);
   CHECK(run(w,issue(w,8,ship,Action::SurveyLocal))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==0);
  }
  {
   PersistentWorld w(db,Simulation(1));
   CHECK(w.world().find(ship)->fitting.exploration_scanner());
   CHECK(w.world().discoveries().count({7,rock})==1);
   CHECK(w.world().total_ore()==22);
   PersistentWorld::recover_copy(db,copy);
  }
  {
   PersistentWorld w(copy,Simulation(2));
   CHECK(w.world().find(ship)->fitting.hardpoints[0]==1);
   CHECK(w.world().find(ship)->fitting.support[0]==0);
   CHECK(w.world().total_ore()==22);
  }
  {
   Simulation seed(1212);auto unfitted=seed.add_colony_ship(7,{0,0},60,24);
   PersistentWorld w((dir/"unfitted.sqlite").string(),std::move(seed));
   CHECK(run(w,issue(w,9,unfitted,Action::SurveyLocal))==Outcome::NoResources);
   CHECK(run(w,issue(w,10,unfitted,Action::ConfigureExplorationScanner,1))==Outcome::NoResources);
   CHECK(!w.world().find(unfitted)->fitting.exploration_scanner());
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-11 four finite racks, scanner mount authority and conserved persisted module\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-11 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
