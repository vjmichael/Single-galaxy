// DOCK-03A: real NPC shore leave and workforce: original world SQLite only.
// Staged for consolidated native acceptance; this file does not claim a pass.
#include "galaxy/Persistence.hpp"
#include "galaxy/DockLogistics.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("DOCK03 ")+std::to_string(__LINE__)+": " #x);}while(0)
static void clear(const std::string& db){
 for(const char* ext:{"","-wal","-shm"}){std::error_code ec;fs::remove(db+ext,ec);}
}
static int movements(const std::string& db){
 sqlite3* raw=nullptr;
 CHECK(sqlite3_open_v2(db.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
 sqlite3_stmt* q=nullptr;
 CHECK(sqlite3_prepare_v2(raw,"SELECT COUNT(*) FROM sg_dock_worker_moves",-1,&q,nullptr)==SQLITE_OK);
 CHECK(sqlite3_step(q)==SQLITE_ROW);
 const int n=sqlite3_column_int(q,0);
 sqlite3_finalize(q);sqlite3_close(raw);
 return n;
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::string db=argv[1];
 clear(db);
 try{
  EntityId station=0,ship=0,foreign=0;
  std::uint64_t worker=0;
  {
   Simulation seed(0xD03);
   station=seed.add_station({0,0});
   ship=seed.add_colony_ship(7,{0,0},12,8);
   foreign=seed.add_colony_ship(8,{1200,0},8,3);
   const auto rock=seed.add_geological_asteroid(
       {2500,0},AsteroidClass::Metallic,10,5000);
   seed.enable_individual_workforce();
   seed.enable_station_ore_provenance();
   // POP-07 exact-head repair: station ore/workforce initialization has
   // already pinned the original galactic system registry. Replaying its
   // actual immutable origin is valid; introducing another one is forbidden.
   CHECK(seed.native_galactic_physics());
   CHECK(seed.galactic_system_origins().count(1)==1);
   seed.enable_native_galactic_physics(seed.galactic_system_origins());
   PersistentWorld world(db,std::move(seed));
   for(std::uint64_t cmd_id=57098;cmd_id<=57100;++cmd_id){
     Command mine{};mine.id=cmd_id;mine.sequence=mine.id;
     mine.issuer=7;mine.actor=ship;mine.target=rock;
     mine.execute_tick=world.world().tick()+1;
     mine.action=Action::Mine;mine.amount=1;
     CHECK(world.submit(mine));
     world.step();
     CHECK(world.world().events().back().result==Outcome::Applied);
   }
   CHECK(world.world().find(ship)->cargo==3);
   Command docking{};docking.id=57101;docking.sequence=docking.id;
   docking.issuer=7;docking.actor=ship;docking.target=station;
   docking.execute_tick=world.world().tick()+1;
   docking.action=Action::RequestDock;
   CHECK(world.submit(docking));
   world.step();
   CHECK(world.world().events().back().result==Outcome::Applied);
   world.steps(Simulation::kDockTransitionTicks);
   CHECK(world.world().find(ship)->dock_state==DockState::Docked);
   for(const auto& [id,npc]:world.world().npc_records()){
    if(npc.home_ship==ship && npc.location==ship && !npc.transport_id){
     worker=id;break;
    }
   }
   CHECK(worker!=0);
   CHECK(!world.station_dock_worker_transfer(8,ship,station,worker,true,57102));
   CHECK(!world.station_dock_worker_transfer(7,foreign,station,worker,true,57103));
   CHECK(!world.station_dock_worker_transfer(7,ship,station,99999,true,57104));
   const auto generation=world.generation(),tick=world.world().tick();
   try{
     (void)world.station_dock_worker_transfer(
       7,ship,station,worker,true,57105,FailPoint::AfterSnapshotWrite);
     CHECK(false);
   }catch(const std::exception&){}
   CHECK(world.generation()==generation && world.world().tick()==tick);
   CHECK(world.world().npc_records().at(worker).location==ship);
   CHECK(world.station_dock_worker_transfer(7,ship,station,worker,true,57105));
   CHECK(world.world().npc_records().at(worker).location==station);
   CHECK(world.world().colony_ship(ship)->crew==8); // same contracted finite crew
   CHECK(world.world().npc_records().size()==11); // eight owned, three foreign
   dock::Ledger ledger(db);
   CHECK(ledger.assign(world.world(),station,7,worker));
   CHECK(ledger.active_staff(world.world(),station,7)==1);
   CHECK(!ledger.assign(world.world(),station,8,worker));
   CHECK(ledger.documented_cargo_units(worker)==0);
   CHECK(ledger.enqueue_freight(world.world(),station,7,ship,3));
   CHECK(ledger.snapshot(world.world(),station,7).freight_tonnes==3);
   for(int n=0;n<3;++n){
     const auto available=ledger.service_roster(world.world(),station,7);
     CHECK(!available.empty() && available.front()==worker);
     CHECK(ledger.serve_one(world,station,7,available.front()));
     CHECK(ledger.documented_cargo_units(worker)==static_cast<std::uint64_t>(n+1));
   }
   CHECK(world.world().find(ship)->cargo==0);
   CHECK(world.world().hangar(station,7)->ore==3);
   CHECK(ledger.snapshot(world.world(),station,7).freight_tonnes==0);
   CHECK(ledger.documented_cargo_units(worker)==3);
   // POP-05: the named dock worker may not be reauthorized from a caller's
   // stale pre-step Simulation. The original SQLite snapshot is canonical.
   const auto stale_world=world.world();
   world.step();
   CHECK(!ledger.renew(stale_world,station,7,worker));
   CHECK(ledger.renew(world.world(),station,7,worker));
   CHECK(ledger.active_staff(world.world(),station,7)==1);
   CHECK(ledger.snapshot(world.world(),station,7).qualified_staff==1);
   CHECK(ledger.snapshot(world.world(),station,7).service_capacity==2);
  }
  CHECK(movements(db)==1);
  {
   PersistentWorld world(db,Simulation(0xD031));
   CHECK(world.world().npc_records().at(worker).location==station);
   dock::Ledger ledger(db);
   CHECK(ledger.active_staff(world.world(),station,7)==1);
   CHECK(ledger.documented_cargo_units(worker)==3);
   CHECK(world.world().hangar(station,7)->ore==3);
   // An existing shore worker blocks a normal host-queued ship undock.
   Command undock{};undock.id=57110;undock.sequence=undock.id;
   undock.issuer=7;undock.actor=ship;undock.action=Action::RequestUndock;
   undock.execute_tick=world.world().tick()+1;
   CHECK(!world.submit(undock));
   CHECK(world.station_dock_worker_transfer(7,ship,station,worker,false,57106));
   CHECK(ledger.release(station,7,worker));
   CHECK(ledger.active_staff(world.world(),station,7)==0);
   CHECK(world.world().npc_records().at(worker).location==ship);
   CHECK(world.world().npc_records().at(worker).home_ship==ship);
  }
  CHECK(movements(db)==2);
  {
   PersistentWorld world(db,Simulation(0xD032));
   CHECK(world.world().npc_records().at(worker).location==ship);
   CHECK(world.world().colony_ship(ship)->crew==8);
   dock::Ledger ledger(db);
   CHECK(ledger.active_staff(world.world(),station,7)==0);
  }
  clear(db);
  std::cout<<"DOCK03 physical custody / no-duplicate / rollback / restart assertions passed\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"DOCK03 native source test FAIL: "<<e.what()<<'\n';
  return 1;
 }
}
