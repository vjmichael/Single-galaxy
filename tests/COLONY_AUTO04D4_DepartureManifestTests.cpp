#include "galaxy/ColonyEvacuation.hpp"
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4 manifest: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    Simulation w(7144);
    const auto source=w.add_colony_ship(7,{0,0},100,4);
    const auto stocked=w.add_colony_ship(8,{0,0},100,4);
    const auto distant=w.add_colony_ship(9,{50000,0},100,4);
    const auto empty=w.add_colony_ship(10,{1000,0},100,60);
    w.provision_colony_genesis(source,100,100,100,200,500);
    w.provision_colony_genesis(stocked,1000,1000,1000,1000,1000);
    w.provision_colony_genesis(distant,1000,1000,1000,1000,1000);
    w.provision_colony_genesis(empty,100,100,100,200,500);
    w.enable_individual_workforce();
    w.upgrade_assignment_snapshot_format();
    w.enable_passenger_life_support();
    const auto m=colony_evacuation::survey(w,source);
    CHECK(m.world_tick==0 && m.declared_population==4);
    CHECK(m.named_people.size()==4);
    CHECK(m.people_by_physical_location.at(source)==4);
    CHECK(m.roster_complete && m.physically_reconciled);
    CHECK(m.sufficient_local_berths); // candidate capacity, NOT evacuation
    CHECK(m.receiving_vessels.size()==3);
    CHECK(m.receiving_vessels[0].ship==stocked);
    CHECK(m.receiving_vessels[0].locally_accessible);
    CHECK(m.receiving_vessels[0].funded_two_period_berths==16);
    CHECK(m.receiving_vessels[1].ship==distant);
    CHECK(!m.receiving_vessels[1].locally_accessible);
    CHECK(m.receiving_vessels[2].ship==empty);
    CHECK(m.receiving_vessels[2].funded_two_period_berths==0);
    CHECK(!m.dependent_guardianship_verified);
    CHECK(!m.last_operating_crew_departure_supported);
    CHECK(!m.executable); // positive capacity alone cannot fake completed rescue
    const auto restored=Simulation::from_snapshot(w.canonical_state());
    const auto replayed=colony_evacuation::survey(restored,source);
    CHECK(replayed.named_people==m.named_people);
    CHECK(replayed.people_by_physical_location==m.people_by_physical_location);
    CHECK(replayed.receiving_vessels.size()==m.receiving_vessels.size());
    CHECK(!replayed.executable);
    std::cout<<"PASS AUTO04D4: complete named roster and real capacity; dependent/last crew closure still refused\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
