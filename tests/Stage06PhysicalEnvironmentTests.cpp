// S6-D/v128: original finite phase conservation and durable SQLite upgrade.
// Source-staged regression. Native results must be recorded separately.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetaryPhysicalProcesses.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error("S6D line "+std::to_string(__LINE__)+": " #x);}while(false)
static bool denies(const std::function<void()>& f){
    try{f();return false;}catch(const std::exception&){return true;}
}
static void cleanup(const std::string& f){
    for(const auto suffix:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(f+suffix,ec);
    }
}
static void local_processes(){
    using namespace galaxy::planetary;
    SeasonalCell weather{};
    auto physical=initial_physical_environment(6000,12000,20,0);
    CHECK(physical.water_kg[0]==6000 && physical.soil_kg[0]==12000);
    weather.temperature_c=-45;weather.snowfall=80;
    advance_physical_environment(physical,weather,false,20,6000,12000);
    CHECK(physical.hazard==PhysicalHazard::Cold);
    CHECK(physical.water_kg[1]>0);
    weather.temperature_c=25;weather.precipitation=160;
    weather.evaporation=40;weather.environmental_risk=3;
    advance_physical_environment(physical,weather,false,40,6000,12000);
    CHECK(physical.water_kg[2]>0); // local soil infiltration
    CHECK(physical.soil_kg[1]>0); // real sediment displaced from topsoil
    CHECK(physical.hazard==PhysicalHazard::Flood);
    const auto previous=physical;
    CHECK(denies([&]{
        advance_physical_environment(physical,weather,false,40,6000,12000);}));
    CHECK(physical.updated_tick==previous.updated_tick);
    CHECK(denies([&]{verify_physical_environment(physical,6001,12000);}));
    weather.environmental_risk=0;
    advance_physical_environment(physical,weather,false,60,6000,12000);
    CHECK(physical.soil_kg[1]<=previous.soil_kg[1]);
    CHECK(physical.updated_tick==60);
    CHECK(denies([&]{
        auto altered=physical;altered.water_kg[1]+=100;
        verify_physical_environment(altered,6000,12000);
    }));
    // D6: runoff, groundwater and physically retained sediment must
    // debit the same exact v127 funded source, including negative fixtures.
    auto upstream=initial_physical_environment(6000,12000,20,0);
    auto downstream=initial_physical_environment(100,500,20,0);
    upstream.water_kg={{4000,0,2000,0}};
    upstream.soil_kg={{9000,3000}};
    upstream.hazard=PhysicalHazard::Flood;
    std::array<std::int64_t,4> source{{6000,12000,0,0}};
    std::array<std::int64_t,4> destination{{100,500,0,0}};
    const auto flow=propose_neighbor_flux(upstream,downstream,source,
        destination,true,true);
    CHECK(flow.liquid_kg>0 && flow.aquifer_kg>0 &&
          flow.sediment_kg>0);
    CHECK(propose_neighbor_flux(upstream,downstream,source,
        destination,false,true).empty());
    const auto prior_water=source[0]+destination[0];
    const auto prior_soil=source[1]+destination[1];
    apply_neighbor_flux(upstream,downstream,source,destination,flow);
    CHECK(source[0]+destination[0]==prior_water);
    CHECK(source[1]+destination[1]==prior_soil);
    verify_physical_environment(upstream,source[0],source[1]);
    verify_physical_environment(downstream,destination[0],destination[1]);
    CHECK(denies([&]{
        apply_neighbor_flux(upstream,downstream,source,destination,
            NeighborPhysicalFlux{1000000,0,0});
    }));
    auto dry=initial_physical_environment(0,0,40,0);
    weather={};weather.temperature_c=40;
    advance_physical_environment(dry,weather,false,20,0,0);
    CHECK(dry.water_kg==(std::array<std::int64_t,4>{}));
    CHECK(dry.soil_kg==(std::array<std::int64_t,2>{}));
    CHECK(dry.hazard==PhysicalHazard::Drought);
}
static void world_processes(const std::string& db){
    cleanup(db);
    Simulation sim(777,1);
    const auto ship=sim.add_colony_ship(7,{0,0},50,5);
    sim.provision_starter_exploration_scanner(ship);
    sim.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    sim.ensure_planetary_genesis_manifest();
    std::uint64_t body=0;
    std::uint16_t latitude=0,longitude=0;
    for(const auto& [id,gen]:sim.planetary_genesis()){
        if(!gen.envelope.water_coverage)continue;
        const auto map=planetary::generate_watersheds(gen.key,gen.envelope);
        for(int i=0;i<planetary::kWatershedCells;++i){
            if(map.cells[i].ocean)continue;
            body=id;latitude=std::uint16_t(i/planetary::kWatershedColumns);
            longitude=std::uint16_t(i%planetary::kWatershedColumns);break;
        }
        if(body)break;
    }
    CHECK(body);
    const auto before=sim.total_ore();
    sim.provision_planetary_body_genesis(body,{6000,12000,16000,4000});
    sim.place_planetary_region_genesis(body,latitude,longitude,
                                      {6000,12000,16000,4000});
    const auto mass=sim.total_ore();
    CHECK(mass==before+38);
    CHECK(!sim.scan_planetary_physical_environment(7,ship,body,latitude,longitude));
    sim.enable_physical_planetary_environment();
    CHECK(sim.total_ore()==mass);
    CHECK(sim.physical_planetary_environment_enabled());
    const auto phase=sim.scan_planetary_physical_environment(
        7,ship,body,latitude,longitude);
    CHECK(phase.has_value());
    CHECK(!sim.scan_planetary_physical_environment(8,ship,body,latitude,longitude));
    planetary::verify_physical_environment(*phase,6000,12000);
    CHECK(denies([&]{
        sim.place_planetary_region_genesis(body,latitude,longitude,{1000,0,0,0});}));
    const auto unchanged=sim.canonical_state();
    sim.enable_physical_planetary_environment();
    CHECK(sim.canonical_state()==unchanged);
    {
        // Create the SQLite world at original tick zero, then advance its
        // journal. Never adopt a nonzero-tick transient simulation as genesis.
        PersistentWorld world(db,std::move(sim));
        world.steps(80);
    CHECK(world.world().total_ore()==mass);
    const auto observed=world.world().scan_planetary_physical_environment(
        7,ship,body,latitude,longitude);
    CHECK(observed && observed->updated_tick==80);
    planetary::verify_physical_environment(*observed,6000,12000);
    const auto canonical=world.world().canonical_state();
    CHECK(canonical.find("galaxy-kernel-v128|")==0);
    CHECK(Simulation::from_snapshot(canonical).canonical_state()==canonical);
    auto altered=canonical;
    const auto begin=altered.find("planetprocess|");
    CHECK(begin!=std::string::npos);
    const auto end=altered.find('\n',begin);
    CHECK(end!=std::string::npos);
    const auto delimiter=altered.find('|',begin+14); // body -> revision
    CHECK(delimiter<end);
    // Missing physical process row must always be rejected.
    altered.erase(begin,end-begin+1);
    CHECK(denies([&]{(void)Simulation::from_snapshot(altered);}));
        // The same original writer owns the full physical environment test.
        CHECK(world.world().total_ore()==mass);
        const auto generation=world.generation();
        world.ensure_physical_planetary_environment(); // exact idempotent
        CHECK(world.generation()==generation);
        world.steps(21);
        CHECK(world.world().total_ore()==mass);
        const PlanetarySiteKey key{body,latitude,longitude,1};
        CHECK(world.world().planetary_physical_environment().at(key).updated_tick==100);
    }
    {
        PersistentWorld reopened(db,Simulation(777,1));
        CHECK(reopened.world().physical_planetary_environment_enabled());
        CHECK(reopened.world().total_ore()==mass);
        const PlanetarySiteKey key{body,latitude,longitude,1};
        CHECK(reopened.world().planetary_physical_environment().at(key).updated_tick==100);
    }
    cleanup(db);
    Simulation historical(900,1);
    historical.add_colony_ship(7,{0,0},30,3);
    historical.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    historical.ensure_planetary_genesis_manifest(); // historical zero geology
    const auto oldMass=historical.total_ore();
    {
        PersistentWorld world(db,std::move(historical));
        const auto original=world.world().canonical_state();
        const auto generation=world.generation();
        CHECK(denies([&]{
            world.ensure_physical_planetary_environment(
                FailPoint::AfterSnapshotWrite);}));
        CHECK(world.world().canonical_state()==original);
        CHECK(world.generation()==generation);
        world.ensure_physical_planetary_environment();
        CHECK(world.generation()==generation+1);
        CHECK(world.world().total_ore()==oldMass);
        CHECK(world.world().planetary_physical_environment().empty());
    }
    {
        PersistentWorld reopened(db,Simulation(900,1));
        CHECK(reopened.world().physical_planetary_environment_enabled());
        CHECK(reopened.world().total_ore()==oldMass);
    }
    cleanup(db);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        local_processes();
        world_processes(argv[1]);
        std::cout<<"Stage06-D source physical processes and SQLite migration PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Stage06-D regression FAILED: "<<e.what()<<'\n';
        cleanup(argv[1]);return 1;
    }
}
