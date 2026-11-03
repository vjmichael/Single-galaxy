#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(){auto path=(std::filesystem::temp_directory_path()/"sg_bob_v32.db").string();std::filesystem::remove(path);EntityId home=0, ferry=0;uint64_t bob=1;{
 Simulation s(42);home=s.add_colony_ship(7,{0,0},100,6);ferry=s.add_colony_ship(7,{0,0},100,6);s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();PersistentWorld w(path,std::move(s));
 Command c; c.id=500;c.execute_tick=1;c.issuer=Simulation::kServerAuthority;c.actor=ferry;c.target=home;c.action=Action::HostEmbarkPassengers;c.transport_id=900;c.passenger_ids={bob};if(w.submit(c))throw std::runtime_error("public host bypass");
 Command invalid=c;invalid.id=502;invalid.issuer=7;
 if(w.submit_host_passenger_action(invalid))throw std::runtime_error("unauthorized host");
 if(!w.submit_host_passenger_action(c))throw std::runtime_error("embark submit");
 Command conflict=c;conflict.id=503;conflict.transport_id=901;
 if(w.submit_host_passenger_action(conflict))throw std::runtime_error("conflicting pending passenger");
 conflict=c;conflict.id=504;
 if(w.submit_host_passenger_action(conflict))throw std::runtime_error("duplicate voyage");w.step();
 Command duplicate=c;duplicate.id=505;duplicate.execute_tick=2;
 if(w.submit_host_passenger_action(duplicate))throw std::runtime_error("duplicate boarding");
 auto n=w.world().npc_records().at(bob);if(n.location!=ferry||n.assigned_site!=home||n.transport_id!=900)throw std::runtime_error("embark state");
 }
 {auto copy=path+".embark.replay";std::filesystem::remove(copy);
 PersistentWorld::recover_copy(path,copy);
 PersistentWorld replay(copy,Simulation(7));auto n=replay.world().npc_records().at(bob);
 if(n.id!=bob || n.location!=ferry || n.assigned_site!=home || n.transport_id!=900)
 throw std::runtime_error("embark historical replay mismatch");
 std::filesystem::remove(copy);}
 {
 PersistentWorld w(path,Simulation(7));auto n=w.world().npc_records().at(bob);if(n.location!=ferry||n.transport_id!=900)throw std::runtime_error("restart state");Command c;c.id=501;c.execute_tick=w.world().tick()+1;c.issuer=Simulation::kServerAuthority;c.actor=ferry;c.target=home;c.action=Action::HostDisembarkPassengers;c.transport_id=900;c.passenger_ids={bob};if(!w.submit_host_passenger_action(c))throw std::runtime_error("disembark submit");
 Command conflict=c;conflict.id=506;conflict.transport_id=901;
 if(w.submit_host_passenger_action(conflict))throw std::runtime_error("conflicting disembark");w.step();
 Command reuse=c;reuse.id=507;reuse.execute_tick=w.world().tick()+1;reuse.action=Action::HostEmbarkPassengers;reuse.actor=home;reuse.target=ferry;
 if(w.submit_host_passenger_action(reuse))throw std::runtime_error("completed voyage reused");
 n=w.world().npc_records().at(bob);if(n.id!=bob||n.location!=home||n.assigned_site!=home||n.transport_id)throw std::runtime_error("disembark state");
 }{PersistentWorld w(path,Simulation(7));auto n=w.world().npc_records().at(bob);if(n.id!=bob||n.location!=home||n.transport_id)throw std::runtime_error("final restart");}auto recovered=path+".recovered";std::filesystem::remove(recovered);
 PersistentWorld::recover_copy(path,recovered);
 {PersistentWorld replay(recovered,Simulation(7));auto n=replay.world().npc_records().at(bob);
 if(n.id!=bob || n.location!=home || n.assigned_site!=home || n.transport_id)
 throw std::runtime_error("historical replay mismatch");}
 std::filesystem::remove(recovered);std::filesystem::remove(path);
 std::cout<<"PASS authorization, conflicting manifests, Bob embark/restart/disembark, recovery replay\n";}
