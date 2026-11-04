#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command order(PersistentWorld& w,std::uint64_t id,EntityId ship,Action kind,
                     EntityId target=0,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.issuer=issuer;c.actor=ship;c.target=target;c.action=kind;return c;
}
static Outcome execute(PersistentWorld& w,const Command& c){
 CHECK(w.submit(c));w.step();
 CHECK(w.world().events().back().command_id==c.id);
 return w.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play13_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try {
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string(),copy=(dir/"recovered.sqlite").string();
  EntityId ship=0,ore=0,ordinary=0,far=0;std::int64_t original=0;
  {
   Simulation seed(1313);
   ship=seed.add_colony_ship(7,{0,0},60,24);
   seed.enable_individual_workforce();
   seed.provision_starter_exploration_scanner(ship);
   seed.provision_starter_probe_kit(ship);
   ore=seed.add_geological_asteroid({20000,0},AsteroidClass::Metallic,55,5700);
   ordinary=seed.add_asteroid({2000,0},40);
   far=seed.add_geological_asteroid({11000000,0},AsteroidClass::Silicate,10,2000);
   original=seed.total_ore();
   PersistentWorld w(db,std::move(seed));
   CHECK(execute(w,order(w,1,ship,Action::LaunchGeologicalDrone,ore,8))==Outcome::Unauthorized);
   CHECK(w.world().survey_probes().empty());
   CHECK(execute(w,order(w,2,ship,Action::LaunchGeologicalDrone,ordinary))==Outcome::InvalidTarget);
   // The full 10 km drone control radius is native millimetres, not 10,000 mm.
   CHECK(Simulation::kDroneControlRangeMm==10000000LL);
   CHECK(Simulation::kDeepSpaceProbeCommsRangeMm==74798935350000LL);
   CHECK(w.world().survey_probes().empty());
   CHECK(execute(w,order(w,101,ship,Action::LaunchGeologicalDrone,far))==Outcome::OutOfRange);
   CHECK(w.world().find(ship)->survey_probes_carried==1);
   CHECK(w.world().survey_probes().empty());
   CHECK(execute(w,order(w,3,ship,Action::LaunchGeologicalDrone,ore))==Outcome::Applied);
   CHECK(w.world().find(ship)->survey_probes_carried==0);
   CHECK(w.world().survey_probes().count(3)==1);
   CHECK(w.world().survey_probes().at(3).drone_phase==1);
   CHECK(w.world().survey_probes().at(3).position.x>0);
   CHECK(w.world().total_ore()==original);
   CHECK(execute(w,order(w,4,ship,Action::LaunchGeologicalDrone,ore))==Outcome::NoResources);
   CHECK(execute(w,order(w,5,ship,Action::RecoverSurveyProbe,3))==Outcome::InTransition);
   w.steps(10);
   CHECK(w.world().survey_probes().at(3).sample_ore==0);
   CHECK(w.world().survey_probes().at(3).taxonomy==3);
   CHECK(w.world().find(ore)->ore==55); // geological scan never consumes ore
   CHECK(w.world().discoveries().count({7,ore})==0); // report still on drone
   CHECK(w.world().total_ore()==original);
  }
  {
   PersistentWorld w(db,Simulation(1));
   CHECK(w.world().survey_probes().count(3)==1);
   CHECK(w.world().survey_probes().at(3).sample_ore==0);
   CHECK(w.world().total_ore()==original);
   w.steps(20);
   CHECK(w.world().survey_probes().at(3).drone_phase==3);
   CHECK(w.world().survey_probes().at(3).position.x==0);
   CHECK(w.world().discoveries().count({7,ore})==1);
   CHECK(w.world().discoveries().at({7,ore}).survey_taxonomy==3);
   // Report arrived over the native radio link, without taking drone custody.
   CHECK(w.world().total_ore()==original);
   PersistentWorld::recover_copy(db,copy);
  }
  {
   PersistentWorld w(copy,Simulation(2));
   CHECK(w.world().survey_probes().at(3).drone_phase==3);
   CHECK(w.world().survey_probes().at(3).sample_ore==0);
   CHECK(execute(w,order(w,6,ship,Action::RecoverSurveyProbe,3))==Outcome::Applied);
   CHECK(w.world().survey_probes().empty());
   CHECK(w.world().find(ship)->survey_probes_carried==1);
   CHECK(w.world().find(ship)->cargo==0);
   CHECK(w.world().discoveries().count({7,ore})==1);
   CHECK(w.world().discoveries().at({7,ore}).survey_taxonomy==3);
   CHECK(w.world().discoveries().at({7,ore}).geological);
   CHECK(w.world().total_ore()==original);
   CHECK(execute(w,order(w,7,ship,Action::RecoverSurveyProbe,3))==Outcome::InvalidTarget);
   CHECK(w.world().total_ore()==original);
  }
  {
   PersistentWorld w(copy,Simulation(2));
   CHECK(w.world().discoveries().at({7,ore}).survey_taxonomy==3);
   CHECK(w.world().find(ship)->cargo==0);
   CHECK(w.world().total_ore()==original);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-13 non-extractive geological drone, survey data and SQLite recovery\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-13 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
