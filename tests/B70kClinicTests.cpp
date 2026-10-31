#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B7.0k check ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;
    c.action=action;c.target=target;c.amount=amount;c.thrust_x=selector;
    c.execute_tick=w.world().tick()+1;
    if(!w.submit(c))throw std::runtime_error("submit rejected id="+std::to_string(id));
    w.step();return w.world().events().back();
}
int main(){
    const auto tmp=fs::temp_directory_path()/("sg_b70k_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp);
    const auto db=(tmp/"original.sqlite").string(),copy=(tmp/"copy.sqlite").string();
    try{
       EntityId ship=0,site=0;std::int64_t mass=0;
       {
        Simulation s(20261031);ship=s.add_colony_ship(7,{0,0},100,24);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto rock=s.add_asteroid({2000,0},90);mass=s.total_material_kg();
        PersistentWorld w(db,std::move(s));
        CHECK(send(w,ship,1,Action::Mine,rock,80).result==Outcome::Applied);
        CHECK(send(w,ship,2,Action::RefineOre,0,80).result==Outcome::Applied);
        CHECK(send(w,ship,3,Action::FabricateModule,0,35).result==Outcome::Applied);
        CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
        site=w.world().built_outposts().begin()->first;
        CHECK(send(w,ship,5,Action::ConstructClinic,site).result==Outcome::NoResources);
        CHECK(send(w,ship,6,Action::ConstructUtility,site).result==Outcome::Applied);
        CHECK(send(w,ship,7,Action::ConstructFarm,site).result==Outcome::Applied);
        CHECK(send(w,ship,8,Action::ConstructHabitat,site).result==Outcome::Applied);
        CHECK(send(w,ship,9,Action::ConstructClinic,site).result==Outcome::Applied);
        CHECK(send(w,ship,10,Action::ConstructClinic,site).result==Outcome::InvalidTarget);
        CHECK(send(w,ship,11,Action::ConstructCommunity,site).result==Outcome::Applied);
        w.steps(21);
        CHECK(w.world().clinic(site)->shortages==0); // unoccupied clinic not punished
        CHECK(send(w,ship,12,Action::AssignClinicStaff,site,1).result==Outcome::NoResources);
        for(int sel=1;sel<=3;++sel){
            CHECK(send(w,ship,12+sel,Action::TransferHabitatStock,site,140,sel).result==Outcome::Applied);
            CHECK(send(w,ship,15+sel,Action::TransferFarmStock,site,140,sel).result==Outcome::Applied);
        }
        CHECK(send(w,ship,19,Action::TransferFarmStock,site,140,4).result==Outcome::Applied);
        CHECK(send(w,ship,20,Action::TransferFarmStock,site,140,5).result==Outcome::Applied);
        CHECK(send(w,ship,21,Action::AssignHabitatResidents,site,4).result==Outcome::Applied);
        CHECK(send(w,ship,22,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
        CHECK(send(w,ship,23,Action::AssignClinicStaff,site,2).result==Outcome::Applied);
        CHECK(send(w,ship,24,Action::AssignCommunityStaff,site,2).result==Outcome::Applied);
        CHECK(send(w,ship,25,Action::AssignClinicStaff,site,3).result==Outcome::NoResources);
        CHECK(send(w,ship,26,Action::AssignUtilityTechnicians,site,1).result==Outcome::NoResources);
        CHECK(send(w,ship,27,Action::TransferClinicStock,site,20,1).result==Outcome::Applied);
        CHECK(send(w,ship,28,Action::TransferClinicStock,site,20,2).result==Outcome::Applied);
        CHECK(send(w,ship,29,Action::TransferCommunityStock,site,24,1).result==Outcome::Applied);
        CHECK(send(w,ship,30,Action::TransferCommunityStock,site,24,2).result==Outcome::Applied);
        Command bad{};bad.id=31;bad.sequence=31;bad.issuer=7;bad.actor=ship;
        bad.target=site;bad.action=Action::TransferClinicStock;bad.amount=1;
        bad.thrust_x=3;bad.execute_tick=w.world().tick()+1;
        CHECK(!w.submit(bad));
        CHECK(send(w,ship,32,Action::TransferClinicStock,site,3,1,8).result==Outcome::Unauthorized);
        w.steps(81);
        const auto *c=w.world().clinic(site);
        CHECK(c && c->consecutive_care_cycles>=3 && c->active_cases==0);
        CHECK(w.world().assess_resilience(ship).basic_clinic_access_proven);
        CHECK(w.world().assess_resilience(ship).blocker_mask&HealthRestUnmodeled);
        CHECK(!w.world().assess_resilience(ship).autonomous_growth_ready);
        CHECK(w.world().total_material_kg()==mass);
        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v16|",0)==0);
        const auto snap=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
        const auto habFood=w.world().habitat(site)->life.food_kg;
        CHECK(habFood>0);
        CHECK(send(w,ship,33,Action::TransferHabitatStock,site,-habFood,1).result==Outcome::Applied);
        w.steps(21);
        CHECK(w.world().clinic(site)->active_cases==1);
        CHECK(w.world().clinic(site)->recorded_cases==1);
        CHECK(w.world().clinic(site)->consecutive_care_cycles==0);
        CHECK(send(w,ship,34,Action::AssignClinicStaff,site,1).result==Outcome::Applied);
        CHECK(send(w,ship,35,Action::TransferHabitatStock,site,100,1).result==Outcome::Applied);
        w.steps(21);
        c=w.world().clinic(site);
        CHECK(c && c->active_cases==0 && c->treated_cases==1);
        CHECK(c->medical_waste_kg==1 && c->wastewater_kg==1);
        CHECK(c->water_kg==19 && c->substrate_kg==19);
        CHECK(c->consecutive_care_cycles==0); // one medic can treat but not cover six people
        CHECK(!w.world().assess_resilience(ship).basic_clinic_access_proven);
        CHECK(send(w,ship,36,Action::AssignClinicStaff,site,2).result==Outcome::Applied);
        for(int k=0;k<4;++k)CHECK(send(w,ship,40+k,Action::Thrust,0,0,1).result==Outcome::Applied);
        for(int k=0;k<4;++k)CHECK(send(w,ship,44+k,Action::Thrust,0,0,-1).result==Outcome::Applied);
        CHECK(w.world().find(ship)->position.x==4000);
        CHECK(send(w,ship,48,Action::TransferClinicStock,site,1,1).result==Outcome::OutOfRange);
        CHECK(w.world().total_material_kg()==mass);
        PersistentWorld::recover_copy(db,copy);
       }
       for(const auto& file:{db,copy}){
           PersistentWorld w(file,Simulation(123));
           const auto c=w.world().clinic(site);
           CHECK(c && c->treated_cases==1 && c->active_cases==0);
           CHECK(c->recorded_cases==1 && c->water_kg==19);
           CHECK(w.world().canonical_state().rfind("galaxy-kernel-v16|",0)==0);
           CHECK(w.world().total_material_kg()==mass);
       }
       sqlite3* sql=nullptr;CHECK(sqlite3_open(copy.c_str(),&sql)==SQLITE_OK);
       char* msg=nullptr;
       const auto rc=sqlite3_exec(sql,"UPDATE resource_ledger SET amount=amount+1 WHERE reason='clinic_stock_transfer_kg' AND amount=20",nullptr,nullptr,&msg);
       if(msg)sqlite3_free(msg);CHECK(rc==SQLITE_OK && sqlite3_changes(sql)>0);
       sqlite3_close(sql);
       bool caught=false;
       try{PersistentWorld tampered(copy,Simulation(123));(void)tampered;}catch(...){caught=true;}
       CHECK(caught);
       fs::remove_all(tmp);
       std::cout<<"B7.0k clinic construction, physical care, patient recovery, conservation, restart and ledger defense: PASS\n";
       return 0;
    }catch(const std::exception& e){
       std::cerr<<"B7.0k failed: "<<e.what()<<'\n';fs::remove_all(tmp);return 1;
    }
}
