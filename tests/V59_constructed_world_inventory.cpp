#include "galaxy/RegionalProductionNetwork.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
using namespace galaxy;
namespace re=galaxy::regional_events;
namespace ind=galaxy::industry;
static void ck(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void reject(F fn,const char* why){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}ck(failed,why);}
static void send(PersistentWorld& w,std::uint64_t id,EntityId ship,Action a,EntityId target=0,std::int64_t amount=1,int choice=0){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;c.issuer=7;c.actor=ship;c.action=a;
 c.target=target;c.amount=amount;c.thrust_x=choice;
 ck(w.submit(c),"physical player action was refused");w.step();
 ck(w.world().events().back().result==Outcome::Applied,"physical player action did not execute");
}
static ind::NetworkState network(EntityId home){
 ind::NetworkState n;n.tick=0;n.initial_mass=10000;n.warehouse.capacity_grams=40000;n.warehouse.grams["aluminum"]=10000;
 ind::NetworkMachine first;first.id=10;first.worksite=home;first.workers={1,2};
 first.recipe={"aluminum-press","press",{{"aluminum",1000}},{"plates",950},{"offcuts",50}};
 first.machine={1,"press",true,true,true};first.state.feed.capacity_grams=15000;
 first.state.finished.capacity_grams=15000;first.state.scrap.capacity_grams=15000;
 ind::NetworkMachine second;second.id=20;second.worksite=home;second.workers={3,4};
 second.recipe={"frame-finisher","assemble",{{"plates",950}},{"frames",900},{"scrap",50}};
 second.machine={1,"assemble",true,true,true};second.state.feed.capacity_grams=15000;
 second.state.finished.capacity_grams=15000;second.state.scrap.capacity_grams=15000;
 n.machines={first,second};return n;
}
static void set_region(re::CheckpointStore& region){re::Scheduler s;galaxy::cold_colony::State c;
 c.population=6;c.habitat.food_kg=100;c.habitat.water_kg=100;c.habitat.oxygen_kg=100;
 s.add_colony("home",c);ck(region.save(s,0)==1,"region bootstrap failed");}
int main(){
 const auto base=(std::filesystem::temp_directory_path()/"sg_v52_canonical_warehouse").string();
 const auto path=base+".db",copy=base+".replay",corrupt=base+".tampered";std::filesystem::remove(path);std::filesystem::remove(copy);std::filesystem::remove(corrupt);
 try{
  Simulation first(5152);auto home=first.add_colony_ship(7,{0,0},300,6);
  first.provision_colony_genesis(home,1000,3000,1000,1000,1000);
  auto plain=first.add_asteroid({2000,0},50);
  auto geological=first.add_geological_asteroid({1500,0},AsteroidClass::Silicate,10,3000);
  first.enable_individual_workforce();first.upgrade_assignment_snapshot_format();first.enable_medical_workforce_rules();
  PersistentWorld w(path,std::move(first));
  send(w,1,home,Action::Mine,plain,25);
  send(w,2,home,Action::RefineOre,0,25);
  send(w,3,home,Action::FabricateModule,0,10);
  send(w,4,home,Action::ConstructOutpost);
  ck(!w.world().built_outposts().empty(),"outpost not constructed from materials");
  auto site=w.world().built_outposts().begin()->first;
  send(w,5,home,Action::ConstructStorage,site);
  send(w,6,home,Action::Mine,geological,6);
  send(w,7,home,Action::RefineMineral,10,100);
  send(w,8,home,Action::TransferStorageStack,site,10,0);
  ck(w.world().storage(site)->goods_kg.at("aluminum")==10,"warehouse did not receive mined/refined aluminum");
  for(const auto& family:std::vector<std::pair<std::string,std::vector<std::uint64_t>>>{{"east",{1,2,3}},{"west",{4,5,6}}}){
   NpcAppearanceStore store(path,family.first,5152,[&](auto id){return w.world().npc_records().count(id)!=0;});
   store.register_settlement(home,3);for(auto id:family.second)store.enroll_family_founder(id,home);
  }
  re::CheckpointStore region(path,"world-backed-net");set_region(region);
  re::RegionalModeAuthority owner(region);owner.enroll();
  ind::RegionalProductionNetwork net(w,region,owner,path,"home",{"east","west"});
  auto state=network(home);const auto material_before=w.world().total_material_kg();
  reject([&]{net.bootstrap_from_constructed_storage(state,"world-debit",site,9000,ind::NetworkFault::AfterPayload);},"first world escrow write was not rolled back");
  ck(w.world().storage(site)->goods_kg.at("aluminum")==10&&w.world().storage(site)->network_escrow_kg==0,"failed world debit leaked stock");
  reject([&]{net.bootstrap_from_constructed_storage(state,"world-debit",site,9000,ind::NetworkFault::AfterEvent);},"late world escrow write was not rolled back");
  reject([&]{net.bootstrap_from_constructed_storage(state,"world-debit",site,9000,ind::NetworkFault::AfterWorldSnapshot);},"failed post-snapshot world debit leaked stock");
  ck(w.world().storage(site)->goods_kg.at("aluminum")==10&&w.world().storage(site)->network_escrow_kg==0,"rollback changed canonical constructed warehouse");
  net.bootstrap_from_constructed_storage(state,"world-debit",site,9000);
  ck(w.world().storage(site)->goods_kg.count("aluminum")==0&&w.world().storage(site)->network_escrow_kg==10,"real world stock was not reserved");
  ck(net.active().warehouse.amount("aluminum")==10000,"network stock does not match physically withdrawn goods");
  ck(w.world().total_material_kg()==material_before,"canonical world mass changed during escrow");
  reject([&]{net.bootstrap_from_constructed_storage(state,"duplicate",site,9001);},"same constructed source seeded twice");
  net.verify();
  {PersistentWorld reopened(path,Simulation(5152));re::CheckpointStore rg(path,"world-backed-net");re::RegionalModeAuthority auth(rg);
   ind::RegionalProductionNetwork other(reopened,rg,auth,path,"home",{"east","west"});other.verify();
   ck(reopened.world().storage(site)->network_escrow_kg==10,"restart lost canonical world escrow");}
  PersistentWorld::recover_copy(path,copy);
  {PersistentWorld recovered(copy,Simulation(5152));ck(recovered.world().storage(site)->network_escrow_kg==10,"independent world journal recovery lost reserved material");}
  // An offline edit to the second ledger must be rejected on fresh restore.
  sqlite3* original=nullptr;sqlite3* forged=nullptr;
  ck(sqlite3_open(path.c_str(),&original)==SQLITE_OK,"cannot open source for SQLite-safe backup");
  ck(sqlite3_open(corrupt.c_str(),&forged)==SQLITE_OK,"cannot open forged ledger fixture");
  sqlite3_backup* backup=sqlite3_backup_init(forged,"main",original,"main");
  ck(backup!=nullptr,"cannot begin WAL-aware integrity fixture backup");
  const int copied=sqlite3_backup_step(backup,-1);
  ck(copied==SQLITE_DONE && sqlite3_backup_finish(backup)==SQLITE_OK,"cannot copy physical ledger fixture");
  sqlite3_close(original);
  ck(sqlite3_exec(forged,"UPDATE resource_ledger SET destination_id='network-escrow:0' WHERE reason='constructed_network_stock_kg'",nullptr,nullptr,nullptr)==SQLITE_OK,"cannot alter source custody evidence");
  sqlite3_close(forged);
  reject([&]{PersistentWorld altered(corrupt,Simulation(5152));(void)altered;},"forged warehouse debit destination was accepted");
  // Actual production transforms the same reserved material, not extra world stock.
  ind::NetworkTransfer t{0,10,ind::NetworkBin::Warehouse,ind::NetworkBin::Feed,"aluminum",2000};
  net.move("feed",t);for(int i=0;i<5;++i)w.step();
  ck(net.advance_cold(5,"cold-work")>=1,"real debited stock not processed");
  ck(w.world().total_material_kg()==material_before,"world mass changed when factory produced goods");
  net.verify();
  sqlite3* db=nullptr;ck(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"cannot open corruption fixture");
  ck(sqlite3_exec(db,"DROP TRIGGER sg_v52_escrow_immutable_del; DELETE FROM sg_v52_world_escrow WHERE scope='world-backed-net'; CREATE TRIGGER sg_v52_escrow_immutable_del BEFORE DELETE ON sg_v52_world_escrow BEGIN SELECT RAISE(ABORT,'immutable world stock escrow');END;",nullptr,nullptr,nullptr)==SQLITE_OK,"cannot corrupt escrow fixture");sqlite3_close(db);
  reject([&]{net.verify();},"deleted authoritative source receipt passed recovery");
  std::cout<<"v52 canonical constructed stock -> world escrow -> two machines; crash/replay/integrity PASS\n";
  std::filesystem::remove(path);std::filesystem::remove(copy);std::filesystem::remove(corrupt);return 0;
 }catch(const std::exception& e){std::cerr<<"v52 warehouse integration FAIL: "<<e.what()<<'\n';return 1;}
}
