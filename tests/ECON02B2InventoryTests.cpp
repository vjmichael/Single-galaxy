#include "galaxy/Inventory.hpp"
#include "galaxy/Simulation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::inventory;
#define CHECK(x) do {if(!(x))throw std::runtime_error("ECON-02B2 line "+std::to_string(__LINE__)+": " #x);}while(0)

template<typename F> void reject(F fn){bool did=false;try{fn();}catch(const std::exception&){did=true;}CHECK(did);}
int main(){
    try {
        const auto& standard=Catalogue::standard();
        CHECK(standard.liters_per_unit("ore")==1000);
        CHECK(standard.liters_per_unit("construction_module")==2000);
        CHECK(standard.liters_per_unit("good/aluminum")==1);
        CHECK(standard.liters_per_unit("good/aluminum_scrap")==1);
        CHECK(!standard.contains("good/magic_metal"));
        reject([]{Catalogue empty({});});
        reject([]{Catalogue bad({{"a",1},{"a",2}});});
        reject([]{Catalogue bad({{"a",0}});});
        reject([]{Catalogue bad({{"bad|item",1}});});
        Catalogue custom({{"aluminum",3},{"panels",5},{"scrap",2}});
        StackStore ship(custom,100),factory(custom,25);
        ship.load("aluminum",20);ship.load("scrap",8);
        CHECK(ship.used_liters()==76 && ship.free_liters()==24);
        CHECK(ship.transfer_to(factory,"aluminum",5)==TransferStatus::Applied);
        CHECK(ship.amount("aluminum")==15 && factory.amount("aluminum")==5);
        CHECK(ship.used_liters()==61 && factory.used_liters()==15);
        CHECK(ship.transfer_to(factory,"aluminum",4)==TransferStatus::DestinationFull);
        CHECK(ship.amount("aluminum")==15 && factory.used_liters()==15);
        CHECK(ship.transfer_to(factory,"panels",1)==TransferStatus::SourceShortage);
        CHECK(ship.transfer_to(factory,"other",1)==TransferStatus::InvalidItem);
        CHECK(ship.transfer_to(factory,"scrap",-1)==TransferStatus::InvalidAmount);
        CHECK(ship.transfer_to(ship,"scrap",1)==TransferStatus::InvalidAmount);
        Catalogue another({{"aluminum",3}}); StackStore alien(another,100);
        CHECK(ship.transfer_to(alien,"aluminum",1)==TransferStatus::IncompatibleCatalogue);
        CHECK(factory.transfer_to(ship,"aluminum",5)==TransferStatus::Applied);
        CHECK(factory.used_liters()==0 && ship.used_liters()==76);
        reject([&]{ship.load("aluminum",100);});
        reject([&]{ship.load("panels",6);}); // 30L cannot fit into 24L remaining
        // Existing world view reads the same catalogue and never applies a mass limit.
        Simulation s(123); const auto id=s.add_colony_ship(7,{0,0},10,2);
        CHECK(s.ship_inventory_snapshot(id).capacity_liters()==10000);
        CHECK(s.ship_inventory_snapshot(id).used_liters()==0);
        CHECK(s.ship_cargo_volume_liters(id)==0);
        CHECK(s.ship_inventory_snapshot(id).stacks().empty());
        reject([&]{s.workshop_bin_inventory_snapshot(999,0);});
        std::cout<<"ECON-02B2 catalogue, volume, atomic transfers, world snapshots: PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ECON-02B2 FAIL: "<<e.what()<<'\n';return 1;}
}
