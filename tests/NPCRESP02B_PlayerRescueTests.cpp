#include "galaxy/Persistence.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto db=(fs::temp_directory_path()/("sg_npcresp02b_"+
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 try{
   Simulation seed(90812);
   const auto player_ship=seed.add_ship(7,{1500,0},100);
   const auto rescuer=seed.add_colony_ship(9,{0,0},100,6);
   seed.provision_colony_genesis(rescuer,2000,2000,2000,2000,2000);
   seed.enable_individual_workforce();
   seed.upgrade_assignment_snapshot_format();
   seed.enable_passenger_life_support();
   const auto initial_material=seed.total_material_kg();
   std::string durable;
   {
     PersistentWorld w(db,std::move(seed));
     CHECK(w.register_player_person(7,player_ship));
     CHECK(w.world().people_physically_aboard(player_ship)==1);
     CHECK(w.world().people_physically_aboard(rescuer)==6);
     CHECK(!w.grant_player_rescue_consent(9,rescuer,99801));
     CHECK(!w.grant_player_rescue_consent(7,player_ship,99801));
     CHECK(!w.complete_player_rescue(7,rescuer,97099,99802));
     const auto before=w.world().canonical_state();
     bool failed=false;
     try{(void)w.grant_player_rescue_consent(
          7,rescuer,99801,FailPoint::AfterSnapshotWrite);}
     catch(const std::exception&){failed=true;}
     CHECK(failed && w.world().canonical_state()==before);
     CHECK(w.grant_player_rescue_consent(7,rescuer,99801));
     CHECK(!w.grant_player_rescue_consent(7,rescuer,99803));
     CHECK(!w.revoke_player_rescue_consent(8,99801));
     CHECK(w.revoke_player_rescue_consent(7,99801));
     CHECK(!w.revoke_player_rescue_consent(7,99801));
     CHECK(!w.complete_player_rescue(7,rescuer,97099,99802));
     CHECK(w.grant_player_rescue_consent(7,rescuer,99804));
     CHECK(w.queue_npc_ship_rendezvous(9,rescuer,0,player_ship,97099,
                                      NpcFlightPurpose::Rescue));
     CHECK(!w.complete_player_rescue(7,rescuer,97099,99802));
     bool arrived=false;
     for(int i=0;i<300;i++){
       w.step();
       if(w.world().npc_flight_orders().at(97099).phase==
          NpcFlightPhase::Arrived){arrived=true;break;}
     }
     CHECK(arrived);
     CHECK(ship_interaction::rendezvous_preflight(w.world(),9,
       rescuer,player_ship,97099)==ship_interaction::Readiness::Ready);
     const auto before_transfer=w.world().canonical_state();
     bool crashed=false;
     try{(void)w.complete_player_rescue(
          7,rescuer,97099,99802,FailPoint::AfterSnapshotWrite);}
     catch(const std::exception&){crashed=true;}
     CHECK(crashed && w.world().canonical_state()==before_transfer);
     CHECK(!w.complete_player_rescue(8,rescuer,97099,99802));
     CHECK(w.complete_player_rescue(7,rescuer,97099,99802));
     CHECK(!w.complete_player_rescue(7,rescuer,97099,99802));
     const auto& person=w.world().player_people().at(7);
     CHECK(person.owner==7 && person.home_ship==player_ship &&
           person.physical_ship==rescuer &&
           person.state==PlayerPersonState::Rescued &&
           person.custodian==9 && person.consent_id==99804 &&
           person.last_receipt==99802 && person.rescue_flight==97099);
     CHECK(w.world().find(player_ship)->owner_id==7);
     // The person is genuinely aboard the rescue vessel. Original direct
     // cockpit controls cannot magically operate the abandoned source hull.
     Command abandoned{};
     abandoned.id=88012;abandoned.sequence=88012;
     abandoned.execute_tick=w.world().tick()+1;
     abandoned.issuer=7;abandoned.actor=player_ship;
     abandoned.action=Action::Thrust;abandoned.thrust_x=1;
     CHECK(!w.submit(abandoned));
     abandoned.id=88013;abandoned.sequence=88013;
     abandoned.action=Action::StartMining;abandoned.thrust_x=0;
     CHECK(!w.submit(abandoned));
     CHECK(w.world().people_physically_aboard(player_ship)==0);
     CHECK(w.world().people_physically_aboard(rescuer)==7);
     CHECK(w.world().total_material_kg()==initial_material);
     bool movement=false;
     for(const auto& e:w.world().activity_events())
        if(e.kind==ActivityKind::PlayerRescued &&
           e.start_command_id==99802 && e.actor==rescuer &&
           e.target==player_ship && e.result==Outcome::Applied)movement=true;
     CHECK(movement);
     const auto ration=w.world().life_support(rescuer)->food_kg;
     const auto until_cycle=20-(w.world().tick()%20);
     w.steps(until_cycle);
     CHECK(w.world().life_support(rescuer)->food_kg==ration-7);
     CHECK(w.world().total_material_kg()==initial_material);
     durable=w.world().canonical_state();
     CHECK(durable.rfind("galaxy-kernel-v99|",0)==0);
     CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
     auto forged=durable;
     const auto row=forged.find("playerperson|7|");
     CHECK(row!=std::string::npos);
     const auto end=forged.find('\n',row);
     CHECK(end!=std::string::npos);
     auto text=forged.substr(row,end-row);
     const auto marker="|99802|";
     const auto at=text.find(marker);
     CHECK(at!=std::string::npos);
     text.replace(at,marker.size(),"|99804|");
     forged.replace(row,end-row,text);
     bool denied=false;
     try{(void)Simulation::from_snapshot(forged);}
     catch(const std::exception&){denied=true;}
     CHECK(denied);
   }
   {
     PersistentWorld recovered(db,Simulation(90812));
     CHECK(recovered.world().canonical_state()==durable);
     CHECK(recovered.world().player_people().at(7).physical_ship==rescuer);
     CHECK(recovered.world().people_physically_aboard(rescuer)==7);
     CHECK(recovered.world().total_material_kg()==initial_material);
   }
   fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
   std::cout<<"PASS NPC-RESP-02B: actual rescue, player consent, finite rations, native receipt, SQLite rollback and recovery\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"FAIL NPC-RESP-02B: "<<e.what()<<'\n';
   fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
   return 1;
 }
}
