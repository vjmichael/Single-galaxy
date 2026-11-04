#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4C site: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
namespace {
Event send(PersistentWorld& world,EntityId ship,std::uint64_t id,
           Action action,EntityId target=0,std::int64_t qty=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.action=action;c.target=target;c.amount=qty;c.thrust_x=selector;
    c.execute_tick=world.world().tick()+1;
    CHECK(world.submit(c));world.step();return world.world().events().back();
}
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("sg_departure_staffed_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    EntityId source=0,receiver=0,site=0;
    std::int64_t material=0;
    {
        Simulation genesis(7214);
        source=genesis.add_colony_ship(7,{0,0},100,12);
        receiver=genesis.add_colony_ship(8,{0,0},100,4);
        genesis.provision_colony_genesis(source,1000,3000,1000,1000,1000);
        genesis.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        const auto ore=genesis.add_asteroid({2000,0},70);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        material=genesis.total_material_kg();
        PersistentWorld w(path,std::move(genesis));
        CHECK(send(w,source,1,Action::Mine,ore,60).result==Outcome::Applied);
        CHECK(send(w,source,2,Action::RefineOre,0,60).result==Outcome::Applied);
        CHECK(send(w,source,3,Action::FabricateModule,0,30).result==Outcome::Applied);
        CHECK(send(w,source,4,Action::ConstructOutpost).result==Outcome::Applied);
        site=w.world().built_outposts().begin()->first;
        CHECK(send(w,source,5,Action::ConstructFarm,site).result==Outcome::Applied);
        CHECK(send(w,source,6,Action::ConstructHabitat,site).result==Outcome::Applied);
        CHECK(send(w,source,7,Action::TransferFarmStock,site,25,1).result==Outcome::Applied);
        CHECK(send(w,source,8,Action::TransferFarmStock,site,100,2).result==Outcome::Applied);
        CHECK(send(w,source,9,Action::TransferFarmStock,site,100,3).result==Outcome::Applied);
        CHECK(send(w,source,10,Action::TransferHabitatStock,site,25,1).result==Outcome::Applied);
        CHECK(send(w,source,11,Action::TransferHabitatStock,site,100,2).result==Outcome::Applied);
        CHECK(send(w,source,12,Action::TransferHabitatStock,site,100,3).result==Outcome::Applied);
        CHECK(send(w,source,13,Action::AssignHabitatResidents,site,3).result==Outcome::Applied);
        CHECK(send(w,source,14,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
        CHECK(w.world().colony_ship(source)->crew==7);
        CHECK(w.world().habitat(site)->residents==3);
        CHECK(w.world().farm(site)->workers==2);
        CHECK(w.world().npc_records().size()==16);
        w.steps(180-w.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(w.world(),source,site).risk)>=
              static_cast<int>(colony_safety::Risk::Critical));
        const auto* receiver_stock=w.world().life_support(receiver);
        const auto receiver_food=receiver_stock->food_kg;
        Command depart{};
        depart.id=1001;depart.sequence=1001;depart.issuer=Simulation::kServerAuthority;
        depart.execute_tick=w.world().tick()+1;depart.actor=source;depart.target=receiver;
        depart.action=Action::HostColonyDeparture;
        for(std::uint64_t id=1;id<=12;++id)depart.passenger_ids.push_back(id);
        CHECK(w.submit_host_colony_departure(depart));
        w.step_with_colony_departure();
        CHECK(w.world().colony_ship(source)->crew==0);
        CHECK(w.world().farm(site)->workers==0);
        CHECK(w.world().habitat(site)->residents==0);
        CHECK(w.world().colony_ship(receiver)->crew==16);
        CHECK(w.world().people_physically_aboard(site)==0);
        CHECK(w.world().people_physically_aboard(receiver)==16);
        CHECK(w.world().npc_records().size()==16);
        CHECK(w.world().colony_departures().at(source).relocated_npcs==depart.passenger_ids);
        CHECK(w.world().life_support(receiver)->food_kg==receiver_food);
        CHECK(w.world().total_material_kg()==material);
        for(auto id:depart.passenger_ids){
            const auto& n=w.world().npc_records().at(id);
            CHECK(n.home_ship==receiver && n.location==receiver &&
                  n.assigned_site==receiver && n.role==NpcRole::Ship);
        }
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==
              w.world().canonical_state());
    }
    {
        PersistentWorld w(path,Simulation(55));
        CHECK(w.world().colony_ship(source)->crew==0);
        CHECK(w.world().colony_ship(receiver)->crew==16);
        CHECK(w.world().farm(site)->workers==0);
        CHECK(w.world().habitat(site)->residents==0);
        CHECK(w.world().colony_departures().at(source).relocated_npcs.size()==12);
        CHECK(w.world().total_material_kg()==material);
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path+"-wal");
    std::filesystem::remove(path+"-shm");
    std::cout<<"PASS all source ship+farm+habitat adults moved atomically, all physical stock conserved\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
