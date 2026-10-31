#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CHECK ")+ #x+" @"+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=action;
    c.amount=amount;c.thrust_x=selector;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
  fs::path temp=fs::temp_directory_path()/("b70e_farm_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try{
    fs::create_directories(temp);
    const auto db=(temp/"colony.sqlite").string();
    const auto copy=(temp/"recover.sqlite").string();
    EntityId ship=0,site=0;
    std::int64_t origin=0;
    {
      Simulation s(707);ship=s.add_colony_ship(7,{0,0},100,24);
      s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
      const auto asteroid=s.add_asteroid({2000,0},50);
      origin=s.total_material_kg();
      PersistentWorld w(db,std::move(s));
      CHECK(send(w,ship,1,Action::Mine,asteroid,40).result==Outcome::Applied);
      CHECK(send(w,ship,2,Action::RefineOre,0,40).result==Outcome::Applied);
      CHECK(send(w,ship,3,Action::FabricateModule,0,20).result==Outcome::Applied);
      CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
      CHECK(w.world().built_outposts().size()==1);
      site=w.world().built_outposts().begin()->first;
      CHECK(w.world().farms().empty());
      CHECK(send(w,ship,5,Action::ConstructFarm,site,1,0,8).result==Outcome::Unauthorized);
      CHECK(send(w,ship,6,Action::ConstructFarm,site).result==Outcome::Applied);
      CHECK(w.world().farm(site)->embodied_ore==4);
      CHECK(w.world().colony_ship(ship)->modules==14);
      CHECK(send(w,ship,7,Action::ConstructFarm,site).result==Outcome::InvalidTarget);
      CHECK(send(w,ship,8,Action::AssignFarmWorkers,site,8).result==Outcome::NoResources);
      // Explicit ship-to-outpost inventory transfers: kg, no teleportation.
      CHECK(send(w,ship,9,Action::TransferFarmStock,site,200,1).result==Outcome::Applied); // food
      CHECK(send(w,ship,10,Action::TransferFarmStock,site,900,2).result==Outcome::Applied); // water
      CHECK(send(w,ship,11,Action::TransferFarmStock,site,200,3).result==Outcome::Applied); // oxygen
      CHECK(send(w,ship,12,Action::TransferFarmStock,site,200,4).result==Outcome::Applied); // nutrients
      CHECK(send(w,ship,13,Action::TransferFarmStock,site,200,5).result==Outcome::Applied); // captured CO2
      CHECK(send(w,ship,14,Action::AssignFarmWorkers,site,8).result==Outcome::Applied);
      CHECK(w.world().colony_ship(ship)->crew==16);
      CHECK(w.world().farm(site)->workers==8);
      CHECK(send(w,ship,15,Action::AssignFarmWorkers,site,1).result==Outcome::NoResources);
      CHECK(w.world().total_material_kg()==origin);
      // Workers farm without new player commands. Food and O2 are made from real inputs.
      w.steps(100);
      const auto* farm=w.world().farm(site);
      CHECK(farm && farm->production_cycles>=3 && farm->life.harvested_food_kg>72);
      CHECK(farm->life.cycles>=3 && farm->life.shortages==0);
      CHECK(w.world().colony_ship(ship)->crew+farm->workers==24);
      const auto assessment=w.world().assess_colony(ship);
      CHECK(assessment.crew_total==24 && assessment.farm_workers==8);
      CHECK(assessment.has_operational_farm && assessment.recent_food_production_kg>0);
      CHECK(assessment.renewable_food_proven);
      CHECK(assessment.stage==ColonyViability::Stage::Stable);
      CHECK(!assessment.autonomous_growth_ready); // no invented full self-sufficiency
      CHECK(w.world().total_material_kg()==origin);
      // Can retrieve physical production, not duplicating it.
      const auto food_before=w.world().life_support(ship)->food_kg;
      const auto farm_before=w.world().farm(site)->life.food_kg;
      CHECK(send(w,ship,16,Action::TransferFarmStock,site,-20,1).result==Outcome::Applied);
      CHECK(w.world().life_support(ship)->food_kg==food_before+20);
      CHECK(w.world().farm(site)->life.food_kg==farm_before-20);
      CHECK(w.world().total_material_kg()==origin);
      auto bad=Command{};bad.id=17;bad.issuer=7;bad.execute_tick=w.world().tick()+1;
      bad.actor=ship;bad.target=site;bad.action=Action::TransferFarmStock;
      bad.amount=10;bad.thrust_x=6;CHECK(!w.submit(bad));
      bad.thrust_x=1;bad.amount=0;CHECK(!w.submit(bad));
      // Travel away. The stationed crew and their facility do not follow the ship.
      CHECK(send(w,ship,18,Action::Thrust,0,0,1).result==Outcome::Applied);
      w.steps(30);
      CHECK(w.world().find(ship)->position.x>2500);
      const auto before=w.world().farm(site)->life.harvested_food_kg;
      w.steps(40);
      CHECK(w.world().farm(site)->life.harvested_food_kg>before);
      CHECK(send(w,ship,19,Action::TransferFarmStock,site,1,1).result==Outcome::OutOfRange);
      CHECK(w.world().total_material_kg()==origin);
      CHECK(w.world().canonical_state().rfind("galaxy-kernel-v11|",0)==0);
      CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
      PersistentWorld::recover_copy(db,copy);
    }
    {
      PersistentWorld reopened(db,Simulation(909));
      CHECK(reopened.world().colony_ship(ship)->crew==16);
      CHECK(reopened.world().farm(site)->workers==8);
      CHECK(reopened.world().total_material_kg()==origin);
      CHECK(reopened.world().farm(site)->production_cycles>=3);
      reopened.steps(20);CHECK(reopened.world().total_material_kg()==origin);
    }
    {
      PersistentWorld recovery(copy,Simulation(919));
      CHECK(recovery.world().total_material_kg()==origin);
      CHECK(recovery.world().farm(site)->workers==8);
    }
    fs::remove_all(temp);
    std::cout<<"B7.0e farm construction, staffed farming, growth, local logistics, reassessment, recovery: PASS\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"B7.0e FAILED: "<<e.what()<<"\n";
    fs::remove_all(temp);return 1;
  }
}
