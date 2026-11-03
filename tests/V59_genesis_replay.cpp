#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void sql(sqlite3* d,const char* q){char* e=nullptr;if(sqlite3_exec(d,q,nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"sql";sqlite3_free(e);throw std::runtime_error(m);}}
int main(){namespace fs=std::filesystem;auto base=(fs::temp_directory_path()/"sg_v33_journal.db").string();fs::remove(base);EntityId home,ferry;{
 Simulation s(42);home=s.add_colony_ship(7,{0,0},100,6);ferry=s.add_colony_ship(7,{0,0},100,6);s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();PersistentWorld w(base,std::move(s));
 Command c;c.id=100;c.execute_tick=1;c.issuer=Simulation::kServerAuthority;c.actor=ferry;c.target=home;c.action=Action::HostEmbarkPassengers;c.transport_id=900;c.passenger_ids={1};if(!w.submit_host_passenger_action(c))throw std::runtime_error("embark rejected");w.step();
 c.id=101;c.execute_tick=2;c.action=Action::HostDisembarkPassengers;if(!w.submit_host_passenger_action(c))throw std::runtime_error("disembark rejected");w.step();
 }
 // Reconstruct from preserved genesis, replaying every command and tick.
 auto genesis=base+".genesis";fs::copy_file(base,genesis,fs::copy_options::overwrite_existing);
 sqlite3* db=nullptr;if(sqlite3_open(genesis.c_str(),&db)!=SQLITE_OK)throw std::runtime_error("open");
 sql(db,"DELETE FROM snapshots WHERE generation<>1 AND generation<>(SELECT MAX(generation) FROM operation_log)");sqlite3_close(db);
 auto replay=base+".replay";fs::remove(replay);PersistentWorld::recover_copy(genesis,replay);
 {PersistentWorld w(replay,Simulation(1));auto n=w.world().npc_records().at(1);if(n.location!=home||n.assigned_site!=home||n.transport_id)throw std::runtime_error("genesis replay custody mismatch");}
 // Corrupt a historical command after the earliest retained checkpoint.
 auto bad=base+".tampered";fs::copy_file(genesis,bad,fs::copy_options::overwrite_existing);
 if(sqlite3_open(bad.c_str(),&db)!=SQLITE_OK)throw std::runtime_error("open bad");
 sql(db,"UPDATE operation_log SET command='corrupted-command' WHERE generation=(SELECT MAX(generation) FROM operation_log WHERE kind=1)");sqlite3_close(db);
 auto rejected=base+".rejected";fs::remove(rejected);bool refused=false;try{PersistentWorld::recover_copy(bad,rejected);}catch(const std::exception&){refused=true;}
 if(!refused)throw std::runtime_error("tampered journal accepted");
 // Corrupt canonical voyage record without recomputing checksum: restoration must refuse.
 auto text=Simulation(1);auto snapshot=std::string();{
 PersistentWorld w(base,Simulation(1));snapshot=w.world().canonical_state();}
 auto pos=snapshot.find("voyage|900|");if(pos==std::string::npos)throw std::runtime_error("missing voyage record");snapshot.replace(pos,6,"voyagX");
 bool snapshot_refused=false;try{(void)Simulation::from_snapshot(snapshot);}catch(const std::exception&){snapshot_refused=true;}
 if(!snapshot_refused)throw std::runtime_error("tampered voyage snapshot accepted");
 for(auto p:{base,genesis,replay,bad,rejected})fs::remove(p);
 std::cout<<"PASS preserved genesis + complete operation journal replay, corrupted command rejection, corrupted voyage snapshot rejection\n";
}
