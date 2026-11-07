#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    "WO3-B original native line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice {std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
static Choice choose() {
    // Same real astronomical catalogue as PLAY13D's native source-host
    // acceptance. No fake or manually invented planetary surface fixture.
    for(std::uint32_t sys=1;sys<=2000;++sys) {
        Simulation p(613,sys);
        const auto ship=p.add_colony_ship(7,{0,0},60,8);
        p.provision_starter_exploration_scanner(ship);
        p.ensure_physical_celestial_systems();
        for(const auto& b:p.physical_celestial_systems().at(sys)) {
            if(b.kind!=CelestialKind::Planet && b.kind!=CelestialKind::Moon)
                continue;
            for(const auto [latitude,longitude]:std::vector<std::pair<unsigned,unsigned>>{
                {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27},
                {26,52},{7,15},{28,8},{12,40},{24,55},{19,50},{15,64}}) {
                const auto region=p.scan_planetary_geography(
                    7,ship,b.id,std::uint16_t(latitude),std::uint16_t(longitude));
                if(region && !region->landscape.region.surface_water)
                    return {sys,b.id,std::uint16_t(latitude),
                            std::uint16_t(longitude)};
            }
        }
    }
    throw std::runtime_error("no legitimate buildable astronomical cell");
}
static void action(PersistentWorld& w,EntityId ship,std::uint64_t id,
                   Action kind,EntityId target=0,std::int64_t amount=1) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=kind;c.amount=amount;
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
int main() {
 try {
    const auto choice=choose();
    const PlanetarySiteKey key{choice.body,choice.lat,choice.lon,1};
    const auto dir=fs::temp_directory_path()/
        ("wo3_surface_"+std::to_string(std::chrono::steady_clock::now().
                                       time_since_epoch().count()));
    fs::create_directories(dir);
    const auto db=(dir/"original.sqlite").string();
    Simulation seed(613,choice.system);
    const auto ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.provision_starter_exploration_scanner(ship);
    const auto rock=seed.add_asteroid({2000,0},60);
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    const auto initial_mass=seed.total_ore();
    PersistentWorld w(db,std::move(seed));
    action(w,ship,1001,Action::Mine,rock,30);
    action(w,ship,1002,Action::RefineOre,0,30);
    action(w,ship,1003,Action::FabricateModule,0,8);
    CHECK(w.begin_celestial_transit(7,ship,choice.body,90001));
    w.steps(4);
    CHECK(w.complete_celestial_transit(90001));
    std::vector<std::uint64_t> roster;
    for(const auto& [id,n]:w.world().npc_records())
        if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
            roster.push_back(id);
    CHECK(roster.size()>=4);
    CHECK(w.build_planetary_site(7,ship,choice.body,
        choice.lat,choice.lon,roster[0],roster[1]));
    w.steps(4);
    CHECK(w.support_planetary_crew(7,ship,choice.body,choice.lat,choice.lon,
                                 false,10));
    CHECK(w.support_planetary_crew(7,ship,choice.body,choice.lat,choice.lon,
                                 true,10));
    w.steps(240);
    CHECK(w.world().planetary_spaceport_operational(choice.body,
                                                   choice.lat,choice.lon));
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    const auto before_house=colony03::assess_surface_supplies(
        w.world(),7,ship,key);
    CHECK(before_house.gate==colony03::SurfaceSupplyGate::NotYetHoused);
    CHECK(!before_house.independently_sustainable);
    CHECK(w.build_planetary_shuttle(7,ship,key));
    CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,roster[2]));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    CHECK(w.world().total_ore()==initial_mass);
    // A module not physically loaded on the shuttle cannot be constructed
    // from ship inventory on the planetary surface.
    CHECK(!w.transfer_planetary_housing_module(8,ship,key,true));
    bool failed_injected=false;
    try{
        (void)w.transfer_planetary_housing_module(7,ship,key,true,
                                                  FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){failed_injected=true;}
    CHECK(failed_injected);
    CHECK(w.world().planetary_shuttles().at(key).housing_module==0);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.world().planetary_shuttles().at(key).housing_module==1);
    const auto manifest=w.world().planetary_transport_manifest(key);
    CHECK(manifest && manifest->valid());
    CHECK(transport::contains_good(*manifest,transport::GoodKind::Equipment,
          transport::kPlanetaryHousingModuleEquipmentId,1));
    CHECK(w.world().total_ore()==initial_mass);
    const auto in_orbit=w.world().canonical_state();
    CHECK(in_orbit.rfind("galaxy-kernel-v109|",0)==0);
    CHECK(Simulation::from_snapshot(in_orbit).canonical_state()==in_orbit);
    CHECK(w.launch_planetary_shuttle(7,ship,key,81001,false));
    // Landing must be completed before material can enter the site store.
    CHECK(!w.transfer_planetary_housing_module(7,ship,key,false));
    w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    CHECK(w.world().planetary_sites().at(key).housing_modules==1);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    CHECK(w.launch_planetary_shuttle(7,ship,key,81002,true));
    w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    CHECK(w.launch_planetary_shuttle(7,ship,key,81003,false));
    w.steps(3);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    CHECK(w.world().planetary_shuttles().at(key).oxygen_kg==0);
    CHECK(w.world().planetary_sites().at(key).oxygen_kg>=30);
    // All three original supplies physically landed via the shuttle,
    // with no direct ship-to-surface provision shortcut.
    CHECK(w.world().planetary_sites().at(key).housing_modules==2);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(!w.start_planetary_housing(8,ship,key,81200));
    CHECK(w.start_planetary_housing(7,ship,key,81200));
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    CHECK(w.world().planetary_sites().at(key).housing_modules==0);
    CHECK(w.world().planetary_sites().at(key).housing_phase==1);
    CHECK(w.world().total_ore()==initial_mass);
    w.steps(10);
    CHECK(!w.set_planetary_housing_pause(8,ship,key,true));
    CHECK(w.set_planetary_housing_pause(7,ship,key,true));
    CHECK(!w.set_planetary_housing_pause(7,ship,key,true));
    const auto paused_progress=w.world().planetary_sites().at(key).housing_progress;
    w.steps(10);
    CHECK(w.world().planetary_sites().at(key).housing_progress==paused_progress);
    CHECK(w.world().planetary_sites().at(key).housing_stalled_ticks>=10);
    CHECK(w.set_planetary_housing_pause(7,ship,key,false));
    w.steps(10);
    CHECK(w.world().planetary_sites().at(key).housing_progress==20);
    const auto provisional=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(provisional).canonical_state()==provisional);
    const auto pending=(dir/"construction_pending.sqlite").string();
    PersistentWorld::recover_copy(db,pending);
    PersistentWorld recovered(pending,Simulation(3));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    w.steps(60);
    recovered.steps(60);
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    CHECK(w.world().planetary_sites().at(key).housing_phase==2);
    CHECK(w.world().planetary_sites().at(key).housing_progress==80);
    CHECK(w.world().planetary_sites().at(key).housing_completed_tick>0);
    CHECK(!w.set_planetary_housing_pause(7,ship,key,true));
    CHECK(w.world().total_ore()==initial_mass);
    // Existing original two named on-surface workers may become founders only
    // with trusted owner approval, explicit attested consent and housing.
    CHECK(!w.settle_planetary_founder(8,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[0],81300,false));
    CHECK(w.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[1],81300,true));
    CHECK(w.settle_planetary_founder(7,ship,key,roster[1],81301,true));
    CHECK(recovered.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(recovered.settle_planetary_founder(7,ship,key,roster[1],81301,true));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    CHECK((w.world().npc_records().at(roster[0]).legal_residence()==
        transport::Endpoint{transport::LocationKind::PlanetarySite,
                            w.world().planetary_sites().at(key).surface_location}));
    CHECK(w.world().total_ore()==initial_mass);
    const auto supplied=colony03::assess_surface_supplies(w.world(),7,ship,key);
    CHECK(supplied.gate==colony03::SurfaceSupplyGate::ImportDependent);
    CHECK(supplied.baseline_ten_tick_reserve_periods>=1);
    CHECK(!supplied.independent_power_proven &&
          !supplied.closed_water_loop_proven &&
          !supplied.independently_sustainable);
    const auto audit=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(audit && audit->permanent_housing_installed &&
          audit->housing_order_id==81200 && audit->unused_housing_capacity==2 &&
          audit->original_permanent_residents==2 &&
          audit->physical_delegates==2);
    // A direct tampered record cannot invent an impossible construction tick.
    auto corrupted=w.world().canonical_state();
    const auto first=corrupted.find("planetsite|");
    CHECK(first!=std::string::npos);
    const auto end=corrupted.find('\n',first);
    const auto field=corrupted.rfind('|',end);
    CHECK(field!=std::string::npos && field>first);
    corrupted.replace(field+1,end-field-1,"81300"); // duplicate consent receipt
    bool denied=false;
    try{(void)Simulation::from_snapshot(corrupted);}
    catch(const std::exception&){denied=true;}
    CHECK(denied);
    const auto final_copy=(dir/"housing_finished.sqlite").string();
    PersistentWorld::recover_copy(db,final_copy);
    PersistentWorld final_world(final_copy,Simulation(4));
    CHECK(final_world.world().state_hash()==w.world().state_hash());
    CHECK(final_world.world().total_ore()==initial_mass);
    fs::remove_all(dir);
    std::cout<<"WO3-B native original planetary module deliveries, "
                "staffed housing, tamper and SQLite recovery PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
