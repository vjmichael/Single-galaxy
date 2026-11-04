#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY04: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static Command make(PersistentWorld& w,std::uint64_t id,EntityId ship,Action action,
                    std::int64_t amount=0,EntityId target=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=action;c.amount=amount;return c;
}
static void applied(PersistentWorld& w,const Command& c){
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    CHECK(w.world().events().back().result==Outcome::Applied);
}
int main(){
  const auto dir=fs::temp_directory_path()/
    ("sg_play04_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto copy=(dir/"copy.sqlite").string();
    EntityId ship=0,rock=0,base=0;
    {
      Simulation seed(904);ship=seed.add_colony_ship(7,{0,0},60,24);
      rock=seed.add_asteroid({1800,0},40);
      PersistentWorld w(db,std::move(seed));
      // Native yaw: no world-axis motion, signed 15-degree turn, full wrap.
      applied(w,make(w,1,ship,Action::Thrust,-15));
      CHECK(w.world().find(ship)->heading_deg==345);
      CHECK(w.world().find(ship)->velocity.x==0);
      applied(w,make(w,2,ship,Action::Thrust,15));
      CHECK(w.world().find(ship)->heading_deg==0);
      applied(w,make(w,3,ship,Action::Thrust,15));
      CHECK(w.world().find(ship)->heading_deg==15);
      auto forged=make(w,4,ship,Action::Thrust,15,0,8);
      CHECK(w.submit(forged));w.step();
      CHECK(w.world().events().back().result==Outcome::Unauthorized);
      CHECK(w.world().find(ship)->heading_deg==15);
      auto invalid=make(w,5,ship,Action::Thrust,60);
      CHECK(w.submit(invalid));w.step();
      CHECK(w.world().events().back().result==Outcome::InvalidArgument);
      CHECK(w.world().find(ship)->heading_deg==15);
      CHECK(w.world().total_ore()==40);
      // Existing industry creates an actual owned base, not a preseeded station.
      applied(w,make(w,10,ship,Action::Mine,24,rock));
      applied(w,make(w,11,ship,Action::RefineOre,16));
      applied(w,make(w,12,ship,Action::FabricateModule,4));
      applied(w,make(w,13,ship,Action::ConstructOutpost,1));
      CHECK(w.world().built_outposts().size()==1);
      base=w.world().built_outposts().begin()->first;
      CHECK(w.world().find(base)->owner_id==7);
      CHECK(w.world().find(ship)->cargo==8);
      CHECK(w.world().total_ore()==40);
    }
    {
      PersistentWorld w(db,Simulation(1));
      CHECK(w.world().find(ship)->heading_deg==15);
      CHECK(w.world().find(ship)->cargo==8);
      CHECK(w.world().find(base)->owner_id==7);
      applied(w,make(w,20,ship,Action::RequestDock,0,base));
      w.steps(Simulation::kDockTransitionTicks+1);
      CHECK(w.world().find(ship)->dock_state==DockState::Docked);
      applied(w,make(w,21,ship,Action::UnloadOre,8,base));
      CHECK(w.world().find(ship)->cargo==0);
      CHECK(w.world().hangar(base,7)->ore==8);
      CHECK(w.world().total_ore()==40);
      applied(w,make(w,22,ship,Action::LoadOre,3,base));
      CHECK(w.world().find(ship)->cargo==3);
      CHECK(w.world().hangar(base,7)->ore==5);
      CHECK(w.world().total_ore()==40);
      PersistentWorld::recover_copy(db,copy);
    }
    {
      PersistentWorld w(copy,Simulation(2));
      CHECK(w.world().find(ship)->heading_deg==15);
      CHECK(w.world().find(ship)->dock_state==DockState::Docked);
      CHECK(w.world().find(ship)->cargo==3);
      CHECK(w.world().hangar(base,7)->ore==5);
      CHECK(w.world().total_ore()==40);
      applied(w,make(w,30,ship,Action::RequestUndock));
      w.steps(Simulation::kDockTransitionTicks+1);
      CHECK(w.world().find(ship)->dock_state==DockState::InSpace);
      CHECK(w.world().find(ship)->heading_deg==15);
    }
    fs::remove_all(dir);
    std::cout<<"PASS PLAY-04 persistent yaw, authenticated physical docking, bulk cargo custody, recovery and undock\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FAIL PLAY-04: "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);return 1;
  }
}
