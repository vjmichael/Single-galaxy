#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C6A: ")+#x+" line "+std::to_string(__LINE__));}while(false)
static std::uint64_t grade(AsteroidClass taxonomy,const char* mineral){
    for(const auto& part:mineral_profile(taxonomy))
        if(std::string(part.name)==mineral)return std::uint64_t(part.permille);
    return 0;
}
int main(){
    const auto dir=fs::temp_directory_path()/
       ("sg_play13c6a_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        const auto db=(dir/"world.sqlite").string();
        EntityId ship=0,site=0;
        std::uint64_t body=0;
        std::string saved;
        {
            Simulation seed(13506);
            ship=seed.add_colony_ship(7,{0,0},60,10);
            seed.provision_colony_genesis(ship,200,400,600,400,400);
            seed.provision_starter_exploration_scanner(ship);
            seed.add_asteroid({30000,0},100);
            PersistentWorld world(db,std::move(seed));
            world.ensure_physical_celestial_systems();
            world.ensure_ship_frame_registry();
            body=world.world().physical_celestial_systems().at(1).front().id;
            CHECK(!world.world().assess_expedition(7,ship)); // no visit, no oracle
            CHECK(world.begin_celestial_transit(7,ship,body,8001));
            world.steps(4);
            CHECK(world.complete_celestial_transit(8001));
            CHECK(!world.world().assess_expedition(8,ship)); // no other owner's data
            const auto before=world.world().canonical_state();
            const auto p=world.world().assess_expedition(7,ship);
            CHECK(p.has_value());
            CHECK(p->body==body);
            CHECK(p->ore_tonnes==14);
            CHECK(p->food_periods==20);
            CHECK(p->water_periods==40);
            CHECK(p->oxygen_periods==60);
            CHECK(p->hazard==3);
            CHECK(!p->local_supply_site);
            const auto manifest=world.world().generated_local_regions().at(body);
            const auto e=world.world().local_environments().at(body);
            CHECK(p->nickel_iron_kg==
              6*grade(e.first,"nickel_iron")+8*grade(e.second,"nickel_iron"));
            CHECK(p->plagioclase_kg==
              6*grade(e.first,"plagioclase")+8*grade(e.second,"plagioclase"));
            CHECK(world.world().canonical_state()==before); // no fuel/ore mutation
            site=manifest[0];
            Command mine{};
            mine.id=9001;mine.sequence=9001;mine.execute_tick=world.world().tick()+1;
            mine.issuer=7;mine.actor=ship;mine.target=site;mine.action=Action::Mine;mine.amount=1;
            CHECK(world.submit(mine));
            world.step();
            CHECK(world.world().events().back().result==Outcome::Applied);
            const auto less=world.world().assess_expedition(7,ship);
            CHECK(less && less->ore_tonnes==13);
            CHECK(less->nickel_iron_kg+grade(e.first,"nickel_iron")==p->nickel_iron_kg);
            CHECK(less->plagioclase_kg+grade(e.first,"plagioclase")==p->plagioclase_kg);
            // Forecast should follow the real 20-tick consumption schedule.
            world.steps(15);
            CHECK(world.world().tick()==20);
            const auto used=world.world().assess_expedition(7,ship);
            CHECK(used && used->food_periods==19 && used->water_periods==39 &&
                  used->oxygen_periods==59);
            CHECK(world.world().life_support(ship)->organic_waste_kg==10);
            CHECK(world.world().life_support(ship)->wastewater_kg==10);
            CHECK(world.world().life_support(ship)->co2_kg==10);
            saved=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
        }
        {
            PersistentWorld world(db,Simulation(1));
            CHECK(world.world().canonical_state()==saved);
            const auto p=world.world().assess_expedition(7,ship);
            CHECK(p && p->ore_tonnes==13 && p->food_periods==19);
            CHECK(world.world().assess_expedition(7,ship).has_value());
            CHECK(world.world().canonical_state()==saved);
        }
        fs::remove_all(dir);
        std::cout<<"PASS PLAY13C6A geological opportunity, authentic survival cycles and persistent unbiased forecast\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL PLAY13C6A "<<error.what()<<'\n';
        std::error_code ec;fs::remove_all(dir,ec);return 1;
    }
}
