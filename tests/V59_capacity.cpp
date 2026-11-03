#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(){
 auto db=(std::filesystem::temp_directory_path()/"sg_berth_limit_v34.db").string();std::filesystem::remove(db);
 EntityId home=0,ferry=0;
 {
 Simulation s(42);home=s.add_colony_ship(7,{0,0},100,18);ferry=s.add_colony_ship(7,{0,0},100,6);
 s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();PersistentWorld w(db,std::move(s));
 Command c{};c.id=400;c.execute_tick=w.world().tick()+1;c.issuer=Simulation::kServerAuthority;c.actor=ferry;c.target=home;c.action=Action::HostEmbarkPassengers;c.transport_id=777;
 for(uint64_t i=1;i<=16;i++)c.passenger_ids.push_back(i);
 if(!w.submit_host_passenger_action(c))throw std::runtime_error("16 berths rejected");
 Command overflow=c;overflow.id=401;overflow.transport_id=778;overflow.passenger_ids={17};
 if(w.submit_host_passenger_action(overflow))throw std::runtime_error("pending overbooking accepted");
 w.step();if(w.world().events().back().result!=Outcome::Applied)throw std::runtime_error("boarding failed");
 overflow.execute_tick=w.world().tick()+1;
 if(w.submit_host_passenger_action(overflow))throw std::runtime_error("occupied overbooking accepted");
 }
 {PersistentWorld w(db,Simulation(9));int count=0;for(const auto& [id,n]:w.world().npc_records())if(n.transport_id==777){++count;if(n.location!=ferry)throw std::runtime_error("custody mismatch");}if(count!=16)throw std::runtime_error("restart count mismatch");}
 std::filesystem::remove(db);std::cout<<"PASS 16 ferry berths, pending and occupied overbooking rejected, restart custody intact\n";
}
