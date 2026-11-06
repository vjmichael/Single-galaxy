#include "galaxy/ShipInteractionPreflight.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::ship_interaction;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); }while(false)
int main(){
    try{
        CHECK(within_axis(0,2500,2500));
        CHECK(!within_axis(0,2501,2500));
        CHECK(within_axis(-2500,0,2500));
        CHECK(!within_axis(std::numeric_limits<std::int64_t>::min(),
                           std::numeric_limits<std::int64_t>::max(),2500));
        CHECK(!within_axis(0,0,-1));
        Simulation world(1804);
        const auto first=world.add_ship(7,{0,0},100);
        const auto second=world.add_ship(8,{2000,0},100);
        const auto far=world.add_ship(8,{20000,0},100);
        CHECK(spatial_preflight(world,7,first,second)==Readiness::Ready);
        CHECK(spatial_preflight(world,8,first,second)==Readiness::WrongOwner);
        CHECK(spatial_preflight(world,7,first,first)==Readiness::InvalidRequest);
        CHECK(spatial_preflight(world,7,first,far)==Readiness::TooFar);
        CHECK(spatial_preflight(world,7,first,0)==Readiness::InvalidRequest);
        CHECK(rendezvous_preflight(world,7,first,second,10001)==
              Readiness::MissingRendezvous);
        CHECK(world.npc_flight_orders().empty());
        CHECK(world.activity_events().empty());
        CHECK(world.find(first)->ore==0 && world.find(second)->ore==0);
        std::cout<<"PASS FLIGHT-04: current geometry != rendezvous receipt != interaction authority\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL FLIGHT-04: "<<e.what()<<'\n';
        return 1;
    }
}
