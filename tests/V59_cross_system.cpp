#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void run(PersistentWorld& w,uint64_t id,EntityId actor,EntityId target,Action a,bool host=false){Command c{};c.id=id;c.execute_tick=w.world().tick()+1;c.issuer=host?Simulation::kServerAuthority:7;c.actor=actor;c.target=target;c.action=a;if(host){c.transport_id=900;c.passenger_ids={1};}if(!(host?w.submit_host_passenger_action(c):w.submit(c)))throw std::runtime_error("submission failed "+std::to_string(id));w.step();if(w.world().events().back().result!=Outcome::Applied)throw std::runtime_error("execution failed "+std::to_string(id)+" "+to_string(w.world().events().back().result));}
int main(){auto db=(std::filesystem::temp_directory_path()/"sg_bob_cross_system_v34.db").string();std::filesystem::remove(db);EntityId home,ferry,dest,gate;{
Simulation s(42);s.add_system(2);home=s.add_colony_ship(7,{0,0},100,6);ferry=s.add_colony_ship(7,{0,0},100,6);dest=s.add_colony_ship(7,{0,0},100,6);gate=s.add_gate(1,{0,0},2,{5000,0});s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();PersistentWorld w(db,std::move(s));
run(w,100,dest,gate,Action::RequestJump);w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);if(w.world().find(dest)->system_id!=2)throw std::runtime_error("destination ship did not arrive");
run(w,101,ferry,home,Action::HostEmbarkPassengers,true);auto n=w.world().npc_records().at(1);if(n.location!=ferry||n.assigned_site!=home||n.transport_id!=900)throw std::runtime_error("embark custody");
run(w,102,ferry,gate,Action::RequestJump);w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);if(w.world().find(ferry)->system_id!=2)throw std::runtime_error("ferry did not arrive");
if(w.world().npc_records().at(1).location!=ferry)throw std::runtime_error("lost Bob during jump");}
{PersistentWorld w(db,Simulation(9));if(w.world().find(ferry)->system_id!=2)throw std::runtime_error("restart ferry");run(w,103,ferry,dest,Action::HostDisembarkPassengers,true);auto n=w.world().npc_records().at(1);if(n.id!=1||n.location!=dest||n.assigned_site!=home||n.transport_id)throw std::runtime_error("disembark custody");if(w.world().canonical_state().find("galaxy-kernel-v34")!=0)throw std::runtime_error("version");}
{PersistentWorld w(db,Simulation(9));auto n=w.world().npc_records().at(1);if(n.id!=1||n.location!=dest||n.assigned_site!=home||n.transport_id)throw std::runtime_error("final restart");}
auto copy=db+".replay";std::filesystem::remove(copy);PersistentWorld::recover_copy(db,copy);{PersistentWorld w(copy,Simulation(9));auto n=w.world().npc_records().at(1);if(n.id!=1||n.location!=dest||n.assigned_site!=home||n.transport_id)throw std::runtime_error("replay mismatch");}std::filesystem::remove(copy);std::filesystem::remove(db);std::cout<<"PASS Bob embarked system 1, jumped with ferry, disembarked system 2, restart and journal recovery\n";}
