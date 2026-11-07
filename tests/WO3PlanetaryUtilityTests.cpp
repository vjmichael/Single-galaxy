#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    "WO3-C native line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice {std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
static Choice choose() {
    for(std::uint32_t sys=1;sys<=2000;++sys) {
        Simulation p(613,sys);
        const auto ship=p.add_colony_ship(7,{0,0},200,8);
        p.provision_starter_exploration_scanner(ship);
        p.ensure_physical_celestial_systems();
        for(const auto& b:p.physical_celestial_systems().at(sys)) {
            if(b.kind!=CelestialKind::Planet && b.kind!=CelestialKind::Moon)continue;
            for(const auto [lat,lon]:std::vector<std::pair<unsigned,unsigned>>{
                 {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},
                 {10,27},{26,52},{7,15},{28,8},{12,40},{24,55}}) {
                const auto region=p.scan_planetary_geography(
                    7,ship,b.id,std::uint16_t(lat),std::uint16_t(lon));
                if(region && !region->landscape.region.surface_water)
                    return {sys,b.id,std::uint16_t(lat),std::uint16_t(lon)};
            }
        }
    }
    throw std::runtime_error("missing real buildable site");
}
static void act(PersistentWorld& w,EntityId ship,std::uint64_t id,Action kind,
                EntityId target=0,std::int64_t amount=1,int opt=0) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=kind;
    c.amount=amount;c.thrust_x=opt;
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
static void down(PersistentWorld& w,EntityId ship,const PlanetarySiteKey& key,
                 std::uint64_t id) {
    CHECK(w.launch_planetary_shuttle(7,ship,key,id,false));w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
}
static void up(PersistentWorld& w,EntityId ship,const PlanetarySiteKey& key,
               std::uint64_t id) {
    CHECK(w.launch_planetary_shuttle(7,ship,key,id,true));w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
}
int main() {
 try {
    const auto chosen=choose();
    const PlanetarySiteKey key{chosen.body,chosen.lat,chosen.lon,1};
    const auto folder=fs::temp_directory_path()/
        ("wo3_utility_"+std::to_string(std::chrono::steady_clock::now().
                                      time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{folder};
    const auto db=(folder/"world.sqlite").string();
    Simulation seed(613,chosen.system);
    const auto ship=seed.add_colony_ship(7,{0,0},200,8);
    seed.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    seed.provision_starter_exploration_scanner(ship);
    const auto bulk=seed.add_asteroid({2000,0},150);
    const auto silicate=seed.add_geological_asteroid({1000,0},
        AsteroidClass::Silicate,30,2500);
    seed.enable_individual_workforce();
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    const auto initial_mass=seed.total_ore();
    PersistentWorld w(db,std::move(seed));
    act(w,ship,1,Action::Mine,bulk,80);
    act(w,ship,2,Action::RefineOre,0,80);
    act(w,ship,3,Action::FabricateModule,0,20);
    act(w,ship,4,Action::ConstructOutpost);
    const auto orbital=w.world().built_outposts().begin()->first;
    act(w,ship,6,Action::ConstructWorkshop,orbital);
    act(w,ship,8,Action::ConstructUtility,orbital);
    act(w,ship,9,Action::ConstructHabitat,orbital);
    for(int i=1;i<=3;++i)act(w,ship,9+i,Action::TransferHabitatStock,orbital,100,i);
    act(w,ship,13,Action::AssignHabitatResidents,orbital,2);
    act(w,ship,14,Action::AssignUtilityTechnicians,orbital,1);
    act(w,ship,15,Action::TransferUtilitySpares,orbital,5);
    act(w,ship,16,Action::SetUtilityMaintenance,orbital,1);
    act(w,ship,19,Action::Mine,silicate,30);
    act(w,ship,20,Action::RefineMineral,10,12000);
    act(w,ship,21,Action::TransferWorkshopFeed,orbital,1000);
    auto slots=workforce02::plan_original_workshop(w.world(),7,ship,orbital);
    CHECK(slots[0].can_offer_assignment());
    auto operator_id=slots[0].suggested_npc;
    auto assignment=workforce02::approved_original_assignment(
        w.world(),7,ship,orbital,false,operator_id,22,true);
    CHECK(assignment && w.submit(*assignment));w.step();
    CHECK(w.world().events().back().result==Outcome::Applied);
    act(w,ship,24,Action::ConstructWorkshopFinisher,orbital);
    w.steps(8);
    CHECK(w.world().workshop(orbital)->panels_kg>=3);
    slots=workforce02::plan_original_workshop(w.world(),7,ship,orbital);
    const auto builder=slots[1].suggested_npc;
    CHECK(builder && builder!=operator_id);
    for(std::uint64_t id=280;id<283;++id) {
        auto training=workforce02::approved_original_trade_training(
            w.world(),7,ship,orbital,builder,WorkshopTrade::Shape,id,true);
        CHECK(training && w.submit(*training));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    }
    CHECK(w.world().workshop_trade_certified(builder,WorkshopTrade::Shape));
    act(w,ship,290,Action::AssignUtilityTechnicians,orbital,0);
    act(w,ship,291,Action::AssignHabitatResidents,orbital,-2);
    CHECK(w.world().npc_records().at(builder).location==ship);
    CHECK(w.world().npc_available_for_duty(builder));
    CHECK(w.begin_celestial_transit(7,ship,chosen.body,90001));
    w.steps(4);CHECK(w.complete_celestial_transit(90001));
    std::vector<std::uint64_t> crew;
    for(const auto& [id,n]:w.world().npc_records())
        if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
            crew.push_back(id);
    CHECK(crew.size()>=4);
    const auto partner=*std::find_if(crew.begin(),crew.end(),
                                   [builder](auto id){return id!=builder;});
    CHECK(w.build_planetary_site(7,ship,chosen.body,chosen.lat,chosen.lon,
                                builder,partner));
    w.steps(4);
    CHECK(w.support_planetary_crew(7,ship,chosen.body,chosen.lat,chosen.lon,false,10));
    CHECK(w.support_planetary_crew(7,ship,chosen.body,chosen.lat,chosen.lon,true,10));
    w.steps(240);
    CHECK(w.world().planetary_spaceport_operational(
        chosen.body,chosen.lat,chosen.lon));
    CHECK(w.build_planetary_shuttle(7,ship,key));
    const auto pilot=*std::find_if(crew.begin(),crew.end(),[&](auto id){
        return id!=builder && id!=partner;
    });
    CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,pilot));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    // Deliver actual first two construction modules on two original trips.
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    down(w,ship,key,81001);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    up(w,ship,key,81002);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    down(w,ship,key,81003);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    for(auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,30));
    CHECK(w.start_planetary_housing(7,ship,key,81200));
    w.steps(80);
    CHECK(w.world().planetary_sites().at(key).housing_phase==2);
    CHECK(!w.start_planetary_surface_utility(8,ship,key,81400,builder));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,pilot));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,builder));
    // Two MORE manufactured modules must physically reach the existing site.
    up(w,ship,key,81004);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    down(w,ship,key,81005);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    up(w,ship,key,81006);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    down(w,ship,key,81007);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    for(auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,30));
    CHECK(w.world().planetary_sites().at(key).housing_modules==2);
    CHECK(w.world().total_ore()==initial_mass);
    bool failed=false;
    try {
        (void)w.start_planetary_surface_utility(
            7,ship,key,81400,builder,FailPoint::AfterSnapshotWrite);
    } catch(const std::exception&) {failed=true;}
    CHECK(failed);
    CHECK(w.world().planetary_sites().at(key).utility_phase==0);
    CHECK(w.start_planetary_surface_utility(7,ship,key,81400,builder));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,builder));
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v110|",0)==0);
    w.steps(10);
    CHECK(w.pause_planetary_surface_utility(7,ship,key,true));
    const auto progress=w.world().planetary_sites().at(key).utility_progress;
    w.steps(7);
    CHECK(w.world().planetary_sites().at(key).utility_progress==progress);
    CHECK(w.pause_planetary_surface_utility(7,ship,key,false));
    w.steps(10);
    const auto pending=(folder/"utility_pending.sqlite").string();
    PersistentWorld::recover_copy(db,pending);
    PersistentWorld recovered(pending,Simulation(8));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    w.steps(40);recovered.steps(40);
    CHECK(w.world().state_hash()==recovered.world().state_hash());
    CHECK(w.world().planetary_sites().at(key).utility_phase==2);
    w.steps(60);
    const auto& utility=w.world().planetary_sites().at(key);
    CHECK(utility.utility_progress==60);
    CHECK(utility.utility_powered_cycles>0 && utility.hvac_cycles>0);
    CHECK(utility.water_recycled_kg>0 && utility.brine_kg>0);
    CHECK(utility.oxygen_recycled_kg>0);
    CHECK(w.world().total_ore()==initial_mass);
    const auto supply=colony03::assess_surface_supplies(w.world(),7,ship,key);
    CHECK(supply.independent_power_proven && supply.hvac_ever_operated &&
          supply.loss_bearing_recycling_operated &&
          !supply.closed_water_loop_proven &&
          !supply.independently_sustainable);
    const auto saved=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
    const auto copy=(folder/"utility_final.sqlite").string();
    PersistentWorld::recover_copy(db,copy);
    PersistentWorld reread(copy,Simulation(9));
    CHECK(reread.world().state_hash()==w.world().state_hash());
    std::cout<<"WO3-C/D original qualified surface utility and SQLite PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
