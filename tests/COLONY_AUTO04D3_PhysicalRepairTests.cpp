#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(v) do{if(!(v))throw std::runtime_error(std::string("AUTO04D3R: ")+ #v+" line "+std::to_string(__LINE__));}while(false)
namespace {
Event command(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
              EntityId target=0,std::int64_t amount=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;
    c.actor=ship;c.action=action;c.target=target;c.amount=amount;
    c.thrust_x=selector;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();
    return w.world().events().back();
}
}
int main(){
try{
    const auto file=(std::filesystem::temp_directory_path()/
      ("sg_auto04d3_repair_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    EntityId site=0;
    std::int64_t material=0;
    {
        Simulation genesis(70043);
        const auto ship=genesis.add_colony_ship(7,{0,0},100,24);
        genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto rock=genesis.add_asteroid({2000,0},70);
        material=genesis.total_material_kg();
        PersistentWorld w(file,std::move(genesis));
        CHECK(command(w,ship,1,Action::Mine,rock,60).result==Outcome::Applied);
        CHECK(command(w,ship,2,Action::RefineOre,0,60).result==Outcome::Applied);
        CHECK(command(w,ship,3,Action::FabricateModule,0,30).result==Outcome::Applied);
        CHECK(command(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
        site=w.world().built_outposts().begin()->first;
        CHECK(command(w,ship,5,Action::ConstructUtility,site).result==Outcome::Applied);
        CHECK(command(w,ship,6,Action::ConstructHabitat,site).result==Outcome::Applied);
        CHECK(command(w,ship,7,Action::TransferHabitatStock,site,25,1).result==Outcome::Applied);
        CHECK(command(w,ship,8,Action::TransferHabitatStock,site,100,2).result==Outcome::Applied);
        CHECK(command(w,ship,9,Action::TransferHabitatStock,site,100,3).result==Outcome::Applied);
        CHECK(command(w,ship,10,Action::AssignHabitatResidents,site,3).result==Outcome::Applied);
        CHECK(command(w,ship,11,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
        CHECK(command(w,ship,12,Action::TransferUtilitySpares,site,2).result==Outcome::Applied);
        CHECK(!w.world().utility(site)->delegated_maintenance);
        CHECK(w.world().utility(site)->spare_modules==2);
        CHECK(w.world().total_material_kg()==material);
        const auto previous=w.world().utility(site)->repairs;
        w.steps(165-w.world().tick()); // risk evaluated before each 20-tick consumption cycle
        const auto* u=w.world().utility(site);
        std::cerr<<"AUTO04D3R trace: tick="<<w.world().tick()
            <<" food="<<w.world().habitat(site)->life.food_kg
            <<" water="<<w.world().habitat(site)->life.water_kg
            <<" oxygen="<<w.world().habitat(site)->life.oxygen_kg
            <<" supplied="<<w.world().habitat(site)->life.consecutive_supplied_cycles
            <<" risk="<<static_cast<int>(colony_safety::assess(w.world(),ship,site).risk)
            <<" condition="<<u->condition<<" repairs="<<u->repairs
            <<" spares="<<u->spare_modules<<" techs="<<u->technicians
            <<std::endl;
        CHECK(u->repairs==previous+1);  // actually executed using physical module
        CHECK(u->delegated_repairs==0); // owner never enabled delegated policy
        CHECK(u->spare_modules==1);     // physical module consumed
        CHECK(u->embodied_ore==6);      // installed module remains in world mass
        CHECK(w.world().total_material_kg()==material);
        CHECK(w.world().colony_ship(ship)->crew==21); // labor reassigned not spawned
        CHECK(w.world().habitat(site)->residents==3);
        const auto snapshot=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
    }
    {
        PersistentWorld w(file,Simulation(22));
        CHECK(w.world().utility(site)->repairs==1);
        CHECK(w.world().utility(site)->delegated_repairs==0);
        CHECK(w.world().utility(site)->spare_modules==1);
        CHECK(w.world().total_material_kg()==material);
    }
    std::filesystem::remove(file);
    std::filesystem::remove(file+"-wal");
    std::filesystem::remove(file+"-shm");
    std::cout<<"PASS emergency physical repair overrides player preference, consumes delivered spares and preserves crew/material\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
