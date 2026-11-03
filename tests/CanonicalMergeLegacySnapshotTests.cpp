#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(int argc,char** argv){
 if(argc!=2)throw std::runtime_error("missing frozen v20 snapshot path");
 std::ifstream in(argv[1],std::ios::binary);
 if(!in)throw std::runtime_error("frozen old snapshot missing");
 std::string original((std::istreambuf_iterator<char>(in)),{});
 if(original.rfind("galaxy-kernel-v20|",0)!=0)throw std::runtime_error("fixture is not original v20");
 auto loaded=Simulation::from_snapshot(original);
 if(loaded.canonical_state()!=original)throw std::runtime_error("legacy snapshot changed on load");
 if(loaded.npc_records().size()!=6 || loaded.colony_ship(1)==nullptr || loaded.colony_ship(1)->crew!=6)
    throw std::runtime_error("legacy crew was not reconstructed");
 auto path=std::filesystem::temp_directory_path()/"sg_merge_v20_fixture.sqlite";
 auto backup=std::filesystem::temp_directory_path()/"sg_merge_v20_recovery.sqlite";
 std::filesystem::remove(path);std::filesystem::remove(backup);
 {
   PersistentWorld w(path.string(),std::move(loaded));
   if(w.world().canonical_state()!=original)throw std::runtime_error("legacy genesis changed on world open");
   w.steps(2);
   if(w.world().npc_records().size()!=6)throw std::runtime_error("legacy NPC lost after steps");
 }
 PersistentWorld::recover_copy(path.string(),backup.string());
 {
   PersistentWorld w(backup.string(),Simulation(1));
   if(w.world().npc_records().size()!=6 || w.world().colony_ship(1)->crew!=6)
       throw std::runtime_error("legacy NPC missing after durable recovery");
 }
 std::filesystem::remove(path);std::filesystem::remove(backup);
 std::cout<<"PASS original v20 fixture reopened, stepped, and recovered through consolidated kernel\n";
}
