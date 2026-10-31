#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B7.0k-2 check ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
 EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;c.target=target;
 c.action=action;c.amount=amount;c.thrust_x=selector;c.execute_tick=w.world().tick()+1;
 if(!w.submit(c))throw std::runtime_error("submit rejected id="+std::to_string(id));
 w.step();return w.world().events().back();
}
int main(){
 const auto tmp=fs::temp_directory_path()/("sg_b70k2_"+
  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);
 const auto db=(tmp/"orig.sqlite").string(),copy=(tmp/"recovered.sqlite").string();
 try{
 EntityId ship=0,site=0;std::int64_t mass=0;
 {
  Simulation s(707);ship=s.add_colony_ship(7,{0,0},100,24);
  s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
  const auto rock=s.add_asteroid({2000,0},90);mass=s.total_material_kg();
  PersistentWorld w(db,std::move(s));
  CHECK(send(w,ship,1,Action::Mine,rock,80).result==Outcome::Applied);
  CHECK(send(w,ship,2,Action::RefineOre,0,80).result==Outcome::Applied);
  CHECK(send(w,ship,3,Action::FabricateModule,0,30).result==Outcome::Applied);
  CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
  site=w.world().built_outposts().begin()->first;
  CHECK(send(w,ship,5,Action::ConstructReclaimer,site).result==Outcome::NoResources);
  CHECK(send(w,ship,6,Action::ConstructUtility,site).result==Outcome::Applied);
  CHECK(send(w,ship,7,Action::ConstructReclaimer,site).result==Outcome::NoResources);
  CHECK(send(w,ship,8,Action::ConstructFarm,site).result==Outcome::Applied);
  CHECK(send(w,ship,9,Action::ConstructHabitat,site).result==Outcome::Applied);
  CHECK(send(w,ship,10,Action::ConstructReclaimer,site).result==Outcome::Applied);
  CHECK(send(w,ship,11,Action::ConstructReclaimer,site).result==Outcome::InvalidTarget);
  CHECK(send(w,ship,12,Action::ConstructReclaimer,site,1,0,8).result==Outcome::Unauthorized);
  Command malformed{};malformed.id=13;malformed.execute_tick=w.world().tick()+1;
  malformed.sequence=13;malformed.issuer=7;malformed.actor=ship;malformed.target=site;
  malformed.action=Action::ConstructReclaimer;malformed.amount=0;
  CHECK(!w.submit(malformed));
  for(int i=1;i<=3;++i){
   CHECK(send(w,ship,13+i,Action::TransferHabitatStock,site,140,i).result==Outcome::Applied);
   CHECK(send(w,ship,16+i,Action::TransferFarmStock,site,140,i).result==Outcome::Applied);
  }
  CHECK(send(w,ship,20,Action::TransferFarmStock,site,140,4).result==Outcome::Applied);
  CHECK(send(w,ship,21,Action::TransferFarmStock,site,140,5).result==Outcome::Applied);
  CHECK(send(w,ship,22,Action::AssignHabitatResidents,site,4).result==Outcome::Applied);
  CHECK(send(w,ship,23,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
  w.steps(87); // at least three needs intervals, some wastewater and CO2 reprocessed
  const auto* r=w.world().reclaimer(site);CHECK(r && r->cycles>=3);
  CHECK(r->water_recovered_total_kg>0 && r->oxygen_generated_total_kg>0);
  CHECK(r->sludge_kg*9==r->water_recovered_total_kg);
  CHECK(r->carbon_residue_kg*7==r->oxygen_generated_total_kg*3);
  CHECK(w.world().total_material_kg()==mass);
  const auto recovery=w.world().assess_life_recovery(ship);
  CHECK(recovery.occupied_sites==1 && recovery.equipped_sites==1);
  CHECK(!recovery.independent_renewal_proven);
  CHECK(w.world().assess_resilience(ship).blocker_mask&WaterRenewalUnverified);
  CHECK(!w.world().assess_resilience(ship).autonomous_growth_ready);
  CHECK(w.world().canonical_state().rfind("galaxy-kernel-v17|",0)==0);
  const auto snap=w.world().canonical_state();
  CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
  // Corrupt a mass-balanced per-plant record without changing its total input.
  auto forged=snap;
  const auto start=forged.find("reclaimer|"),end=forged.find('\n',start);
  CHECK(start!=std::string::npos && end!=std::string::npos);
  const auto line=forged.substr(start,end-start);
  CHECK(line.find('|')!=std::string::npos);
  const auto total=std::to_string(r->water_recovered_total_kg);
  CHECK(r->water_recovered_total_kg>0);
  const auto found=line.find("|"+total+"|");
  CHECK(found!=std::string::npos);
  forged.replace(start+found+1,total.size(),std::to_string(r->water_recovered_total_kg+1));
  bool malformed_rejected=false;
  try{(void)Simulation::from_snapshot(forged);}catch(...){malformed_rejected=true;}
  CHECK(malformed_rejected);
  // Reclaimer throughput stops when installed power fails; no fabricated output.
  const auto old_water=r->water_recovered_total_kg;
  w.steps(240);
  r=w.world().reclaimer(site);CHECK(!w.world().utility(site)->powered);
  CHECK(r->water_recovered_total_kg>=old_water);
  const auto old_powered=r->consecutive_powered_cycles;
  CHECK(old_powered==0);
  CHECK(w.world().total_material_kg()==mass);
  PersistentWorld::recover_copy(db,copy);
 }
 for(const auto& path:{db,copy}){
  PersistentWorld w(path,Simulation(7));
  CHECK(w.world().reclaimer(site));
  CHECK(w.world().reclaimer(site)->consecutive_powered_cycles==0);
  CHECK(w.world().total_material_kg()==mass);
  CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
 }
 sqlite3* handle=nullptr;CHECK(sqlite3_open(copy.c_str(),&handle)==SQLITE_OK);
 char* msg=nullptr;
 const auto rc=sqlite3_exec(handle,"UPDATE resource_ledger SET amount=amount+1 WHERE reason='reclaimer_construction_t'",nullptr,nullptr,&msg);
 if(msg)sqlite3_free(msg);CHECK(rc==SQLITE_OK && sqlite3_changes(handle)>0);
 sqlite3_close(handle);
 bool rejected=false;
 try{PersistentWorld corrupt(copy,Simulation(123));(void)corrupt;}catch(...){rejected=true;}
 CHECK(rejected);
 fs::remove_all(tmp);
 std::cout<<"B7.0k-2 local recycler production, physical losses, power outage, persistence, recovery and tamper defense: PASS\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"B7.0k-2 FAIL: "<<e.what()<<'\n';fs::remove_all(tmp);return 1;}
}
