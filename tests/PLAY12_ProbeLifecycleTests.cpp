#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY12: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command order(PersistentWorld& w,std::uint64_t id,EntityId ship,Action kind,
                     EntityId target=0,PlayerId issuer=7){
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
  ("sg_play12_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  fs::create_directories(dir);
  const auto db=(dir/"world.sqlite").string(),copy=(dir/"recovered.sqlite").string();
  EntityId ship=0,near=0,far=0;
  {
   Simulation seed(1212);
   ship=seed.add_colony_ship(7,{0,0},60,24);
   seed.enable_individual_workforce();
   seed.provision_starter_exploration_scanner(ship);
   seed.provision_starter_probe_kit(ship);
   near=seed.add_asteroid({2000,0},20);
   far=seed.add_asteroid({100000,0},30);
   CHECK(seed.find(ship)->fitting.hardpoints[0]==1);
   CHECK(seed.find(ship)->fitting.hardpoints[1]==2);
   CHECK(seed.find(ship)->survey_probes_carried==1);
   CHECK(seed.total_ore()==56); // 50 ore + 2t scanner + 2t launcher + 2t probe
   // Gameplay link is radial, not a square around the carrier.
   const auto link=Simulation::kDeepSpaceProbeCommsRangeMm;
   CHECK(Simulation::within_deep_probe_link(link,0));
   CHECK(!Simulation::within_deep_probe_link(link+1,0));
   CHECK(Simulation::within_deep_probe_link(link/2,link/2));
   CHECK(!Simulation::within_deep_probe_link(link*4/5,link*4/5));
   PersistentWorld w(db,std::move(seed));
   CHECK(run(w,order(w,1,ship,Action::DeploySurveyProbe,0,8))==Outcome::Unauthorized);
   CHECK(w.world().survey_probes().empty());
   CHECK(run(w,order(w,2,ship,Action::DeploySurveyProbe))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==1);
   CHECK(w.world().find(ship)->survey_probes_carried==0);
   CHECK(w.world().survey_probes().count(2)==1);
   CHECK(w.world().survey_probes().at(2).position.x==0);
   CHECK(w.world().total_ore()==56);
   CHECK(run(w,order(w,3,ship,Action::DeploySurveyProbe))==Outcome::NoResources);
   CHECK(run(w,order(w,4,ship,Action::ScanSurveyProbe,2,8))==Outcome::Unauthorized);
   CHECK(run(w,order(w,5,ship,Action::ScanSurveyProbe,2))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==1);
   CHECK(w.world().pending_probe_report_count()==1);
   CHECK(w.world().canonical_state().find("galaxy-kernel-v68|")==0);
   {
    const auto serialized=w.world().canonical_state();
    const auto restored=Simulation::from_snapshot(serialized);
    CHECK(restored.pending_probe_report_count()==1);
    CHECK(restored.discoveries().count({7,near})==0);
    // Native parser must reject forged ownership of a withheld coordinate.
    auto corrupted=serialized;
    const auto marker=std::string("pendingprobe|7|");
    const auto where=corrupted.find(marker);
    CHECK(where!=std::string::npos);
    corrupted.replace(where,marker.size(),"pendingprobe|8|");
    bool rejected=false;
    try{(void)Simulation::from_snapshot(corrupted);}
    catch(const std::exception&){rejected=true;}
    CHECK(rejected);
   }
   CHECK(w.world().discoveries().count({7,near})==0); // no early revelation
   CHECK(w.world().discoveries().count({7,far})==0);
   CHECK(run(w,order(w,6,ship,Action::ScanSurveyProbe,2))==Outcome::Applied);
   CHECK(w.world().events().back().transferred==0);
   CHECK(run(w,order(w,7,ship,Action::RecoverSurveyProbe,2))==Outcome::Applied);
   CHECK(w.world().survey_probes().empty());
   CHECK(w.world().find(ship)->survey_probes_carried==1);
   CHECK(w.world().total_ore()==56);
   CHECK(run(w,order(w,8,ship,Action::RecoverSurveyProbe,2))==Outcome::InvalidTarget);
   CHECK(run(w,order(w,9,ship,Action::DeploySurveyProbe))==Outcome::Applied);
   CHECK(w.world().survey_probes().count(9)==1);
  }
  {
   PersistentWorld w(db,Simulation(1));
   CHECK(w.world().survey_probes().count(9)==1);
   CHECK(w.world().find(ship)->survey_probes_carried==0);
   CHECK(w.world().pending_probe_report_count()==1);
   CHECK(w.world().discoveries().count({7,near})==0);
   CHECK(w.world().total_ore()==56);
   // Reconfiguration changes future scans, never the due tick of already
   // committed pending observations.
   w.configure_probe_report_tick_ms(20);
   CHECK(w.world().probe_report_delay_ticks()==500);
   CHECK(w.world().pending_probe_report_count()==1);
   w.steps(5);
   CHECK(w.world().discoveries().count({7,near})==0);
   w.steps(1);
   CHECK(w.world().pending_probe_report_count()==0);
   CHECK(w.world().discoveries().count({7,near})==1);
   PersistentWorld::recover_copy(db,copy);
  }
  {
   PersistentWorld w(copy,Simulation(2));
   CHECK(w.world().survey_probes().count(9)==1);
   CHECK(w.world().survey_probes().at(9).carrier==ship);
   CHECK(w.world().find(ship)->fitting.probe_launcher());
   CHECK(w.world().total_ore()==56);
   CHECK(run(w,order(w,10,ship,Action::RecoverSurveyProbe,9))==Outcome::Applied);
   CHECK(w.world().total_ore()==56);
  }
  fs::remove_all(dir);
  std::cout<<"PASS PLAY-12 finite native survey probe launch, scanning, recovery, persistence\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL PLAY-12 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
