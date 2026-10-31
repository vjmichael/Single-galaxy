#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B7.0i check: ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event cmd(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                 EntityId target=0,std::int64_t quantity=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.execute_tick=w.world().tick()+1;c.action=action;c.target=target;
    c.amount=quantity;c.thrust_x=selector;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
 const auto dir=fs::temp_directory_path()/("sg_b70i_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);
 try{
  const auto db=(dir/"colony.sqlite").string(),copy=(dir/"copy.sqlite").string();
  EntityId ship=0,site=0;std::int64_t original_mass=0;ColonyResilience saved{};
  {
   Simulation s(701);ship=s.add_colony_ship(7,{0,0},100,9);
   s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
   auto rock=s.add_asteroid({2000,0},85);original_mass=s.total_material_kg();
   PersistentWorld w(db,std::move(s));
   auto first=w.world().assess_resilience(ship);
   CHECK(first.blocker_mask&NoOccupiedSettlement);
   CHECK(first.blocker_mask&FoodBufferUnproven);
   CHECK(!first.operational_delegation_candidate && !first.autonomous_growth_ready);
   CHECK(cmd(w,ship,1,Action::Mine,rock,80).result==Outcome::Applied);
   CHECK(cmd(w,ship,2,Action::RefineOre,0,80).result==Outcome::Applied);
   CHECK(cmd(w,ship,3,Action::FabricateModule,0,35).result==Outcome::Applied);
   CHECK(cmd(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
   site=w.world().built_outposts().begin()->first;
   CHECK(cmd(w,ship,5,Action::ConstructUtility,site).result==Outcome::Applied);
   CHECK(cmd(w,ship,6,Action::ConstructFarm,site).result==Outcome::Applied);
   CHECK(cmd(w,ship,7,Action::ConstructHabitat,site).result==Outcome::Applied);
   // Place only the minimum three-period provisions, then settle real crew.
   for(auto [id,sel]:{std::pair<int,int>{8,1},{9,2},{10,3}})
       CHECK(cmd(w,ship,id,Action::TransferFarmStock,site,12,sel).result==Outcome::Applied);
   for(auto [id,sel]:{std::pair<int,int>{11,1},{12,2},{13,3}})
       CHECK(cmd(w,ship,id,Action::TransferHabitatStock,site,6,sel).result==Outcome::Applied);
   CHECK(cmd(w,ship,14,Action::AssignHabitatResidents,site,2).result==Outcome::Applied);
   CHECK(cmd(w,ship,15,Action::AssignFarmWorkers,site,4).result==Outcome::Applied);
   auto under=w.world().assess_resilience(ship);
   CHECK(under.occupied_sites==1 && under.sites_with_reserves==0);
   CHECK(under.blocker_mask&SiteReservesLow);
   CHECK(under.blocker_mask&MaintenanceUnsupported);
   CHECK(under.blocker_mask&RecentShortage);
   // Physical ship -> site deliveries create actual resilience reserves.
   for(auto [id,sel,amt]:{std::tuple<int,int,int>{16,1,288},{17,2,588},
       {18,3,288},{19,4,160},{20,5,160}})
       CHECK(cmd(w,ship,id,Action::TransferFarmStock,site,amt,sel).result==Outcome::Applied);
   for(auto [id,sel,amt]:{std::tuple<int,int,int>{21,1,154},{22,2,154},{23,3,154}})
       CHECK(cmd(w,ship,id,Action::TransferHabitatStock,site,amt,sel).result==Outcome::Applied);
   CHECK(cmd(w,ship,24,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
   CHECK(cmd(w,ship,25,Action::TransferUtilitySpares,site,3).result==Outcome::Applied);
   CHECK(cmd(w,ship,26,Action::SetUtilityMaintenance,site,1).result==Outcome::Applied);
   w.steps(70);
   auto ready=w.world().assess_resilience(ship);
   CHECK(ready.occupied_sites==1 && ready.sites_with_reserves==1);
   CHECK(ready.serviced_sites==1 && ready.maintained_sites==1);
   CHECK(ready.nonmaintenance_residents==1 && ready.spare_modules>=2);
   CHECK(ready.food_buffer_proven);
   CHECK((ready.blocker_mask&255)==0);
   CHECK(ready.operational_delegation_candidate);
   CHECK(!ready.autonomous_growth_ready);
   CHECK((ready.blocker_mask&(WaterRenewalUnverified|HealthRestUnmodeled|GrowthPolicyUnmodeled))==
      (WaterRenewalUnverified|HealthRestUnmodeled|GrowthPolicyUnmodeled));
   CHECK(w.world().assess_colony(ship).stage==ColonyViability::Stage::Livable);
   CHECK(w.world().total_material_kg()==original_mass);
   // Assign the remaining resident to maintenance, displacing rest capacity;
   // delegated readiness immediately fails, despite adequate power/stock.
   CHECK(cmd(w,ship,27,Action::AssignUtilityTechnicians,site,2).result==Outcome::Applied);
   auto over=w.world().assess_resilience(ship);
   CHECK(over.blocker_mask&StaffOvercommitted);
   CHECK(!over.operational_delegation_candidate);
   CHECK(cmd(w,ship,28,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
   CHECK(cmd(w,ship,29,Action::TransferUtilitySpares,site,-2).result==Outcome::Applied);
   auto depleted=w.world().assess_resilience(ship);
   CHECK(depleted.blocker_mask&MaintenanceUnsupported);
   CHECK(!depleted.operational_delegation_candidate);
   CHECK(depleted.maintained_sites==0);
   CHECK(w.world().total_material_kg()==original_mass);
   // Add a SECOND outpost by ordinary inertial flight; the ship cannot
   // teleport a first outpost's ample provisions to the newcomer.
   for(int n=0;n<4;++n)
       CHECK(cmd(w,ship,30+n,Action::Thrust,0,0,1).result==Outcome::Applied);
   for(int n=0;n<4;++n)
       CHECK(cmd(w,ship,34+n,Action::Thrust,0,0,-1).result==Outcome::Applied);
   CHECK(w.world().find(ship)->velocity.x==0);
   CHECK(w.world().find(ship)->position.x==4000);
   {auto event=cmd(w,ship,38,Action::ConstructOutpost);if(event.result!=Outcome::Applied)
      throw std::runtime_error("second outpost rejected: "+std::to_string(static_cast<int>(event.result)));}
   CHECK(w.world().built_outposts().size()==2);
   const auto remote_site=w.world().built_outposts().rbegin()->first;
   CHECK(cmd(w,ship,39,Action::ConstructHabitat,remote_site).result==Outcome::Applied);
   for(auto [id,sel]:{std::pair<int,int>{40,1},{41,2},{42,3}})
       CHECK(cmd(w,ship,id,Action::TransferHabitatStock,remote_site,4,sel).result==Outcome::Applied);
   CHECK(cmd(w,ship,43,Action::AssignHabitatResidents,remote_site,1).result==Outcome::Applied);
   const auto disparity=w.world().assess_resilience(ship);
   CHECK(disparity.occupied_sites==2);
   CHECK(disparity.sites_with_reserves==1 && disparity.lowest_site_reserve_cycles==4);
   CHECK(disparity.blocker_mask&SiteReservesLow);
   CHECK(disparity.blocker_mask&UnreliablePower);
   CHECK(!disparity.operational_delegation_candidate && !disparity.autonomous_growth_ready);
   CHECK(w.world().total_material_kg()==original_mass);
   // The computed diagnostic must survive identical saved world restoration.
   saved=w.world().assess_resilience(ship);
   auto snapshot=w.world().canonical_state();
   CHECK(snapshot.rfind("galaxy-kernel-v14|",0)==0);
   CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
   auto replay=Simulation::from_snapshot(snapshot).assess_resilience(ship);
   CHECK(replay.blocker_mask==saved.blocker_mask);
   CHECK(replay.lowest_site_reserve_cycles==saved.lowest_site_reserve_cycles);
   CHECK(replay.spare_modules==saved.spare_modules);
   PersistentWorld::recover_copy(db,copy);
  }
  for(const auto& path:{db,copy}){
   PersistentWorld w(path,Simulation(700));
   const auto r=w.world().assess_resilience(ship);
   CHECK(r.blocker_mask==saved.blocker_mask);
   CHECK(r.lowest_site_reserve_cycles==saved.lowest_site_reserve_cycles);
   CHECK(r.spare_modules==saved.spare_modules);
   CHECK(!r.autonomous_growth_ready);
   CHECK(w.world().total_material_kg()==original_mass);
  }
  fs::remove_all(dir);
  std::cout<<"B7.0i safe staffing / stocked sites / buffer history / no-growth / durable assessment: PASS\n";
  return 0;
 }catch(const std::exception&ex){std::cerr<<"B7.0i FAILED: "<<ex.what()<<'\n';fs::remove_all(dir);return 1;}
}
