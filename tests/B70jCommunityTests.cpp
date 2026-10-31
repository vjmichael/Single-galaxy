#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B7.0j check ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w, EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;
    c.action=action;c.target=target;c.amount=amount;c.thrust_x=selector;
    c.execute_tick=w.world().tick()+1;
    if(!w.submit(c))throw std::runtime_error("submit rejected id="+std::to_string(id));
    w.step();return w.world().events().back();
}
int main(){
    const auto tmp=fs::temp_directory_path()/("sg_b70j_"+
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
        CHECK(send(w,ship,5,Action::ConstructCommunity,site).result==Outcome::NoResources);
        CHECK(send(w,ship,6,Action::ConstructUtility,site).result==Outcome::Applied);
        CHECK(send(w,ship,7,Action::ConstructFarm,site).result==Outcome::Applied);
        CHECK(send(w,ship,8,Action::ConstructHabitat,site).result==Outcome::Applied);
        CHECK(send(w,ship,9,Action::ConstructCommunity,site).result==Outcome::Applied);
        CHECK(send(w,ship,10,Action::ConstructCommunity,site).result==Outcome::InvalidTarget);
        w.steps(21); // empty/unassigned settlement must not collect artificial penalties
        CHECK(w.world().community(site)->shortages==0);
        CHECK(send(w,ship,11,Action::AssignCommunityStaff,site,1).result==Outcome::NoResources);
        // Supplies must arrive by the physically nearby ship before personnel.
        for(int sel=1;sel<=3;++sel){
            CHECK(send(w,ship,11+sel,Action::TransferHabitatStock,site,180,sel).result==Outcome::Applied);
            CHECK(send(w,ship,14+sel,Action::TransferFarmStock,site,180,sel).result==Outcome::Applied);
        }
        CHECK(send(w,ship,18,Action::TransferFarmStock,site,150,4).result==Outcome::Applied);
        CHECK(send(w,ship,19,Action::TransferFarmStock,site,150,5).result==Outcome::Applied);
        CHECK(send(w,ship,20,Action::AssignHabitatResidents,site,4).result==Outcome::Applied);
        CHECK(send(w,ship,21,Action::AssignFarmWorkers,site,4).result==Outcome::Applied);
        CHECK(send(w,ship,22,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
        CHECK(send(w,ship,23,Action::AssignCommunityStaff,site,4).result==Outcome::NoResources);
        CHECK(send(w,ship,24,Action::AssignCommunityStaff,site,2).result==Outcome::Applied);
        CHECK(send(w,ship,25,Action::TransferCommunityStock,site,24,1).result==Outcome::Applied);
        CHECK(send(w,ship,26,Action::TransferCommunityStock,site,24,2).result==Outcome::Applied);
        { Command invalid{};invalid.id=27;invalid.sequence=27;invalid.issuer=7;
          invalid.actor=ship;invalid.target=site;invalid.action=Action::TransferCommunityStock;
          invalid.amount=1;invalid.thrust_x=3;invalid.execute_tick=w.world().tick()+1;
          CHECK(!w.submit(invalid)); }
        CHECK(send(w,ship,28,Action::TransferCommunityStock,site,1,1,8).result==Outcome::Unauthorized);
        CHECK(send(w,ship,29,Action::TransferCommunityStock,site,1,1,7).result==Outcome::Applied);
        // 3 consecutive 20-tick periods: don't mark healthy/rested at first opening.
        auto before=w.world().assess_resilience(ship);
        CHECK(before.blocker_mask&CommunityServicesMissing);
        CHECK(!before.restorative_leisure_proven);
        w.steps(80);
        auto community=w.world().community(site);
        CHECK(community && community->consecutive_service_cycles>=3);
        CHECK(community->attendants==2 && community->served_visits>=24);
        auto supported=w.world().assess_resilience(ship);
        CHECK(supported.restorative_leisure_proven);
        CHECK(supported.community_supported_sites==1 && supported.occupied_sites==1);
        CHECK(!(supported.blocker_mask&CommunityServicesMissing));
        CHECK(supported.blocker_mask&HealthRestUnmodeled); // no healthcare claim
        CHECK(supported.blocker_mask&WaterRenewalUnverified);
        CHECK(supported.blocker_mask&GrowthPolicyUnmodeled);
        CHECK(!supported.autonomous_growth_ready);
        CHECK(w.world().total_material_kg()==mass);
        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v15|",0)==0);
        auto snap=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
        // Removing supplies is permitted only physically, and ends service.
        const auto remaining_food=community->food_kg;
        CHECK(send(w,ship,30,Action::TransferCommunityStock,site,-remaining_food,1).result==Outcome::Applied);
        w.steps(21);
        CHECK(w.world().community(site)->consecutive_service_cycles==0);
        CHECK(w.world().assess_resilience(ship).blocker_mask&CommunityServicesMissing);
        CHECK(w.world().total_material_kg()==mass);
        // Capacity/role collision is rejected; no phantom worker creation.
        CHECK(send(w,ship,31,Action::AssignUtilityTechnicians,site,3).result==Outcome::NoResources);
        CHECK(send(w,ship,32,Action::AssignHabitatResidents,site,-3).result==Outcome::NoResources);
        // Even with owner credentials, physical distance blocks new deliveries.
        for(int k=0;k<4;++k)CHECK(send(w,ship,33+k,Action::Thrust,0,0,1).result==Outcome::Applied);
        for(int k=0;k<4;++k)CHECK(send(w,ship,37+k,Action::Thrust,0,0,-1).result==Outcome::Applied);
        CHECK(w.world().find(ship)->position.x==4000);
        CHECK(send(w,ship,41,Action::TransferCommunityStock,site,1,2).result==Outcome::OutOfRange);
        CHECK(w.world().total_material_kg()==mass);
        PersistentWorld::recover_copy(db,copy);
      }
      for(const auto& file:{db,copy}){
          PersistentWorld w(file,Simulation(15));
          auto c=w.world().community(site);
          CHECK(c && c->attendants==2 && c->shortages>0);
          CHECK(w.world().total_material_kg()==mass);
          CHECK(w.world().canonical_state().rfind("galaxy-kernel-v15|",0)==0);
      }
      // The ledger must be a separate, authenticated receipt; amounts cannot be forged.
      sqlite3* sql=nullptr;CHECK(sqlite3_open(copy.c_str(),&sql)==SQLITE_OK);
      char* msg=nullptr;
      const auto rc=sqlite3_exec(sql,"UPDATE resource_ledger SET amount=amount+1 WHERE reason='community_stock_transfer_kg' AND amount=24",nullptr,nullptr,&msg);
      if(msg)sqlite3_free(msg);CHECK(rc==SQLITE_OK && sqlite3_changes(sql)>0);
      sqlite3_close(sql);
      bool caught=false;try{PersistentWorld bad(copy,Simulation(15));(void)bad;}catch(...){caught=true;}
      CHECK(caught);
      fs::remove_all(tmp);
      std::cout<<"B7.0j community hall, staff, service, conservation, recovery, tamper: PASS\n";
      return 0;
    }catch(const std::exception& ex){
      std::cerr<<"B7.0j failed: "<<ex.what()<<'\n';fs::remove_all(tmp);return 1;
    }
}
