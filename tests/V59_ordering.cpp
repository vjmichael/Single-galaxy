#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void ok(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
static Command act(int id,std::uint64_t tick,Action a,EntityId actor,EntityId target,bool host=false){
 Command c{};c.id=id;c.execute_tick=tick;c.issuer=host?Simulation::kServerAuthority:7;
 c.action=a;c.actor=actor;c.target=target;
 if(host){c.transport_id=900;c.passenger_ids={1};c.amount=2;}
 return c;
}
static void run(bool strand_first){
 const auto path=(std::filesystem::temp_directory_path()/(strand_first?"sg_order_a.db":"sg_order_b.db")).string();
 std::filesystem::remove(path);
 Simulation s(99);s.add_system(2);
 const auto home=s.add_colony_ship(7,{0,0},100,6);
 const auto ferry=s.add_colony_ship(7,{0,0},100,6);
 const auto gate=s.add_gate(1,{0,0},2,{0,0});
 s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
 {
  PersistentWorld w(path,std::move(s));
  auto embark=act(10,1,Action::HostEmbarkPassengers,ferry,home,true);
  embark.amount=0;ok(w.submit_host_passenger_action(embark),"embark");w.step();
  auto strand=act(11,2,Action::HostStrandFerry,ferry,ferry,true);
  auto jump=act(12,2,Action::RequestJump,ferry,gate);
  strand.sequence=strand_first?1:2;jump.sequence=strand_first?2:1;
  if(strand_first){ok(w.submit_host_passenger_action(strand),"first strand");ok(w.submit(jump),"second jump");}
  else{ok(w.submit(jump),"first jump");ok(w.submit_host_passenger_action(strand),"second strand");}
  w.step();
  ok(w.world().voyage_status(900)==(strand_first?3U:1U),"wrong command ordering");
  bool stranded_applied=false,jump_applied=false;
  for(auto e:w.world().events()){
   if(e.command_id==11)stranded_applied=e.result==Outcome::Applied;
   if(e.command_id==12)jump_applied=e.result==Outcome::Applied;
  }
  ok(stranded_applied==strand_first && jump_applied!=strand_first,"incorrect ordered outcomes");
  ok(w.world().npc_records().at(1).location==ferry,"Bob duplicated during race");
 }
 {
  PersistentWorld w(path,Simulation(1));
  ok(w.world().voyage_status(900)==(strand_first?3U:1U),"race not restartable");
 }
 auto copy=path+".replay";std::filesystem::remove(copy);
 PersistentWorld::recover_copy(path,copy);
 {
  PersistentWorld a(path,Simulation(1)),b(copy,Simulation(1));
  ok(a.world().canonical_state()==b.world().canonical_state(),"ordered incident replay divergence");
 }
 std::filesystem::remove(path);std::filesystem::remove(copy);
}
int main(){run(true);run(false);std::cout<<"PASS same-tick strand vs jump ordering and independent genesis replay in both orders\n";}
