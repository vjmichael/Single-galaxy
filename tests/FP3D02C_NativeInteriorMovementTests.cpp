#include "galaxy/NativeInteriorMovement.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
using namespace galaxy::fp3d;
namespace fs=std::filesystem;
static void need(bool x,const char* reason){if(!x)throw std::runtime_error(reason);}
static void tamper(const std::string& path,const char* sql){
 sqlite3* db=nullptr;
 need(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"open tamper SQLite");
 char* err=nullptr;
 const auto rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
 if(err)sqlite3_free(err);
 sqlite3_close(db);
 need(rc==SQLITE_OK,"tamper operation failed");
}
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("expected temporary SQLite path");
  const std::string save=argv[1];
  fs::remove(save);
  EntityId home=0,ship=0;
  {
   Simulation seed(0xC021);
   home=seed.add_station({0,0});
   ship=seed.spawn_ship_docked(7,home,24,60);
   need(seed.register_player_person(7,ship),"seed physical registered player");
   PersistentWorld native(save,std::move(seed));
   InteriorMovement motion(save,native);
   need(motion.current(7).sequence==0,"origin receipt");
   try{motion.current(8);throw std::runtime_error("foreign player should be rejected");}catch(const std::runtime_error& e){
     need(std::string(e.what())=="missing authorized physical player","unexpected foreign player error");
   }
   need(!motion.move(7,0,100,80,0),"movement cannot run at same native tick");
   native.step();
   need(motion.move(7,0,100,80,0),"real world-tick movement");
   need(!motion.move(7,1,101,80,0),"cannot move twice in one tick");
   need(!motion.move(7,0,102,80,0),"stale sequence rejected");
   need(!motion.move(8,0,103,80,0),"unregistered player rejected");
   auto a=motion.current(7);
   need(a.x_mm==80 && a.z_mm==0 && a.sequence==1,"first movement state");
   native.step();
   need(!motion.move(7,1,104,100,100),"diagonal speed gain rejected");
   need(!motion.move(7,1,105,0,0),"null step rejected");
   need(motion.move(7,1,106,60,80),"bounded diagonal succeeds");
   a=motion.current(7);
   need(a.x_mm==140 && a.z_mm==80 && a.sequence==2,"diagonal movement state");
   native.step();
   need(!motion.move(7,2,100,80,0),"reused receipt rejected");
   need(motion.move(7,2,107,0,-100),"third native step");
  }
  {
   Simulation fallback(0xC021);
   PersistentWorld recovered(save,std::move(fallback));
   need(recovered.world().player_people().count(7)==1,"player identity from snapshot");
   InteriorMovement motion(save,recovered);
   const auto a=motion.current(7);
   need(a.sequence==3 && a.x_mm==140 && a.z_mm==-20 && a.y_mm==0,
        "native pose survives restart");
   recovered.step();
   need(motion.move(7,3,108,-40,0),"continued receipt after restart");
   // Approach a solid crate. With a 340mm capsule, crate begins at x=860mm.
   // A 100mm step that would enter its inflated AABB MUST be denied.
   std::uint64_t seq=4;
   for(std::uint64_t i=0;i<7;++i){
     recovered.step();
     need(motion.move(7,seq,109+i,100,0),"interior free space advance");
     ++seq;
   }
   const auto before=motion.current(7);
   need(before.x_mm==800 && before.z_mm==-20,"approached crate collision boundary");
   recovered.step();
   need(!motion.move(7,seq,117,100,0),"capsule collision prevents solid crate entry");
   need(motion.current(7).sequence==seq,"collision did not consume receipt");
  }
  // Deliberate corruption of tip; a casual stale state write is not accepted.
  tamper(save,"UPDATE sg_fp3d02c_pose SET x_mm=x_mm+1 WHERE owner=7");
  {
   Simulation fallback(0xC021);
   PersistentWorld recovered(save,std::move(fallback));
   InteriorMovement motion(save,recovered);
   bool rejected=false;
   try{(void)motion.current(7);}catch(const std::runtime_error&){rejected=true;}
   need(rejected,"forged pose must not bypass receipt replay");
  }
  std::cout<<"FP3D02C PASS: native identity, one-step/tick, bounds, stale/duplicate, restart and corruption refusal\n";
 }catch(const std::exception& e){std::cerr<<"FP3D02C FAIL: "<<e.what()<<'\n';return 1;}
 return 0;
}
