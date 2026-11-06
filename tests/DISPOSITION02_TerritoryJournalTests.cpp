#include "galaxy/ColonyTerritoryResponse.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::colony_disposition;
#define CHECK(v) do {if(!(v))throw std::runtime_error(std::string(#v)+" line "+std::to_string(__LINE__));}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto path=(fs::temp_directory_path()/("sg_territory02_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 auto cleanup=[&]{for(const auto* ext:{"","-wal","-shm"})fs::remove(path+ext);};
 try{
  Simulation seed(260220);
  const auto colony=seed.add_colony_ship(7,{0,0},100,6);
  const auto visitor=seed.add_ship(8,{1000,0},100);
  seed.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
  seed.enable_individual_workforce();
  seed.upgrade_assignment_snapshot_format();
  seed.enable_passenger_life_support();
  const auto matter=seed.total_material_kg();
  std::string snapshot;
  std::uint64_t observer=0,leader=0;
  {
   PersistentWorld w(path,std::move(seed));
   for(const auto& [id,n]:w.world().npc_records()){
     if(n.home_ship==colony && n.location==colony){
       if(!leader)leader=id;
       else if(!observer)observer=id;
     }
   }
   CHECK(leader && observer);
   w.establish_colony_conflict_agenda(
      70001,colony,leader,colony_conflict::Agenda::Isolationist);
   w.record_colony_watch_contact(70002,colony,observer,visitor);
   TerritoryJournal j(path);
   CHECK(!j.claim(w,8,colony,71000,2000));
   CHECK(!j.claim(w,7,colony,71000,7000));
   CHECK(j.claim(w,7,colony,71000,3000));
   CHECK(!j.claim(w,7,colony,71000,3000));
   CHECK(j.history().size()==1);
   CHECK(!j.invite(w,8,colony,8,71001));
   CHECK(j.invite(w,7,colony,8,71001));
   CHECK(!j.warn(w,7,colony,visitor,observer,71002,5));
   CHECK(j.standing(w,colony,visitor).standing==
      npc_response::VisitorStanding::Invited);
   CHECK(!j.record_trespass(w,7,colony,visitor,observer,71003));
   CHECK(j.revoke_invitation(w,7,colony,8,71004));
   CHECK(!j.revoke_invitation(w,7,colony,8,71005));
   CHECK(!j.warn(w,7,colony,visitor,observer,71006,1));
   CHECK(j.warn(w,7,colony,visitor,observer,71006,5));
   CHECK(!j.warn(w,7,colony,visitor,observer,71007,5));
   CHECK(j.standing(w,colony,visitor).standing==
      npc_response::VisitorStanding::Unknown);
   CHECK(!j.record_trespass(w,7,colony,visitor,observer,71008));
   const auto interpretation=assess_territorial(w,j,colony,visitor,70002);
   CHECK(interpretation.territory.warned);
   CHECK(interpretation.interpreted.intent==npc_response::ThreatIntent::WarnLeave);
   CHECK(!interpretation.interpreted.can_attack);
   CHECK(!interpretation.interpreted.can_arrest);
   CHECK(!interpretation.interpreted.can_seize_goods);
   // The warning itself can dispatch a lawful movement-only patrol
   // through the ORIGINAL qualified NPC flight execution path.
   CHECK(dispatch_territorial_patrol(w,j,colony,visitor,observer,72001)==
      npc_response::Status::Accepted);
   CHECK(w.world().npc_flight_orders().at(72001).purpose==NpcFlightPurpose::Patrol);
   w.steps(5);
   CHECK(j.standing(w,colony,visitor).standing==
      npc_response::VisitorStanding::Uninvited);
   CHECK(j.record_trespass(w,7,colony,visitor,observer,71009));
   CHECK(!j.record_trespass(w,7,colony,visitor,observer,71010));
   CHECK(j.standing(w,colony,visitor).trespass_recorded);
   CHECK(j.expel(w,7,colony,visitor,observer,71011));
   CHECK(!j.expel(w,7,colony,visitor,observer,71012));
   CHECK(j.standing(w,colony,visitor).standing==
      npc_response::VisitorStanding::Expelled);
   CHECK(j.invite(w,7,colony,8,71013));
   CHECK(j.standing(w,colony,visitor).standing==
      npc_response::VisitorStanding::Invited);
   CHECK(j.history().size()==8);
   CHECK(w.world().find(visitor)->hull==100);
   CHECK(w.world().total_material_kg()==matter);
   j.verify();
   snapshot=w.world().canonical_state();
   CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
  }
  {
   PersistentWorld w(path,Simulation(1));
   TerritoryJournal reopened(path);
   CHECK(w.world().canonical_state()==snapshot);
   CHECK(reopened.history().size()==8);
   CHECK(reopened.standing(w,colony,visitor).invited);
   CHECK(w.world().total_material_kg()==matter);
   // Ordinary attempted history rewrite must fail inside SQLite.
   sqlite3* db=nullptr;
   CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
   CHECK(sqlite3_exec(db,"UPDATE sg_disposition02 SET radius=4000 "
        "WHERE event_id=71000",nullptr,nullptr,nullptr)!=SQLITE_OK);
   sqlite3_close(db);
   reopened.verify();
  }
  // An attacker who drops a trigger and rewrites a historical field
  // without corresponding witness evidence cannot pass journal audit.
  {
   sqlite3* db=nullptr;
   CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
   CHECK(sqlite3_exec(db,"DROP TRIGGER sg_disp02_no_update",nullptr,nullptr,nullptr)==SQLITE_OK);
   CHECK(sqlite3_exec(db,"UPDATE sg_disposition02 SET radius=4000 "
        "WHERE event_id=71000",nullptr,nullptr,nullptr)==SQLITE_OK);
   sqlite3_close(db);
   bool rejected=false;
   try{TerritoryJournal tampered(path);}
   catch(const std::runtime_error&){rejected=true;}
   CHECK(rejected);
  }
  cleanup();
  std::cout<<"PASS DISPOSITION-02: territorial claims, invitations, grace, watched trespass, expulsion, original patrol, SQLite restart and tamper rejection\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL DISPOSITION-02: "<<e.what()<<'\n';cleanup();return 1;
 }
}
