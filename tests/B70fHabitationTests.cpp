#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CHECK ")+ #x+" @"+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=action;
    c.amount=amount;c.thrust_x=selector;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
    const auto tmp=fs::temp_directory_path()/("b70f_habitat_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp);
    try{
        const auto db=(tmp/"colony.sqlite").string();
        const auto copy=(tmp/"copy.sqlite").string();
        EntityId ship=0,site=0;std::int64_t start_mass=0;
        {
            Simulation s(707);ship=s.add_colony_ship(7,{0,0},100,24);
            s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
            const auto rock=s.add_asteroid({2000,0},60);
            start_mass=s.total_material_kg();
            PersistentWorld w(db,std::move(s));
            CHECK(send(w,ship,1,Action::Mine,rock,50).result==Outcome::Applied);
            CHECK(send(w,ship,2,Action::RefineOre,0,50).result==Outcome::Applied);
            CHECK(send(w,ship,3,Action::FabricateModule,0,25).result==Outcome::Applied);
            CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
            site=w.world().built_outposts().begin()->first;
            CHECK(w.world().habitats().empty());
            CHECK(send(w,ship,5,Action::ConstructHabitat,site,1,0,8).result==Outcome::Unauthorized);
            CHECK(send(w,ship,6,Action::ConstructHabitat,site).result==Outcome::Applied);
            CHECK(w.world().habitat(site)->capacity==8);
            CHECK(w.world().habitat(site)->embodied_ore==4);
            CHECK(send(w,ship,7,Action::ConstructHabitat,site).result==Outcome::InvalidTarget);
            CHECK(send(w,ship,8,Action::AssignHabitatResidents,site,3).result==Outcome::NoResources);
            CHECK(send(w,ship,9,Action::TransferHabitatStock,site,240,1).result==Outcome::Applied);
            CHECK(send(w,ship,10,Action::TransferHabitatStock,site,300,2).result==Outcome::Applied);
            CHECK(send(w,ship,11,Action::TransferHabitatStock,site,240,3).result==Outcome::Applied);
            CHECK(send(w,ship,12,Action::AssignHabitatResidents,site,3).result==Outcome::Applied);
            CHECK(w.world().colony_ship(ship)->crew==21);
            CHECK(w.world().habitat(site)->residents==3);
            CHECK(send(w,ship,13,Action::ConstructFarm,site).result==Outcome::Applied);
            CHECK(send(w,ship,14,Action::TransferFarmStock,site,160,1).result==Outcome::Applied);
            CHECK(send(w,ship,15,Action::TransferFarmStock,site,700,2).result==Outcome::Applied);
            CHECK(send(w,ship,16,Action::TransferFarmStock,site,160,3).result==Outcome::Applied);
            CHECK(send(w,ship,17,Action::TransferFarmStock,site,160,4).result==Outcome::Applied);
            CHECK(send(w,ship,18,Action::TransferFarmStock,site,160,5).result==Outcome::Applied);
            CHECK(send(w,ship,19,Action::AssignFarmWorkers,site,5).result==Outcome::Applied);
            CHECK(send(w,ship,20,Action::AssignFarmWorkers,site,1).result==Outcome::NoResources); // 3+5 full
            CHECK(send(w,ship,21,Action::AssignHabitatResidents,site,1).result==Outcome::NoResources);
            CHECK(w.world().total_material_kg()==start_mass);
            const auto initial=w.world().assess_colony(ship);
            CHECK(initial.stage==ColonyViability::Stage::Survival);
            CHECK(initial.crew_total==24 && initial.housed_colonists==8 && initial.housing_capacity==8);
            CHECK(initial.habitat_residents==3 && initial.farm_workers==5);
            CHECK(!initial.housing_provisioned);
            w.steps(65);
            const auto settled=w.world().assess_colony(ship);
            CHECK(settled.stage==ColonyViability::Stage::Livable);
            CHECK(settled.housing_provisioned && settled.sufficient_quality_of_life);
            CHECK(!settled.autonomous_growth_ready);
            CHECK(w.world().habitat(site)->consecutive_comfort_cycles>=3);
            CHECK(w.world().farm(site)->production_cycles>=3);
            CHECK(w.world().total_material_kg()==start_mass);
            const auto* h=w.world().habitat(site);
            const auto before_food=h->life.food_kg;
            CHECK(send(w,ship,22,Action::TransferHabitatStock,site,-20,1).result==Outcome::Applied);
            CHECK(w.world().habitat(site)->life.food_kg==before_food-20);
            CHECK(w.world().total_material_kg()==start_mass);
            auto forged=Command{};forged.id=23;forged.issuer=7;forged.actor=ship;
            forged.target=site;forged.action=Action::TransferHabitatStock;
            forged.amount=5;forged.thrust_x=4;forged.execute_tick=w.world().tick()+1;
            CHECK(!w.submit(forged));
            forged.thrust_x=1;forged.amount=0;CHECK(!w.submit(forged));
            CHECK(send(w,ship,24,Action::Thrust,0,0,1).result==Outcome::Applied);
            w.steps(35);
            CHECK(w.world().find(ship)->position.x>2500);
            CHECK(send(w,ship,25,Action::TransferHabitatStock,site,10,1).result==Outcome::OutOfRange);
            CHECK(w.world().habitat(site)->residents==3);
            CHECK(w.world().total_material_kg()==start_mass);
            const auto snapshot=w.world().canonical_state();
            CHECK(snapshot.rfind("galaxy-kernel-v12|",0)==0);
            CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
            PersistentWorld::recover_copy(db,copy);
        }
        for(const auto& path:{db,copy}){
            PersistentWorld reopened(path,Simulation(909));
            CHECK(reopened.world().colony_ship(ship)->crew==16);
            CHECK(reopened.world().farm(site)->workers==5);
            CHECK(reopened.world().habitat(site)->residents==3);
            CHECK(reopened.world().habitat(site)->consecutive_comfort_cycles>=3);
            CHECK(reopened.world().total_material_kg()==start_mass);
            reopened.steps(20);
            CHECK(reopened.world().total_material_kg()==start_mass);
        }
        // Tampering a resource receipt must not be mistaken for valid recovery.
        {
            sqlite3* handle=nullptr;
            CHECK(sqlite3_open(copy.c_str(),&handle)==SQLITE_OK);
            char* err=nullptr;
            const int rc=sqlite3_exec(handle,
                "UPDATE resource_ledger SET amount=amount+1 WHERE reason='habitat_stock_transfer_kg'",nullptr,nullptr,&err);
            if(err)sqlite3_free(err);
            CHECK(rc==SQLITE_OK);
            CHECK(sqlite3_changes(handle)>0);
            sqlite3_close(handle);
            bool rejected=false;
            try{PersistentWorld invalid(copy,Simulation(15));(void)invalid;}
            catch(const std::exception&){rejected=true;}
            CHECK(rejected);
        }
        fs::remove_all(tmp);
        std::cout<<"B7.0f habitat construction, local stocks, crew, livability, audit and recovery: PASS\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"B7.0f FAILED: "<<ex.what()<<'\n';
        fs::remove_all(tmp);return 1;
    }
}
