#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY10: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command order(PersistentWorld& w,std::uint64_t id,EntityId ship,Action kind,EntityId target=0,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.issuer=issuer;c.actor=ship;c.target=target;c.action=kind;return c;
}
static Outcome run(PersistentWorld& w,const Command& c){
 CHECK(w.submit(c));w.step();
 CHECK(w.world().events().back().command_id==c.id);
 return w.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play10_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string(),copy=(dir/"recovered.sqlite").string();
  EntityId ship=0,rock=0,far=0;
  {
   Simulation genesis(1010);
   ship=genesis.add_colony_ship(7,{0,0},60,24);
   rock=genesis.add_asteroid({2000,0},20);
   far=genesis.add_asteroid({100000,0},30);
   PersistentWorld w(db,std::move(genesis));
   CHECK(w.world().discoveries().empty());
   CHECK(run(w,order(w,1,ship,Action::SurveyLocal,0,8))==Outcome::Unauthorized);
   CHECK(w.world().discoveries().empty());
   CHECK(run(w,order(w,2,ship,Action::SurveyLocal))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==1);
   CHECK(w.world().discoveries().size()==1);
   const auto rec=w.world().discoveries().at({7,rock});
   CHECK(rec.first_tick==w.world().tick());
   CHECK(rec.location.x==2000 && rec.system==1 && !rec.geological);
   CHECK(run(w,order(w,3,ship,Action::SurveyLocal))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==0);
   CHECK(w.world().discoveries().size()==1);
   // Physical world-state change remains native and persisted.
   auto mining=order(w,4,ship,Action::Mine,rock);mining.amount=20;
   CHECK(run(w,mining)==Outcome::Applied);
   CHECK(w.world().discoveries().at({7,rock}).first_tick==rec.first_tick);
   CHECK(w.world().total_ore()==50);
  }
  {
   PersistentWorld w(db,Simulation(1));
   CHECK(w.world().discoveries().size()==1);
   CHECK(w.world().discoveries().at({7,rock}).location.x==2000);
   // Move with real one-axis acceleration + native simulation steps.
   Command thrust=order(w,5,ship,Action::Thrust);
   thrust.thrust_x=1;
   CHECK(run(w,thrust)==Outcome::Applied);
   w.steps(240);
   CHECK(w.world().find(ship)->position.x>50000);
   CHECK(run(w,order(w,6,ship,Action::SurveyLocal))==Outcome::Applied);
   CHECK(w.world().discoveries().count({7,far})==1);
   CHECK(w.world().discoveries().size()==2);
   CHECK(w.world().discoveries().at({7,rock}).first_tick==2);
   CHECK(w.world().total_ore()==50);
   PersistentWorld::recover_copy(db,copy);
  }
  {
   PersistentWorld w(copy,Simulation(2));
   CHECK(w.world().discoveries().size()==2);
   CHECK(w.world().discoveries().at({7,rock}).first_tick==2);
   CHECK(w.world().discoveries().at({7,far}).location.x==100000);
   CHECK(w.world().total_ore()==50);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-10 first observations, repeat scan, travel, SQLite recovery\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-10 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
