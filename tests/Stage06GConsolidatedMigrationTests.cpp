// S6-G original-world integration regression. Uses the real PersistentWorld,
// canonical SQLite migration transactions, finite source seed and physical
// clock; NO Godot substitute, synthetic inventory or offline time catch-up.
// Register as a native CTest; source existence is NOT test execution.
#include "galaxy/Stage6ConsolidatedAdmission.hpp"
#include "galaxy/PlanetaryGenesisBootstrap.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    "S6-G original-native line "+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
bool rejects(const std::function<void()>& fn) {
    try {fn();return false;}catch(const std::exception&) {return true;}
}
void cleanup(const std::string& db) {
    for(const char* suffix:{"","-wal","-shm","-journal"}) {
        std::error_code ec;fs::remove(fs::u8path(db+suffix),ec);
    }
}
void historical(const std::string& db) {
    cleanup(db);
    std::uint64_t migrated_hash=0, migrated_tick=0, migrated_generation=0;
    std::int64_t initial_material=0,initial_ore=0,initial_credits=0;
    {
        Simulation legacy(810,1);
        legacy.add_colony_ship(7,{0,0},50,5);
        legacy.ensure_physical_celestial_systems();
        PersistentWorld source(db,std::move(legacy));
        source.steps(3);
        const auto first=stage6::consolidated::observe(source);
        CHECK(!first.atmosphere_v135);
        initial_material=first.total_material_kg;
        initial_ore=first.total_ore;
        initial_credits=first.total_credits;
        // Explicit v127 first migration failure cannot leave a half-accepted
        // new source entry or increment original journal generation.
        const auto hash=first.state_hash;
        const auto gen=first.generation;
        CHECK(rejects([&]{
            source.ensure_planetary_genesis_manifest(FailPoint::AfterSnapshotWrite);
        }));
        CHECK(source.generation()==gen);
        CHECK(source.world().state_hash()==hash);
        const auto admitted=stage6::consolidated::admit(source);
        CHECK(admitted.ecology_v130);
        CHECK(admitted.atmosphere_v135);
        CHECK(admitted.continuous_v133);
        CHECK(admitted.tick==first.tick);
        CHECK(admitted.total_material_kg==initial_material);
        CHECK(admitted.total_ore==initial_ore);
        CHECK(admitted.total_credits==initial_credits);
        CHECK(admitted.generation>first.generation);
        // Identical second admission must NOT create another source, credit,
        // world tick, migration receipt or SQLite commit.
        const auto again=stage6::consolidated::admit(source);
        CHECK(again.generation==admitted.generation);
        CHECK(again.state_hash==admitted.state_hash);
        CHECK(again.total_material_kg==admitted.total_material_kg);
        migrated_hash=admitted.state_hash;
        migrated_tick=admitted.tick;
        migrated_generation=admitted.generation;
    }
    {
        PersistentWorld reloaded(db,Simulation(999,1));
        const auto saved=stage6::consolidated::observe(reloaded);
        CHECK(saved.generation==migrated_generation);
        CHECK(saved.tick==migrated_tick);
        CHECK(saved.state_hash==migrated_hash);
        CHECK(saved.total_ore==initial_ore);
        CHECK(saved.total_material_kg==initial_material);
        CHECK(saved.total_credits==initial_credits);
        CHECK(stage6::consolidated::admit(reloaded).generation==migrated_generation);
    }
    cleanup(db);
}
void finite_scale(const std::string& db) {
    cleanup(db);
    std::uint64_t completed_tick=0, completed_generation=0;
    std::int64_t conserved_ore=0, conserved_kg=0, conserved_credits=0;
    {
        Simulation physical(777,1);
        const auto ship=physical.add_colony_ship(7,{0,0},50,5);
        physical.provision_starter_exploration_scanner(ship);
        CHECK(stage6::provision_first_planet_seed(physical));
        PersistentWorld original(db,std::move(physical));
        auto state=stage6::consolidated::admit(original);
        CHECK(state.bodies>=1 && state.regional_sources>=1);
        CHECK(state.physical_regions>=1);
        conserved_ore=state.total_ore;
        conserved_kg=state.total_material_kg;
        conserved_credits=state.total_credits;
        const auto first_tick=state.tick,first_generation=state.generation;
        // The original world advances, including S6-C/D/E time, not a
        // synthetic mass-conserving timer or Godot callback.
        for(int block=0;block<16;++block) {
            original.steps(64);
            const auto next=stage6::consolidated::observe(original);
            CHECK(next.tick==first_tick+std::uint64_t((block+1)*64));
            CHECK(next.generation==first_generation+std::uint64_t((block+1)*64));
            CHECK(next.total_ore==conserved_ore);
            CHECK(next.total_material_kg==conserved_kg);
            CHECK(next.total_credits==conserved_credits);
            CHECK(next.bodies==state.bodies);
            CHECK(next.regional_sources==state.regional_sources);
            CHECK(next.physical_regions==state.physical_regions);
            CHECK(next.atmosphere_v135 && next.ecology_v130);
            state=next;
        }
        completed_tick=state.tick;
        completed_generation=state.generation;
    }
    {
        PersistentWorld recovered(db,Simulation(0xC021));
        const auto a=stage6::consolidated::observe(recovered);
        CHECK(a.tick==completed_tick);
        CHECK(a.generation==completed_generation);
        CHECK(a.total_ore==conserved_ore);
        CHECK(a.total_material_kg==conserved_kg);
        CHECK(a.total_credits==conserved_credits);
        CHECK(stage6::consolidated::admit(recovered).generation==completed_generation);
    }
    cleanup(db);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const std::string db=argv[1];
        historical(db);
        finite_scale(db);
        std::cout<<"S6-G native old-save rollback/idempotency + 1024 real C/D/E ticks PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"S6-G original-world consolidation FAILED: "<<e.what()<<'\n';
        cleanup(argv[1]);
        return 1;
    }
}
