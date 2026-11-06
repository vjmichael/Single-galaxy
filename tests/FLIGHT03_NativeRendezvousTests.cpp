#include "galaxy/Persistence.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto dir=fs::temp_directory_path()/("sg_flight03_native_"+
     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  Simulation seed(3011);
  const auto escort=seed.add_colony_ship(7,{0,0},120,4);
  seed.provision_colony_genesis(escort,2000,2000,2000,2000,2000);
  seed.enable_individual_workforce();
  const auto target=seed.add_ship(8,{12000,0},120);
  const auto before_ore=seed.total_ore();
  std::string finished;
  {
   PersistentWorld world(dir.string(),std::move(seed));
   CHECK(!world.queue_npc_ship_rendezvous(8,escort,0,target,97001));
   CHECK(!world.queue_npc_ship_rendezvous(7,escort,0,escort,97001));
   const auto before=world.world().canonical_state();
   bool fault=false;
   try{(void)world.queue_npc_ship_rendezvous(7,escort,0,target,97001,
            NpcFlightPurpose::Rescue,FailPoint::AfterSnapshotWrite);}
   catch(const std::exception&){fault=true;}
   CHECK(fault && world.world().canonical_state()==before);
   CHECK(world.queue_npc_ship_rendezvous(7,escort,0,target,97001,NpcFlightPurpose::Rescue));
   CHECK(!world.queue_npc_ship_rendezvous(7,escort,0,target,97002));
   CHECK(world.world().npc_flight_orders().at(97001).mode==NpcFlightMode::LocalRendezvous);
   CHECK(world.world().canonical_state().rfind("galaxy-kernel-v97|",0)==0);
   CHECK(Simulation::from_snapshot(world.world().canonical_state()).canonical_state()==
         world.world().canonical_state());
   // Target is a real player ship: independently accelerate it during pursuit.
   Command drift{};
   drift.id=7501;drift.sequence=7501;drift.execute_tick=world.world().tick()+1;
   drift.issuer=8;drift.actor=target;drift.action=Action::Thrust;drift.thrust_x=1;
   CHECK(world.submit(drift));
   bool arrived=false;
   for(int i=0;i<400;i++){
     world.step();
     if(i==6){
      const auto underway=world.world().canonical_state();
      CHECK(Simulation::from_snapshot(underway).canonical_state()==underway);
     }
     if(world.world().npc_flight_orders().at(97001).phase==NpcFlightPhase::Arrived){
       arrived=true;break;
     }
   }
   CHECK(arrived);
   const auto& flight=world.world().npc_flight_orders().at(97001);
   CHECK(flight.last_local_command!=0);
   CHECK(world.world().find(escort)->dock_state==DockState::InSpace);
   CHECK(world.world().find(escort)->dock_station==0);
   bool receipt=false;
   for(const auto& e:world.world().activity_events())
     if(e.kind==ActivityKind::NpcRendezvous && e.actor==escort &&
        e.target==target && e.start_command_id==flight.last_local_command &&
        e.result==Outcome::Applied && e.transferred==0)receipt=true;
   CHECK(receipt);
   CHECK(world.world().total_ore()==before_ore);
   finished=world.world().canonical_state();
   CHECK(Simulation::from_snapshot(finished).canonical_state()==finished);
  }
  {
   PersistentWorld reopened(dir.string(),Simulation(1));
   CHECK(reopened.world().canonical_state()==finished);
   CHECK(reopened.world().npc_flight_orders().at(97001).phase==NpcFlightPhase::Arrived);
   CHECK(reopened.world().total_ore()==before_ore);
  }
  fs::remove_all(dir);
  std::cout<<"PASS FLIGHT-03 real ship rendezvous, native receipt and SQLite recovery\n";
  return 0;
 }catch(const std::exception& e){
  std::error_code ec;fs::remove_all(dir,ec);
  std::cerr<<"FAIL FLIGHT-03: "<<e.what()<<'\n';return 1;
 }
}
