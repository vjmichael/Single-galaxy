#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void require(bool yes,const char* msg){if(!yes)throw std::runtime_error(msg);}
static Command host(std::uint64_t id,std::uint64_t tick,Action action,
                    EntityId actor,EntityId target,std::uint64_t voyage,
                    std::vector<std::uint64_t> passengers,std::int64_t amount=0){
    Command c{};c.id=id;c.execute_tick=tick;c.issuer=Simulation::kServerAuthority;
    c.action=action;c.actor=actor;c.target=target;c.transport_id=voyage;
    c.passenger_ids=std::move(passengers);c.amount=amount;return c;
}
int main(){
 const auto db=(std::filesystem::temp_directory_path()/"sg_demog05q_incident_v35.db").string();
 const auto recovered=db+".recovered";
 std::filesystem::remove(db);std::filesystem::remove(recovered);
 EntityId home=0,ferry=0,rescue=0;
 {
  Simulation s(43);home=s.add_colony_ship(7,{0,0},100,20);
  ferry=s.add_colony_ship(7,{0,0},100,6);
  rescue=s.add_colony_ship(7,{0,0},100,6);
  s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
  PersistentWorld w(db,std::move(s));
  auto embark=host(700,1,Action::HostEmbarkPassengers,ferry,home,900,{1});
  require(w.submit_host_passenger_action(embark),"board accepted");w.step();
  require(w.world().voyage_status(900)==1,"board status");
  auto strand=host(701,2,Action::HostStrandFerry,ferry,ferry,900,{1},2);
  require(!w.submit(strand),"host authority bypass blocked");
  require(w.submit_host_passenger_action(strand),"strand accepted");
  auto clash=host(702,2,Action::HostResumeFerry,ferry,ferry,900,{1});
  require(!w.submit_host_passenger_action(clash),"queued conflict rejected");
  w.step();require(w.world().voyage_status(900)==3,"stranded status");
  auto bob=w.world().npc_records().at(1);
  require(bob.location==ferry && bob.transport_id==900 && bob.assigned_site==home,"Bob remained aboard stranded ship");
 }
 {
  PersistentWorld w(db,Simulation(14));
  require(w.world().voyage_status(900)==3,"stranded across restart");
  auto resume=host(703,w.world().tick()+1,Action::HostResumeFerry,ferry,ferry,900,{1});
  require(w.submit_host_passenger_action(resume),"resume accepted");w.step();
  require(w.world().voyage_status(900)==1,"resumed voyage");
  auto strand=host(704,w.world().tick()+1,Action::HostStrandFerry,ferry,ferry,900,{1},1);
  require(w.submit_host_passenger_action(strand),"second incident accepted");w.step();
  require(w.world().voyage_status(900)==3,"second stranding");
  auto evac=host(705,w.world().tick()+1,Action::HostEvacuatePassengers,ferry,rescue,900,{1},901);
  require(!w.submit_host_passenger_action(host(706,w.world().tick()+1,Action::HostEvacuatePassengers,ferry,ferry,900,{1},901)),"bad pending rescue new ID accepted");
  require(w.submit_host_passenger_action(evac),"rescue accepted");
  require(!w.submit_host_passenger_action(evac),"duplicate rescue accepted");
  w.step();
  require(w.world().voyage_status(900)==5,"original completed rescue");
  require(w.world().voyage_status(901)==1,"new rescue leg active");
  auto bob=w.world().npc_records().at(1);
  require(bob.id==1 && bob.assigned_site==home && bob.location==rescue && bob.transport_id==901,"Bob rescued unchanged");
 }
 {
  PersistentWorld w(db,Simulation(14));
  auto bob=w.world().npc_records().at(1);
  require(bob.location==rescue && bob.transport_id==901,"rescue persistent");
  auto land=host(707,w.world().tick()+1,Action::HostDisembarkPassengers,rescue,home,901,{1});
  require(w.submit_host_passenger_action(land),"disembark new leg accepted");w.step();
  bob=w.world().npc_records().at(1);
  require(bob.location==home && bob.transport_id==0 && bob.assigned_site==home,"rescue disembark");
  require(w.world().voyage_status(900)==5 && w.world().voyage_status(901)==2,"both receipts retained");
 }
 PersistentWorld::recover_copy(db,recovered);
 {
  PersistentWorld live(db,Simulation(17)),replay(recovered,Simulation(17));
  require(live.world().canonical_state()==replay.world().canonical_state(),"genesis replay divergence");
  require(replay.world().npc_records().at(1).location==home,"replayed Bob home");
 }
 std::filesystem::remove(db);std::filesystem::remove(recovered);
 std::cout<<"PASS v35 Bob strand/restart/resume/re-strand/rescue/new-voyage/disembark/genesis-recovery\n";
}
