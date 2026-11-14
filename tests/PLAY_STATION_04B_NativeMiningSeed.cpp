// Explicit, finite PLAY-STATION-04B test fixture; never a production free module.
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(int argc,char** argv){
    try{
        if(argc!=2 || std::filesystem::exists(argv[1]))
            throw std::invalid_argument("04B seed requires a NEW SQLite path");
        Simulation seed(0xB104);
        const auto station=seed.add_station({0,0});
        const auto ship=seed.add_colony_ship(7,{0,0},24,8);
        (void)seed.add_colony_ship(8,{60000,0},8,2);
        const auto rock=seed.add_geological_asteroid({25000,0},
            AsteroidClass::Metallic,10,900);
        seed.enable_native_galactic_physics({{1,galactic::Point{
            {0,100000},{0,100000},{0,100000}}}});
        seed.enable_station_ore_provenance();
        seed.enable_ship_mining_equipment();
        seed.provision_mining_test_refined_stock(ship);
        if(!seed.register_player_person(7,ship))
            throw std::runtime_error("04B no registered native player");
        PersistentWorld world(argv[1],std::move(seed));
        Command berth{};
        berth.id=87001;berth.sequence=berth.id;
        berth.execute_tick=world.world().tick()+1;
        berth.issuer=7;berth.actor=ship;berth.target=station;
        berth.action=Action::RequestDock;
        if(!world.submit(berth))throw std::runtime_error("04B dock admission");
        world.step();world.steps(Simulation::kDockTransitionTicks);
        if(world.world().find(ship)->dock_state!=DockState::Docked ||
           world.world().colony_ship(ship)->refined!=2)
            throw std::runtime_error("04B berth or finite initial material missing");
        std::cout<<"04B-FIXTURE|"<<station<<'|'<<ship<<'|'<<rock<<'|'
                 <<world.world().find(rock)->ore<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
