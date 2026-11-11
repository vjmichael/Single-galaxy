// Group3 finite-resource pilot. No made-up module or genesis refined stock.
// The original v123+ source-lot mining and v125 swept 3D physics are required.
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(int argc,char** argv){
 try{
    if(argc!=2 || std::filesystem::exists(argv[1]))
       throw std::invalid_argument("resource seed requires a new SQLite path");
    Simulation seed(0xB204);
    const auto station=seed.add_station({0,0});
    const auto ship=seed.add_colony_ship(7,{0,0},24,8);
    (void)seed.add_colony_ship(8,{60000,0},8,2);
    const auto rock=seed.add_geological_asteroid({25000,0},
        AsteroidClass::Metallic,10,900);
    seed.enable_native_galactic_physics({{1,galactic::Point{
       {0,100000},{0,100000},{0,100000}}}});
    seed.enable_native_swept_asteroid_collision();
    seed.enable_station_ore_provenance();
    if(!seed.register_player_person(7,ship))
       throw std::runtime_error("cannot register real original pilot");
    PersistentWorld world(argv[1],std::move(seed));
    Command berth{};
    berth.id=87002;berth.sequence=berth.id;
    berth.execute_tick=world.world().tick()+1;
    berth.issuer=7;berth.actor=ship;berth.target=station;
    berth.action=Action::RequestDock;
    if(!world.submit(berth))
       throw std::runtime_error("original berth command denied");
    world.step();world.steps(Simulation::kDockTransitionTicks);
    if(world.world().find(ship)->dock_state!=DockState::Docked ||
       !world.world().colony_ship(ship) ||
       world.world().colony_ship(ship)->modules!=0 ||
       world.world().colony_ship(ship)->refined!=0)
       throw std::runtime_error("not a drill-free real colony ship berth");
    std::cout<<"GROUP3-ORE-FIXTURE|"<<station<<'|'<<ship<<'|'<<rock
             <<'|'<<world.world().find(rock)->ore<<'\n';
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
