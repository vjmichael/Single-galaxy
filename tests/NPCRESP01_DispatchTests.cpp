#include "galaxy/NpcResponseDispatch.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::npc_response;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto db=(fs::temp_directory_path()/("sg_npcresp01_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 try{
    Simulation initial(90103);
    auto npc=initial.add_colony_ship(7,{0,0},100,6);
    initial.provision_colony_genesis(npc,2000,2000,2000,2000,2000);
    auto player_ship=initial.add_ship(7,{10000,0},100);
    auto other=initial.add_ship(9,{14000,0},100);
    initial.enable_individual_workforce();
    const auto ore=initial.total_ore();
    std::string committed;
    {
      PersistentWorld world(db,std::move(initial));
      Request req{98001,7,npc,player_ship,0,Kind::OwnerDistress};
      CHECK(dispatch(world,{98001,8,npc,player_ship,0,Kind::OwnerDistress})==Status::WrongOwner);
      CHECK(dispatch(world,{98001,7,npc,other,0,Kind::OwnerDistress})==Status::NoAuthority);
      CHECK(dispatch(world,{98001,7,npc,other,0,Kind::LawfulIntercept})==Status::NoAuthority);
      CHECK(dispatch(world,{98001,7,npc,player_ship,0,Kind::Enforcement})==Status::NoAuthority);
      CHECK(dispatch(world,{98001,7,npc,player_ship,0,Kind::PlayerRecovery})==Status::NoAuthority);
      CHECK(!physically_arrived(world.world(),req));
      const auto before=world.world().canonical_state();
      bool failed=false;
      try{(void)dispatch(world,req,FailPoint::AfterSnapshotWrite);}
      catch(const std::exception&){failed=true;}
      CHECK(failed && world.world().canonical_state()==before);
      CHECK(dispatch(world,req)==Status::Accepted);
      CHECK(dispatch(world,req)==Status::Refused);
      CHECK(world.world().npc_flight_orders().at(98001).purpose==NpcFlightPurpose::Rescue);
      CHECK(world.world().total_ore()==ore);
      bool arrived=false;
      for(int n=0;n<400;n++){
        world.step();
        if(physically_arrived(world.world(),req)){arrived=true;break;}
      }
      CHECK(arrived);
      CHECK(world.world().npc_flight_orders().at(98001).phase==NpcFlightPhase::Arrived);
      CHECK(world.world().total_ore()==ore);
      CHECK(world.world().npc_records().size()==6);
      CHECK(world.world().colony_ship(npc)->crew==6);
      committed=world.world().canonical_state();
      CHECK(Simulation::from_snapshot(committed).canonical_state()==committed);
    }
    {
      PersistentWorld recovered(db,Simulation(90103));
      CHECK(recovered.world().canonical_state()==committed);
      CHECK(recovered.world().total_ore()==ore);
      CHECK(recovered.world().npc_flight_orders().at(98001).phase==NpcFlightPhase::Arrived);
    }
    fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
    std::cout<<"PASS NPC-RESP-01: owner dispatch, no unauthorized enforcement, original pursuit and SQLite recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL NPC-RESP-01: "<<e.what()<<'\n';
    fs::remove(db);fs::remove(db+"-wal");fs::remove(db+"-shm");
    return 1;
 }
}
