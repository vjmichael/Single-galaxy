// DOCK-04: original physical passenger voyage, finite gangways, cargo
// staff reservations, and million-scale demand WITHOUT imaginary residents.
// Staged for the consolidated native matrix; no tests executed here.
#include "galaxy/Dock04Traffic.hpp"
#include "galaxy/PopulationCohorts.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("DOCK04 line ")+std::to_string(__LINE__)+": " #x);}while(0)
static void clear(const std::string& path) {
    for(const auto* suffix:{"","-wal","-shm"}) {
        std::error_code ec;fs::remove(path+suffix,ec);
    }
}
static void dock_ship(PersistentWorld& world,EntityId ship,EntityId station,
                      std::uint64_t id){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;
    c.actor=ship;c.target=station;c.action=Action::RequestDock;
    c.execute_tick=world.world().tick()+1;
    CHECK(world.submit(c));
    world.step();
    CHECK(world.world().events().back().result==Outcome::Applied);
    world.steps(Simulation::kDockTransitionTicks);
    CHECK(world.world().find(ship)->dock_state==DockState::Docked);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::string path=argv[1];clear(path);
    try {
        EntityId station=0,home=0,ferry=0;
        std::uint64_t worker=0,passenger=0,voyage=0;
        {
            Simulation genesis(0xD044);
            station=genesis.add_station({0,0});
            home=genesis.add_colony_ship(7,{0,0},100,8);
            ferry=genesis.add_colony_ship(7,{0,0},100,4);
            const auto rock=genesis.add_geological_asteroid(
                {2200,0},AsteroidClass::Metallic,10,5000);
            genesis.provision_colony_genesis(home,1000,3000,1000,1000,1000);
            genesis.provision_colony_genesis(ferry,1000,3000,1000,1000,1000);
            genesis.enable_individual_workforce();
            genesis.upgrade_assignment_snapshot_format();
            genesis.enable_passenger_life_support();
            genesis.enable_native_galactic_physics(
                {{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
            const auto roster=genesis.npc_records();
            for(const auto& [id,npc]:roster)if(npc.home_ship==home){
                if(!worker)worker=id;
                else if(!passenger){passenger=id;break;}
            }
            CHECK(worker&&passenger&&worker!=passenger);
            const auto original_total=roster.size();
            PersistentWorld world(path,std::move(genesis));
            Command mine{};mine.id=9109;mine.sequence=mine.id;
            mine.execute_tick=world.world().tick()+1;
            mine.issuer=7;mine.actor=home;mine.target=rock;
            mine.action=Action::Mine;mine.amount=1;
            CHECK(world.submit(mine));
            world.step();
            CHECK(world.world().events().back().result==Outcome::Applied);
            CHECK(world.world().find(home)->cargo==1);
            dock_ship(world,home,station,9111);
            dock_ship(world,ferry,station,9112);
            dock04::Traffic traffic(path);
            dock::Ledger workforce(path);
            CHECK(traffic.snapshot(world.world(),station,7).passenger_gangways==2);
            CHECK(!traffic.enqueue(world.world(),station,8,passenger,ferry));
            CHECK(!traffic.enqueue(world.world(),station,7,worker,home));
            CHECK(world.station_dock_worker_transfer(
                7,home,station,worker,true,9113));
            CHECK(workforce.assign(world.world(),station,7,worker));
            CHECK(workforce.active_staff(world.world(),station,7)==1);
            CHECK(workforce.enqueue_freight(world.world(),station,7,home,1));
            CHECK(workforce.snapshot(world.world(),station,7).freight_tonnes==1);
            CHECK(!traffic.enqueue(world.world(),station,7,worker,ferry));
            CHECK(traffic.enqueue(world.world(),station,7,passenger,ferry));
            CHECK(!traffic.enqueue(world.world(),station,7,passenger,ferry));
            CHECK(traffic.snapshot(world.world(),station,7).waiting==1);
            CHECK(traffic.serve_one(world,station,7,worker));
            CHECK(traffic.snapshot(world.world(),station,7).waiting==0);
            CHECK(traffic.snapshot(world.world(),station,7).boarded==1);
            CHECK(traffic.streak(station,7)==1);
            const auto handlers=workforce.service_roster(world.world(),station,7);
            CHECK(!handlers.empty()&&handlers.front()==worker);
            CHECK(workforce.serve_one(world,station,7,handlers.front(),true));
            traffic.freight_completed(station,7);
            CHECK(traffic.streak(station,7)==0);
            CHECK(world.world().find(home)->cargo==0);
            CHECK(world.world().hangar(station,7)->ore==1);
            CHECK(workforce.snapshot(world.world(),station,7).freight_tonnes==0);
            CHECK(world.world().npc_records().at(passenger).location==ferry);
            voyage=world.world().npc_records().at(passenger).transport_id;
            CHECK(voyage!=0 && world.world().voyage_status(voyage)==1);
            CHECK(world.world().passengers_aboard(ferry)==1);
            CHECK(world.world().npc_records().at(worker).location==station);
            CHECK(world.world().npc_records().size()==original_total);
            CHECK(!traffic.enqueue(world.world(),station,7,passenger,ferry));
            CHECK(world.world().colony_ship(home)->crew==8);
            CHECK(world.world().colony_ship(ferry)->crew==4);
            CHECK(world.world().assess_npc_residence().reconciled);
        }
        {
            PersistentWorld world(path,Simulation(0xD045));
            dock04::Traffic traffic(path);
            dock::Ledger workforce(path);
            CHECK(world.world().npc_records().at(passenger).location==ferry);
            CHECK(world.world().npc_records().at(passenger).transport_id==voyage);
            CHECK(world.world().voyage_status(voyage)==1);
            CHECK(traffic.snapshot(world.world(),station,7).boarded==1);
            CHECK(workforce.active_staff(world.world(),station,7)==1);
            CHECK(world.world().hangar(station,7)->ore==1);
            CHECK(traffic.streak(station,7)==0);
            CHECK(world.world().assess_npc_residence().reconciled);
            CHECK(world.station_dock_worker_transfer(
                7,home,station,worker,false,9114));
            CHECK(workforce.release(station,7,worker));
        }
        // Population is aggregate pressure; NEVER a created manifest or
        // another physically boarded NPC. One million residents are O(1)
        // counters, with capacity never exceeding the real sixteen seats.
        PopulationCohorts cohorts;
        cohorts.add({station,2,1,3},1000000);
        cohorts.add({station,4,3,1},1000000);
        CHECK(cohorts.population()==2000000);
        CHECK(cohorts.cohort_count()==2);
        CHECK(dock04::projected_demand(cohorts.population(),500,16)==16);
        CHECK(dock04::projected_demand(0,500,16)==0);
        CHECK(dock04::projected_demand(cohorts.population(),1,0)==0);
        {
            PersistentWorld world(path,Simulation(0xD046));
            CHECK(world.world().npc_records().at(worker).location==home);
            CHECK(world.world().npc_records().at(passenger).location==ferry);
            CHECK(world.world().passengers_aboard(ferry)==1);
            CHECK(world.world().npc_records().size()==12);
        }
        clear(path);
        std::cout<<"DOCK04 staged source matrix: physical ferry and cohort bounds PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DOCK04 staged source matrix FAILED: "<<e.what()<<'\n';
        clear(path);return 1;
    }
}
