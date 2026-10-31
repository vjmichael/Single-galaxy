#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/PopulationCohorts.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B70n check ")+ #x +" @"+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amt=1,int selector=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;
    c.amount=amt;c.thrust_x=selector;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
 const auto tmp=fs::temp_directory_path()/("single_galaxy_b70n_"+
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);
 try{
   // Stratified populations are counted, not iterated. Conserve person counts
   // and retain heterogeneity instead of collapsing everyone to one average.
   PopulationCohorts cohorts;
   CohortKey workers{1,2,3,4},workers_new{2,2,3,4},novices{1,2,0,1};
   constexpr std::uint64_t trillion=1000000000000ULL;
   const auto t0=std::chrono::steady_clock::now();
   cohorts.add(workers,trillion);cohorts.add(novices,300000000000ULL);
   CHECK(cohorts.population()==1300000000000ULL && cohorts.cohort_count()==2);
   CHECK(cohorts.migrate_fraction(workers,workers_new,125,300000000000ULL)==125000000000ULL);
   CHECK(cohorts.count(workers_new)==125000000000ULL &&
         cohorts.count(workers)==875000000000ULL &&
         cohorts.count(novices)==300000000000ULL &&
         cohorts.population()==1300000000000ULL);
   bool rejected=false;
   try{cohorts.transfer(novices,workers_new,500000000000ULL);}catch(const std::invalid_argument&){rejected=true;}
   CHECK(rejected && cohorts.population()==1300000000000ULL);
   rejected=false;
   try{cohorts.add({3,1,5,0},1);}catch(const std::invalid_argument&){rejected=true;}
   CHECK(rejected);
   const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now()-t0).count();
   EntityId ship=0,site=0;
   const auto db=(tmp/"workforce.sqlite").string(),copy=(tmp/"copy.sqlite").string();
   std::int64_t mass=0;
   {
      Simulation s(1701);ship=s.add_colony_ship(7,{0,0},100,24);
      s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
      s.enable_individual_workforce();
      CHECK(s.npc_records().size()==24);
      CHECK(s.canonical_state().rfind("galaxy-kernel-v20|",0)==0);
      CHECK(Simulation::from_snapshot(s.canonical_state()).canonical_state()==s.canonical_state());
      // Existing worlds are never silently upgraded mid-session.
      const auto rock=s.add_asteroid({2000,0},100);
      mass=s.total_material_kg();
      PersistentWorld w(db,std::move(s));
      CHECK(send(w,ship,1,Action::Mine,rock,40).result==Outcome::Applied);
      CHECK(send(w,ship,2,Action::RefineOre,0,40).result==Outcome::Applied);
      CHECK(send(w,ship,3,Action::FabricateModule,0,20).result==Outcome::Applied);
      CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
      site=w.world().built_outposts().begin()->first;
      CHECK(send(w,ship,5,Action::ConstructFarm,site).result==Outcome::Applied);
      CHECK(send(w,ship,6,Action::TransferFarmStock,site,200,1).result==Outcome::Applied);
      CHECK(send(w,ship,7,Action::TransferFarmStock,site,900,2).result==Outcome::Applied);
      CHECK(send(w,ship,8,Action::TransferFarmStock,site,200,3).result==Outcome::Applied);
      CHECK(send(w,ship,9,Action::TransferFarmStock,site,200,4).result==Outcome::Applied);
      CHECK(send(w,ship,10,Action::TransferFarmStock,site,200,5).result==Outcome::Applied);
      CHECK(send(w,ship,11,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
      CHECK(w.world().npc_records().at(1).role==NpcRole::Farm);
      CHECK(w.world().npc_records().at(2).role==NpcRole::Farm);
      CHECK(w.world().colony_ship(ship)->crew==22);
      w.steps(55);
      CHECK(w.world().npc_records().at(1).farm_xp>0);
      const auto mentor_xp=w.world().npc_records().at(1).farm_xp;
      CHECK(w.world().assess_farm_workforce(site).cooperative_cycles>0);
      CHECK(send(w,ship,12,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
      CHECK(w.world().npc_records().at(3).farm_xp==0);
      CHECK(send(w,ship,13,Action::AssignFarmMentor,site,1,3,8).result==Outcome::Unauthorized);
      CHECK(send(w,ship,14,Action::AssignFarmMentor,site,1,3).result==Outcome::Applied);
      CHECK(w.world().npc_records().at(3).mentor_id==1);
      CHECK(send(w,ship,15,Action::AssignFarmMentor,site,1,4).result==Outcome::NoResources);
      CHECK(w.world().npc_records().at(4).mentor_id==0);
      // Mentor and apprentice must actually share a workplace; no remote assignment.
      CHECK(send(w,ship,16,Action::AssignFarmMentor,site,2,20).result==Outcome::InvalidTarget);
      w.steps(35);
      CHECK(w.world().npc_records().at(1).farm_xp>mentor_xp);
      CHECK(w.world().npc_records().at(3).farm_xp>w.world().npc_records().at(4).farm_xp);
      CHECK(w.world().total_material_kg()==mass);
      CHECK(send(w,ship,17,Action::AssignFarmWorkers,site,-1).result==Outcome::Applied);
      CHECK(w.world().npc_records().at(3).mentor_id==0);
      CHECK(w.world().npc_records().at(1).role==NpcRole::Ship);
      CHECK(send(w,ship,18,Action::AssignFarmMentor,site,1,3).result==Outcome::InvalidTarget);
      CHECK(w.world().npc_records().size()==24);
      CHECK(w.world().colony_ship(ship)->crew+w.world().farm(site)->workers==24);
      const auto state=w.world().canonical_state();
      CHECK(Simulation::from_snapshot(state).canonical_state()==state);
      auto bad=state;
      const auto pos=bad.find("npc|3|");CHECK(pos!=std::string::npos);
      const auto role=bad.find("|1|",pos+6);CHECK(role!=std::string::npos);
      bad.replace(role+1,1,"9");
      rejected=false;try{(void)Simulation::from_snapshot(bad);}catch(const std::runtime_error&){rejected=true;}
      CHECK(rejected);
      auto missing=state;
      const auto first=missing.find("npc|1|");CHECK(first!=std::string::npos);
      const auto end=missing.find('\n',first);CHECK(end!=std::string::npos);
      missing.erase(first,end-first+1);
      rejected=false;try{(void)Simulation::from_snapshot(missing);}catch(const std::runtime_error&){rejected=true;}
      CHECK(rejected); // no silent population disappearance
      PersistentWorld::recover_copy(db,copy);
   }
   for(const auto& path:{db,copy}){
       PersistentWorld w(path,Simulation(99));
       CHECK(w.world().npc_records().size()==24);
       CHECK(w.world().npc_records().at(3).farm_xp>0);
       CHECK(w.world().npc_records().at(3).mentor_id==0);
       CHECK(w.world().total_material_kg()==mass);
       CHECK(w.world().colony_ship(ship)->crew+w.world().farm(site)->workers==24);
   }
   fs::remove_all(tmp);
   std::cout<<"B7.0n identities, real-job XP, 1:1 mentorship, count conservation, v20 SQLite recovery PASS; "
            <<"1.3T cohort inhabitants processed in "<<elapsed<<" microseconds (local observation)\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"B7.0n FAILED: "<<e.what()<<"\n";
   fs::remove_all(tmp);return 1;
 }
}
