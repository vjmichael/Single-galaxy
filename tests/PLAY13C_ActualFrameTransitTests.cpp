#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C2C: ")+#x+" line "+std::to_string(__LINE__));}while(false)
static bool rejected(const std::string& s) {
    try {(void)Simulation::from_snapshot(s);return false;}
    catch(const std::exception&){return true;}
}
int main(){
    auto dir=fs::temp_directory_path()/
       ("sg_play13c2c_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        const auto db=(dir/"world.sqlite").string(),copy=(dir/"copy.sqlite").string();
        EntityId ship=0,other=0;
        std::uint64_t body=0;
        std::string during,arrived;
        std::int64_t before_mass=0;
        {
            Simulation seed(20261104);
            ship=seed.add_colony_ship(7,{200,0},60,24);
            other=seed.add_ship(8,{0,0},25);
            seed.add_asteroid({500,0},40);
            seed.provision_starter_exploration_scanner(ship);
            before_mass=seed.total_material_kg();
            PersistentWorld w(db,std::move(seed));
            w.ensure_physical_celestial_systems();
            w.ensure_ship_frame_registry();
            body=w.world().physical_celestial_systems().at(1).front().id;
            CHECK(!w.begin_celestial_transit(8,ship,body,501));
            CHECK(!w.begin_celestial_transit(7,ship,(std::uint64_t(2)<<32)|1,501));
            CHECK(!w.begin_celestial_transit(7,ship,body,0));
            CHECK(w.begin_celestial_transit(7,ship,body,501));
            CHECK(!w.begin_celestial_transit(7,ship,body,501));
            CHECK(!w.begin_celestial_transit(7,ship,body,502));
            CHECK(!w.complete_celestial_transit(501));
            during=w.world().canonical_state();
            CHECK(during.rfind("galaxy-kernel-v72|",0)==0);
            CHECK(w.world().pending_celestial_transits().at(501).ship==ship);
            CHECK(w.world().ship_frames().at(ship).anchor_body==0);
            CHECK(w.world().total_material_kg()==before_mass);
            CHECK(rejected(during.substr(0,during.find("frametransit_pending|"))+
               during.substr(during.find("\n",during.find("frametransit_pending|"))+1)));
        }
        {
            PersistentWorld w(db,Simulation(1));
            CHECK(w.world().canonical_state()==during);
            w.steps(3);
            CHECK(!w.complete_celestial_transit(501));
            w.step();
            CHECK(w.complete_celestial_transit(501));
            CHECK(!w.complete_celestial_transit(501));
            CHECK(!w.begin_celestial_transit(7,ship,body,501));
            CHECK(w.world().pending_celestial_transits().empty());
            CHECK(w.world().completed_celestial_transits().at(501).ship==ship);
            CHECK(w.world().ship_frames().at(ship).anchor_body==body);
            CHECK(w.world().find(ship)->position.x==0);
            CHECK(w.world().find(ship)->velocity.x==0);
            CHECK(w.world().find(ship)->owner_id==7);
            CHECK(w.world().colony_ship(ship)->crew==24);
            CHECK(w.world().total_material_kg()==before_mass);
            CHECK(!w.world().same_local_frame(ship,other));
            arrived=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(arrived).canonical_state()==arrived);
            PersistentWorld::recover_copy(db,copy);
        }
        {
            PersistentWorld w(db,Simulation(2));
            CHECK(w.world().canonical_state()==arrived);
            CHECK(!w.complete_celestial_transit(501));
            CHECK(w.world().ship_frames().at(other).anchor_body==0);
            CHECK(w.world().colony_ship(ship)->crew==24);
            CHECK(w.world().total_material_kg()==before_mass);
        }
        {
            PersistentWorld w(copy,Simulation(3));
            CHECK(w.world().canonical_state()==arrived);
        }
        fs::remove_all(dir);
        std::cout<<"PASS PLAY13C2C durable owner-authorized frame transition\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY13C2C "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(dir,ec);return 1;
    }
}
