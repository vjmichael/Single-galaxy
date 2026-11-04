#include "galaxy/Simulation.hpp"
#include "galaxy/ColonySafety.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("V59 dependent: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    Simulation sim(7402);
    const auto source=sim.add_colony_ship(7,{0,0},100,2);
    const auto receiver=sim.add_colony_ship(7,{0,0},100,2);
    sim.provision_colony_genesis(source,100,100,100,200,500);
    sim.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
    sim.enable_individual_workforce();
    sim.upgrade_assignment_snapshot_format();
    sim.enable_passenger_life_support();
    sim.enable_physical_dependent_accounting();
    const auto mass=sim.total_material_kg();
    const auto child=(1ULL<<62)+7402;
    Command birth{};
    birth.action=Action::HostRegisterDependentBirth;
    birth.id=101;birth.sequence=101;birth.issuer=Simulation::kServerAuthority;
    birth.execute_tick=1;birth.actor=source;birth.target=source;
    birth.transport_id=1;birth.amount=2;birth.passenger_ids={child};
    CHECK(sim.submit(birth));
    sim.step();
    CHECK(sim.events().back().result==Outcome::Applied);
    CHECK(sim.physical_dependents().at(child).home_ship==source);
    CHECK(sim.dependents_physically_at(source)==1);
    CHECK(sim.people_physically_aboard(source)==2);
    CHECK(colony_safety::assess(sim,source,source).population==3);
    CHECK(sim.npc_records().size()==4); // children are never extra workers
    sim.steps(20-sim.tick());
    CHECK(sim.life_support(source)->food_kg==97);
    CHECK(sim.life_support(source)->water_kg==97);
    CHECK(sim.life_support(source)->oxygen_kg==97);
    CHECK(sim.total_material_kg()==mass);
    CHECK(Simulation::from_snapshot(sim.canonical_state()).canonical_state()==
          sim.canonical_state());
    sim.steps(620-sim.tick());
    CHECK(static_cast<int>(colony_safety::assess(sim,source,source).risk)>=
          static_cast<int>(colony_safety::Risk::Critical));
    Command unsafe{};
    unsafe.id=102;unsafe.sequence=102;
    unsafe.issuer=Simulation::kServerAuthority;
    unsafe.action=Action::HostColonyDeparture;
    unsafe.actor=source;unsafe.target=receiver;
    unsafe.passenger_ids={1,2};unsafe.execute_tick=sim.tick()+1;
    CHECK(!sim.submit(unsafe)); // adult-only must never abandon a child
    Command family=unsafe;
    family.id=103;family.sequence=103;
    family.action=Action::HostColonyFamilyDeparture;
    family.passenger_ids={1,2,child};
    CHECK(sim.submit(family));
    sim.step();
    CHECK(sim.events().back().result==Outcome::Applied);
    CHECK(sim.events().back().transferred==0);
    CHECK(sim.colony_ship(source)->crew==0);
    CHECK(sim.colony_ship(receiver)->crew==4);
    CHECK(sim.dependents_physically_at(source)==0);
    CHECK(sim.dependents_physically_at(receiver)==1);
    CHECK(sim.physical_dependents().at(child).home_ship==receiver);
    CHECK(sim.physical_dependents().at(child).location==receiver);
    CHECK(sim.colony_departures().at(source).relocated_npcs==
          std::vector<std::uint64_t>({1,2}));
    CHECK(sim.colony_departures().at(source).relocated_children==
          std::vector<std::uint64_t>({child}));
    const auto before=sim.life_support(receiver)->food_kg;
    sim.steps(640-sim.tick());
    CHECK(sim.life_support(receiver)->food_kg==before-5); // 4 adults + 1 child
    CHECK(sim.total_material_kg()==mass);
    const auto restored=Simulation::from_snapshot(sim.canonical_state());
    CHECK(restored.canonical_state()==sim.canonical_state());
    CHECK(restored.dependents_physically_at(receiver)==1);
    std::cout<<"PASS v59: real dependent birth, local needs, total custody, family relocation and snapshot\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
