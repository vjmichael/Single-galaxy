#include "galaxy/Simulation.hpp"
#include "galaxy/ColonySafety.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4D2B: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    Simulation world(7501);
    const auto source=world.add_colony_ship(7,{0,0},100,2);
    const auto receiver=world.add_colony_ship(7,{0,0},100,2);
    world.provision_colony_genesis(source,100,100,100,200,500);
    world.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
    world.enable_individual_workforce();
    world.upgrade_assignment_snapshot_format();
    world.enable_passenger_life_support();
    const auto original_material=world.total_material_kg();
    constexpr std::uint64_t child=(1ULL<<62)+7501;
    Command register_child{};
    register_child.id=7501;register_child.sequence=7501;
    register_child.issuer=Simulation::kServerAuthority;
    register_child.actor=source;register_child.target=source;
    register_child.action=Action::HostRegisterDependent;
    register_child.transport_id=child;
    register_child.passenger_ids={1};
    register_child.execute_tick=world.tick()+1;
    // A kernel-only fixture: ART02 is intentionally tested at the host boundary.
    CHECK(world.submit(register_child));
    world.step();
    CHECK(world.events().back().result==Outcome::Applied);
    CHECK(world.children_physically_aboard(source)==1);
    CHECK(world.npc_records().size()==4);
    // Every person consumes from the original physical location until transfer.
    world.steps(640-world.tick());
    CHECK(colony_safety::assess(world,source,source).risk==colony_safety::Risk::Critical);
    Command leave{};
    leave.id=7502;leave.sequence=7502;leave.issuer=Simulation::kServerAuthority;
    leave.actor=source;leave.target=receiver;
    leave.action=Action::HostColonyDeparture;
    leave.passenger_ids={1,2};leave.execute_tick=world.tick()+1;
    auto incomplete=leave;incomplete.id=7503;incomplete.passenger_ids={2};
    CHECK(!world.submit(incomplete));
    auto forged=leave;forged.id=7504;forged.passenger_ids={1,2,2};
    CHECK(!world.submit(forged));
    CHECK(world.submit(leave));
    const auto pending=world.canonical_state();
    CHECK(pending.rfind("galaxy-kernel-v60|",0)==0);
    CHECK(Simulation::from_snapshot(pending).canonical_state()==pending);
    world.step();
    CHECK(world.events().back().command_id==7502);
    CHECK(world.events().back().result==Outcome::Applied);
    CHECK(world.colony_ship(source)->crew==0);
    CHECK(world.colony_ship(receiver)->crew==4);
    CHECK(world.children_physically_aboard(source)==0);
    CHECK(world.children_physically_aboard(receiver)==1);
    CHECK(world.people_physically_aboard(source)==0);
    CHECK(world.people_physically_aboard(receiver)==4); // adult-only NPC API
    CHECK(world.people_physically_aboard(receiver)+world.children_physically_aboard(receiver)==5);
    CHECK(world.physical_dependents().at(child).home_ship==receiver);
    CHECK(world.physical_dependents().at(child).location==receiver);
    CHECK(world.physical_dependents().at(child).guardian_id==1);
    CHECK(world.colony_departures().at(source).relocated_npcs==
          std::vector<std::uint64_t>({1,2}));
    CHECK(world.colony_departures().at(source).relocated_dependents==
          std::vector<std::uint64_t>({child}));
    CHECK(world.npc_records().size()==4); // no phantom child worker
    CHECK(world.total_material_kg()==original_material);
    const auto after=world.canonical_state();
    CHECK(after.rfind("galaxy-kernel-v60|",0)==0);
    CHECK(Simulation::from_snapshot(after).canonical_state()==after);
    const auto receiver_food=world.life_support(receiver)->food_kg;
    const auto source_food=world.life_support(source)->food_kg;
    world.steps(660-world.tick());
    CHECK(world.life_support(receiver)->food_kg==receiver_food-5);
    CHECK(world.life_support(source)->food_kg==source_food);
    CHECK(world.total_material_kg()==original_material);
    // A forged record cannot claim a child belongs to a different guardian.
    auto tampered=after;
    const auto tag=std::string("physicaldependent|")+std::to_string(child)+"|1|";
    const auto pos=tampered.find(tag);
    CHECK(pos!=std::string::npos);
    tampered.replace(pos,tag.size(),
        std::string("physicaldependent|")+std::to_string(child)+"|3|");
    bool rejected=false;
    try{(void)Simulation::from_snapshot(tampered);}
    catch(const std::runtime_error&){rejected=true;}
    CHECK(rejected);
    std::cout<<"PASS v60 native entire-family physical movement, needs, custody and snapshot tampering\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
