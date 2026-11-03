#include "galaxy/Simulation.hpp"
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(){
 Simulation s(42);auto home=s.add_colony_ship(7,{0,0},100,6);auto ferry=s.add_colony_ship(7,{0,0},100,6);
 s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
 Command c;c.id=100;c.execute_tick=1;c.issuer=Simulation::kServerAuthority;c.actor=ferry;c.target=home;c.action=Action::HostEmbarkPassengers;c.transport_id=900;c.passenger_ids={1};
 if(!s.submit(c))throw std::runtime_error("submit");s.step();
 auto snapshot=s.canonical_state();auto p=snapshot.find("galaxy-kernel-v34");if(p==std::string::npos)throw std::runtime_error("version");snapshot.replace(p,17,"galaxy-kernel-v32");p=snapshot.find("voyage|900|");if(p==std::string::npos)throw std::runtime_error("voyage");snapshot.erase(p,snapshot.find('\n',p)-p+1);
 Simulation::LegacyVoyageManifest m{900,ferry,home,{1}};
 auto migrated=Simulation::migrate_legacy_active_voyages(snapshot,{m});
 if(migrated.npc_records().at(1).transport_id!=900 || migrated.npc_records().at(1).location!=ferry)throw std::runtime_error("custody");
 auto restored=Simulation::from_snapshot(migrated.canonical_state());if(restored.npc_records().at(1).transport_id!=900)throw std::runtime_error("roundtrip");
 bool rejected=false;try{auto x=Simulation::migrate_legacy_active_voyages(snapshot,{{900,ferry,home,{2}}});(void)x;}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("wrong passenger accepted");
 rejected=false;try{auto x=Simulation::migrate_legacy_active_voyages(snapshot,{m,m});(void)x;}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("duplicate accepted");
 std::cout<<"PASS v32 active voyage migrated to v33, roundtrip, wrong manifest and duplicate rejected\n";
}
