#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/Simulation.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("ART02b line ")+std::to_string(__LINE__)+" "#x);}while(false)
struct Temp {fs::path dir;
 Temp():dir(fs::temp_directory_path()/("sg_art02b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(dir);}
 ~Temp(){std::error_code ec;fs::remove_all(dir,ec);}
};
template<typename F>void rejected(F fn){bool yes=false;try{fn();}catch(const std::exception&){yes=true;}CHECK(yes);}
static Event submit(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId target=0,
                    std::int64_t amount=1,int selector=0){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
 c.issuer=7;c.actor=ship;c.target=target;c.action=a;c.amount=amount;c.thrust_x=selector;
 CHECK(w.submit(c));w.step();return w.world().events().back();
}
int main(){
 Temp t;auto file=(t.dir/"game.sqlite").string();
 constexpr std::uint64_t seed=21002;
 EntityId ship=0,site=0;std::int64_t origin=0;
 try{
  {
   Simulation s(seed);ship=s.add_colony_ship(7,{0,0},100,24);
   s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
   s.enable_individual_workforce();
   auto rock=s.add_asteroid({2000,0},100);
   origin=s.total_material_kg();
   PersistentWorld w(file,std::move(s));
   CHECK(submit(w,ship,1,Action::Mine,rock,40).result==Outcome::Applied);
   CHECK(submit(w,ship,2,Action::RefineOre,0,40).result==Outcome::Applied);
   CHECK(submit(w,ship,3,Action::FabricateModule,0,20).result==Outcome::Applied);
   CHECK(submit(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
   site=w.world().built_outposts().begin()->first;
   CHECK(submit(w,ship,5,Action::ConstructHabitat,site).result==Outcome::Applied);
   for(int i=1;i<=3;++i)CHECK(submit(w,ship,5+i,Action::TransferHabitatStock,site,100,i).result==Outcome::Applied);
   CHECK(submit(w,ship,9,Action::AssignHabitatResidents,site,2).result==Outcome::Applied);
   CHECK(w.world().habitat(site)->residents==2);
   CHECK(w.world().npc_records().at(1).location==site);
   CHECK(w.world().npc_records().at(2).location==site);
   NpcAppearanceStore faces(file,"site_family",seed,[&](std::uint64_t id){return w.world().npc_records().count(id)!=0;});
   faces.register_settlement(site,2);
   faces.enroll_family_founder(1,site);faces.enroll_family_founder(2,site);
   auto policy=[](auto,auto,auto,auto){return true;};
   w.enable_live_family_births("site_family",seed,policy);
   Command forged{};forged.id=888;forged.sequence=888;forged.issuer=7;
   forged.actor=site;forged.target=1;forged.amount=2;forged.action=Action::HostBirth;
   forged.execute_tick=w.world().tick()+1;
   CHECK(!w.submit(forged));
   CHECK(w.submit_host_birth(site,1,2,1001));
   // Submission is journaled; no prematurely created newborn.
   CHECK(w.world().npc_records().size()==24);
   CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
   w.step();
   CHECK(w.world().events().back().result==Outcome::Applied);
   CHECK(w.world().habitat(site)->residents==2);
   CHECK(w.world().habitat(site)->dependents==1);
   CHECK(w.world().npc_records().size()==25);
   const auto child=w.world().npc_records().at(25);
   CHECK(child.role==NpcRole::Dependent && child.parent_a==1 && child.parent_b==2);
   CHECK(child.birth_event==1001 && child.location==site);
   CHECK(faces.family_census(site).current_population==3);
   CHECK(faces.family_census(site).revision==1);
   CHECK(faces.get_birth(1001).child_id==25);
   CHECK(faces.get(25).digest==faces.get_birth(1001).appearance_digest);
   CHECK(w.world().assess_colony(ship).crew_total==25);
   CHECK(w.world().total_material_kg()==origin);
   CHECK(w.world().canonical_state().rfind("galaxy-kernel-v31|",0)==0);
   CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
   CHECK(!w.submit_host_birth(site,1,2,1001));
   // Roll back the already-staged new profile, census and world snapshot together.
   CHECK(w.submit_host_birth(site,1,2,1002));
   const auto before=w.world().canonical_state(); const auto gen=w.generation();
   rejected([&]{w.step(FailPoint::AfterSnapshotWrite);});
   CHECK(w.generation()==gen && w.world().canonical_state()==before);
   CHECK(faces.family_census(site).current_population==3);
   CHECK(!faces.exists(26));
   rejected([&]{faces.get_birth(1002);});
   w.step();CHECK(w.world().npc_records().size()==26);
   CHECK(faces.family_census(site).current_population==4);
   CHECK(faces.get_birth(1002).child_id==26);
   auto broken=w.world().canonical_state();
   const auto pos=broken.find("npc|26|");CHECK(pos!=std::string::npos);
   broken.erase(pos,broken.find('\n',pos)-pos+1);
   rejected([&]{Simulation::from_snapshot(broken);});
   const auto start_food=w.world().habitat(site)->life.food_kg;
   while(w.world().tick()%20)w.step();
   CHECK(w.world().habitat(site)->life.food_kg<=start_food-4);
   CHECK(w.world().total_material_kg()==origin);
   std::cout<<"live births, idempotency, rollback, dependent consumption and snapshot roundtrip passed\n";
  }
  {
   PersistentWorld restarted(file,Simulation(9));
   CHECK(restarted.world().npc_records().size()==26);
   CHECK(restarted.world().habitat(site)->dependents==2);
   CHECK(restarted.world().total_material_kg()==origin);
   NpcAppearanceStore faces(file,"site_family",seed,[&](std::uint64_t id){return restarted.world().npc_records().count(id)!=0;});
   CHECK(faces.family_census(site).current_population==4);
   CHECK(faces.get_birth(1001).child_id==25 && faces.get_birth(1002).child_id==26);
   restarted.enable_live_family_births("site_family",seed,
      [](auto,auto,auto,auto){return true;},[](auto,auto,auto){return true;});
   CHECK(restarted.submit_host_birth(site,1,2,1003));
   restarted.step();
   CHECK(restarted.world().npc_records().size()==27);
   CHECK(faces.family_census(site).current_population==5);
   CHECK(restarted.world().total_material_kg()==origin);
   // Real dependent death must not erase family identity; the other people
   // and physical material survive, and a failed death rolls back fully.
   CHECK(restarted.submit_host_dependent_death(site,25,2001));
   const auto before_death=restarted.world().canonical_state();
   const auto gen_death=restarted.generation();
   rejected([&]{restarted.step(FailPoint::AfterSnapshotWrite);});
   CHECK(restarted.generation()==gen_death && restarted.world().canonical_state()==before_death);
   CHECK(faces.family_census(site).current_population==5);
   rejected([&]{faces.get_death(2001);});
   restarted.step();
   CHECK(restarted.world().events().back().result==Outcome::Applied);
   CHECK(restarted.world().npc_records().count(25)==0);
   CHECK(restarted.world().npc_records().size()==26);
   CHECK(restarted.world().habitat(site)->dependents==2);
   CHECK(faces.family_census(site).current_population==4);
   CHECK(faces.get_death(2001).npc_id==25);
   CHECK(faces.exists(25)); // the deceased keeps a historical face/lineage
   CHECK(restarted.world().total_material_kg()==origin);
  }
  const auto copied=(t.dir/"recovery_copy.sqlite").string();
  PersistentWorld::recover_copy(file,copied);
  {
   PersistentWorld recovered(copied,Simulation(7));
   CHECK(recovered.world().npc_records().size()==26);
   CHECK(recovered.world().habitat(site)->dependents==2);
   CHECK(recovered.world().total_material_kg()==origin);
  }
  sqlite3* raw=nullptr;
  CHECK(sqlite3_open(copied.c_str(),&raw)==SQLITE_OK);
  char* error=nullptr;
  const int rc=sqlite3_exec(raw,"UPDATE npc_appearance_profiles SET digest='0' WHERE npc_id='26'",
      nullptr,nullptr,&error);
  CHECK(rc==SQLITE_OK);
  sqlite3_close(raw);
  rejected([&]{PersistentWorld tampered(copied,Simulation(88));});
  std::cout<<"ART-02b real NPC births + conserved world population + tamper rejection PASS\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"ART-02b FAILED "<<e.what()<<"\n";return 1;}
}
