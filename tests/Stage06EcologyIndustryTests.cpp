// S6-E/v130: original physical ecology, crop-material conservation, actual
// finite-source water/soil and original-world SQLite migration. These tests
// are staged, not native-accepted until executed on Windows and Linux.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetaryGenesisBootstrap.hpp"
#include "galaxy/PlanetaryEcology.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error("S6-E native line "+std::to_string(__LINE__)+": " #x);}while(false)
static bool rejects(const std::function<void()>& fn){
    try {fn();return false;}catch(const std::exception&) {return true;}
}
static void cleanup(const std::string& path){
    for(const auto suffix:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+suffix,ec);
    }
}
static void assessment(){
    using namespace galaxy::planetary;
    PhysicalEnvironment physical{};
    physical.water_kg={{2000,1000,1000,0}};
    physical.soil_kg={{15000,1000}};
    verify_physical_environment(physical,4000,16000);
    auto crop=cultivation_budget(&physical,true,false,0);
    CHECK(crop.mode==CropMode::Field);
    CHECK(crop.water_kg==10 && crop.nutrients_kg==5 &&
          crop.food_kg==12 && crop.wastewater_kg==3 &&
          crop.electricity_units==0);
    physical.hazard=PhysicalHazard::Drought;
    CHECK(cultivation_budget(&physical,true,true,2).mode==CropMode::Blocked);
    crop=cultivation_budget(&physical,true,true,3);
    CHECK(crop.mode==CropMode::Controlled && crop.food_kg==10 &&
          crop.wastewater_kg==5 && crop.electricity_units==3);
    CHECK(crop.food_kg+crop.wastewater_kg==crop.water_kg+crop.nutrients_kg);
    physical.hazard=PhysicalHazard::Cold;
    CHECK(habitat_hvac_energy(physical.hazard)==3);
    CHECK(cultivation_budget(&physical,true,false,100).mode==CropMode::Blocked);
    CHECK(cultivation_budget(&physical,true,true,3).mode==CropMode::Controlled);
    physical.hazard=PhysicalHazard::Heat;
    CHECK(habitat_hvac_energy(physical.hazard)==3);
    CHECK(cultivation_budget(&physical,true,true,3).mode==CropMode::Controlled);
    physical.hazard=PhysicalHazard::Flood;
    CHECK(surface_work_hazard(physical.hazard));
    CHECK(cultivation_budget(&physical,true,true,120).mode==CropMode::Blocked);
    // Depleted or eroded physical topsoil does not magically regrow.
    physical.hazard=PhysicalHazard::None;
    physical.soil_kg={{0,16000}};
    verify_physical_environment(physical,4000,16000);
    CHECK(!can_harvest_open_field(&physical,true));
    CHECK(cultivation_budget(&physical,true,false,120).mode==CropMode::Blocked);
    crop=cultivation_budget(&physical,true,true,2);
    CHECK(crop.mode==CropMode::Controlled && crop.food_kg==11 &&
          crop.wastewater_kg==4 && crop.electricity_units==2);
    CHECK(crop.food_kg+crop.wastewater_kg==15);
    // The real site-local finite SOIL inventory becomes a physical growing
    // medium. It reduces utility costs and improves recovery but creates
    // neither additional site soil nor extra mass.
    crop=cultivation_budget(nullptr,false,true,1,true);
    CHECK(crop.mode==CropMode::Controlled && crop.electricity_units==1 &&
          crop.food_kg==12 && crop.wastewater_kg==3);
    CHECK(crop.water_kg+crop.nutrients_kg==
          crop.food_kg+crop.wastewater_kg);
    // Unfunded local geological deposits do not become crops or water.
    CHECK(cultivation_budget(nullptr,true,false,120).mode==CropMode::Blocked);
    CHECK(cultivation_budget(nullptr,true,true,1).mode==CropMode::Blocked);
    CHECK(cultivation_budget(nullptr,true,true,2).mode==CropMode::Controlled);
}
static void recovery(const std::string& db){
    cleanup(db);
    Simulation seed(613,1);
    seed.add_colony_ship(7,{0,0},50,5);
    seed.ensure_physical_celestial_systems();
    seed.ensure_planetary_genesis_manifest();
    seed.enable_planetary_source_custody();
    CHECK(!seed.planetary_ecology_enabled());
    const auto amount=seed.total_ore();
    const auto previous=seed.canonical_state();
    CHECK(previous.find("galaxy-kernel-v129|")==0);
    {
        PersistentWorld world(db,std::move(seed));
        const auto generation=world.generation();
        CHECK(rejects([&]{world.ensure_planetary_ecology(
            FailPoint::AfterSnapshotWrite);}));
        CHECK(world.generation()==generation);
        CHECK(world.world().canonical_state()==previous);
        world.ensure_planetary_ecology();
        CHECK(world.generation()==generation+1);
        CHECK(world.world().planetary_ecology_enabled());
        CHECK(world.world().total_ore()==amount);
        world.ensure_planetary_ecology();
        CHECK(world.generation()==generation+1);
        const auto installed=world.world().canonical_state();
        CHECK(installed.find("galaxy-kernel-v130|")==0);
        CHECK(Simulation::from_snapshot(installed).canonical_state()==installed);
        // An invented ecological site cannot be used to authorize crops.
        auto forged=installed;
        const auto end=forged.find("system|");
        CHECK(end!=std::string::npos);
        forged.insert(end,"planetecology|4294967300|17|36|1|1|0|0|0|0\n");
        CHECK(rejects([&]{(void)Simulation::from_snapshot(forged);}));
        world.steps(80);
        CHECK(world.world().total_ore()==amount);
    }
    {
        PersistentWorld reopened(db,Simulation(613,1));
        CHECK(reopened.world().planetary_ecology_enabled());
        CHECK(reopened.world().total_ore()==amount);
    }
    cleanup(db);
    // New worlds can provision ONE original geological source and start at
    // v130, with neither ecological crops nor new labor materialized.
    Simulation new_world(777,1);
    const auto ship=new_world.add_colony_ship(7,{0,0},50,5);
    new_world.provision_starter_exploration_scanner(ship);
    (void)stage6::provision_first_planet_seed(new_world);
    CHECK(new_world.planetary_ecology_enabled());
    CHECK(new_world.canonical_state().find("galaxy-kernel-v130|")==0);
    const auto genesis_mass=new_world.total_ore();
    CHECK(Simulation::from_snapshot(new_world.canonical_state()).total_ore()==genesis_mass);
    new_world.steps(40);
    CHECK(new_world.total_ore()==genesis_mass);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try {
        assessment();
        recovery(argv[1]);
        std::cout<<"S6-E physical ecology, crop conservation and original SQLite PASS\n";
        return 0;
    } catch(const std::exception& e){
        std::cerr<<"S6-E ecological industry FAILED: "<<e.what()<<'\n';
        cleanup(argv[1]);
        return 1;
    }
}
