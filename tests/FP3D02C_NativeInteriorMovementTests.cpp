#include "galaxy/NativeInteriorMovement.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
using namespace galaxy::fp3d;
namespace fs=std::filesystem;
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static long long count(const std::string& path,const char* query){
 sqlite3* db=nullptr;need(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"open sqlite readback");
 sqlite3_stmt* s=nullptr;need(sqlite3_prepare_v2(db,query,-1,&s,nullptr)==SQLITE_OK,"readback query");
 need(sqlite3_step(s)==SQLITE_ROW,"readback row");auto v=sqlite3_column_int64(s,0);
 sqlite3_finalize(s);sqlite3_close(db);return v;
}
static void tamper(const std::string& path,const char* sql){
 sqlite3* db=nullptr;need(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"open tamper sqlite");
 char* err=nullptr;const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
 if(err)sqlite3_free(err);sqlite3_close(db);need(rc==SQLITE_OK,"tamper sqlite query");
}
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("expected temporary SQLite path");
  const std::string save=argv[1];fs::remove(save);fs::remove(save+"-wal");fs::remove(save+"-shm");
  {
   Simulation genesis(0xC021);
   const auto station=genesis.add_station({0,0});
   const auto ship=genesis.spawn_ship_docked(7,station,24,60);
   need(genesis.register_player_person(7,ship),"register original physical player");
   PersistentWorld world(save,std::move(genesis));
   InteriorMovement motion(save,world);
   const auto start_gen=world.generation(),start_tick=world.world().tick();
   need(motion.current(7).sequence==0,"native zero-seq pose");
   need(!motion.move(8,0,55,100,0),"foreign player denied");
   need(!motion.move(7,0,55,100,100),"diagonal gain denied");
   need(!motion.move(7,0,55,0,0),"zero-movement denied");
   need(world.generation()==start_gen && world.world().tick()==start_tick,
        "denied inputs cannot advance world");
   try{motion.current(8);throw std::runtime_error("foreign player should fail");}
   catch(const std::runtime_error& e){need(std::string(e.what())=="missing authorized physical player","wrong foreign error");}
   const auto seat_base_generation=world.generation();
   const auto seat_base_logs=count(save,"SELECT COUNT(*) FROM operation_log");
   bool seat_fault=false;
   try{(void)motion.set_seated(7,0,201,true,SeatFailPoint::AfterSeatReceipt);}
   catch(const std::exception& e){seat_fault=std::string(e.what()).find("injected cockpit")!=std::string::npos;}
   need(seat_fault,"seat receipt fault injected");
   need(!motion.current(7).seated&&motion.current(7).seat_sequence==0&&
        world.generation()==seat_base_generation&&
        count(save,"SELECT COUNT(*) FROM operation_log")==seat_base_logs,
        "failed seat receipt must rollback original world and cockpit state");
   need(motion.set_seated(7,0,201,true),"atomic cockpit sit");
   auto seated=motion.current(7);
   need(seated.seated&&seated.seat_sequence==1,"seat receipt accepted");
   const auto seated_generation=world.generation();
   need(!motion.move(7,0,205,80,0),"cannot walk while seated");
   need(!motion.set_seated(8,1,206,false),"foreign seat transition rejected");
   need(!motion.set_seated(7,0,207,false),"stale seat sequence rejected");
   need(world.generation()==seated_generation,"denied commands cannot tick cockpit");
   bool stand_fault=false;
   try{(void)motion.set_seated(7,1,202,false,SeatFailPoint::AfterSeatUpdate);}
   catch(const std::exception& e){stand_fault=std::string(e.what()).find("injected cockpit")!=std::string::npos;}
   need(stand_fault,"stand update fault injected");
   need(motion.current(7).seated&&motion.current(7).seat_sequence==1&&
        world.generation()==seated_generation,"stand fault must rollback completely");
   need(motion.set_seated(7,1,202,false),"atomic cockpit stand");
   need(!motion.current(7).seated&&motion.current(7).seat_sequence==2,"stand receipt accepted");
   need(count(save,"SELECT COUNT(*) FROM sg_fp3d03a_seat_event")==2,"two immutable seat receipts");
   const auto baseline=count(save,"SELECT COUNT(*) FROM operation_log");
   bool fault1=false;
   try{(void)motion.move(7,0,100,80,0,MoveFailPoint::AfterStepReceipt);}
   catch(const std::exception& e){fault1=std::string(e.what()).find("injected crash")!=std::string::npos;}
   need(fault1,"receipt fault injected");
   need(motion.current(7).sequence==0 && world.generation()==start_gen &&
        world.world().tick()==start_tick && count(save,"SELECT COUNT(*) FROM operation_log")==baseline,
        "receipt injection MUST rollback operation log + world + pose atomically");
   need(motion.move(7,0,100,80,0),"atomic first movement");
   auto a=motion.current(7);
   need(a.x_mm==80 && a.sequence==1 && a.world_generation==world.generation() &&
        a.world_tick==world.world().tick() &&
        count(save,"SELECT COUNT(*) FROM operation_log")==baseline+1,
        "one atomic SQLite world+pose+STEP commit");
   const auto gen1=world.generation(),tick1=world.world().tick();
   need(!motion.move(7,0,101,80,0),"stale sequence denied");
   need(!motion.move(7,1,100,80,0),"reused movement receipt denied");
   need(world.generation()==gen1 && world.world().tick()==tick1,
        "rejected movement cannot consume world generation");
   bool fault2=false;
   try{(void)motion.move(7,1,106,60,80,MoveFailPoint::AfterPoseUpdate);}
   catch(const std::exception& e){fault2=std::string(e.what()).find("injected crash")!=std::string::npos;}
   need(fault2,"pose-update fault injected");
   need(motion.current(7).sequence==1 && world.generation()==gen1 &&
        count(save,"SELECT COUNT(*) FROM operation_log")==baseline+1,
        "pose injection MUST rollback operation log + world + receipt");
   need(motion.move(7,1,106,60,80),"bounded diagonal movement");
   a=motion.current(7);
   need(a.x_mm==140 && a.z_mm==80 && a.sequence==2,"second movement correct");
   need(motion.move(7,2,107,0,-100),"third atomic movement");
  }
  {
   Simulation fallback(0xC021);PersistentWorld world(save,std::move(fallback));
   InteriorMovement motion(save,world);
   auto a=motion.current(7);
   need(a.sequence==3 && a.x_mm==140 && a.z_mm==-20 &&
        !a.seated && a.seat_sequence==2,
        "atomic movement survives real SQLite recovery");
   need(motion.move(7,3,108,-40,0),"resume atomic walking after restart");
   std::uint64_t seq=4;
   for(std::uint64_t i=0;i<7;++i){need(motion.move(7,seq,109+i,100,0),"advance towards obstacle");++seq;}
   a=motion.current(7);need(a.x_mm==800,"obstacle approach");
   const auto prior=world.generation();
   need(!motion.move(7,seq,117,100,0),"collision must block 340mm capsule");
   need(world.generation()==prior && motion.current(7).sequence==seq,
        "collision does not consume native tick");
   tamper(save,"UPDATE operation_log SET kind=0 WHERE generation=2");
   bool rejected=false;
   try{(void)motion.current(7);}catch(const std::exception& e){rejected=std::string(e.what()).find("STEP journal proof")!=std::string::npos;}
   need(rejected,"forged original operation type refused");
   tamper(save,"UPDATE operation_log SET kind=2 WHERE generation=2");
   need(motion.current(7).sequence==seq,"restored STEP provenance valid");
  }
  tamper(save,"UPDATE sg_fp3d03a_seat SET seated=1 WHERE owner=7");
  {
   Simulation fallback(0xC021);PersistentWorld world(save,std::move(fallback));
   InteriorMovement motion(save,world);bool rejected=false;
   try{(void)motion.current(7);}catch(const std::exception&){rejected=true;}
   need(rejected,"tampered cockpit state rejected after restart");
  }
  tamper(save,"UPDATE sg_fp3d03a_seat SET seated=0 WHERE owner=7");
  tamper(save,"UPDATE sg_fp3d02c_pose SET x_mm=x_mm+1 WHERE owner=7");
  {
   Simulation fallback(0xC021);PersistentWorld world(save,std::move(fallback));
   InteriorMovement motion(save,world);bool refused=false;
   try{(void)motion.current(7);}catch(const std::exception&){refused=true;}
   need(refused,"tampered world pose refused on restart");
  }
  std::cout<<"FP3D02F PASS: ONE SQLite transaction for world STEP+snapshot+pose+receipt,"
           <<" cockpit seat/stand rollback, recovery, spoof/collision rejection\n";
 }catch(const std::exception& e){std::cerr<<"FP3D02F FAIL: "<<e.what()<<'\n';return 1;}
 return 0;
}
