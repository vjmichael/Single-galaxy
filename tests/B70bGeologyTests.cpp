#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define REQUIRE(v) do{if(!(v))throw std::runtime_error("Check: " #v " at line "+std::to_string(__LINE__));}while(0)
static Event exec(PersistentWorld& p, EntityId ship,std::uint64_t id,Action a,EntityId target=0,std::int64_t amount=1,int tx=0){
    Command cmd{};cmd.id=id;cmd.sequence=id;cmd.execute_tick=p.world().tick()+1;cmd.issuer=7;cmd.actor=ship;
    cmd.target=target;cmd.amount=amount;cmd.thrust_x=tx;cmd.action=a;
    REQUIRE(p.submit(cmd));p.step();return p.world().events().back();
}
int main(){
 try{
    auto db=fs::temp_directory_path()/("b70b_geology_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    std::uint64_t expected=0;EntityId rock=0,ship=0;
    {
      Simulation start(0x700B);ship=start.add_colony_ship(7,{0,0},60,24);
      auto small=start.add_asteroid({500,0},40);
      rock=start.add_geological_asteroid({72000,0},AsteroidClass::Carbonaceous,60,2000);
      const auto silicate=start.add_geological_asteroid({200000,80000},AsteroidClass::Silicate,80,2900);
      const auto metal=start.add_geological_asteroid({-160000,-30000},AsteroidClass::Metallic,55,5600);
      REQUIRE(start.entities().size()==5);
      REQUIRE(start.built_outposts().empty());
      for(auto [id,type]:{std::pair<EntityId,AsteroidClass>{rock,AsteroidClass::Carbonaceous},
                {silicate,AsteroidClass::Silicate},{metal,AsteroidClass::Metallic}}){
          auto* g=start.geology(id);REQUIRE(g && g->taxonomy==type && g->original_tonnes==start.find(id)->ore);
          int sum=0;for(auto m:mineral_profile(type))sum+=m.permille;REQUIRE(sum==1000);
          REQUIRE(g->original_tonnes>100000);
      }
      REQUIRE(start.canonical_state().rfind("galaxy-kernel-v8|",0)==0);
      REQUIRE(Simulation::from_snapshot(start.canonical_state()).canonical_state()==start.canonical_state());
      auto bad=start.canonical_state();auto pos=bad.find("geology|"+std::to_string(rock)+"|0|");
      REQUIRE(pos!=std::string::npos);bad.replace(pos+std::string("geology|"+std::to_string(rock)).size()+1,1,"9");
      bool rejected=false;try{(void)Simulation::from_snapshot(bad);}catch(const std::exception&){rejected=true;}
      REQUIRE(rejected);
      expected=start.total_ore();
      PersistentWorld p(db.string(),std::move(start));
      REQUIRE(exec(p,ship,1,Action::Mine,rock,2).result==Outcome::OutOfRange);
      REQUIRE(exec(p,ship,2,Action::Mine,small,8).transferred==8);
      REQUIRE(exec(p,ship,3,Action::RefineOre,0,8).result==Outcome::Applied);
      REQUIRE(exec(p,ship,4,Action::FabricateModule,0,4).result==Outcome::Applied);
      REQUIRE(exec(p,ship,5,Action::ConstructOutpost).result==Outcome::Applied);
      REQUIRE(p.world().built_outposts().size()==1);
      const auto view=encode_frame(ServerViewBridge(p.world(),31337,7).snapshot());
      REQUIRE(view.find("SGVIEW|1|")==0 && view.find("|2|7|")!=std::string::npos);
      for(int n=0;n<8;++n)REQUIRE(exec(p,ship,10+n,Action::Thrust,0,0,1).result==Outcome::Applied);
      auto mined=exec(p,ship,19,Action::Mine,rock,12);
      REQUIRE(mined.result==Outcome::Applied && mined.transferred==12);
      REQUIRE(p.world().geology(rock)->original_tonnes==expected - 40 - p.world().find(silicate)->ore - p.world().find(metal)->ore); // constant geological origin
      REQUIRE(p.world().total_ore()==expected);
      REQUIRE(p.world().find(rock)->ore==p.world().geology(rock)->original_tonnes-12);
      REQUIRE(Simulation::from_snapshot(p.world().canonical_state()).state_hash()==p.world().state_hash());
    }
    {
      Simulation ignored(7);ignored.add_colony_ship(7,{0,0},60,24);
      PersistentWorld restored(db.string(),std::move(ignored));
      REQUIRE(restored.world().total_ore()==expected);
      REQUIRE(restored.world().geology(rock) && restored.world().find(rock)->ore==restored.world().geology(rock)->original_tonnes-12);
      REQUIRE(restored.world().built_outposts().size()==1);
    }
    fs::remove(db);
    std::cout<<"PASS B7.0b large scientific mineral profiles, conservation, surface range, owned outpost projection, persistent save\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL B7.0b geology: "<<e.what()<<'\n';return 1;}
}
