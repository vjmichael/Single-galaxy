#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C2B: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static bool rejects(const std::string& text) {
    try {(void)Simulation::from_snapshot(text);return false;}
    catch(const std::exception&){return true;}
}
int main() {
    const auto dir=fs::temp_directory_path()/
        ("sg_play13c2b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(dir);
        const auto db=(dir/"world.sqlite").string();
        const auto copy=(dir/"copy.sqlite").string();
        EntityId first=0,second=0;
        std::string canonical;
        std::int64_t initial_mass=0;
        {
            Simulation genesis(13579);
            first=genesis.add_colony_ship(7,{0,0},60,8);
            second=genesis.add_ship(8,{200,300},50);
            genesis.add_asteroid({10000,0},50);
            initial_mass=genesis.total_material_kg();
            PersistentWorld world(db,std::move(genesis));
            CHECK(world.world().ship_frames().empty());
            world.ensure_physical_celestial_systems();
            const auto old_generation=world.generation();
            world.ensure_ship_frame_registry();
            CHECK(world.generation()==old_generation+1);
            CHECK(world.world().ship_frames().size()==2);
            CHECK(world.world().ship_frames().at(first).system==world.world().find(first)->system_id);
            CHECK(world.world().ship_frames().at(first).anchor_body==0);
            CHECK(world.world().ship_frames().at(second).origin.x_mm==0);
            CHECK(world.world().total_material_kg()==initial_mass);
            CHECK(world.world().colony_ship(first)->crew==8);
            canonical=world.world().canonical_state();
            CHECK(canonical.rfind("galaxy-kernel-v71|",0)==0);
            CHECK(Simulation::from_snapshot(canonical).canonical_state()==canonical);
            const auto no_change=world.generation();
            world.ensure_ship_frame_registry();
            CHECK(world.generation()==no_change);
            // Removing a single custody record must fail complete restore.
            const auto at=canonical.find("shipframe|");
            CHECK(at!=std::string::npos);
            const auto end=canonical.find('\n',at);
            CHECK(end!=std::string::npos);
            auto missing=canonical;
            missing.erase(at,end-at+1);
            CHECK(rejects(missing));
            // Each ship must have exactly one custody record.
            auto duplicate=canonical;
            duplicate.insert(at,canonical.substr(at,end-at+1));
            CHECK(rejects(duplicate));
            // An unanchored legacy frame may not secretly carry an origin.
            auto moved=canonical;
            const auto last_pipe=moved.rfind('|',end);
            CHECK(last_pipe!=std::string::npos);
            moved.replace(last_pipe+1,end-last_pipe-1,"1");
            CHECK(rejects(moved));
            PersistentWorld::recover_copy(db,copy);
        }
        {
            PersistentWorld restored(db,Simulation(7));
            CHECK(restored.world().canonical_state()==canonical);
            CHECK(restored.world().ship_frames().size()==2);
            CHECK(restored.world().colony_ship(first)->crew==8);
            CHECK(restored.world().find(second)->position.x==200);
            CHECK(restored.world().total_material_kg()==initial_mass);
            restored.ensure_ship_frame_registry();
            CHECK(restored.world().canonical_state()==canonical);
        }
        {
            PersistentWorld recovered(copy,Simulation(8));
            CHECK(recovered.world().canonical_state()==canonical);
            CHECK(recovered.world().ship_frames().size()==2);
        }
        fs::remove_all(dir);
        std::cout<<"PASS PLAY-13C2B durable v71 ship frame registry\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY-13C2B "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(dir,ec);return 1;
    }
}
