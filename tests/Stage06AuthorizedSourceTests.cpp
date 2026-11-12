// S6-D5/v129: original world geological source custody, no-minting,
// request preflight, strict canonical restore, and SQLite rollback.
// Source-staged: native execution and installed Windows acceptance remain OPEN.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetaryGenesisBootstrap.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error("S6D5 line "+std::to_string(__LINE__)+": " #x);}while(false)
static bool rejects(const std::function<void()>& fn){
    try{fn();return false;}catch(const std::exception&){return true;}
}
static void cleanup(const std::string& db){
    for(const auto suffix:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(db+suffix,ec);
    }
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string db=argv[1];cleanup(db);
    try {
        Simulation seed(777,1);
        const auto ship=seed.add_colony_ship(7,{0,0},50,5);
        seed.provision_starter_exploration_scanner(ship);
        CHECK(stage6::provision_first_planet_seed(seed));
        CHECK(seed.physical_planetary_environment_enabled());
        const auto sources=seed.planetary_region_sources();
        CHECK(sources.size()==1);
        const auto key=sources.begin()->first;
        const auto body=std::get<0>(key);
        const auto latitude=std::get<1>(key),longitude=std::get<2>(key);
        const auto source=sources.begin()->second;
        CHECK(source.stock_kg[0]==6000 && source.stock_kg[1]==12000 &&
              source.stock_kg[2]==16000 && source.stock_kg[3]==4000);
        const auto total=seed.total_ore();
        const auto prior=seed.canonical_state();
        CHECK(prior.find("galaxy-kernel-v130|")==0);
        CHECK(!seed.extract_planetary_regional_source(
            8,ship,body,latitude,longitude,PlanetaryResource::Water,
            0,1000,999,40001)); // wrong owner
        CHECK(!seed.extract_planetary_regional_source(
            7,ship,body,latitude,longitude,PlanetaryResource::Water,
            1,1000,999,40002)); // ice is NOT automatically liquid
        CHECK(!seed.extract_planetary_regional_source(
            7,ship,body,latitude,longitude,PlanetaryResource::Soil,
            0,1000,999,40003)); // no installed source machine/NPC
        CHECK(!seed.extract_planetary_regional_source(
            7,ship,body,latitude,longitude,PlanetaryResource::Mineral,
            0,500,999,40004)); // not whole tonnes
        CHECK(seed.canonical_state()==prior && seed.total_ore()==total);
        CHECK(seed.planetary_source_transfers().empty());
        const auto direct=Simulation::from_snapshot(prior);
        CHECK(direct.canonical_state()==prior && direct.total_ore()==total);
        // Forge an immutable transfer without the original existing site.
        auto forged=prior;
        const auto insert=forged.find("planetprocess|");
        CHECK(insert!=std::string::npos);
        forged.insert(insert,
          "planetrelease|40001|7|"+std::to_string(ship)+"|999|"+
          std::to_string(body)+"|1|"+std::to_string(latitude)+"|"+
          std::to_string(longitude)+"|0|0|1000|1|0\n");
        CHECK(rejects([&]{(void)Simulation::from_snapshot(forged);}));
        {
            PersistentWorld world(db,std::move(seed));
            const auto generation=world.generation();
            world.ensure_planetary_source_custody();
            CHECK(world.generation()==generation);
            CHECK(world.world().total_ore()==total);
            world.steps(25);
            CHECK(world.world().total_ore()==total);
            CHECK(world.world().planetary_source_transfers().empty());
        }
        {
            PersistentWorld reopened(db,Simulation(777,1));
            CHECK(reopened.world().total_ore()==total);
            CHECK(reopened.world().canonical_state().find("galaxy-kernel-v130|")==0);
            CHECK(reopened.world().planetary_source_transfers().empty());
        }
        cleanup(db);
        // Historical v128 world: zero-created mass; exactly one original
        // generation CAS, idempotent repeat and injected rollback.
        Simulation historical(900,1);
        historical.add_colony_ship(7,{0,0},30,3);
        historical.ensure_planetary_genesis_manifest();
        historical.enable_physical_planetary_environment();
        const auto old_mass=historical.total_ore();
        {
            PersistentWorld world(db,std::move(historical));
            const auto before=world.world().canonical_state();
            const auto generation=world.generation();
            CHECK(rejects([&]{world.ensure_planetary_source_custody(
                FailPoint::AfterSnapshotWrite);}));
            CHECK(world.generation()==generation);
            CHECK(world.world().canonical_state()==before);
            world.ensure_planetary_source_custody();
            CHECK(world.generation()==generation+1);
            CHECK(world.world().total_ore()==old_mass);
            world.ensure_planetary_source_custody();
            CHECK(world.generation()==generation+1);
        }
        {
            PersistentWorld reopened(db,Simulation(900,1));
            CHECK(reopened.world().total_ore()==old_mass);
            CHECK(reopened.world().canonical_state().find("galaxy-kernel-v129|")==0);
        }
        cleanup(db);
        std::cout<<"Stage06-D5 source custody, denial and SQLite recovery PASS\n";
        return 0;
    } catch(const std::exception& e){
        std::cerr<<"Stage06-D5 source regression FAILED: "<<e.what()<<'\n';
        cleanup(db);
        return 1;
    }
}
