#include "galaxy/PlanetaryAgriculture.hpp"
#include <stdexcept>
#define CHECK(c) do { if(!(c)) throw std::runtime_error("PLAY13D failed: " #c); } while(false)
#include <iostream>
using namespace galaxy::planetary;
int main() {
    MaterialCustody world;
    Stock soil{};soil.water=100;soil.nutrients=50;
    world.genesis_site(9001,7,soil);
    world.genesis_ship(200,Stock{});
    const auto before=world.total_mass();
    CHECK(before==150);
    CHECK(!world.grow(9002,1));
    CHECK(!world.grow(9001,0));
    CHECK(!world.grow(9001,11));
    CHECK(world.total_mass()==before);
    CHECK(world.grow(9001,2));
    CHECK(world.site(9001).stock.food==24);
    CHECK(world.site(9001).stock.wastewater==6);
    CHECK(world.site(9001).harvest_cycles==2);
    CHECK(world.transfer_site_to_ship(9001,200,Material::Food,24));
    CHECK(!world.transfer_site_to_ship(9001,200,Material::Food,1));
    CHECK(world.consume_ship_food(200,2));
    CHECK(world.ship(200).organic_waste==16);
    CHECK(world.ship(200).wastewater==8);
    // Cosmic Law: off-world consumption leaves matter requiring physical return.
    CHECK(world.transfer_ship_to_site(200,9001,Material::OrganicWaste,16));
    CHECK(world.transfer_ship_to_site(200,9001,Material::Wastewater,8));
    CHECK(world.compost(9001,2));
    CHECK(world.site(9001).stock.compost==12);
    CHECK(world.site(9001).stock.wastewater==18);
    CHECK(world.total_mass()==before);
    CHECK(!world.compost(9001,1));
    CHECK(!world.transfer_site_to_ship(9001,200,Material::Water,-1));
    CHECK(world.total_mass()==before);
    bool duplicate=false;
    try {world.genesis_site(9001,7,soil);} catch(const std::invalid_argument&) {duplicate=true;}
    CHECK(duplicate && world.total_mass()==before);
    std::cout<<"PASS PLAY13D.1 planet farm / shipping / offworld waste return / conserved mass\n";
}
