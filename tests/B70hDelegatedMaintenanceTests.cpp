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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B70h check: ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,
                  EntityId target=0,std::int64_t amt=1,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.issuer=issuer;c.actor=ship;c.action=a;c.target=target;c.amount=amt;
 CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
 const auto tmp=fs::temp_directory_path()/("single_galaxy_b70h_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);
 try{
  const auto db=(tmp/"maintenance.sqlite").string(),copy=(tmp/"copy.sqlite").string();
  EntityId ship=0,site=0;std::int64_t mass=0;
  {
   Simulation s(70);ship=s.add_colony_ship(7,{0,0},100,24);
   s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
   auto rock=s.add_asteroid({2000,0},70);mass=s.total_material_kg();
   PersistentWorld w(db,std::move(s));
   CHECK(send(w,ship,1,Action::Mine,rock,60).result==Outcome::Applied);
   CHECK(send(w,ship,2,Action::RefineOre,0,60).result==Outcome::Applied);
   CHECK(send(w,ship,3,Action::FabricateModule,0,30).result==Outcome::Applied);
   CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
   site=w.world().built_outposts().begin()->first;
   CHECK(send(w,ship,5,Action::ConstructUtility,site).result==Outcome::Applied);
   CHECK(send(w,ship,6,Action::ConstructFarm,site).result==Outcome::Applied);
   CHECK(send(w,ship,7,Action::ConstructHabitat,site).result==Outcome::Applied);
  } // reopen to preserve deterministic test command helper for selectors
  {
   PersistentWorld w(db,Simulation(99));
   auto stock=[&](std::uint64_t id,Action act,std::int64_t amount,int selector){
      Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;c.target=site;
      c.execute_tick=w.world().tick()+1;c.action=act;c.amount=amount;c.thrust_x=selector;
      CHECK(w.submit(c));w.step();return w.world().events().back();
   };
   CHECK(stock(9,Action::TransferFarmStock,200,1).result==Outcome::Applied);
   CHECK(stock(10,Action::TransferFarmStock,650,2).result==Outcome::Applied);
   CHECK(stock(11,Action::TransferFarmStock,200,3).result==Outcome::Applied);
   CHECK(stock(12,Action::TransferFarmStock,150,4).result==Outcome::Applied);
   CHECK(stock(13,Action::TransferFarmStock,150,5).result==Outcome::Applied);
   CHECK(stock(14,Action::TransferHabitatStock,100,1).result==Outcome::Applied);
   CHECK(stock(15,Action::TransferHabitatStock,120,2).result==Outcome::Applied);
   CHECK(stock(16,Action::TransferHabitatStock,100,3).result==Outcome::Applied);
   CHECK(send(w,ship,17,Action::AssignHabitatResidents,site,3).result==Outcome::Applied);
   CHECK(send(w,ship,18,Action::AssignFarmWorkers,site,4).result==Outcome::Applied);
   CHECK(send(w,ship,19,Action::AssignUtilityTechnicians,site,4).result==Outcome::NoResources);
   CHECK(send(w,ship,20,Action::AssignUtilityTechnicians,site,2).result==Outcome::Applied);
   CHECK(send(w,ship,21,Action::TransferUtilitySpares,site,3,8).result==Outcome::Unauthorized);
   CHECK(send(w,ship,22,Action::TransferUtilitySpares,site,3).result==Outcome::Applied);
   CHECK(w.world().utility(site)->spare_modules==3);
   CHECK(send(w,ship,23,Action::SetUtilityMaintenance,site,1).result==Outcome::Applied);
   CHECK(w.world().total_material_kg()==mass);
   CHECK(w.world().colony_ship(ship)->crew==17); // technicians are resident job roles
   w.steps(151);
   auto u=w.world().utility(site);
   CHECK(u->delegated_repairs>=1 && u->repairs==u->delegated_repairs);
   CHECK(u->spare_modules==3-static_cast<std::int64_t>(u->delegated_repairs));
   CHECK(w.world().total_material_kg()==mass);
   CHECK(w.world().assess_colony(ship).delegated_maintenance_ready);
   CHECK(!w.world().assess_colony(ship).autonomous_growth_ready);
   CHECK(w.world().canonical_state().rfind("galaxy-kernel-v14|",0)==0);
   const auto snapshot=w.world().canonical_state();
   CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
   CHECK(send(w,ship,24,Action::AssignHabitatResidents,site,-2).result==Outcome::NoResources);
   CHECK(send(w,ship,25,Action::AssignUtilityTechnicians,site,0).result==Outcome::Applied);
   CHECK(send(w,ship,26,Action::AssignHabitatResidents,site,-2).result==Outcome::Applied);
   CHECK(send(w,ship,27,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
   CHECK(w.world().total_material_kg()==mass);
   // Authorize departure once. The occupied site must continue to repair
   // from already-delivered parts; the player ship cannot teleport new ones.
   Command travel{};travel.id=28;travel.sequence=28;travel.issuer=7;
   travel.actor=ship;travel.action=Action::Thrust;travel.thrust_x=1;
   travel.execute_tick=w.world().tick()+1;
   CHECK(w.submit(travel));w.step();
   w.steps(500); // remote, delegated, and finite: no repair without physical modules
   CHECK(send(w,ship,29,Action::RepairUtility,site).result==Outcome::OutOfRange);
   CHECK(send(w,ship,30,Action::TransferUtilitySpares,site,1).result==Outcome::OutOfRange);
   u=w.world().utility(site);
   CHECK(u->spare_modules==0 && u->delegated_repairs==3);
   CHECK(u->condition==0);
   CHECK(u->blocked_maintenance_cycles>0);
   CHECK(!w.world().assess_colony(ship).delegated_maintenance_ready);
   CHECK(w.world().total_material_kg()==mass);
   PersistentWorld::recover_copy(db,copy);
   CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
  }
  for(auto& file:{db,copy}){
   PersistentWorld w(file,Simulation(44));
   CHECK(w.world().utility(site)->delegated_repairs==3);
   CHECK(w.world().utility(site)->spare_modules==0);
   CHECK(w.world().total_material_kg()==mass);
   CHECK(w.world().colony_ship(ship)->crew==19);
  }
  // Independent replay must reject modified delivery ledger quantities.
  sqlite3* sql=nullptr;CHECK(sqlite3_open(copy.c_str(),&sql)==SQLITE_OK);
  char* msg=nullptr;
  auto rc=sqlite3_exec(sql,"UPDATE resource_ledger SET amount=amount+2 WHERE reason='utility_spare_transfer_t'",nullptr,nullptr,&msg);
  if(msg)sqlite3_free(msg);
  CHECK(rc==SQLITE_OK && sqlite3_changes(sql)==1);sqlite3_close(sql);
  bool caught=false;try{PersistentWorld bad(copy,Simulation(44));(void)bad;}catch(...){caught=true;}
  CHECK(caught);
  fs::remove_all(tmp);
  std::cout<<"B7.0h finite locally supplied delegated maintenance, SQLite, ledger: PASS\n";
  return 0;
 }catch(const std::exception&ex){std::cerr<<"B7.0h FAILED: "<<ex.what()<<'\n';fs::remove_all(tmp);return 1;}
}
