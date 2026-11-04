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
static void physical_celestial_world() {
 const auto dir=fs::temp_directory_path()/
   ("sg_play13b_cosmos_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);
 const auto db=(dir/"world.sqlite").string();
 try {
  std::string original_snapshot;
  std::uint64_t original_ore=0;
  {
   Simulation seed(71337);
   seed.add_system(2);
   const auto ship=seed.add_colony_ship(7,{0,0},60,24);
   seed.provision_starter_exploration_scanner(ship);
   seed.add_asteroid({2000,0},50);
   original_ore=static_cast<std::uint64_t>(seed.total_ore());
   CHECK(seed.physical_celestial_systems().empty());
   PersistentWorld w(db,std::move(seed));
   CHECK(w.world().scan_physical_celestials(7,ship).empty());
   w.ensure_physical_celestial_systems(); // durable upgrade from old world
   CHECK(w.world().canonical_state().find("galaxy-kernel-v70|")==0);
   CHECK(w.world().physical_celestial_systems().size()==2);
   const auto bodies=w.world().scan_physical_celestials(7,ship);
   CHECK(!bodies.empty());
   CHECK(w.world().scan_physical_celestials(8,ship).empty());
   CHECK(!w.world().celestial_navigation_anchor(8,ship,bodies.front().id));
   CHECK(w.world().celestial_navigation_anchor(7,ship,bodies.front().id).has_value());
   CHECK(!w.world().celestial_navigation_anchor(7,ship,0).has_value());
   CHECK(!w.world().celestial_navigation_anchor(7,ship,
       (std::uint64_t(2)<<32)|1).has_value());
   CHECK(w.world().total_ore()==static_cast<std::int64_t>(original_ore));
   const auto& second=w.world().physical_celestial_systems().at(2);
   CHECK(!second.empty()&&second.front().system==2);
   std::set<std::uint64_t> all_ids;
   bool elevated_body=false;
   for(const auto& [system,world_bodies]:w.world().physical_celestial_systems()){
    CHECK(!world_bodies.empty()&&world_bodies.size()<=50);
    for(const auto& body:world_bodies){
     CHECK(body.system==system&&body.id!=0&&body.physical_radius_mm>0);
     if(body.position.z_mm!=0)elevated_body=true;
     CHECK(all_ids.insert(body.id).second);
     if(body.kind!=CelestialKind::Star){
       CHECK(body.parent_id!=0);
       CHECK(all_ids.count(body.parent_id)==1);
     }
    }
   }
   CHECK(elevated_body); // world coordinates are genuinely 3D
   const auto generation=w.generation();
   w.ensure_physical_celestial_systems();
   CHECK(w.generation()==generation);
   original_snapshot=w.world().canonical_state();
   CHECK(Simulation::from_snapshot(original_snapshot).canonical_state()==original_snapshot);
   auto invalid=original_snapshot;
   const auto at=invalid.find("celestial|");
   CHECK(at!=std::string::npos);
   const auto end=invalid.find('\n',at);
   CHECK(end!=std::string::npos);
   auto line=invalid.substr(at,end-at);
   const auto pos=line.rfind('|');
   CHECK(pos!=std::string::npos);
   line.replace(pos+1,line.size()-pos-1,"12345");
   invalid.replace(at,end-at,line);
   bool rejected=false;
   try {(void)Simulation::from_snapshot(invalid);}
   catch(const std::exception&){rejected=true;}
   CHECK(rejected);
  }
  {
   PersistentWorld restored(db,Simulation(1));
   CHECK(restored.world().canonical_state()==original_snapshot);
   CHECK(restored.world().physical_celestial_systems().size()==2);
   CHECK(restored.world().total_ore()==static_cast<std::int64_t>(original_ore));
  }
  fs::remove_all(dir);
 }catch(...){std::error_code ec;fs::remove_all(dir,ec);throw;}
}
static void focus_mission() {
 const auto dir=fs::temp_directory_path()/
   ("sg_play13b_focus_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);
 const auto db=(dir/"focus.sqlite").string();
 try {
  EntityId ship=0,rock=0,farrock=0;
  std::uint64_t probe=2;
  {
   Simulation seed(8091);
   ship=seed.add_colony_ship(7,{0,0},60,24);
   seed.enable_individual_workforce();
   seed.provision_starter_exploration_scanner(ship);
   seed.provision_starter_probe_kit(ship);
   rock=seed.add_asteroid({16000000,0},20); // 16 km from carrier
   farrock=seed.add_asteroid({200000000,0},20); // distant cancellation target
   PersistentWorld w(db,std::move(seed));
   CHECK(!w.world().scan_celestial_profile(8,ship).has_value());
   CHECK(!w.world().scan_environment(8,ship).has_value());
   const auto celestial=w.world().scan_celestial_profile(7,ship);
   CHECK(celestial.has_value() && celestial->star_count>=1);
   CHECK(celestial->orbital_bands.size()>=3);
   const auto environmental=w.world().scan_environment(7,ship);
   CHECK(environmental.has_value() && environmental->system==1);
   CHECK(environmental->ship_speed_mm_per_tick==0);
   CHECK(w.world().coarse_probe_signatures(8,ship).empty());
   auto signatures=w.world().coarse_probe_signatures(7,ship);
   CHECK(signatures.size()==2 && signatures[0].id==rock);
   CHECK(signatures[0].uncertainty_mm==5000000);
   CHECK(w.world().coarse_probe_signatures(7,ship,5000000).empty());
   CHECK(signatures[0].coarse.x==15000000);
   CHECK(run(w,order(w,probe,ship,Action::DeploySurveyProbe))==Outcome::Applied);
   auto unauthorized=order(w,3,ship,Action::FocusSurveyProbe,probe,8);
   unauthorized.amount=static_cast<std::int64_t>(rock);
   CHECK(run(w,unauthorized)==Outcome::Unauthorized);
   auto lock=order(w,4,ship,Action::FocusSurveyProbe,probe);
   lock.amount=static_cast<std::int64_t>(rock);
   CHECK(run(w,lock)==Outcome::Applied);
   CHECK(w.world().canonical_state().find("galaxy-kernel-v69|")==0);
   CHECK(w.world().survey_probes().at(probe).focus_site==rock);
   CHECK(w.world().survey_probes().at(probe).position.x>0);
   CHECK(Simulation::from_snapshot(w.world().canonical_state())
     .survey_probes().at(probe).focus_site==rock);
   // A forged waypoint must be rejected even when the outer record is valid.
   auto corrupt=w.world().canonical_state();
   const auto probe_prefix=std::string("surveyprobe|")+std::to_string(probe)+"|";
   const auto pos=corrupt.find(probe_prefix);
   CHECK(pos!=std::string::npos);
   const auto line_end=corrupt.find('\n',pos);
   auto line=corrupt.substr(pos,line_end-pos);
   const auto delimiter=line.rfind('|');
   CHECK(delimiter!=std::string::npos);
   line.replace(delimiter+1,line.size()-delimiter-1,"25000");
   corrupt.replace(pos,line_end-pos,line);
   bool rejected=false;
   try{(void)Simulation::from_snapshot(corrupt);}
   catch(const std::exception&){rejected=true;}
   CHECK(rejected);
   CHECK(run(w,order(w,5,ship,Action::RecoverSurveyProbe,probe))==Outcome::InTransition);
  }
  {
   PersistentWorld w(db,Simulation(22));
   const auto catalogue=w.world().scan_celestial_profile(7,ship);
   CHECK(catalogue.has_value());
   CHECK(catalogue->star_count>=1 && catalogue->orbital_bands.size()>=3);
   CHECK(!w.world().scan_celestial_profile(8,ship).has_value());
   CHECK(w.world().survey_probes().at(probe).focus_site==rock);
   // First approach arrives at the coarse region; only then may the
   // scanner obtain a fine target waypoint (persisted safely at restart).
   w.step();
   CHECK(w.world().survey_probes().at(probe).focus_site==rock);
   CHECK(w.world().survey_probes().at(probe).focus_waypoint.x==16000000);
   CHECK(Simulation::from_snapshot(w.world().canonical_state())
     .survey_probes().at(probe).focus_waypoint.x==16000000);
   w.steps(4);
   CHECK(w.world().survey_probes().at(probe).focus_site==0);
   CHECK(w.world().pending_probe_report_count()==1);
   CHECK(w.world().discoveries().count({7,rock})==0);
   w.steps(6);
   CHECK(w.world().discoveries().count({7,rock})==0);
   w.step();
   CHECK(w.world().discoveries().count({7,rock})==1);
   CHECK(w.world().pending_probe_report_count()==0);
   // A second, still-unresolved signature can be focused then cancelled.
   auto cancel_target=order(w,6,ship,Action::FocusSurveyProbe,probe);
   cancel_target.amount=static_cast<std::int64_t>(farrock);
   CHECK(run(w,cancel_target)==Outcome::Applied);
   CHECK(w.world().survey_probes().at(probe).focus_site==farrock);
   CHECK(run(w,order(w,7,ship,Action::CancelSurveyFocus,probe))==Outcome::Applied);
   CHECK(w.world().survey_probes().at(probe).focus_site==0);
   w.steps(5);
   CHECK(w.world().discoveries().count({7,farrock})==0);
   CHECK(w.world().pending_probe_report_count()==0);
   CHECK(w.world().total_ore()==46); // 40 asteroid ore plus six physical equipment units
  }
  fs::remove_all(dir);
 }catch(...){std::error_code ignored;fs::remove_all(dir,ignored);throw;}
}
int main(){
 const auto dir=fs::temp_directory_path()/
  ("sg_play12_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  physical_celestial_world();
  focus_mission();
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
