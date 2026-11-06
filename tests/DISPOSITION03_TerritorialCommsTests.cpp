#include "galaxy/TerritorialEnforcement.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::colony_disposition;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto path=(fs::temp_directory_path()/("sg_disposition03_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 const auto clean=[&]{for(const auto* ext:{"","-wal","-shm"})fs::remove(path+ext);};
 try{
  Simulation seed(30303);
  const auto colony=seed.add_colony_ship(7,{0,0},100,6);
  const auto visitor=seed.add_ship(8,{1000,0},100);
  seed.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
  seed.enable_individual_workforce();
  seed.upgrade_assignment_snapshot_format();
  seed.enable_passenger_life_support();
  const auto original=seed.total_material_kg();
  std::uint64_t guard=0;
  std::string saved;
  {
   PersistentWorld world(path,std::move(seed));
   TerritoryJournal territory(path);
   for(const auto& [id,n]:world.world().npc_records())
       if(n.home_ship==colony && n.location==colony &&
          world.world().npc_available_for_duty(id)){guard=id;break;}
   CHECK(guard);
   CHECK(territory.claim(world,7,colony,53001,3000));
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::NeedsWarning);
   CHECK(!territory.respond_to_notice(world,8,colony,visitor,53002,53003,false));
   const auto warned=supervise_territorial_contact(
        world,territory,colony,visitor,guard,53002,53004,0,5);
   CHECK(warned.warning_recorded);
   CHECK(warned.final==EnforcementPhase::AwaitingReceipt);
   CHECK(territory.pending_notices(world,8).size()==1);
   CHECK(territory.pending_notices(world,9).empty());
   CHECK(territory.pending_notices(world,8)[0].event_id==53002);
   // Even 12 unacknowledged ticks can never retroactively establish
   // delivery, start a punitive deadline, or confer law-enforcement power.
   world.steps(12);
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::AwaitingReceipt);
   CHECK(!territory.record_trespass(world,7,colony,visitor,guard,53004));
   CHECK(!territory.respond_to_notice(world,9,colony,visitor,53002,53003,false));
   bool crashed=false;
   const auto prior=territory.history().size();
   try{(void)territory.respond_to_notice(world,8,colony,visitor,
       53002,53003,true,TerritoryFailPoint::AfterInsert);}
   catch(const std::runtime_error&){crashed=true;}
   CHECK(crashed && territory.history().size()==prior);
   {TerritoryJournal independent(path);
    CHECK(independent.history().size()==prior);}
   CHECK(!territory.standing(world,colony,visitor).notice_received);
   CHECK(territory.respond_to_notice(world,8,colony,visitor,53002,53003,true));
   CHECK(!territory.respond_to_notice(world,8,colony,visitor,53002,53007,false));
   CHECK(territory.pending_notices(world,8).empty());
   CHECK(territory.standing(world,colony,visitor).notice_refused);
   CHECK(territory.standing(world,colony,visitor).grace_until==
         world.world().tick()+5);
   world.steps(4);
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::GracePeriod);
   CHECK(enforcement_view(world,territory,colony,visitor).ticks_remaining==1);
   CHECK(!territory.record_trespass(world,7,colony,visitor,guard,53004));
   world.step();
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::MayRecordTrespass);
   CHECK(!territory.record_trespass(world,8,colony,visitor,guard,53004));
   const auto enforced=supervise_territorial_contact(
       world,territory,colony,visitor,guard,53008,53004,54001,5);
   CHECK(enforced.trespass_recorded);
   CHECK(enforced.final==EnforcementPhase::TrespassRecorded);
   CHECK(enforced.patrol_status==npc_response::Status::Accepted);
   CHECK(world.world().npc_flight_orders().at(54001).purpose==
         NpcFlightPurpose::Patrol);
   CHECK(!enforcement_view(world,territory,colony,visitor).allows_damage);
   CHECK(!enforcement_view(world,territory,colony,visitor).allows_arrest);
   CHECK(!enforcement_view(world,territory,colony,visitor).allows_seizure);
   CHECK(territory.expel(world,7,colony,visitor,guard,53005));
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::Expelled);
   CHECK(territory.invite(world,7,colony,8,53006));
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::Invited);
   CHECK(territory.revoke_invitation(world,7,colony,8,53007));
   CHECK(enforcement_view(world,territory,colony,visitor).phase==
         EnforcementPhase::NeedsWarning);
   CHECK(!territory.standing(world,colony,visitor).trespass_recorded);
   CHECK(territory.history().size()==8);
   CHECK(world.world().total_material_kg()==original);
   CHECK(world.world().find(visitor)->hull==100);
   saved=world.world().canonical_state();
  }
  {
   PersistentWorld recovered(path,Simulation(30304));
   TerritoryJournal journal(path);
   CHECK(recovered.world().canonical_state()==saved);
   CHECK(journal.history().size()==8);
   CHECK(enforcement_view(recovered,journal,colony,visitor).phase==
         EnforcementPhase::NeedsWarning);
   CHECK(recovered.world().total_material_kg()==original);
  }
  // A forged acknowledgement after dropping the SQLite immutability
  // trigger must fail the independently verified evidence chain.
  {
   sqlite3* db=nullptr;
   CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
   CHECK(sqlite3_exec(db,"DROP TRIGGER sg_disp02_no_update",
       nullptr,nullptr,nullptr)==SQLITE_OK);
   CHECK(sqlite3_exec(db,
       "UPDATE sg_disposition02 SET deadline=99999 WHERE event_id=53003",
       nullptr,nullptr,nullptr)==SQLITE_OK);
   sqlite3_close(db);
   bool rejected=false;
   try{TerritoryJournal tampered(path);}
   catch(const std::runtime_error&){rejected=true;}
   CHECK(rejected);
  }
  clean();
  std::cout<<"PASS DISPOSITION-03: unreceived notice never trespasses, visitor refusal restarts grace, witnessed pursuit, rollback and forged receipt denial\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL DISPOSITION-03: "<<e.what()<<'\n';
  clean();return 1;
 }
}
