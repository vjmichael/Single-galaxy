#include "galaxy/PlanetaryAgriculture.hpp"
#include <cassert>
#include <iostream>
using namespace galaxy::planetary;
int main() {
    MaterialCustody world;
    Stock soil{};soil.water=100;soil.nutrients=50;
    world.genesis_site(9001,7,soil);
    world.genesis_ship(200,Stock{});
    const auto before=world.total_mass();
    assert(before==150);
    assert(!world.grow(9002,1));
    assert(!world.grow(9001,0));
    assert(!world.grow(9001,11));
    assert(world.total_mass()==before);
    assert(world.grow(9001,2));
    assert(world.site(9001).stock.food==24);
    assert(world.site(9001).stock.wastewater==6);
    assert(world.site(9001).harvest_cycles==2);
    assert(world.transfer_site_to_ship(9001,200,Material::Food,24));
    assert(!world.transfer_site_to_ship(9001,200,Material::Food,1));
    assert(world.consume_ship_food(200,2));
    assert(world.ship(200).organic_waste==16);
    assert(world.ship(200).wastewater==8);
    // Cosmic Law: off-world consumption leaves matter requiring physical return.
    assert(world.transfer_ship_to_site(200,9001,Material::OrganicWaste,16));
    assert(world.transfer_ship_to_site(200,9001,Material::Wastewater,8));
    assert(world.compost(9001,2));
    assert(world.site(9001).stock.compost==12);
    assert(world.site(9001).stock.wastewater==18);
    assert(world.total_mass()==before);
    assert(!world.compost(9001,1));
    assert(!world.transfer_site_to_ship(9001,200,Material::Water,-1));
    assert(world.total_mass()==before);
    bool duplicate=false;
    try {world.genesis_site(9001,7,soil);} catch(const std::invalid_argument&) {duplicate=true;}
    assert(duplicate && world.total_mass()==before);
    std::cout<<"PASS PLAY13D.1 planet farm / shipping / offworld waste return / conserved mass\n";
}
