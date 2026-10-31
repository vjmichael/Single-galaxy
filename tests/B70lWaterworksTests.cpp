#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <tuple>
#include <stdexcept>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B7.0l check ")+#x+" line "+std::to_string(__LINE__));}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
 EntityId target=0,std::int64_t amount=1,PlayerId issuer=7){
 Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;c.target=target;
 c.action=action;c.amount=amount;c.execute_tick=w.world().tick()+1;
 if(!w.submit(c))throw std::runtime_error("submit rejected id="+std::to_string(id));
 w.step();return w.world().events().back();
}
int main(){
 auto tmp=fs::temp_directory_path()/("sg_b70l_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(tmp);const auto db=(tmp/"orig.sqlite").string(),copy=(tmp/"copy.sqlite").string();
 try{
 // A queued v18 command must survive canonical serialization before execution.
 {
  Simulation queued(88);auto qship=queued.add_colony_ship(7,{0,0},30,8);
  Command c{};c.id=800;c.sequence=800;c.issuer=7;c.actor=qship;
  c.target=99;c.amount=1;c.action=Action::ConstructWaterworks;c.execute_tick=2;
  CHECK(queued.submit(c));
  const auto state=queued.canonical_state();
  CHECK(state.rfind("galaxy-kernel-v18|",0)==0);
  CHECK(Simulation::from_snapshot(state).canonical_state()==state);
 }
 EntityId ship=0,site=0;std::int64_t initial_mass=0;
 {
  Simulation s(908);ship=s.add_colony_ship(7,{0,0},250,24);
  s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
  auto basic=s.add_asteroid({2000,0},100);
  auto hydrous=s.add_geological_asteroid({2000,0},AsteroidClass::Carbonaceous,10,1500);
  initial_mass=s.total_material_kg();
  PersistentWorld w(db,std::move(s));
  CHECK(send(w,ship,1,Action::Mine,basic,80).result==Outcome::Applied);
  CHECK(send(w,ship,2,Action::RefineOre,0,80).result==Outcome::Applied);
  CHECK(send(w,ship,3,Action::FabricateModule,0,30).result==Outcome::Applied);
  CHECK(send(w,ship,4,Action::ConstructOutpost).result==Outcome::Applied);
  site=w.world().built_outposts().begin()->first;
  CHECK(send(w,ship,5,Action::ConstructWaterworks,site).result==Outcome::NoResources);
  CHECK(send(w,ship,6,Action::ConstructUtility,site).result==Outcome::Applied);
  CHECK(send(w,ship,7,Action::ConstructHabitat,site).result==Outcome::Applied);
  CHECK(send(w,ship,8,Action::ConstructWaterworks,site).result==Outcome::Applied);
  CHECK(send(w,ship,9,Action::ConstructWaterworks,site).result==Outcome::InvalidTarget);
  CHECK(send(w,ship,10,Action::TransferWaterworksFeed,site,1000).result==Outcome::NoResources);
  CHECK(send(w,ship,11,Action::Mine,hydrous,100).result==Outcome::Applied);
  CHECK(send(w,ship,12,Action::RefineMineral,1,10000).result==Outcome::Applied);
  CHECK(w.world().colony_ship(ship)->goods_kg.at("water")==1000);
  CHECK(send(w,ship,13,Action::TransferWaterworksFeed,site,1000,8).result==Outcome::Unauthorized);
  CHECK(send(w,ship,14,Action::TransferWaterworksFeed,site,1000).result==Outcome::Applied);
  CHECK(!w.world().colony_ship(ship)->goods_kg.count("water"));
  for(int sel=1;sel<=3;++sel){
    Command c{};c.id=30+sel;c.sequence=c.id;c.issuer=7;c.actor=ship;c.target=site;
    c.action=Action::TransferHabitatStock;c.thrust_x=sel;c.amount=100;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();CHECK(w.world().events().back().result==Outcome::Applied);
  }
  CHECK(send(w,ship,15,Action::AssignHabitatResidents,site,4).result==Outcome::Applied);
  CHECK(w.world().waterworks(site)->raw_feed_kg==1000);
  CHECK(send(w,ship,34,Action::ConstructFarm,site).result==Outcome::Applied);
  for(const auto [id,sel,kg]: {std::tuple<int,int,int>{35,1,100}, {36,2,6}, {37,3,100}}){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;c.target=site;
    c.action=Action::TransferFarmStock;c.thrust_x=sel;c.amount=kg;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();CHECK(w.world().events().back().result==Outcome::Applied);
  }
  CHECK(send(w,ship,38,Action::AssignFarmWorkers,site,2).result==Outcome::Applied);
  while(w.world().tick()%20!=0)w.step();
  // On the first occupied needs tick: +2kg pumped to the greenhouse, -2kg
  // worker water consumption and -4kg of crop planting leaves 2kg. Without
  // the physical water line it would be empty, concealing a worker shortfall.
  CHECK(w.world().farm(site)->life.water_kg==2);
  w.steps(63);
  const auto* p=w.world().waterworks(site);CHECK(p);
  CHECK(p->purified_total_kg>=54 && p->brine_kg*9==p->purified_total_kg);
  CHECK(p->raw_feed_kg+p->brine_kg+p->purified_total_kg==1000);
  const auto a=w.world().assess_water_supply(ship);
  CHECK(a.occupied_sites==1 && a.equipped_sites==1 && a.measured_sites==1);
  CHECK(a.short_window_coverage && !a.indefinitely_renewable);
  CHECK(w.world().total_material_kg()==initial_mass);
  const auto snap=w.world().canonical_state();
  CHECK(snap.rfind("galaxy-kernel-v18|",0)==0);
  CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
  auto forged=snap;auto pos=forged.find("waterworks|");CHECK(pos!=std::string::npos);
  auto end=forged.find('\n',pos);CHECK(end!=std::string::npos);
  auto row=forged.substr(pos,end-pos);auto needle="|"+std::to_string(p->brine_kg)+"|";
  auto where=row.find(needle);CHECK(where!=std::string::npos);
  forged.replace(pos+where+1,std::to_string(p->brine_kg).size(),std::to_string(p->brine_kg+1));
  bool reject=false;try{(void)Simulation::from_snapshot(forged);}catch(...){reject=true;}CHECK(reject);
  // Command authority still enforces physical transfer distance, even with
  // existing processed local water and an authenticated owner.
  Command accel{};accel.id=41;accel.sequence=41;accel.issuer=7;accel.actor=ship;
  accel.action=Action::Thrust;accel.thrust_x=1;accel.execute_tick=w.world().tick()+1;
  CHECK(w.submit(accel));w.step();w.steps(30);
  CHECK(send(w,ship,42,Action::TransferWaterworksFeed,site,-1000).result==Outcome::OutOfRange);
  CHECK(w.world().total_material_kg()==initial_mass);
  PersistentWorld::recover_copy(db,copy);
 }
 for(const auto& path:{db,copy}){
  PersistentWorld w(path,Simulation(9));
  CHECK(w.world().waterworks(site));
  CHECK(w.world().total_material_kg()==initial_mass);
  CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
 }
 sqlite3* handle=nullptr;CHECK(sqlite3_open(copy.c_str(),&handle)==SQLITE_OK);
 char* msg=nullptr;
 auto rc=sqlite3_exec(handle,"UPDATE resource_ledger SET amount=amount+1 WHERE reason='waterworks_feed_transfer_kg'",nullptr,nullptr,&msg);
 if(msg)sqlite3_free(msg);CHECK(rc==SQLITE_OK && sqlite3_changes(handle)>0);sqlite3_close(handle);
 bool rejected=false;try{PersistentWorld bad(copy,Simulation(2));(void)bad;}catch(...){rejected=true;}CHECK(rejected);
 fs::remove_all(tmp);std::cout<<"B7.0l geological waterworks conservation/delivery/recovery/ledger tamper PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<"B7.0l FAIL: "<<e.what()<<'\n';fs::remove_all(tmp);return 1;}
}
