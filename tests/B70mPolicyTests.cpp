#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B70m check ")+#x+" line "+std::to_string(__LINE__));}while(0)

static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amt=1,int domain=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;
    c.amount=amt;c.thrust_x=domain;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static const PolicyAudit& find_audit(const std::vector<PolicyAudit>& rows, PolicyDomain d){
    for(const auto& row:rows)if(row.domain==d)return row;
    throw std::runtime_error("missing audit row");
}
int main(){
 const auto tmp=fs::temp_directory_path()/("single_galaxy_b70m_"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);
 try {
   const auto db=(tmp/"policy.sqlite").string(),copy=(tmp/"recovered.sqlite").string();
   // A queued v19 setting must be representable without executing the action.
   {
     Simulation queued(900);const auto ship=queued.add_colony_ship(7,{0,0},60,24);
     Command c{};c.id=800;c.sequence=800;c.issuer=7;c.actor=ship;
     c.target=30;c.action=Action::SetPolicyAssurance;c.amount=3;c.execute_tick=2;
     CHECK(queued.submit(c));
     const auto snap=queued.canonical_state();
     CHECK(snap.rfind("galaxy-kernel-v19|",0)==0);
     CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
     c.id=801;c.sequence=801;c.amount=4;CHECK(!queued.submit(c));
     c.id=802;c.sequence=802;c.thrust_x=4;c.amount=1;CHECK(!queued.submit(c));
   }
   EntityId ship=0,site=0;std::int64_t initial_mass=0;
   {
     Simulation s(908);ship=s.add_colony_ship(7,{0,0},100,24);
     s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
     const auto rock=s.add_asteroid({2000,0},100);
     initial_mass=s.total_material_kg();
     PersistentWorld w(db,std::move(s));
     CHECK(send(w,ship,1,Action::Mine,rock,60).result==Outcome::Applied);
     CHECK(send(w,ship,2,Action::RefineOre,0,60).result==Outcome::Applied);
     CHECK(send(w,ship,3,Action::FabricateModule,0,25).result==Outcome::Applied);
     CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
     site=w.world().built_outposts().begin()->first;
     CHECK(send(w,ship,5,Action::ConstructUtility,site).result==Outcome::Applied);
     CHECK(send(w,ship,6,Action::ConstructHabitat,site).result==Outcome::Applied);
     for(int n=1;n<=3;++n)CHECK(send(w,ship,6+n,Action::TransferHabitatStock,site,100,n).result==Outcome::Applied);
     CHECK(send(w,ship,10,Action::AssignHabitatResidents,site,5).result==Outcome::Applied);
     CHECK(send(w,ship,11,Action::SetPolicyAssurance,site,3,0,8).result==Outcome::Unauthorized);
     CHECK(send(w,ship,12,Action::SetPolicyAssurance,site,3,0).result==Outcome::Applied);
     auto rows=w.world().assess_policies(ship);
     auto audit=find_audit(rows,PolicyDomain::Maintenance);
     CHECK(audit.declared==AssuranceLevel::Strict && audit.required_inspectors==3);
     CHECK(!audit.oversight_staffed && !audit.operational_evidence && !audit.assurance_supported);
     CHECK(send(w,ship,13,Action::AssignPolicyInspectors,site,3,0).result==Outcome::Applied);
     rows=w.world().assess_policies(ship);audit=find_audit(rows,PolicyDomain::Maintenance);
     CHECK(audit.oversight_staffed && !audit.operational_evidence && !audit.assurance_supported);
     CHECK(w.world().assigned_policy_inspectors(site)==3);
     CHECK(send(w,ship,14,Action::AssignUtilityTechnicians,site,3).result==Outcome::NoResources);
     CHECK(send(w,ship,15,Action::AssignUtilityTechnicians,site,1).result==Outcome::Applied);
     CHECK(send(w,ship,16,Action::SetPolicyAssurance,site,2,1).result==Outcome::Applied);
     CHECK(send(w,ship,17,Action::AssignPolicyInspectors,site,2,1).result==Outcome::NoResources);
     CHECK(send(w,ship,18,Action::TransferUtilitySpares,site,3).result==Outcome::Applied);
     CHECK(send(w,ship,19,Action::SetUtilityMaintenance,site,1).result==Outcome::Applied);
     const auto crew=w.world().colony_ship(ship)->crew;
     const auto mass=w.world().total_material_kg();
     CHECK(mass==initial_mass);
     w.steps(70);
     CHECK(w.world().colony_ship(ship)->crew==crew);
     CHECK(w.world().total_material_kg()==mass);
     rows=w.world().assess_policies(ship);
     audit=find_audit(rows,PolicyDomain::Maintenance);
     CHECK(audit.oversight_staffed && audit.operational_evidence && audit.assurance_supported);
     CHECK(!find_audit(rows,PolicyDomain::Healthcare).assurance_supported);
     CHECK(!w.world().assess_resilience(ship).autonomous_growth_ready);
     CHECK(send(w,ship,20,Action::AssignHabitatResidents,site,-2).result==Outcome::NoResources);
     const auto snap=w.world().canonical_state();
     CHECK(snap.rfind("galaxy-kernel-v19|",0)==0);
     CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
     const auto pos=snap.find("policy|");CHECK(pos!=std::string::npos);
     auto damaged=snap;
     const auto marker=damaged.find("|3|3|",pos);CHECK(marker!=std::string::npos);
     damaged.replace(marker+3,1,"9");
     bool bad=false;try{(void)Simulation::from_snapshot(damaged);}catch(...){bad=true;}
     CHECK(bad);
     // Unauthorized or distant commands never change a policy.
     CHECK(send(w,ship,21,Action::AssignPolicyInspectors,site,1,0,8).result==Outcome::Unauthorized);
     Command away{};away.id=22;away.sequence=22;away.issuer=7;away.actor=ship;
     away.action=Action::Thrust;away.thrust_x=1;away.execute_tick=w.world().tick()+1;
     CHECK(w.submit(away));w.step();w.steps(31);
     CHECK(send(w,ship,23,Action::SetPolicyAssurance,site,0,0).result==Outcome::OutOfRange);
     PersistentWorld::recover_copy(db,copy);
   }
   for(const auto& path:{db,copy}){
      PersistentWorld w(path,Simulation(1));
      const auto rows=w.world().assess_policies(ship);
      const auto& audit=find_audit(rows,PolicyDomain::Maintenance);
      CHECK(audit.declared==AssuranceLevel::Strict && audit.assigned_inspectors==3);
      CHECK(audit.assurance_supported);
      CHECK(w.world().total_material_kg()==initial_mass);
   }
   fs::remove_all(tmp);
   std::cout<<"B7.0m policy assurance, finite staffing, service evidence, v19 recovery: PASS\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"B7.0m failure: "<<e.what()<<'\n';fs::remove_all(tmp);return 1;
 }
}
