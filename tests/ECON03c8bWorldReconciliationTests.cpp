#include "galaxy/RegionalPersistentWorldBridge.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
using namespace galaxy;
using namespace galaxy::regional_events;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)) throw std::runtime_error(std::string("ECON03c8b @")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp{
 fs::path root;
 Temp():root(fs::temp_directory_path()/("sg_c8b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(root);}
 ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
 std::string path(const char* s="colony.sqlite")const{return (root/s).string();}
};
void sql(const std::string& path,const char* cmd){
 sqlite3* db=nullptr;CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
 char* err=nullptr;const int rc=sqlite3_exec(db,cmd,nullptr,nullptr,&err);
 const std::string msg(err?err:"");sqlite3_free(err);sqlite3_close(db);
 if(rc!=SQLITE_OK)throw std::runtime_error(msg);
}
std::int64_t count(const std::string& path,const char* query){
 sqlite3* db=nullptr;CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
 sqlite3_stmt* q=nullptr;CHECK(sqlite3_prepare_v2(db,query,-1,&q,nullptr)==SQLITE_OK);
 CHECK(sqlite3_step(q)==SQLITE_ROW);auto n=sqlite3_column_int64(q,0);
 sqlite3_finalize(q);sqlite3_close(db);return n;
}
template<class F>void throws(F&& f){
 try{f();}catch(const std::exception&){return;}
 throw std::runtime_error("expected operation to be denied");
}
void queue(PersistentWorld& w,std::uint64_t id,EntityId ship,EntityId target,Action action,std::int64_t amount){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.issuer=7;c.actor=ship;c.target=target;c.action=action;c.amount=amount;
 CHECK(w.submit(c));w.step();
}
void native_effect_roundtrip(){
 Temp t;
 Simulation s(321);const auto ship=s.add_colony_ship(7,{0,0},90,5);
 const auto rock=s.add_asteroid({1000,0},80);s.enable_individual_workforce();
 std::string after_effects;std::uint64_t gen=0;
 {
  PersistentWorld w(t.path(),std::move(s));
  PersistentWorldBridge b(t.path(),"main",w);
  CHECK(b.enroll().owner==SimulationOwner::Cold);
  CHECK(b.promote("opening").owner==SimulationOwner::Detailed);
  CHECK(w.submit_npc_mining_work(1,ship,rock,10,1001));w.step();
  CHECK(w.verified_npc_mining_execution(1001,1));
  queue(w,1002,ship,0,Action::RefineOre,10);
  CHECK(w.submit_npc_fabrication_work(2,ship,Action::FabricateModule,0,2,1003));w.step();
  CHECK(w.verified_npc_fabrication_execution(1003,2));
  CHECK(w.world().colony_ship(ship)->modules==2);
  CHECK(w.world().total_ore()==w.resource_origin());
  after_effects=w.world().canonical_state();gen=w.generation();
  // Legacy bridge guard must continue to refuse changed worlds.
  throws([&]{b.demote_if_unchanged("old-method-rejects");});
  CHECK(b.state().owner==SimulationOwner::Detailed);
  CHECK(b.demote_preserving_effects("seal-mining-and-fabrication").owner==SimulationOwner::Cold);
  CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_cold_manifest")==1);
  CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==3);
  CHECK(count(t.path(),"SELECT physical_movements FROM sg_world_cold_manifest")==3);
  CHECK(b.checkpoint().load()->scheduler.colony_count()==0);
  CHECK(b.checkpoint().load()->scheduler.custody().food_kg==0);
  throws([&]{w.step();});
  CHECK(w.world().canonical_state()==after_effects);
 }
 {
  PersistentWorld w(t.path(),Simulation(555));
  CHECK(w.generation()==gen && w.world().canonical_state()==after_effects);
  PersistentWorldBridge b(t.path(),"main",w);
  CHECK(b.enroll().owner==SimulationOwner::Cold);
  CHECK(b.promote("reawaken").owner==SimulationOwner::Detailed);
  CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_cold_manifest")==0);
  CHECK(w.world().canonical_state()==after_effects);
  // Same material cannot be reconciled twice in another handoff.
  CHECK(b.demote_preserving_effects("empty-second-epoch").owner==SimulationOwner::Cold);
  CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==3);
  CHECK(b.promote("reawaken-again").owner==SimulationOwner::Detailed);
 }
}
void denied_unknown_action_and_recovery(){
 Temp t;Simulation s(17);const auto ship=s.add_colony_ship(7,{0,0},30,2);
 PersistentWorld w(t.path(),std::move(s));PersistentWorldBridge b(t.path(),"main",w);
 b.enroll();b.promote("opening");
 Command c{};c.id=7;c.sequence=7;c.execute_tick=1;c.issuer=7;c.actor=ship;
 c.action=Action::Thrust;c.thrust_x=1;
 CHECK(w.submit(c));w.step();
 CHECK(w.world().events().back().result==Outcome::Applied);
 throws([&]{b.demote_preserving_effects("unsupported-movement");});
 CHECK(b.state().owner==SimulationOwner::Detailed);
 CHECK(b.state().phase==TransitionPhase::Stable);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==0);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_cold_manifest")==0);
}
void injected_sql_failure_is_fully_atomic(){
 Temp t;Simulation s(35);const auto ship=s.add_colony_ship(7,{0,0},50,3);
 const auto rock=s.add_asteroid({1000,0},20);
 PersistentWorld w(t.path(),std::move(s));PersistentWorldBridge b(t.path(),"main",w);
 b.enroll();b.promote("opening");
 queue(w,11,ship,rock,Action::Mine,8);
 const auto snap=w.world().canonical_state();
 sql(t.path(),"CREATE TRIGGER fail_cold_record BEFORE INSERT ON sg_world_cold_manifest "
              "BEGIN SELECT RAISE(ABORT,'simulated IO failure'); END");
 throws([&]{b.demote_preserving_effects("fault-injection");});
 CHECK(b.state().owner==SimulationOwner::Detailed);
 CHECK(b.state().phase==TransitionPhase::Stable);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_cold_manifest")==0);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==0);
 CHECK(w.world().canonical_state()==snap);
 sql(t.path(),"DROP TRIGGER fail_cold_record");
 CHECK(b.demote_preserving_effects("after-retry").owner==SimulationOwner::Cold);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==1);
}
void missing_physical_receipt_is_denied(){
 Temp t;Simulation s(41);const auto ship=s.add_colony_ship(7,{0,0},50,3);
 const auto rock=s.add_asteroid({1000,0},20);
 PersistentWorld w(t.path(),std::move(s));PersistentWorldBridge b(t.path(),"main",w);
 b.enroll();b.promote("opening");queue(w,11,ship,rock,Action::Mine,5);
 // Test-only corruption of an otherwise immutable DB source. No allocation
 // is accepted merely because the in-memory source still claims ore moved.
 sql(t.path(),"DROP TRIGGER physical_no_delete; DELETE FROM physical_incidents "
              "WHERE incident_id=(SELECT incident_id FROM physical_incidents LIMIT 1)");
 throws([&]{b.demote_preserving_effects("missing-incident");});
 CHECK(b.state().owner==SimulationOwner::Detailed);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_reconciled_movements")==0);
 CHECK(count(t.path(),"SELECT COUNT(*) FROM sg_world_cold_manifest")==0);
}
int main(){
 try{native_effect_roundtrip();denied_unknown_action_and_recovery();injected_sql_failure_is_fully_atomic();missing_physical_receipt_is_denied();
  std::cout<<"ECON03c8b physical journal-seal and zero-duplication tests passed\n";return 0;}
 catch(const std::exception& e){std::cerr<<"ECON03c8b failure: "<<e.what()<<'\n';return 1;}
}
