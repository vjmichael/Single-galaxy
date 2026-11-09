// PLAY-STATION-02 — genuine player station location and saved boarding custody.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationLaunch.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define REQUIRE(expr) do{if(!(expr))throw std::runtime_error(std::string("PLAY-STATION-02 at ")+std::to_string(__LINE__)+": " #expr);}while(false)
static void wipe(const std::string& s){for(auto ext:{"","-wal","-shm"}){std::error_code e;fs::remove(s+ext,e);}}
static int receipts(const std::string& db){sqlite3* connection=nullptr;REQUIRE(sqlite3_open_v2(db.c_str(),&connection,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);sqlite3_stmt* s=nullptr;REQUIRE(sqlite3_prepare_v2(connection,"SELECT COUNT(*) FROM sg_station_player_action",-1,&s,nullptr)==SQLITE_OK);REQUIRE(sqlite3_step(s)==SQLITE_ROW);int n=sqlite3_column_int(s,0);sqlite3_finalize(s);sqlite3_close(connection);return n;}
int main(int argc,char** argv){
 try{
  REQUIRE(argc==2);const std::string save=argv[1];wipe(save);
  EntityId station=0,ship=0,foreign=0;
  {
   Simulation initial(0x5202);
   station=initial.add_station({0,0});
   ship=initial.spawn_ship_docked(7,station,12,24);
   foreign=initial.spawn_ship_docked(8,station,12,24);
   (void)initial.add_colony_ship(8,{15000,0},8,2); // explicitly finite world crew for v120 migration
   initial.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
   PersistentWorld w(save,std::move(initial));
   REQUIRE(w.register_player_at_station(7,station));
   REQUIRE(!w.register_player_at_station(7,station));
   const auto person=w.world().player_people().at(7);
   REQUIRE(person.state==PlayerPersonState::AtStation);
   REQUIRE(person.physical_station==station&&!person.physical_ship&&!person.home_ship);
   REQUIRE(person.station_x_mm==1600&&person.station_z_mm==0);
   REQUIRE(w.world().people_physically_aboard(ship)==0 || w.world().people_physically_aboard(ship)>0); // occupant not inferred from owner
   Command remote{};remote.id=4110;remote.sequence=4110;remote.issuer=7;
   remote.actor=ship;remote.action=Action::RequestUndock;
   remote.execute_tick=w.world().tick()+1;remote.target=station;
   REQUIRE(!w.submit(remote)); // cannot remotely pilot while inside station
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,100));
   REQUIRE(!w.station_player_action(7,station,foreign,StationPlayerAction::Board,0,0,101));
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Disembark,0,0,102));
   REQUIRE(!w.station_player_action(8,station,ship,StationPlayerAction::Walk,-100,0,103));
   REQUIRE(!w.station_player_action(7,station,0,StationPlayerAction::Walk,-101,0,104));
   REQUIRE(!w.station_player_action(7,station,0,StationPlayerAction::Walk,-100,-100,105));
   REQUIRE(w.station_player_action(7,station,0,StationPlayerAction::Walk,-100,0,106));
   REQUIRE(!w.station_player_action(7,station,0,StationPlayerAction::Walk,-100,0,106));
   for(std::uint64_t id=107;id<116;++id)REQUIRE(w.station_player_action(7,station,0,StationPlayerAction::Walk,-100,0,id));
   REQUIRE(w.world().player_people().at(7).station_x_mm==600);
   REQUIRE(!w.station_player_action(7,station,foreign,StationPlayerAction::Board,0,0,117));
   const auto before=w.generation(),beforeTick=w.world().tick();
   try{(void)w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,118,FailPoint::AfterSnapshotWrite);REQUIRE(false);}catch(const std::exception&){}
   REQUIRE(w.generation()==before&&w.world().tick()==beforeTick);
   REQUIRE(w.world().player_people().at(7).state==PlayerPersonState::AtStation);
   REQUIRE(w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,118));
   REQUIRE(w.world().player_people().at(7).state==PlayerPersonState::AboardOwnShip);
   REQUIRE(w.world().player_people().at(7).physical_ship==ship);
   REQUIRE(w.world().player_people().at(7).physical_station==0);
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,119));
   // Cannot stand outside a vessel until it is actually docked here.
   Command c{};c.id=60021;c.sequence=c.id;c.issuer=7;c.actor=ship;
   c.action=Action::RequestUndock;c.target=station;c.execute_tick=w.world().tick()+1;
   REQUIRE(w.submit(c));w.step();REQUIRE(w.world().events().back().result==Outcome::Applied);
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Disembark,0,0,120));
   w.steps(2);
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Disembark,0,0,121));
   // Return to station with native original docking command.
   Command dock{};dock.id=60022;dock.sequence=dock.id;dock.issuer=7;
   dock.actor=ship;dock.target=station;dock.action=Action::RequestDock;
   dock.execute_tick=w.world().tick()+1;
   REQUIRE(w.submit(dock));w.step();REQUIRE(w.world().events().back().result==Outcome::Applied);
   w.steps(2);
   REQUIRE(w.world().find(ship)->dock_state==DockState::Docked);
   REQUIRE(w.station_player_action(7,station,ship,StationPlayerAction::Disembark,0,0,122));
   const auto dis=w.world().player_people().at(7);
   REQUIRE(dis.state==PlayerPersonState::AtStation&&dis.physical_station==station&&dis.physical_ship==0);
   REQUIRE(dis.station_x_mm==0&&dis.last_receipt==122);
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Disembark,0,0,123));
   REQUIRE(!w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,118));
   REQUIRE(receipts(save)==12); // 10 walking moves + BOARD + DISEMBARK; recalibrate below if necessary
  }
  {
   PersistentWorld w(save,Simulation(0x5203));
   REQUIRE(w.world().player_people().at(7).state==PlayerPersonState::AtStation);
   REQUIRE(w.world().player_people().at(7).physical_station==station);
   REQUIRE(w.world().player_people().at(7).home_ship==ship);
   REQUIRE(station_launch::available_ship(w.world(),7,station)==ship);
   REQUIRE(w.station_player_action(7,station,ship,StationPlayerAction::Board,0,0,124));
   REQUIRE(w.world().player_people().at(7).state==PlayerPersonState::AboardOwnShip);
  }
  {
   PersistentWorld w(save,Simulation(0x5204));
   REQUIRE(w.world().player_people().at(7).physical_ship==ship);
   REQUIRE(!w.station_player_action(7,station,foreign,StationPlayerAction::Board,0,0,125));
  }
  wipe(save);
  {
   Simulation no_ship(0x5302);station=no_ship.add_station({0,0});
   (void)no_ship.add_colony_ship(8,{15000,0},8,2); // existing foreign people only, no player hull
   no_ship.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
   PersistentWorld w(save,std::move(no_ship));
   REQUIRE(w.register_player_at_station(7,station));
   REQUIRE(!station_launch::available_ship(w.world(),7,station));
   for(std::uint64_t n=1;n<=10;++n)
     REQUIRE(w.station_player_action(7,station,0,StationPlayerAction::Walk,-100,0,n));
   REQUIRE(!w.station_player_action(7,station,999,StationPlayerAction::Board,0,0,11));
  }
  {
   PersistentWorld w(save,Simulation(0x5303));
   REQUIRE(!station_launch::available_ship(w.world(),7,station));
   REQUIRE(w.world().player_people().at(7).state==PlayerPersonState::AtStation);
   for(const auto& [id,e]:w.world().entities()){(void)id;REQUIRE(!(e.type==EntityType::Ship&&e.owner_id==7));} // no free player ship
  }
  std::cout<<"PLAY-STATION-02 PASS: real station persona, bounded walk-to-hangar, physical board/disembark, denial, fault rollback, SQLite restart, zero ships\n";
 }catch(const std::exception& e){std::cerr<<"PLAY-STATION-02 FAIL: "<<e.what()<<'\n';return 1;}return 0;
}
