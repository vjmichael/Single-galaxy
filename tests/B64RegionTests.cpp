#include "galaxy/Region.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" failed at line "+std::to_string(__LINE__));}while(0)
int main() {
 try {
  Simulation simulation(0xB64);
  simulation.add_system(2);
  auto a=simulation.add_station({0,0});
  auto ship=simulation.spawn_ship_docked(7,a,20,100);
  auto ore=simulation.add_asteroid({2500,700},40);
  auto b=simulation.add_station_in_system(2,{17000,0});
  auto ab=simulation.add_gate(1,{1500,0},2,{5000,0});
  auto ba=simulation.add_gate(2,{17000,0},1,{-12000,0});
  auto ore2=simulation.add_asteroid_in_system(2,{18000,800},25);
  auto region=build_lumen_reach(simulation,a,b,ore,ore2,ab,ba);
  CHECK(simulation.systems().size()==7);
  CHECK(region.directed_gates.size()==18);
  CHECK(region.stations.size()==7);
  CHECK(region.asteroids.size()==7);
  CHECK(simulation.find(ship)->system_id==1);
  CHECK(region.gate(1,2)==ab && region.gate(2,1)==ba);
  CHECK(region.gate(1,7)==0 && region.gate(7,1)==0);
  for(unsigned sys=1;sys<=7;++sys) {
    CHECK(!region.map.system(sys).name.empty());
    CHECK(region.map.visible_on_entry(sys).size()==2);
    CHECK(region.map.sites(sys).size()==4);
    CHECK(simulation.find(region.stations.at(sys))->system_id==sys);
    CHECK(simulation.find(region.asteroids.at(sys))->system_id==sys);
  }
  const auto routes=region.operational_routes(simulation);
  CHECK((routes.route(1,7,RoutePolicy::Shortest).systems==std::vector<std::uint32_t>{1,2,6,7}));
  CHECK((routes.route(1,7,RoutePolicy::PreferSecure).systems==std::vector<std::uint32_t>{1,3,4,5,7}));
  CHECK((routes.route(7,1,RoutePolicy::Shortest).systems==std::vector<std::uint32_t>{7,6,2,1}));
  CHECK((routes.route(1,1,RoutePolicy::Shortest).systems==std::vector<std::uint32_t>{1}));
  GalaxyMap outage=routes;
  outage.set_link_operational(2,6,false);
  auto reroute=outage.route(1,7,RoutePolicy::Shortest).systems;
  CHECK(!reroute.empty() && reroute.front()==1 && reroute.back()==7);
  CHECK(std::find(reroute.begin(),reroute.end(),6)==reroute.end() ||
        std::find(reroute.begin(),reroute.end(),2)==reroute.end());
  outage.set_link_operational(1,3,false);
  outage.set_link_operational(2,4,false);
  outage.set_link_operational(1,2,false);
  CHECK(outage.route(1,7,RoutePolicy::Shortest).systems.empty());
  CHECK(region.map.system(7).sovereignty==Sovereignty::Player);
  CHECK(region.map.system(7).security<region.map.system(1).security);
  std::cout<<"PASS B6.4 original seven-system region, 18 physical gates, shortest/secure routes, reroute and partition topology\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
