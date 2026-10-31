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
static Event command(PersistentWorld& p,EntityId ship,std::uint64_t id,Action action,std::int64_t amount=1,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=p.world().tick()+1;
 c.action=action;c.actor=ship;c.issuer=issuer;c.amount=amount;
 CHECK(p.submit(c));p.step();return p.world().events().back();
}
int main(){
 try {
  const auto base=fs::temp_directory_path()/("sg_b70d_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  const auto db=(base/"colony.sqlite").string();
  const auto recovery=(base/"recovery.sqlite").string();
  EntityId ship=0;std::int64_t start_mass=0;
  {
    Simulation s(707);ship=s.add_colony_ship(7,{0,0},60,24);
    const auto carbon=s.add_geological_asteroid({2000,0},AsteroidClass::Carbonaceous,70,1700);
    s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    CHECK(s.canonical_state().rfind("galaxy-kernel-v10|",0)==0);
    CHECK(s.life_support(ship)->food_kg==1000);
    CHECK(s.entities().size()==2 && s.built_outposts().empty());
    CHECK(Simulation::from_snapshot(s.canonical_state()).canonical_state()==s.canonical_state());
    start_mass=s.total_material_kg();
    PersistentWorld p(db,std::move(s));
    // The player can supply a hydroponic batch but cannot produce food from thin air.
    CHECK(command(p,ship,1,Action::PlantCrops,20,8).result==Outcome::Unauthorized);
    CHECK(command(p,ship,2,Action::HarvestCrops,5).result==Outcome::NoResources);
    CHECK(command(p,ship,3,Action::PlantCrops,20).result==Outcome::Applied);
    CHECK(p.world().life_support(ship)->crops_kg==60);
    CHECK(p.world().life_support(ship)->oxygen_kg==1020);
    CHECK(command(p,ship,4,Action::HarvestCrops,20).result==Outcome::NoResources);
    CHECK(p.world().total_material_kg()==start_mass);
    p.steps(5);
    CHECK(command(p,ship,5,Action::HarvestCrops,20).result==Outcome::Applied);
    CHECK(p.world().life_support(ship)->food_kg==1040);
    CHECK(p.world().life_support(ship)->compost_kg==20);
    CHECK(command(p,ship,6,Action::RecycleCompost,20).result==Outcome::Applied);
    CHECK(p.world().life_support(ship)->nutrients_kg==1000);
    CHECK(command(p,ship,7,Action::RecycleWater,1).result==Outcome::NoResources);
    CHECK(command(p,ship,8,Action::ProduceOxygen,5).result==Outcome::Applied);
    CHECK(p.world().life_support(ship)->hydrogen_kg==10);
    // Three 20-tick population cycles: food/water/oxygen consumed, no mass deleted.
    p.steps(60);
    const auto* l=p.world().life_support(ship);
    CHECK(l->cycles>=3 && l->consecutive_supplied_cycles>=3 && l->shortages==0);
    CHECK(l->wastewater_kg>=72 && l->organic_waste_kg>=72 && l->co2_kg>=72);
    CHECK(p.world().assess_colony(ship).stage==ColonyViability::Stage::Stable);
    CHECK(!p.world().assess_colony(ship).autonomous_growth_ready);
    CHECK(command(p,ship,9,Action::RecycleWater,70).result==Outcome::Applied);
    CHECK(p.world().life_support(ship)->recovered_water_kg==63);
    // Real geological input: carbonaceous rock -> serpentine -> 1.1 tonnes
    // recovered water -> 1 tonne delivered to actual life-support tank.
    CHECK(command(p,ship,10,Action::Mine,20,7).result==Outcome::InvalidTarget); // no destination
    Command mine{};mine.id=11;mine.sequence=11;mine.execute_tick=p.world().tick()+1;
    mine.issuer=7;mine.actor=ship;mine.action=Action::Mine;mine.target=carbon;mine.amount=20;
    CHECK(p.submit(mine));p.step();CHECK(p.world().events().back().result==Outcome::Applied);
    Command refine=mine;refine.id=12;refine.sequence=12;refine.execute_tick=p.world().tick()+1;
    refine.action=Action::RefineMineral;refine.target=1;refine.amount=11000;
    CHECK(p.submit(refine));p.step();CHECK(p.world().events().back().result==Outcome::Applied);
    CHECK(command(p,ship,13,Action::LoadLifeWater,1).result==Outcome::Applied);
    CHECK(p.world().colony_ship(ship)->goods_kg.at("water")==100);
    CHECK(p.world().life_support(ship)->water_kg>3000);
    CHECK(p.world().total_material_kg()==start_mass);
    // Tampering with an aggregate need record is rejected by strict canonical parser.
    auto snapshot=p.world().canonical_state();
    auto altered=snapshot; auto ix=altered.find("lifesupport|");CHECK(ix!=std::string::npos);
    const auto val=altered.find('|',ix+12);CHECK(val!=std::string::npos);
    altered.replace(val+1,1,"-");
    bool rejected=false;try{(void)Simulation::from_snapshot(altered);}catch(...){rejected=true;}
    CHECK(rejected);
    // Command validation blocks forged targets, negative mass, and absurd batches.
    Command bad{};bad.id=17;bad.sequence=17;bad.execute_tick=p.world().tick()+1;
    bad.issuer=7;bad.actor=ship;bad.action=Action::PlantCrops;bad.amount=1;bad.target=ship;
    CHECK(!p.submit(bad));bad.target=0;bad.amount=10001;CHECK(!p.submit(bad));
    PersistentWorld::recover_copy(db,recovery);
  }
  {
    PersistentWorld recovered(db,Simulation(999));
    CHECK(recovered.world().total_material_kg()==start_mass);
    const auto* l=recovered.world().life_support(ship);CHECK(l);
    CHECK(l->recovered_water_kg==63);
    CHECK(l->harvested_food_kg==40);
    CHECK(recovered.world().assess_colony(ship).stage==ColonyViability::Stage::Stable);
    PersistentWorld restored(recovery,Simulation(888));
    CHECK(restored.world().canonical_state()==recovered.world().canonical_state());
    CHECK(restored.world().total_material_kg()==start_mass);
  }
  // Isolated starvation test: shortages change survival stage, but no magic rescue
  // or instantaneous NPC deaths (demography is future work).
  {
    Simulation s(4);const auto id=s.add_colony_ship(7,{0,0},20,24);
    s.provision_colony_genesis(id,100,1000,900,1000,1000);
    const auto mass=s.total_material_kg();
    s.steps(100);
    CHECK(s.life_support(id)->shortages>=1);
    CHECK(s.assess_colony(id).stage==ColonyViability::Stage::Survival);
    CHECK(s.colony_ship(id)->crew==24);
    CHECK(s.total_material_kg()==mass);
    // A recovered colony is not permanently trapped in survival merely because
    // the historical shortage counter is nonzero.
    Command plant{};plant.id=91;plant.sequence=91;plant.execute_tick=s.tick()+1;
    plant.issuer=7;plant.actor=id;plant.action=Action::PlantCrops;plant.amount=100;
    CHECK(s.submit(plant));s.step();CHECK(s.events().back().result==Outcome::Applied);
    s.steps(5);
    Command harvest=plant;harvest.id=92;harvest.sequence=92;
    harvest.execute_tick=s.tick()+1;harvest.action=Action::HarvestCrops;
    CHECK(s.submit(harvest));s.step();CHECK(s.events().back().result==Outcome::Applied);
    s.steps(60);
    CHECK(s.life_support(id)->shortages>=1);
    CHECK(s.assess_colony(id).stage==ColonyViability::Stage::Stable);
    CHECK(s.total_material_kg()==mass);
  }
  // Queued B7.0d command recovers before its first execution.
  {
    Simulation s(4);const auto id=s.add_colony_ship(7,{0,0},20,24);
    Command queued{};queued.id=77;queued.sequence=77;queued.issuer=7;
    queued.actor=id;queued.execute_tick=1;queued.action=Action::PlantCrops;queued.amount=1;
    CHECK(s.submit(queued));
    auto state=s.canonical_state();CHECK(state.rfind("galaxy-kernel-v10|",0)==0);
    auto loaded=Simulation::from_snapshot(state);CHECK(loaded.canonical_state()==state);
    loaded.step();CHECK(loaded.events().back().result==Outcome::NotColonyShip);
  }
  fs::remove_all(base);
  std::cout<<"PASS B7.0d agriculture, crew consumption, reuse, conservation, stages, rollback, restart, recovery\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL B7.0d "<<e.what()<<'\n';return 1;}
}
