#include "galaxy/TerritorialTravelClearance.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::colony_disposition;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto path=(fs::temp_directory_path()/("sg_clearance_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 const auto clean=[&]{for(const char* suffix:{"","-wal","-shm"})fs::remove(path+suffix);};
 try{
  {
   Simulation seed(30503);
   const auto colony=seed.add_colony_ship(7,{0,0},100,6);
   const auto visitor=seed.add_ship(8,{1000,0},100);
   seed.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
   seed.enable_individual_workforce();
   seed.upgrade_assignment_snapshot_format();
   seed.enable_passenger_life_support();
   PersistentWorld world(path,std::move(seed));
   TerritoryJournal journal(path);
   CHECK(journal.claim(world,7,colony,73001,3000));
   ControlledAccessRequest request{};
   request.requesting_owner=8; request.ship=visitor;
   request.controlling_colony=colony;
   request.service=TerritorialService::HyperlaneEntry;
   request.infrastructure_control_verified=true;
   auto decide=[&]{return territorial_service_clearance(world,journal,request);};
   CHECK(decide().decision==ClearanceDecision::DenyPendingHail);
   CHECK(!decide().can_block_physical_flight);
   request.infrastructure_control_verified=false;
   CHECK(decide().decision==ClearanceDecision::NotAuthorizedToDecide);
   request.infrastructure_control_verified=true;
   request.requesting_owner=9;
   CHECK(decide().decision==ClearanceDecision::NotAuthorizedToDecide);
   request.requesting_owner=8;
   std::uint64_t guard=0;
   for(const auto& [id,n]:world.world().npc_records())
    if(n.home_ship==colony && n.location==colony &&
       world.world().npc_available_for_duty(id)){guard=id;break;}
   CHECK(guard);
   CHECK(journal.warn(world,7,colony,visitor,guard,73002,5));
   CHECK(decide().decision==ClearanceDecision::DenyPendingHail);
   CHECK(player_respond_to_hail(world,journal,8,colony,visitor,73002,73003,HailChoice::Acknowledge));
   CHECK(decide().decision==ClearanceDecision::Allow);
   request.service_requires_invitation=true;
   CHECK(decide().decision==ClearanceDecision::DenyNoInvitation);
   CHECK(journal.invite(world,7,colony,8,73004));
   CHECK(decide().decision==ClearanceDecision::Allow);
   CHECK(journal.revoke_invitation(world,7,colony,8,73005));
   CHECK(decide().decision==ClearanceDecision::DenyNoInvitation);
   journal.verify();
  }
  clean();
  std::cout<<"PASS DISPOSITION-03C: owner-verified installation, pending hail, reply and invitation gates; no propulsion denial\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL DISPOSITION-03C "<<e.what()<<'\n';clean();return 1;}
}
