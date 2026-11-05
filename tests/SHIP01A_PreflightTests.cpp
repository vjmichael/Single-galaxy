#include "galaxy/ShipyardConstructionPreflight.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
using namespace galaxy;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("SHIP01A: ")+#x+" line "+std::to_string(__LINE__));}while(0)
static void command(Simulation& world,std::uint64_t id,EntityId ship,
                    Action action,std::int64_t amount,EntityId target=0) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=world.tick()+1;
    c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
    CHECK(world.submit(c));world.step();
    CHECK(world.events().back().result==Outcome::Applied);
}
int main(){
    Simulation w(641);
    const auto colony=w.add_colony_ship(7,{0,0},80,6);
    const auto ore=w.add_asteroid({2000,0},40);
    const auto other=w.add_colony_ship(8,{0,0},80,3);
    w.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
    w.provision_colony_genesis(other,1000,1000,1000,1000,1000);
    w.enable_individual_workforce();
    std::vector<std::uint64_t> people;
    for(const auto& [id,n]:w.npc_records())if(n.home_ship==colony)people.push_back(id);
    CHECK(people.size()==6);
    const std::array<std::uint64_t,2> crew{people[0],people[1]};
    using industry::ShipyardBlocker;
    auto pre=[&](PlayerId owner,EntityId ship,std::uint64_t id,
                 std::array<std::uint64_t,2> roster){
        return industry::inspect_shipyard_build(w,owner,ship,id,roster);
    };
    CHECK(pre(7,colony,99001,crew).blocker==ShipyardBlocker::InsufficientModules);
    CHECK(pre(8,colony,99001,crew).blocker==ShipyardBlocker::Unauthorized);
    CHECK(pre(7,ore,99001,crew).blocker==ShipyardBlocker::InvalidColony);
    CHECK(pre(7,colony,0,crew).blocker==ShipyardBlocker::InvalidRequest);
    CHECK(pre(7,colony,99001,{people[0],people[0]}).blocker==ShipyardBlocker::InvalidRequest);
    CHECK(pre(7,colony,99001,{people[0],987654}).blocker==ShipyardBlocker::InsufficientModules);
    // Assemble actual modules from mined ore, not a test-only inventory edit.
    command(w,9001,colony,Action::Mine,4,ore);
    command(w,9002,colony,Action::RefineOre,4);
    command(w,9003,colony,Action::FabricateModule,2);
    command(w,9004,colony,Action::Mine,4,ore);
    command(w,9005,colony,Action::RefineOre,4);
    command(w,9006,colony,Action::FabricateModule,2);
    CHECK(w.colony_ship(colony)->modules>=4);
    CHECK(pre(7,colony,99001,{people[0],987654}).blocker==ShipyardBlocker::MissingNamedWorkers);
    std::uint64_t foreign=0;
    for(const auto& [id,n]:w.npc_records())if(n.home_ship==other){foreign=id;break;}
    CHECK(foreign!=0);
    CHECK(pre(7,colony,99001,{people[0],foreign}).blocker==ShipyardBlocker::MissingNamedWorkers);
    const auto before=w.canonical_state();
    const auto approved=pre(7,colony,99001,crew);
    CHECK(approved.permitted());
    CHECK(approved.required_modules==4);
    CHECK(approved.existing_modules>=4);
    CHECK(w.canonical_state()==before); // preflight never spends/creates stock
    const auto restored=Simulation::from_snapshot(before);
    CHECK(industry::inspect_shipyard_build(restored,7,colony,99001,crew).permitted());
    CHECK(restored.canonical_state()==before);
    std::cout << "PASS SHIP-01A read-only preflight: owner, physical modules, named NPCs, restore\n";
    return 0;
}
