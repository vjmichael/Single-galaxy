// Creates a genuine persistent colony outpost for local HTTP construction E2E.
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
void build(PersistentWorld& world,EntityId ship,std::uint64_t id,Action action,EntityId target=0,std::int64_t amount=1){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;c.target=target;
    c.action=action;c.amount=amount;c.execute_tick=world.world().tick()+1;
    if(!world.submit(c))throw std::runtime_error("fixture failed to submit");
    world.step();
    if(world.world().events().back().result!=Outcome::Applied)throw std::runtime_error("fixture action failed");
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::invalid_argument("fixture expects path to NEW database");
        if(std::filesystem::exists(argv[1]))throw std::invalid_argument("fixture refuses existing database");
        Simulation simulation(40042);
        const auto ship=simulation.add_colony_ship(7,{0,0},400,8);
        simulation.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto asteroid=simulation.add_asteroid({1000,0},180);
        simulation.enable_individual_workforce();
        PersistentWorld w(argv[1],std::move(simulation));
        build(w,ship,1,Action::Mine,asteroid,80);
        build(w,ship,2,Action::RefineOre,0,80);
        build(w,ship,3,Action::FabricateModule,0,30);
        build(w,ship,4,Action::ConstructOutpost);
        const auto site=w.world().built_outposts().begin()->first;
        std::cout<<"SHIP="<<ship<<" SITE="<<site<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"fixture failed: "<<e.what()<<'\n';return 1;}
}
