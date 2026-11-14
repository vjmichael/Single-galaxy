// DOCK-02: focused source-level native persistence and conservative admission.
// No mocked population, imaginary cargo or second inventory is provisioned.
#include "galaxy/DockLogistics.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <iostream>
namespace fs=std::filesystem;
using namespace galaxy;
static void require(bool ok,const char* what) {
 if(!ok)throw std::runtime_error(what);
}
int main(){
 const fs::path path=fs::temp_directory_path()/"sg_dock_logistics_v1_source_test.sqlite";
 fs::remove(path);
 sqlite3* db=nullptr;
 require(sqlite3_open(path.string().c_str(),&db)==SQLITE_OK,"SQLite fixture file");
 sqlite3_close(db);
 try{
  Simulation simulation(0xD0C2);
  const auto station=simulation.add_station({0,0});
  const auto ship=simulation.add_colony_ship(7,{0,0},12,8);
  simulation.enable_individual_workforce();
  {
   dock::Ledger ledger(path.string());
   const auto initial=ledger.snapshot(simulation,station,7);
   require(initial.station==station && initial.staff==0 && initial.passengers==0
          && initial.freight_tonnes==0,"empty dock cannot invent people or goods");
   require(!ledger.assign(simulation,station,7,0),"zero worker must be rejected");
   require(!ledger.assign(simulation,station,7,999999),"nonexistent worker must be rejected");
   require(!ledger.enqueue_passenger(simulation,station,7,999999),
           "nonexistent passenger cannot enter service queue");
   require(!ledger.enqueue_freight(simulation,station,7,ship,1),
           "undocked ship cannot enter freight queue");
   require(!ledger.enqueue_freight(simulation,station,7,ship,65),
           "freight exceeds bounded per-job capacity");
   require(!ledger.release(station,7,999999),"cannot release invented worker");
  }
  {
   dock::Ledger reopened(path.string());
   const auto restored=reopened.snapshot(simulation,station,7);
   require(restored.staff==0 && restored.passengers==0 &&
           restored.freight_jobs==0,"restart must not invent dock work");
  }
  fs::remove(path);
  std::cout<<"DOCK-02 baseline: original NPC/cargo custody and empty restart verified\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"DOCK-02 baseline FAILED: "<<e.what()<<"\n";
  fs::remove(path);
  return 1;
 }
}
