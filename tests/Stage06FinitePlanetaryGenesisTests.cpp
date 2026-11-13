// S6-C / v127: finite original-world planetary genesis and regional
// physical source placement. Source-staged, NOT a native pass until executed.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetarySeasonalClimate.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace galaxy;
static const char* s6c_stage="initialization";
#define CHECK(x) do{if(!(x))throw std::runtime_error("S6C line "+std::to_string(__LINE__)+": " #x);}while(false)
static void cleanup(const std::string& name) {
    for(const char* suffix:{"","-wal","-shm"}) {
        std::error_code ec;fs::remove(name+suffix,ec);
    }
}
static std::uint64_t first_planet(const Simulation& sim) {
    for(const auto& [system,bodies]:sim.physical_celestial_systems()) {
        (void)system;
        for(const auto& b:bodies)
            if(b.kind==CelestialKind::Planet || b.kind==CelestialKind::Moon)
                return b.id;
    }
    throw std::runtime_error("no original physical planetary body");
}
static std::pair<std::uint16_t,std::uint16_t> land(
    const PlanetaryGenesisBody& gen,bool skip_first=false) {
    const auto map=planetary::generate_watersheds(gen.key,gen.envelope);
    int skipped=0;
    for(int i=0;i<planetary::kWatershedCells;++i)
        if(!map.cells[i].ocean) {
            if(skip_first && !skipped++)continue;
            return {std::uint16_t(i/planetary::kWatershedColumns),
                    std::uint16_t(i%planetary::kWatershedColumns)};
        }
    throw std::runtime_error("no terrain available for source placement");
}
static bool rejects(const std::function<void()>& fn) {
    try {fn();return false;}
    catch(const std::exception&) {return true;}
}
static void seeded(const std::string& db) {
    cleanup(db);
    std::uint64_t body=0,ship=0;
    std::int64_t previous_mass=0;
    std::pair<std::uint16_t,std::uint16_t> first{},second{};
    std::string initial_world;
    {
        Simulation seed(777,1);
        ship=seed.add_colony_ship(7,{0,0},50,5);
        seed.provision_starter_exploration_scanner(ship);
        seed.ensure_physical_celestial_systems();
        // v127 may only seal a physically anchored original galaxy.
        seed.enable_native_galactic_physics({
            {1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
        body=first_planet(seed);
        s6c_stage="planetary-manifest";
        seed.ensure_planetary_genesis_manifest();
        CHECK(seed.planetary_genesis().count(body));
        CHECK(seed.planetary_genesis().at(body).original_kg==
              (std::array<std::int64_t,4>{}));
        previous_mass=seed.total_ore();
        CHECK(rejects([&]{
            seed.provision_planetary_body_genesis(
                body,{0,0,-1000,0});}));
        CHECK(rejects([&]{
            seed.provision_planetary_body_genesis(
                9999999,{1000,0,0,0});}));
        // No split before original finite stock. Four physical goods
        // classes have independent mass and their own immutable identity.
        s6c_stage="funded-original-body";
        seed.provision_planetary_body_genesis(
            body,{4000,6000,8000,2000});
        CHECK(seed.total_ore()==previous_mass+20);
        CHECK(rejects([&]{
            seed.provision_planetary_body_genesis(
                body,{1000,0,0,0});}));
        const auto& gen=seed.planetary_genesis().at(body);
        s6c_stage="first-land";
        first=land(gen);
        s6c_stage="second-land";
        second=land(gen,true);
        CHECK(first!=second);
        CHECK(rejects([&]{
            seed.place_planetary_region_genesis(
                body,first.first,first.second,{5000,0,0,0});}));
        s6c_stage="first-regional-source";
        seed.place_planetary_region_genesis(
            body,first.first,first.second,{3000,4000,5000,1000});
        CHECK(seed.total_ore()==previous_mass+20);
        CHECK(rejects([&]{
            seed.place_planetary_region_genesis(
                body,first.first,first.second,{1000,0,0,0});}));
        s6c_stage="second-regional-source";
        seed.place_planetary_region_genesis(
            body,second.first,second.second,{1000,2000,3000,1000});
        CHECK(seed.total_ore()==previous_mass+20);
        CHECK(seed.planetary_genesis().at(body).unallocated_kg==
              (std::array<std::int64_t,4>{}));
        CHECK(seed.planetary_region_sources().size()==2);
        CHECK(!seed.scan_planetary_region_source(
            8,ship,body,first.first,first.second));
        const auto observed=seed.scan_planetary_region_source(
            7,ship,body,first.first,first.second);
        CHECK(observed && observed->stock_kg[0]==3000 &&
              observed->stock_kg[1]==4000 &&
              observed->stock_kg[2]==5000 &&
              observed->stock_kg[3]==1000);
        CHECK(!seed.scan_planetary_region_source(7,ship,body,36,0));
        // Choose a genuine unallocated land cell, not a column that
        // could accidentally coincide with the second funded region.
        const auto map=planetary::generate_watersheds(gen.key,gen.envelope);
        bool checked_unallocated=false;
        for(int i=0;i<planetary::kWatershedCells;++i){
            if(map.cells[i].ocean)continue;
            const auto candidate=std::pair<std::uint16_t,std::uint16_t>{
                std::uint16_t(i/planetary::kWatershedColumns),
                std::uint16_t(i%planetary::kWatershedColumns)};
            if(candidate==first || candidate==second)continue;
            CHECK(!seed.scan_planetary_region_source(
                7,ship,body,candidate.first,candidate.second));
            checked_unallocated=true;
            break;
        }
        CHECK(checked_unallocated);
        initial_world=seed.canonical_state();
        s6c_stage="canonical-reparse";
        const auto direct=Simulation::from_snapshot(initial_world);
        CHECK(direct.canonical_state()==initial_world);
        CHECK(direct.total_ore()==seed.total_ore());
        // Direct identity tampering and per-region mass alteration must
        // fail even if a caller supplies a superficially canonical string.
        auto bad=initial_world;
        const auto start=bad.find("planetresource|");
        CHECK(start!=std::string::npos);
        const auto end=bad.find('\n',start);
        CHECK(end!=std::string::npos);
        const auto tail=bad.rfind('|',end);
        CHECK(tail!=std::string::npos && tail>start);
        bad.replace(tail+1,end-tail-1,"2000"); // old volatile 1000
        CHECK(rejects([&]{(void)Simulation::from_snapshot(bad);}));
        s6c_stage="original-SQLite-open";
        PersistentWorld world(db,std::move(seed));
        CHECK(world.world().total_ore()==previous_mass+20);
        const auto generation=world.generation();
        world.ensure_planetary_genesis_manifest();
        CHECK(world.generation()==generation); // strict idempotency
        world.steps(2);
        CHECK(world.world().total_ore()==previous_mass+20);
        CHECK(rejects([&]{
            auto copy=world.world();
            copy.provision_planetary_body_genesis(
                body,{1000,0,0,0});}));
        CHECK(rejects([&]{
            auto copy=world.world();
            copy.place_planetary_region_genesis(
                body,first.first,first.second,{1000,0,0,0});}));
    }
    {
        PersistentWorld world(db,Simulation(777,1));
        CHECK(world.world().total_ore()==previous_mass+20);
        CHECK(world.world().planetary_region_sources().size()==2);
        CHECK(world.world().planetary_genesis().at(body).unallocated_kg==
              (std::array<std::int64_t,4>{}));
        CHECK(world.world().scan_planetary_region_source(
            7,ship,body,second.first,second.second).has_value());
        const auto h=world.world().state_hash();
        world.ensure_planetary_genesis_manifest();
        CHECK(world.world().state_hash()==h);
    }
    cleanup(db);
}
static void migration(const std::string& db) {
    s6c_stage="historical-migration";
    cleanup(db);
    // An old unanchored save must refuse v127, not invent a galaxy frame
    // from an arbitrary coordinate or manufacture original matter.
    const auto unanchored=db+".unanchored";
    cleanup(unanchored);
    {
        Simulation old(810,1);
        old.add_colony_ship(7,{0,0},50,5);
        old.ensure_physical_celestial_systems();
        PersistentWorld missing_origin(unanchored,std::move(old));
        const auto hash=missing_origin.world().state_hash();
        const auto generation=missing_origin.generation();
        CHECK(rejects([&]{missing_origin.ensure_planetary_genesis_manifest();}));
        CHECK(missing_origin.world().state_hash()==hash);
        CHECK(missing_origin.generation()==generation);
    }
    cleanup(unanchored);
    std::int64_t baseline=0;
    std::string historical;
    {
        Simulation legacy(810,1);
        legacy.add_colony_ship(7,{0,0},50,5);
        legacy.ensure_physical_celestial_systems();
        legacy.enable_native_galactic_physics({
            {1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
        CHECK(legacy.planetary_genesis().empty());
        PersistentWorld original(db,std::move(legacy));
        original.steps(3);
        baseline=original.world().total_ore();
        historical=original.world().canonical_state();
        CHECK(historical.find("planetgenesis|")==std::string::npos);
        const auto old_generation=original.generation();
        CHECK(rejects([&]{
            original.ensure_planetary_genesis_manifest(
                FailPoint::AfterSnapshotWrite);}));
        CHECK(original.generation()==old_generation);
        CHECK(original.world().canonical_state()==historical);
        original.ensure_planetary_genesis_manifest();
        CHECK(original.generation()==old_generation+1);
        CHECK(!original.world().planetary_genesis().empty());
        CHECK(original.world().planetary_region_sources().empty());
        CHECK(original.world().total_ore()==baseline);
        for(const auto& [body,gen]:original.world().planetary_genesis()) {
            (void)body;
            CHECK(gen.original_kg==(std::array<std::int64_t,4>{}));
            CHECK(gen.unallocated_kg==(std::array<std::int64_t,4>{}));
        }
        const auto one=original.generation();
        original.ensure_planetary_genesis_manifest();
        CHECK(original.generation()==one);
    }
    {
        PersistentWorld reopened(db,Simulation(810,1));
        CHECK(reopened.world().total_ore()==baseline);
        CHECK(!reopened.world().planetary_genesis().empty());
        CHECK(reopened.world().planetary_region_sources().empty());
    }
    cleanup(db);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::string db=argv[1];
    try {
        seeded(db);
        migration(db);
        std::cout<<"S6-C original finite planetary genesis + v127 migration source PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"S6-C staged native regression failed at "
                 <<s6c_stage<<": "<<e.what()<<'\n';
        cleanup(db);
        return 1;
    }
}
