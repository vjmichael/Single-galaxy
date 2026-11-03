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
 ind::NetworkState n;n.tick=0;n.initial_mass=10000;n.warehouse.capacity_grams=40000;
 n.warehouse.grams["aluminum"]=10000;
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
static void region_seed(re::CheckpointStore& region){
 re::Scheduler s;galaxy::cold_colony::State c;c.population=6;
 c.habitat.food_kg=100;c.habitat.water_kg=100;c.habitat.oxygen_kg=100;
 s.add_colony("home",c);ck(region.save(s,0)==1,"regional census bootstrap failed");
}
int main(){
 const auto base=(std::filesystem::temp_directory_path()/"sg_v53_finished_world_return").string();
 const auto path=base+".db",copy=base+".replay",corrupt=base+".tampered",badledger=base+".badledger";
 for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
 try{
  Simulation first(5153);auto home=first.add_colony_ship(7,{0,0},300,6);
  first.provision_colony_genesis(home,1000,3000,1000,1000,1000);
  auto plain=first.add_asteroid({2000,0},50);
  auto geological=first.add_geological_asteroid({1500,0},AsteroidClass::Silicate,10,3000);
  first.enable_individual_workforce();first.upgrade_assignment_snapshot_format();first.enable_medical_workforce_rules();
  PersistentWorld w(path,std::move(first));
  send(w,1,home,Action::Mine,plain,25);
  send(w,2,home,Action::RefineOre,0,25);
  send(w,3,home,Action::FabricateModule,0,10);
  send(w,4,home,Action::ConstructOutpost);
  auto site=w.world().built_outposts().begin()->first;
  send(w,5,home,Action::ConstructStorage,site);
  send(w,6,home,Action::Mine,geological,6);
  send(w,7,home,Action::RefineMineral,10,100);
  send(w,8,home,Action::TransferStorageStack,site,10,0);
  ck(w.world().storage(site)->goods_kg.at("aluminum")==10,"not stocked by real mining/refining");
  for(const auto& [scope,people]:std::vector<std::pair<std::string,std::vector<std::uint64_t>>>{{"east",{1,2,3}},{"west",{4,5,6}}}){
   NpcAppearanceStore store(path,scope,5153,[&](auto id){return w.world().npc_records().count(id)!=0;});
   store.register_settlement(home,3);for(const auto id:people)store.enroll_family_founder(id,home);
  }
  re::CheckpointStore region(path,"world-return-net");region_seed(region);
  re::RegionalModeAuthority owner(region);owner.enroll();
  ind::RegionalProductionNetwork net(w,region,owner,path,"home",{"east","west"});
  const auto original_mass=w.world().total_material_kg();
  net.bootstrap_from_constructed_storage(network(home),"canonical-ore",site,9000);
  ck(w.world().storage(site)->network_escrow_kg==10,"physical warehouse escrow missing");
  net.move("feed-press",{0,10,ind::NetworkBin::Warehouse,ind::NetworkBin::Feed,"aluminum",2000});
  for(int i=0;i<10;++i)w.step();
  ck(net.advance_cold(10,"press-10")==2,"press did not produce two batches");
  net.move("feed-assembly",{10,20,ind::NetworkBin::Finished,ind::NetworkBin::Feed,"plates",1900});
  for(int i=0;i<10;++i)w.step();
  ck(net.advance_cold(10,"assembly-10")>=2,"second machine did not build frames");
  net.move("store-frames",{20,0,ind::NetworkBin::Finished,ind::NetworkBin::Warehouse,"frames",1800});
  ck(net.active().warehouse.amount("frames")==1800,"manufactured frames absent");
  reject([&]{net.return_finished_to_constructed_storage("bad-code",site,9100,"aluminum",1);},"unapproved finished material accepted");
  reject([&]{net.return_finished_to_constructed_storage("too-much",site,9101,"frames",2);},"return created phantom frames");
  for(const auto f:{ind::NetworkFault::AfterPayload,ind::NetworkFault::AfterEvent,ind::NetworkFault::AfterWorldSnapshot}){
   reject([&]{net.return_finished_to_constructed_storage("finished-shipment",site,9102,"frames",1,f);},"failed return committed a partial ledger");
   ck(net.active().warehouse.amount("frames")==1800,"failed return withdrew factory frames");
   ck(w.world().storage(site)->network_escrow_kg==10&&
      !w.world().storage(site)->goods_kg.count("aluminum_frames"),"failed return credited world frames");
   net.verify();
  }
  net.return_finished_to_constructed_storage("finished-shipment",site,9102,"frames",1);
  ck(net.active().warehouse.amount("frames")==800,"network failed to debit 1000g frames");
  ck(net.active().initial_mass==9000,"factory total mass not decremented");
  ck(w.world().storage(site)->network_escrow_kg==9,"world escrow failed to release one kg");
  ck(w.world().storage(site)->goods_kg.at("aluminum_frames")==1,"world warehouse lacks manufactured frames");
  ck(w.world().total_material_kg()==original_mass,"manufacture return invented/lost total world mass");
  // The same authoritative warehouse bridge must also work when detailed owns
  // the production graph, and must demote without replaying returned goods.
  net.move("feed-detailed-press",{0,10,ind::NetworkBin::Warehouse,ind::NetworkBin::Feed,"aluminum",2000});
  (void)net.promote("promote-with-returns");
  for(int i=0;i<10;++i)w.step();
  ck(net.advance_detailed(10,"detailed-press")>=2,"detailed production after return did not run");
  net.move("finished-plates",{10,0,ind::NetworkBin::Finished,ind::NetworkBin::Warehouse,"plates",1900});
  net.return_finished_to_constructed_storage("panels-return",site,9106,"plates",1);
  ck(net.active().initial_mass==8000,"detailed return did not debit network mass");
  ck(w.world().storage(site)->network_escrow_kg==8&&
     w.world().storage(site)->goods_kg.at("aluminum_panels")==1,
     "detailed return did not credit physical panels");
  (void)net.demote("demote-with-returns");
  ck(net.active().initial_mass==8000,"demotion duplicated returned stock");
  ck(w.world().total_material_kg()==original_mass,"detailed return invented physical mass");
  net.verify();
  reject([&]{net.return_finished_to_constructed_storage("finished-shipment",site,9103,"frames",1);},"replayed finished event ID accepted");
  reject([&]{net.return_finished_to_constructed_storage("other-shipment",site,9102,"frames",1);},"replayed command ID accepted");
  {PersistentWorld reopened(path,Simulation(5153));re::CheckpointStore cp(path,"world-return-net");re::RegionalModeAuthority authority(cp);
   ind::RegionalProductionNetwork restore(reopened,cp,authority,path,"home",{"east","west"});restore.verify();
   ck(reopened.world().storage(site)->goods_kg.at("aluminum_frames")==1,"world restart lost finished stock");}
  PersistentWorld::recover_copy(path,copy);
  {PersistentWorld reopened(copy,Simulation(5153));ck(reopened.world().storage(site)->network_escrow_kg==8,"genesis recovery lost exported escrow");
   ck(reopened.world().storage(site)->goods_kg.at("aluminum_frames")==1&&
      reopened.world().storage(site)->goods_kg.at("aluminum_panels")==1,"genesis recovery lost returned finished goods");}
  // Preserve original and corrupt only the returned-goods receipt in a WAL-safe clone.
  sqlite3* src=nullptr;sqlite3* dst=nullptr;
  ck(sqlite3_open(path.c_str(),&src)==SQLITE_OK&&sqlite3_open(corrupt.c_str(),&dst)==SQLITE_OK,"backup connections unavailable");
  auto* b=sqlite3_backup_init(dst,"main",src,"main");ck(b!=nullptr,"backup unavailable");
  ck(sqlite3_backup_step(b,-1)==SQLITE_DONE&&sqlite3_backup_finish(b)==SQLITE_OK,"backup failed");sqlite3_close(src);
  ck(sqlite3_exec(dst,"DROP TRIGGER sg_v53_return_immutable_del; DELETE FROM sg_v53_world_return WHERE event_id='finished-shipment'; CREATE TRIGGER sg_v53_return_immutable_del BEFORE DELETE ON sg_v53_world_return BEGIN SELECT RAISE(ABORT,'immutable world finished return');END;",nullptr,nullptr,nullptr)==SQLITE_OK,"cannot create missing-return fixture");sqlite3_close(dst);
  reject([&]{PersistentWorld reopened(corrupt,Simulation(5153));re::CheckpointStore cp(corrupt,"world-return-net");re::RegionalModeAuthority auth(cp);
   ind::RegionalProductionNetwork invalid(reopened,cp,auth,corrupt,"home",{"east","west"});},"deleted world finished-goods receipt survived recovery");
  sqlite3* raw=nullptr;sqlite3* altered=nullptr;
  ck(sqlite3_open(path.c_str(),&raw)==SQLITE_OK&&
     sqlite3_open(badledger.c_str(),&altered)==SQLITE_OK,"ledger backup unavailable");
  auto* legal=sqlite3_backup_init(altered,"main",raw,"main");ck(legal!=nullptr,"ledger copy unavailable");
  ck(sqlite3_backup_step(legal,-1)==SQLITE_DONE&&
     sqlite3_backup_finish(legal)==SQLITE_OK,"ledger integrity backup failed");sqlite3_close(raw);
  ck(sqlite3_exec(altered,"UPDATE resource_ledger SET destination_id='storage:0:aluminum_frames' WHERE reason='constructed_network_return_kg' AND command_id='9102'",nullptr,nullptr,nullptr)==SQLITE_OK,"cannot alter return source ledger");
  sqlite3_close(altered);
  reject([&]{PersistentWorld compromised(badledger,Simulation(5153));(void)compromised;},
         "forged resource destination survived world recovery");
  // Ordinary cargo access is deliberately not assumed here: physical ship
  // capacity and item transfer routing are independent gameplay gates.
  net.verify();
  ck(w.world().total_material_kg()==original_mass,
     "world mass changed after two finished-product releases");
  std::cout<<"v53 mined aluminum -> two machines -> panels/frames -> world storage -> real warehouse, rollback, replay and tamper refusal PASS\n";
  for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
  return 0;
 }catch(const std::exception& e){std::cerr<<"v53 return FAIL: "<<e.what()<<'\n';return 1;}
}
