// Test-only genesis for authentic owned ship and finite original asteroid.
// No default production save, rescue, or free mining-tool fitting.
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::invalid_argument("expected SQLITE test save path");
  if(std::filesystem::exists(argv[1]))throw std::runtime_error("refuse overwrite existing test save");
  Simulation seed(0xC021);
  const auto station=seed.add_station({0,0});
  const auto ship=seed.spawn_ship_docked(7,station,24,60);
  (void)seed.add_colony_ship(8,{15000,0},8,2);
  const auto rock=seed.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
  seed.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
  if(!seed.register_player_person(7,ship))throw std::runtime_error("fixture real owned person refused");
  PersistentWorld world(argv[1],std::move(seed));
  const auto* r=world.world().find(rock);
  if(!r||r->ore<1)throw std::runtime_error("test asteroid did not persist");
  std::cout<<"04A-FIXTURE|"<<station<<'|'<<ship<<'|'<<rock<<'|'<<r->ore<<'\n';
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
