#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void ck(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static Command h(int id,int tick,Action a,EntityId ship,EntityId destination,
                 std::uint64_t voyage,std::vector<std::uint64_t> manifest,std::int64_t amount=0){
 Command c{};c.id=static_cast<std::uint64_t>(id);c.execute_tick=static_cast<std::uint64_t>(tick);
 c.action=a;c.issuer=Simulation::kServerAuthority;c.actor=ship;c.target=destination;
 c.transport_id=voyage;c.passenger_ids=std::move(manifest);c.amount=amount;return c;
}
int main(){
 const auto db=(std::filesystem::temp_directory_path()/"sg_v35_negative.db").string();
 std::filesystem::remove(db);EntityId home=0,ferry=0,rescue=0;
 {
  Simulation s(42);home=s.add_colony_ship(7,{0,0},100,22);
  ferry=s.add_colony_ship(7,{0,0},100,6);rescue=s.add_colony_ship(7,{0,0},100,6);
  auto distant=s.add_colony_ship(7,{9000,0},100,6);
  s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();PersistentWorld w(db,std::move(s));
  std::vector<std::uint64_t> sixteen;for(std::uint64_t n=1;n<=16;n++)sixteen.push_back(n);
  auto full=h(800,1,Action::HostEmbarkPassengers,rescue,home,801,sixteen);
  ck(w.submit_host_passenger_action(full),"rescue ship first embark");w.step();
  ck(w.world().passengers_aboard(rescue)==16,"receiver full");
  auto bob=h(801,2,Action::HostEmbarkPassengers,ferry,home,802,{17});
  ck(w.submit_host_passenger_action(bob),"Bob embark");w.step();
  auto stranded=h(802,3,Action::HostStrandFerry,ferry,ferry,802,{17},1);
  ck(w.submit_host_passenger_action(stranded),"stranding accepted");w.step();
  const auto original=w.world().canonical_state();
  ck(!w.submit_host_passenger_action(h(803,4,Action::HostEvacuatePassengers,ferry,rescue,802,{17},803)),"rescue into full ship accepted");
  ck(!w.submit_host_passenger_action(h(804,4,Action::HostEvacuatePassengers,ferry,ferry,802,{17},803)),"self rescue accepted");
  ck(!w.submit_host_passenger_action(h(810,4,Action::HostEvacuatePassengers,ferry,distant,802,{17},803)),"out of range rescue accepted");
  ck(!w.submit_host_passenger_action(h(805,4,Action::HostEvacuatePassengers,ferry,rescue,802,{17,17},803)),"duplicate passenger accepted");
  ck(!w.submit_host_passenger_action(h(806,4,Action::HostEvacuatePassengers,ferry,rescue,802,{18},803)),"wrong manifest accepted");
  ck(!w.submit_host_passenger_action(h(807,4,Action::HostResumeFerry,ferry,ferry,802,{18})),"wrong repair manifest accepted");
  ck(!w.submit_host_passenger_action(h(808,4,Action::HostDisembarkPassengers,ferry,home,802,{17})),"stranded normal disembark accepted");
  ck(original==w.world().canonical_state(),"invalid mutations changed state");
  // Snapshot tampering: v35 voyage 802 status/incident must fail restoration.
  auto bad=original;const auto start=bad.find("voyage|802|");ck(start!=std::string::npos,"voyage row missing");
  auto e=bad.find('\n',start);auto line=bad.substr(start,e-start);
  const auto first=line.find('|',7);(void)first;
  // Every valid v35 stranded record contains the exact status/status-dependent fields.
  // Setting stranded reason 0 must fail closed, not silently revive Bob.
  auto sep=std::vector<std::size_t>{};for(size_t pos=0;(pos=line.find('|',pos))!=std::string::npos;++pos)sep.push_back(pos);
  ck(sep.size()>=10,"v35 lifecycle fields missing");
  const size_t cause_begin=sep[8]+1,cause_end=sep[9]; // voyage|id|ferry|origin|status|count|dest|last|tick|reason|rescue|ids
  bad.replace(start+cause_begin,cause_end-cause_begin,"0");
  bool rejected=false;try{auto invalid=Simulation::from_snapshot(bad);(void)invalid;}catch(const std::exception&){rejected=true;}
  ck(rejected,"stranding reason tamper accepted");
  bad=original;
  const size_t incident_begin=sep[6]+1,incident_end=sep[7];
  bad.replace(start+incident_begin,incident_end-incident_begin,"999999");
  rejected=false;try{auto invalid=Simulation::from_snapshot(bad);(void)invalid;}catch(const std::exception&){rejected=true;}
  ck(rejected,"missing incident receipt tamper accepted");
  // Failure at precommit receipt write: no synthetic repair receipt.
  auto repair=h(809,4,Action::HostResumeFerry,ferry,ferry,802,{17});
  bool thrown=false;try{(void)w.submit_host_passenger_action(repair,FailPoint::AfterReceiptWrite);}catch(const std::exception&){thrown=true;}
  ck(thrown && w.world().canonical_state()==original,"failed submit mutated committed world");
  ck(w.submit_host_passenger_action(repair),"repair retry same command ID rejected");
  thrown=false;try{w.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){thrown=true;}
  ck(thrown && w.world().voyage_status(802)==3,"failed step changed live voyage");
 }
 {
  PersistentWorld w(db,Simulation(88));
  ck(w.world().voyage_status(802)==3,"pre-commit failed step persisted");
  w.step();ck(w.world().voyage_status(802)==1,"pending repair not replayed exactly once");
  ck(w.world().npc_records().at(17).transport_id==802,"Bob disappeared during repair");
 }
 std::filesystem::remove(db);
 std::cout<<"PASS rescue overbooking/manifest safety, v35 corruption rejection, precommit injection + restart retry\n";
}
