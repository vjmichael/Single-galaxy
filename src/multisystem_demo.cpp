// B6.2b executable demonstration: an actual PersistentWorld moves a cargo ship
// from system 1 to system 2 across a checkpoint and sells its ore in system 2.
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
static void command(PersistentWorld& w, std::uint64_t id, EntityId ship, EntityId target,
                    Action action,std::int64_t amount=0){
    Command c{};c.id=id;c.issuer=7;c.actor=ship;c.target=target;c.action=action;c.amount=amount;
    c.execute_tick=w.world().tick()+1;c.sequence=id;
    if(!w.submit(c))throw std::runtime_error("command not accepted");
    w.step();
    if(w.world().events().back().result!=Outcome::Applied)
        throw std::runtime_error("operation rejected: "+to_string(w.world().events().back().result));
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: galaxy_multisystem_demo NEW_DATABASE_PATH\n";return 2;}
    try {
        if(std::filesystem::exists(argv[1]))throw std::runtime_error("refusing existing database");
        Simulation setup(4613);
        setup.add_system(2);
        const auto a=setup.add_station({0,0});
        const auto ship=setup.spawn_ship_docked(7,a,30,100);
        const auto rock=setup.add_asteroid({2000,0},30);
        const auto gate=setup.add_gate(1,{1500,0},2,{5000,0});
        const auto b=setup.add_station_in_system(2,{17000,0});
        setup.configure_market(a,2,1000);setup.configure_market(b,5,1000);
        {
            PersistentWorld world(argv[1],std::move(setup));
            command(world,1,ship,a,Action::RequestUndock);world.steps(2);
            command(world,2,ship,rock,Action::Mine,20);
            std::cout<<"BEFORE_JUMP system="<<world.world().find(ship)->system_id
                     <<" cargo="<<world.world().find(ship)->cargo<<"\n";
            command(world,3,ship,gate,Action::RequestJump);
            std::cout<<"ACCEPTED system="<<world.world().find(ship)->system_id
                     <<" protected="<<world.world().protected_from_attack(ship)<<"\n";
        }
        // A new process could reopen at this boundary; no owner or cargo lost.
        {
            PersistentWorld world(argv[1],Simulation(1));
            world.steps(2);
            std::cout<<"AFTER_JUMP system="<<world.world().find(ship)->system_id
                     <<" cargo="<<world.world().find(ship)->cargo
                     <<" protected="<<world.world().protected_from_attack(ship)<<"\n";
            world.steps(Simulation::kArrivalCloakTicks);
            command(world,4,ship,b,Action::RequestDock);world.steps(2);
            command(world,5,ship,b,Action::UnloadOre,20);
            command(world,6,ship,b,Action::SellOre,20);
            std::cout<<"FINAL system="<<world.world().find(ship)->system_id
                     <<" cargo="<<world.world().find(ship)->cargo
                     <<" credits="<<world.world().wallet(7)
                     <<" ore_total="<<world.world().total_ore()
                     <<" credit_total="<<world.world().total_credits()<<"\n";
        }
        return 0;
    } catch(const std::exception& ex){std::cerr<<"B6.2b demo failed: "<<ex.what()<<'\n';return 1;}
}
