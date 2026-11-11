// DOCK-06 source-level acceptance. Staged for consolidated native matrix:
// original shore worker identity -> collision-tested finite native pedestrian
// movements -> restart, original-world custody return, no spoofed local coords.
#include "galaxy/Dock06Corridor.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(expr) do{if(!(expr))throw std::runtime_error(std::string("DOCK06 line ")+std::to_string(__LINE__)+": " #expr);}while(0)
static void erase(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+suffix,ec);
    }
}
static void dock(PersistentWorld& world,EntityId ship,EntityId station){
    Command c{};c.id=61001;c.sequence=c.id;c.issuer=7;
    c.actor=ship;c.target=station;c.execute_tick=world.world().tick()+1;
    c.action=Action::RequestDock;
    CHECK(world.submit(c));
    world.step();
    CHECK(world.world().events().back().result==Outcome::Applied);
    world.steps(Simulation::kDockTransitionTicks);
    CHECK(world.world().find(ship)->dock_state==DockState::Docked);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string file=argv[1];
    erase(file);
    try{
        CHECK(dock06::Corridor::all_route_points_clear());
        EntityId station=0,ship=0;
        std::uint64_t named=0;
        std::int64_t saved_x=0,saved_z=0;
        {
            Simulation genesis(0xD066);
            station=genesis.add_station({0,0});
            ship=genesis.add_colony_ship(7,{0,0},100,6);
            genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
            genesis.enable_individual_workforce();
            genesis.upgrade_assignment_snapshot_format();
            genesis.enable_native_galactic_physics(
                {{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
            for(const auto& [id,n]:genesis.npc_records())
                if(n.home_ship==ship){named=id;break;}
            CHECK(named>0);
            PersistentWorld world(file,std::move(genesis));
            dock(world,ship,station);
            CHECK(world.station_dock_worker_transfer(
                7,ship,station,named,true,61002));
            dock::Ledger shifts(file);
            CHECK(shifts.assign(world.world(),station,7,named));
            dock04::Traffic bookings(file);
            dock06::Corridor corridor(file);
            corridor.synchronize(world.world(),station,7,world.generation());
            const auto first=corridor.observe(world.world(),station,7);
            CHECK(first.total==1 && first.workers==1);
            CHECK(first.visible.size()==1);
            CHECK(first.visible.front().npc==named);
            CHECK(!corridor.worker_ready(world.world(),station,7,named));
            for(int step=0;step<128 && !corridor.worker_ready(
                world.world(),station,7,named);++step)
                CHECK(corridor.advance(world.world(),station,7));
            CHECK(corridor.worker_ready(world.world(),station,7,named));
            const auto settled=corridor.observe(world.world(),station,7);
            CHECK(settled.total==1 && settled.workers==1);
            saved_x=settled.visible[0].x_mm;
            saved_z=settled.visible[0].z_mm;
            CHECK(station_walk::clear(saved_x,saved_z));
            CHECK(world.world().npc_records().at(named).location==station);
            CHECK(!corridor.advance(world.world(),station,7));
        }
        {
            PersistentWorld world(file,Simulation(0xD067));
            dock::Ledger shifts(file);
            dock06::Corridor corridor(file);
            corridor.synchronize(world.world(),station,7,world.generation());
            const auto received=corridor.observe(world.world(),station,7);
            CHECK(received.visible.size()==1);
            CHECK(received.visible[0].npc==named);
            CHECK(received.visible[0].x_mm==saved_x);
            CHECK(received.visible[0].z_mm==saved_z);
            CHECK(corridor.worker_ready(world.world(),station,7,named));
            CHECK(world.station_dock_worker_transfer(
                7,ship,station,named,false,61003));
            CHECK(shifts.release(station,7,named));
            corridor.synchronize(world.world(),station,7,world.generation());
            CHECK(corridor.observe(world.world(),station,7).total==0);
            CHECK(world.world().npc_records().at(named).location==ship);
            CHECK(world.world().assess_npc_residence().reconciled);
        }
        {
            PersistentWorld world(file,Simulation(0xD068));
            dock06::Corridor corridor(file);
            corridor.synchronize(world.world(),station,7,world.generation());
            CHECK(corridor.observe(world.world(),station,7).total==0);
            CHECK(world.world().npc_records().at(named).location==ship);
        }
        erase(file);
        std::cout<<"DOCK-06 staged native station corridor/restart PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DOCK-06 staged corridor source failure: "<<e.what()<<'\n';
        erase(file);
        return 1;
    }
}
