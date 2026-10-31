#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{ if(!(x))throw std::runtime_error(std::string("check: ")+#x+" at line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;c.amount=amount;
    c.thrust_x=selector;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
 const auto tmp=fs::temp_directory_path()/("b70g_utility_"+
     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);
 try{
  auto db=(tmp/"colony.sqlite").string(),copy=(tmp/"journal_copy.sqlite").string();
  EntityId ship=0,site=0;std::int64_t initial_mass=0;
  {
   Simulation s(70);ship=s.add_colony_ship(7,{0,0},100,24);
   s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
   auto rock=s.add_asteroid({2000,0},70);initial_mass=s.total_material_kg();
   PersistentWorld w(db,std::move(s));
   CHECK(send(w,ship,1,Action::Mine,rock,60).result==Outcome::Applied);
   CHECK(send(w,ship,2,Action::RefineOre,0,60).result==Outcome::Applied);
   CHECK(send(w,ship,3,Action::FabricateModule,0,30).result==Outcome::Applied);
   CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
   site=w.world().built_outposts().begin()->first;
   CHECK(send(w,ship,5,Action::ConstructUtility,site,1,0,8).result==Outcome::Unauthorized);
   CHECK(w.world().utility(site)==nullptr);
   CHECK(send(w,ship,6,Action::RepairUtility,site).result==Outcome::InvalidTarget);
   CHECK(send(w,ship,7,Action::ConstructUtility,site).result==Outcome::Applied);
   CHECK(send(w,ship,8,Action::ConstructUtility,site).result==Outcome::InvalidTarget);
   CHECK(send(w,ship,9,Action::RepairUtility,site).result==Outcome::InvalidArgument);
   CHECK(w.world().utility(site)->embodied_ore==4);
   CHECK(send(w,ship,10,Action::ConstructFarm,site).result==Outcome::Applied);
   CHECK(send(w,ship,11,Action::ConstructHabitat,site).result==Outcome::Applied);
   CHECK(send(w,ship,12,Action::TransferFarmStock,site,120,1).result==Outcome::Applied);
   CHECK(send(w,ship,13,Action::TransferFarmStock,site,650,2).result==Outcome::Applied);
   CHECK(send(w,ship,14,Action::TransferFarmStock,site,120,3).result==Outcome::Applied);
   CHECK(send(w,ship,15,Action::TransferFarmStock,site,150,4).result==Outcome::Applied);
   CHECK(send(w,ship,16,Action::TransferFarmStock,site,150,5).result==Outcome::Applied);
   CHECK(send(w,ship,17,Action::TransferHabitatStock,site,100,1).result==Outcome::Applied);
   CHECK(send(w,ship,18,Action::TransferHabitatStock,site,120,2).result==Outcome::Applied);
   CHECK(send(w,ship,19,Action::TransferHabitatStock,site,100,3).result==Outcome::Applied);
   CHECK(send(w,ship,20,Action::AssignHabitatResidents,site,3).result==Outcome::Applied);
   CHECK(send(w,ship,21,Action::AssignFarmWorkers,site,4).result==Outcome::Applied);
   CHECK(w.world().colony_ship(ship)->crew==17);
   CHECK(w.world().total_material_kg()==initial_mass);
   w.steps(65);
   auto v=w.world().assess_colony(ship);
   CHECK(v.utilities_sustained);
   CHECK(v.powered_outposts==1 && v.unpowered_outposts==0);
   CHECK(v.stage==ColonyViability::Stage::Livable);
   CHECK(!v.autonomous_growth_ready);
   CHECK(w.world().utility(site)->generated_energy>0);
   CHECK(w.world().utility(site)->consumed_energy>0);
   CHECK(w.world().utility(site)->condition<100);
   CHECK(w.world().total_material_kg()==initial_mass);
   const auto produced_before=w.world().farm(site)->production_cycles;
   w.steps(245); // wear reaches zero, residual stored power runs out
   const auto* u=w.world().utility(site);
   CHECK(u->condition==0);
   CHECK(!u->powered && u->outages>0);
   CHECK(!w.world().assess_colony(ship).utilities_sustained);
   const auto produced_after=w.world().farm(site)->production_cycles;
   CHECK(produced_after>=produced_before);
   w.steps(35);
   CHECK(w.world().farm(site)->production_cycles==produced_after);
   CHECK(w.world().total_material_kg()==initial_mass);
   // An illegal selector or negative repair amount must fail at submission.
   Command forged{};forged.id=22;forged.execute_tick=w.world().tick()+1;
   forged.sequence=22;forged.issuer=7;forged.actor=ship;
   forged.target=site;forged.action=Action::RepairUtility;forged.amount=-1;
   CHECK(!w.submit(forged));forged.amount=1;forged.thrust_x=1;CHECK(!w.submit(forged));
   CHECK(send(w,ship,23,Action::RepairUtility,site).result==Outcome::Applied);
   CHECK(w.world().utility(site)->condition>=60);
   CHECK(w.world().utility(site)->repairs==1);
   CHECK(w.world().utility(site)->embodied_ore==6);
   CHECK(w.world().total_material_kg()==initial_mass);
   w.steps(65);
   CHECK(w.world().assess_colony(ship).utilities_sustained);
   CHECK(w.world().utility(site)->powered);
   const auto snap=w.world().canonical_state();
   CHECK(snap.rfind("galaxy-kernel-v13|",0)==0);
   CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
   PersistentWorld::recover_copy(db,copy);
  }
  for(const auto& path:{db,copy}){
   PersistentWorld w(path,Simulation(999));
   const auto* u=w.world().utility(site);
   CHECK(u && u->repairs==1 && u->embodied_ore==6);
   CHECK(u->powered && u->generated_energy>u->consumed_energy);
   CHECK(w.world().colony_ship(ship)->crew==17);
   CHECK(w.world().assess_colony(ship).utilities_sustained);
   CHECK(w.world().total_material_kg()==initial_mass);
   w.steps(20);CHECK(w.world().total_material_kg()==initial_mass);
  }
  sqlite3* sql=nullptr;CHECK(sqlite3_open(copy.c_str(),&sql)==SQLITE_OK);
  char* error=nullptr;
  auto rc=sqlite3_exec(sql,"UPDATE resource_ledger SET amount=amount+1 WHERE reason='utility_repair_t'",nullptr,nullptr,&error);
  if(error)sqlite3_free(error);
  CHECK(rc==SQLITE_OK && sqlite3_changes(sql)==1);sqlite3_close(sql);
  bool rejected=false;
  try{PersistentWorld invalid(copy,Simulation(2));(void)invalid;}
  catch(const std::exception&){rejected=true;}
  CHECK(rejected);
  fs::remove_all(tmp);
  std::cout<<"B7.0g power generation, outage, durable repairs, ledger: PASS\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"B7.0g FAILED: "<<e.what()<<'\n';fs::remove_all(tmp);return 1;}
}
