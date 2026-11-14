#include "galaxy/Simulation.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__));}while(false)
static bool rejects(const std::string& snapshot) {
    try { (void)Simulation::from_snapshot(snapshot); return false; }
    catch (const std::exception&) { return true; }
}
int main() {
    try {
        // An unassigned genesis entity inherits the actual simulation's star,
        // never an implicit star 1. Explicit sites in other registered stars
        // remain authoritative, distinct entities.
        Simulation galaxy(425,37);
        galaxy.add_system(1);
        const auto home=galaxy.add_ship(7,{0,0},100);
        const auto ore=galaxy.add_asteroid({1000,0},30);
        const auto foreign_station=galaxy.add_station_in_system(1,{4000,0});
        CHECK(galaxy.find(home)->system_id==37);
        CHECK(galaxy.find(ore)->system_id==37);
        CHECK(galaxy.find(foreign_station)->system_id==1);
        galaxy.ensure_ship_frame_registry();
        const auto multiple=galaxy.canonical_state();
        CHECK(Simulation::from_snapshot(multiple).canonical_state()==multiple);
        // High-version snapshots missing an actual original ship-frame record
        // must be refused, not silently inferred on restore.
        auto forged=multiple;
        const auto pos=forged.find("shipframe|"+std::to_string(home)+"|");
        CHECK(pos!=std::string::npos);
        const auto stop=forged.find('\n',pos);
        CHECK(stop!=std::string::npos);
        forged.erase(pos,stop-pos+1);
        CHECK(rejects(forged));

        Simulation identity(606);
        const auto first=identity.add_ship(7,{0,0},100);
        CHECK(identity.register_player_person(7,first));
        CHECK(!identity.register_player_person(7,first));
        const auto durable=identity.canonical_state();
        CHECK(durable.rfind("galaxy-kernel-v98|",0)==0);
        CHECK(durable.find("shipframe|"+std::to_string(first)+"|")!=std::string::npos);
        const auto restored=Simulation::from_snapshot(durable);
        CHECK(restored.player_people().at(7).home_ship==first);
        CHECK(restored.ship_frames().at(first).system==1);
        CHECK(restored.canonical_state()==durable);
        std::cout<<"PASS SPACE-05M star ownership, v71/v98 custody upgrade, forged frame rejection\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL SPACE-05M "<<e.what()<<'\n';
        return 1;
    }
}
