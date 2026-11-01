#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalHostCommands.hpp"
#include "galaxy/RegionalInterestManager.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("ECON03c7 assertion failed: ")+#c+" @"+std::to_string(__LINE__));}while(false)
namespace {
template<class F>bool denied(F f){try{f();return false;}catch(const std::exception&){return true;}}
void sql(sqlite3* db,const char* command){
 char* error=nullptr;
 if(sqlite3_exec(db,command,nullptr,nullptr,&error)!=SQLITE_OK){
  const std::string msg=error?error:"SQL failure";sqlite3_free(error);throw std::runtime_error(msg);
 }
}
int count(sqlite3* db,const char* command){
 sqlite3_stmt* q=nullptr;
 if(sqlite3_prepare_v2(db,command,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("count prepare");
 const auto rc=sqlite3_step(q);const int n=rc==SQLITE_ROW?sqlite3_column_int(q,0):-1;
 sqlite3_finalize(q);if(rc!=SQLITE_ROW)throw std::runtime_error("count step");return n;
}
Scheduler seed(){Scheduler s;cold_colony::State a,b;a.depot.food_kg=99;a.depot.water_kg=70;
 s.add_colony("Alpha",a);s.add_colony("Beta",b);return s;}
}
int main(){
 const auto path=(std::filesystem::temp_directory_path()/("sg_c7_"+
     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db")).string();
 try{
  {
   CheckpointStore store(path,"near-alpha"); CHECK(store.save(seed(),0)==1);
   RegionalModeAuthority authority(store);
   auto base=authority.enroll();CHECK(base.owner==SimulationOwner::Cold && base.phase==TransitionPhase::Stable);
   CHECK(base.generation==1 && base.checkpoint_revision==1);
   CHECK(denied([&]{store.save(seed(),1);})); // no unaudited compatibility write
   CHECK(authority.enroll().generation==1);
   HostCommands host(store,[](const HostCommand& c){return c.actor=="world-host";});
   HostCommand adv;adv.actor="world-host";adv.command_id="advance-10";
   adv.op=HostOp::Advance;adv.at_tick=10;
   CHECK(denied([&]{host.execute(adv,1);})); // missing owner token
   CHECK(host.execute(adv,1,1).revision==2);
   CHECK(authority.state().checkpoint_revision==2);
   CHECK(authority.state().boundary_tick==10);
   CHECK(denied([&]{authority.prepare("bad-tick",SimulationOwner::Detailed,1,2,9);}));
   CHECK(denied([&]{authority.prepare("bad-revision",SimulationOwner::Detailed,1,1,10);}));
   CHECK(authority.prepare("promote",SimulationOwner::Detailed,1,2,10).phase==TransitionPhase::ToDetailed);
   CHECK(authority.state().owner==SimulationOwner::Cold);
   CHECK(denied([&]{host.execute(adv,1);})); // even replay forbidden during handoff
   CHECK(denied([&]{authority.prepare("competing",SimulationOwner::Detailed,1,2,10);}));
   CHECK(denied([&]{authority.commit("promote",1,2,{});})); // MUST stage durable receiver state
   CHECK(denied([&]{authority.commit("wrong",1,2,[](sqlite3*,Scheduler&){});}));
   CHECK(store.load()->revision==2);
  }
  {
   // Restart while promotion is pending: ownership and handoff are durable.
   CheckpointStore store(path,"near-alpha");RegionalModeAuthority authority(store);
   CHECK(authority.enroll().phase==TransitionPhase::ToDetailed);
   sqlite3* outside=nullptr;CHECK(sqlite3_open(path.c_str(),&outside)==SQLITE_OK);
   sql(outside,"CREATE TABLE sg_test_active_shadow (region TEXT PRIMARY KEY, food INTEGER NOT NULL)");
   // Inject failure AFTER adapter stages a snapshot but BEFORE ownership flip.
   // Both steps are inside one transaction and must be rolled back.
   sql(outside,"CREATE TRIGGER sg_test_reject_flip BEFORE UPDATE ON sg_regional_mode_authority "
       "WHEN NEW.owner=1 BEGIN SELECT RAISE(ABORT,'test flip rollback'); END");
   auto stage=[](sqlite3* db,Scheduler& candidate){
     CHECK(candidate.tick()==10);
     CHECK(candidate.colony("Alpha").depot.food_kg==99);
     sql(db,"INSERT INTO sg_test_active_shadow(region,food) VALUES('near-alpha',99)");
   };
   CHECK(denied([&]{authority.commit("promote",1,2,stage);}));
   CHECK(authority.state().phase==TransitionPhase::ToDetailed);
   CHECK(count(outside,"SELECT COUNT(*) FROM sg_test_active_shadow")==0);
   sql(outside,"DROP TRIGGER sg_test_reject_flip");
   auto applied=authority.commit("promote",1,2,stage);
   CHECK(applied.owner==SimulationOwner::Detailed && applied.phase==TransitionPhase::Stable);
   CHECK(applied.generation==2 && applied.checkpoint_revision==2);
   CHECK(count(outside,"SELECT COUNT(*) FROM sg_test_active_shadow")==1);
   CHECK(denied([&]{authority.commit("promote",1,2,stage);})); // no double enactment
   CHECK(denied([&]{store.commit_command("stray","host","stray",2,[](Scheduler&){return 0ULL;});}));
   CHECK(denied([&]{store.save(seed(),2);}));
   auto intent=authority.prepare("demote",SimulationOwner::Cold,2,2,10);
   CHECK(intent.phase==TransitionPhase::ToCold && intent.owner==SimulationOwner::Detailed);
   CHECK(denied([&]{authority.commit("demote",2,2,[](sqlite3*,Scheduler& s){
     s.schedule_population("Beta",37,11);s.advance_to(11);
     throw std::runtime_error("test active drain failure");
   });}));
   CHECK(store.load()->revision==2 && store.load()->scheduler.colony("Beta").population==0);
   CHECK(authority.state().phase==TransitionPhase::ToCold);
   sqlite3_close(outside);
  }
  {
   // Second restart during demotion: retry staging against the SAME epoch.
   CheckpointStore store(path,"near-alpha");RegionalModeAuthority authority(store);
   CHECK(authority.state().phase==TransitionPhase::ToCold);
   auto demoted=authority.commit("demote",2,2,[](sqlite3* db,Scheduler& active){
       // The production host will replace this test adapter with validated,
       // independently receipted material transfers from its real active world.
       active.schedule_population("Beta",37,11);active.advance_to(11);
       sql(db,"CREATE TABLE IF NOT EXISTS sg_test_drain (region TEXT PRIMARY KEY, tick INTEGER NOT NULL)");
       sql(db,"INSERT INTO sg_test_drain(region,tick) VALUES('near-alpha',11)");
   });
   CHECK(demoted.owner==SimulationOwner::Cold && demoted.phase==TransitionPhase::Stable);
   CHECK(demoted.generation==3 && demoted.checkpoint_revision==3 && demoted.boundary_tick==11);
   CHECK(store.load()->revision==3 && store.load()->scheduler.tick()==11);
   CHECK(store.load()->scheduler.colony("Beta").population==37);
   CHECK(denied([&]{authority.commit("demote",2,2,[](sqlite3*,Scheduler&){});}));
   HostCommands host(store,[](const HostCommand& c){return c.actor=="world-host";});
   HostCommand adv;adv.actor="world-host";adv.command_id="advance-12";adv.op=HostOp::Advance;adv.at_tick=12;
   CHECK(denied([&]{host.execute(adv,3,1);})); // pre-promotion worker stale
   CHECK(denied([&]{host.execute(adv,3);}));   // no token
   CHECK(host.execute(adv,3,3).revision==4);
   CHECK(authority.state().checkpoint_revision==4 && authority.state().boundary_tick==12);
   auto pending=authority.prepare("abort-me",SimulationOwner::Detailed,3,4,12);
   CHECK(pending.phase==TransitionPhase::ToDetailed);
   auto aborted=authority.abort("abort-me",3);
   CHECK(aborted.owner==SimulationOwner::Cold && aborted.generation==4);
   CHECK(denied([&]{authority.commit("abort-me",3,4,[](sqlite3*,Scheduler&){});}));
   CHECK(denied([&]{authority.abort("abort-me",3);}));
   CHECK(authority.state().phase==TransitionPhase::Stable);
   HostCommand after_abort=adv;after_abort.command_id="advance-13";after_abort.at_tick=13;
   CHECK(denied([&]{host.execute(after_abort,4,3);})); // abort also revokes old owner token
   CHECK(host.execute(after_abort,4,4).revision==5);
   CheckpointStore another(path,"far-beta");CHECK(another.save(seed(),0)==1);
   RegionalModeAuthority other(another);CHECK(other.enroll().generation==1);
   CHECK(other.state().owner==SimulationOwner::Cold);
   CHECK(authority.state().generation==4); // no cross-region spillover
  }
  std::filesystem::remove(path);std::filesystem::remove(path+"-wal");std::filesystem::remove(path+"-shm");
  std::cout<<"PASS ECON03c7 durable ownership, cold fence, staged SQLite rollback, restart, demotion, abort, epoch CAS, scope isolation\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
