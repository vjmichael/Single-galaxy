#include "galaxy/WO4ResourceDemand.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error( \
    "WO4-A original native line "+std::to_string(__LINE__)+": " #x); } while(0)
static void applied(PersistentWorld& w,EntityId ship,std::uint64_t id,
                    Action action,EntityId target=0,std::int64_t amount=1,
                    int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.target=target;c.action=action;c.amount=amount;
    c.thrust_x=selector;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();
    CHECK(!w.world().events().empty());
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
int main(){
 try {
    using namespace economy04;
    // Pure forecasts over already held kg: never create stock by planning.
    const auto pressured=calculate(2,{1,3,11},Policy{4,1});
    CHECK(pressured.has_value());
    CHECK((*pressured)[0].vital==Vital::Food &&
          (*pressured)[0].pressure==Pressure::CriticalCurrentWindow &&
          (*pressured)[0].reserve_target_kg==8 &&
          (*pressured)[0].reserve_deficit_kg==7);
    CHECK((*pressured)[1].pressure==Pressure::BelowReserve &&
          (*pressured)[1].complete_windows_covered==1);
    CHECK((*pressured)[2].pressure==Pressure::MeetsReserve &&
          (*pressured)[2].buffer_above_target_kg==3);
    const auto empty=calculate(0,{50,60,70});
    CHECK(empty.has_value());
    for(const auto& need:*empty)
        CHECK(need.pressure==Pressure::NoCurrentConsumers &&
              need.reserve_deficit_kg==0 &&
              need.buffer_above_target_kg==0);
    CHECK(!calculate(1,{0,-1,0}));
    CHECK(!calculate(1,{0,0,0},Policy{0,1}));
    CHECK(!calculate(1,{0,0,0},Policy{1,0}));
    CHECK(!calculate(std::numeric_limits<std::uint64_t>::max(),
                     {0,0,0},Policy{4,1}));
    CHECK(!calculate(2,{0,0,0},Policy{33,1}));

    const auto folder=fs::temp_directory_path()/
        ("wo4_demand_"+std::to_string(std::chrono::steady_clock::now().
                                     time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path path;~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{folder};
    const auto db=(folder/"original.sqlite").string();
    Simulation original(47004);
    const auto ship=original.add_colony_ship(7,{0,0},200,8);
    original.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto ore=original.add_asteroid({2000,0},120);
    original.enable_individual_workforce();
    const auto starting_mass=original.total_material_kg();
    PersistentWorld w(db,std::move(original));
    CHECK(!observe_owner_world(w.world(),8,ship));
    CHECK(!observe_owner_world(w.world(),7,999999));
    const auto baseline_hash=w.world().state_hash();
    const auto baseline=observe_owner_world(w.world(),7,ship);
    CHECK(baseline.has_value() &&
          baseline->compartments.size()==1 &&
          baseline->still_current(w.world()));
    const auto& onboard=baseline->compartments[0];
    CHECK(onboard.kind==CompartmentKind::ColonyShip);
    CHECK(onboard.physical_location==ship &&
          onboard.current_consumers==
             static_cast<std::uint64_t>(w.world().colony_ship(ship)->crew));
    CHECK(onboard.needs[0].physically_present_kg==
          w.world().life_support(ship)->food_kg);
    CHECK(onboard.needs[1].physically_present_kg==
          w.world().life_support(ship)->water_kg);
    CHECK(onboard.needs[2].physically_present_kg==
          w.world().life_support(ship)->oxygen_kg);
    CHECK(w.world().state_hash()==baseline_hash);
    CHECK(w.world().total_material_kg()==starting_mass);

    // Real construction changes the authoritative world. Merely observing
    // it does not transfer any provisions from the ship to the habitat.
    applied(w,ship,1,Action::Mine,ore,80);
    applied(w,ship,2,Action::RefineOre,0,80);
    applied(w,ship,3,Action::FabricateModule,0,25);
    applied(w,ship,4,Action::ConstructOutpost);
    CHECK(w.world().built_outposts().size()==1);
    const auto site=w.world().built_outposts().begin()->first;
    applied(w,ship,5,Action::ConstructHabitat,site);
    const auto new_hash=w.world().state_hash();
    CHECK(!baseline->still_current(w.world()));
    const auto built=observe_owner_world(w.world(),7,ship);
    CHECK(built && built->compartments.size()==2);
    const auto& awaiting=built->compartments[1];
    CHECK(awaiting.kind==CompartmentKind::OutpostHabitat &&
          awaiting.physical_location==site &&
          awaiting.current_consumers==0);
    for(const auto& need:awaiting.needs)
        CHECK(need.pressure==Pressure::NoCurrentConsumers);
    CHECK(w.world().state_hash()==new_hash);

    // Approved original stock transfers and real NPC resident assignment
    // can change the forecast, never the other way round.
    applied(w,ship,6,Action::TransferHabitatStock,site,16,1);
    applied(w,ship,7,Action::TransferHabitatStock,site,16,2);
    applied(w,ship,8,Action::TransferHabitatStock,site,16,3);
    applied(w,ship,9,Action::AssignHabitatResidents,site,2);
    const auto observed=observe_owner_world(w.world(),7,ship,Policy{4,1});
    CHECK(observed && observed->compartments.size()==2);
    const auto& resident=observed->compartments[1];
    CHECK(resident.current_consumers==2);
    CHECK(resident.needs[0].physically_present_kg==
          w.world().habitat(site)->life.food_kg);
    CHECK(resident.needs[0].reserve_target_kg==8);
    CHECK(resident.needs[0].pressure==Pressure::MeetsReserve);
    CHECK(resident.needs[0].buffer_above_target_kg==
          resident.needs[0].physically_present_kg-8);
    CHECK(!observe_owner_world(w.world(),7,ship,Policy{0,1}));
    const auto settled_hash=w.world().state_hash();
    CHECK(observed->still_current(w.world()));
    CHECK(w.world().state_hash()==settled_hash &&
          w.world().total_material_kg()==starting_mass);

    // Independent original SQLite copy: same physical inventory and
    // population imply the same derived demand lines on restart.
    const auto copied=(folder/"restored.sqlite").string();
    PersistentWorld::recover_copy(db,copied);
    PersistentWorld recovered(copied,Simulation(47005));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    const auto replay=observe_owner_world(recovered.world(),7,ship,Policy{4,1});
    CHECK(replay && replay->still_current(recovered.world()) &&
          replay->compartments.size()==observed->compartments.size());
    for(std::size_t i=0;i<replay->compartments.size();++i){
        const auto& a=replay->compartments[i];
        const auto& b=observed->compartments[i];
        CHECK(a.kind==b.kind && a.current_consumers==b.current_consumers &&
              a.physical_location==b.physical_location);
        for(std::size_t j=0;j<a.needs.size();++j){
            CHECK(a.needs[j].reserve_target_kg==b.needs[j].reserve_target_kg);
            CHECK(a.needs[j].physically_present_kg==b.needs[j].physically_present_kg);
            CHECK(a.needs[j].reserve_deficit_kg==b.needs[j].reserve_deficit_kg);
        }
    }
    CHECK(recovered.world().total_material_kg()==starting_mass);
    std::cout<<"WO4-A original inventory demand projection: OK\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<e.what()<<'\n';
    return 1;
 }
}
