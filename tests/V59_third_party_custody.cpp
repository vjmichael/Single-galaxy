#include "galaxy/RegionalProductionNetwork.hpp"
#include "galaxy/ShipmentManifest.hpp"
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
static void send_as(PersistentWorld& w,std::uint64_t id,PlayerId owner,EntityId ship,Action a,EntityId target=0,std::int64_t amount=1,int choice=0){
 Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;c.issuer=owner;c.actor=ship;c.action=a;c.target=target;c.amount=amount;c.thrust_x=choice;
 ck(w.submit(c),"owner-signed physical command was refused");w.step();
 if(w.world().events().back().result!=Outcome::Applied)std::cerr<<"command "<<id<<" action="<<static_cast<int>(a)<<" result="<<static_cast<int>(w.world().events().back().result)<<"\n";
 ck(w.world().events().back().result==Outcome::Applied,"owner's physical construction or travel action failed");
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
 const auto base=(std::filesystem::temp_directory_path()/"sg_v56_independent_custody").string();
 const auto path=base+".db",copy=base+".replay",corrupt=base+".tampered",badledger=base+".badledger";
 for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
 try{
  Simulation first(5153);first.add_system(2);
  const auto gate=first.add_gate(1,{0,0},2,{5000,0});
  auto home=first.add_colony_ship(7,{0,0},300,6);
  auto carrier=first.add_colony_ship(8,{0,0},300,2);
  auto receiver=first.add_colony_ship(9,{0,0},300,6);
  first.provision_colony_genesis(home,1000,3000,1000,1000,1000);
  first.provision_colony_genesis(carrier,1000,3000,1000,1000,1000);
  first.provision_colony_genesis(receiver,1000,3000,1000,1000,1000);
  auto plain=first.add_asteroid({2000,0},50);
  auto receiver_ore=first.add_asteroid({2000,0},100);
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
  // DEMOG-05q6e5b8d: keep the v53 manufacturing + factory return receipts
  // untouched, but prove that those exact goods subsequently use the canonical
  // ship cargo and jump lifecycle, then enter a separately constructed site.
  // Three separate players: the source warehouse (7), carrier (8),
  // and receiving constructed warehouse (9) are physically independent.
  const auto conserved=w.world().total_material_kg();
  send_as(w,9300,9,receiver,Action::Mine,receiver_ore,25);
  send_as(w,9301,9,receiver,Action::RefineOre,0,25);
  send_as(w,9302,9,receiver,Action::FabricateModule,0,10);
  send_as(w,9303,9,receiver,Action::RequestJump,gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(receiver)->system_id==2,"receiver did not cross systems");
  send_as(w,9304,9,receiver,Action::ConstructOutpost);
  EntityId destination=0;
  for(const auto& [id,b]:w.world().built_outposts())if(b.source_colony_ship==receiver)destination=id;
  ck(destination&&w.world().find(destination)->owner_id==9,"independent receiver lacks constructed site");
  send_as(w,9305,9,receiver,Action::ConstructStorage,destination);
  ck(w.world().storage(destination)->source_ship==receiver,"receiving warehouse not genuinely constructed");
  PlayerId active=0;
  auto authenticate=[&](PlayerId caller){return caller==active;};
  shipping::ThirdPartyRegistry contracts(w,path,authenticate);
  shipping::ThirdPartyBooking frames{"frames-third-party",home,carrier,receiver,site,destination,7,8,9,2,2,1,9102,9401,9403};
  shipping::ThirdPartyBooking panels{"panels-third-party",home,carrier,receiver,site,destination,7,8,9,2,1,1,9106,9402,9404};
  reject([&]{contracts.book(frames);},"anonymous source shipment proposal accepted");
  active=7;
  reject([&]{auto forged=frames;forged.id="bad-owner";forged.carrier_owner=7;contracts.book(forged);},"forged carrier ownership accepted");
  reject([&]{auto forged=frames;forged.id="bad-warehouse";forged.receiving_ship=home;contracts.book(forged);},"forged destination ownership accepted");
  contracts.book(frames);contracts.book(panels);
  reject([&]{contracts.load("frames-third-party");},"unsigned contract loaded before all three grants");
  active=8;reject([&]{contracts.approve("frames-third-party",7,1);},"carrier impersonated source authorization");
  active=7;reject([&]{contracts.approve("frames-third-party",7,2);},"source owner consented in carrier's role");
  for(auto name:{"frames-third-party","panels-third-party"}){
   active=7;contracts.approve(name,7,1);
   active=8;contracts.approve(name,8,2);
   active=9;contracts.approve(name,9,3);
  }
  active=9;
  reject([&]{contracts.approve("frames-third-party",9,3);},"duplicate host approval accepted");
  active=0;
  // Separate copy tests a legitimate pre-load revocation; cancellation is
  // durable and must disable the otherwise valid three-party manifest.
  const auto cancelled_path=base+".revoked";
  std::filesystem::remove(cancelled_path);
  {sqlite3* sdb=nullptr;sqlite3* ddb=nullptr;
   ck(sqlite3_open(path.c_str(),&sdb)==SQLITE_OK&&sqlite3_open(cancelled_path.c_str(),&ddb)==SQLITE_OK,"cannot clone consent scenario");
   auto* backup=sqlite3_backup_init(ddb,"main",sdb,"main");
   ck(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,"cannot clone consent journal");
   sqlite3_close(ddb);sqlite3_close(sdb);
  }
  {PersistentWorld revoke_world(cancelled_path,Simulation(5153));
   PlayerId signer=0;shipping::ThirdPartyRegistry cancel(revoke_world,cancelled_path,[&](PlayerId x){return x==signer;});
   // Already-approved legitimate booking cloned from main database.
   signer=7;cancel.revoke("frames-third-party",7);
   reject([&]{cancel.load("frames-third-party");},"revoked consignment moved real stock");
   ck(revoke_world.world().storage(site)->goods_kg.at("aluminum_frames")==1,"revoked order withdrew physical goods");
   cancel.verify();
  }
  {PersistentWorld proof(cancelled_path,Simulation(5153));shipping::verify_third_party_registry(cancelled_path);}
  const auto revoked_forgery=base+".missing-revocation";std::filesystem::remove(revoked_forgery);
  {sqlite3* sdb=nullptr;sqlite3* ddb=nullptr;
   ck(sqlite3_open(cancelled_path.c_str(),&sdb)==SQLITE_OK&&sqlite3_open(revoked_forgery.c_str(),&ddb)==SQLITE_OK,"cannot make revoked-claim fixture");
   auto* backup=sqlite3_backup_init(ddb,"main",sdb,"main");
   ck(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,"revocation proof backup failed");sqlite3_close(sdb);
   ck(sqlite3_exec(ddb,"DROP TRIGGER sg_v56_cancel_immutable_delete; DELETE FROM sg_v56_cancel WHERE contract_id='frames-third-party';",nullptr,nullptr,nullptr)==SQLITE_OK,"cannot forge canceled grant");sqlite3_close(ddb);
  }
  reject([&]{PersistentWorld forged(revoked_forgery,Simulation(5153));},"deleted revocation silently reactivated shipping grant");
  std::filesystem::remove(revoked_forgery);
  std::filesystem::remove(cancelled_path);
  reject([&]{auto forged=frames;forged.id="duplicate-proof";forged.load_command=9500;forged.unload_command=9501;contracts.book(forged);},"same factory return was rebooked");
  Command bypass{};bypass.id=9490;bypass.sequence=9490;bypass.execute_tick=w.world().tick()+1;
  bypass.issuer=8;bypass.actor=carrier;bypass.target=site;bypass.action=Action::TransferStorageStack;
  bypass.amount=1;bypass.thrust_x=2;bypass.thrust_y=1;
  ck(!w.submit(bypass),"carrier bypassed scope of host consignment grant");
  bypass.issuer=7;bypass.actor=home;
  ck(!w.submit(bypass),"warehouse owner double-spent reserved manufactured goods");
  contracts.verify();
  for(const auto& [name,code]:std::vector<std::pair<std::string,int>>{{"frames-third-party",2},{"panels-third-party",1}}){
   const auto k=code==2?"aluminum_frames":"aluminum_panels";
   const auto current=w.world().storage(site)->goods_kg.at(k);
   const auto tick=w.world().tick();
   for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    reject([&]{contracts.load(name,fail);},"load interruption committed partial third-party custody");
    ck(w.world().tick()==tick&&w.world().storage(site)->goods_kg.at(k)==current&&
       w.world().ship_inventory_snapshot(carrier).amount(std::string("good/")+k)==0,
       "interruption moved unsanctioned stock");
   }
   contracts.load(name);
   ck(w.world().ship_inventory_snapshot(carrier).amount(std::string("good/")+k)==1,"carrier never obtained physical goods");
   active=7;reject([&]{contracts.revoke(name,7);},"source owner revoked already-loaded cargo without physical return");active=0;
  }
  ck(w.world().storage(site)->goods_kg.count("aluminum_frames")==0&&
     w.world().storage(site)->goods_kg.count("aluminum_panels")==0,
     "warehouse retained shipped stock");
  ck(w.world().total_material_kg()==conserved,"loading changed mass");
  reject([&]{contracts.unload("frames-third-party");},"remote delivery was allowed without travel");
  send_as(w,9405,8,carrier,Action::RequestJump,gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(carrier)->system_id==2,"carrier did not physically reach second star system");
  // Accelerate then decelerate exactly 25 km at 0.25 km/tick².
  // Arrival at a *system* is insufficient; the carrier must actually reach
  // the independently owned warehouse's docking radius before delivery.
  for(int i=0;i<10;++i)send_as(w,9600+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,0,1);
  for(int i=0;i<10;++i)send_as(w,9610+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,0,-1);
  ck(w.world().find(carrier)->velocity.x==0,"carrier did not brake at receiving warehouse");
  for(const auto& name:{"frames-third-party","panels-third-party"}){
   const auto k=std::string(name)=="frames-third-party"?"aluminum_frames":"aluminum_panels";
   for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    const auto tick=w.world().tick();
    reject([&]{contracts.unload(name,fail);},"unload interruption committed partial third-party delivery");
    ck(w.world().tick()==tick&&w.world().ship_inventory_snapshot(carrier).amount(std::string("good/")+k)==1&&
       w.world().storage(destination)->goods_kg.count(k)==0,
       "unload interrupted without rollback");
   }
   contracts.unload(name);
   ck(w.world().storage(destination)->goods_kg.at(k)==1&&
      w.world().ship_inventory_snapshot(carrier).amount(std::string("good/")+k)==0,
      "independent warehouse did not receive exact shipment");
  }
  ck(w.world().storage(site)->network_escrow_kg==8&&w.world().total_material_kg()==conserved,
     "delivery withdrew duplicated factory escrow or failed conservation");
  contracts.verify();net.verify();
  contracts.load("frames-third-party");contracts.unload("frames-third-party");
  {PersistentWorld reopen(path,Simulation(5153));shipping::ThirdPartyRegistry checked(reopen,path,[](PlayerId){return false;});checked.verify();
   ck(reopen.world().storage(destination)->goods_kg.at("aluminum_frames")==1,
      "restart dropped receiver's physical goods");}
  const auto replay=base+".third_party_replay";std::filesystem::remove(replay);
  PersistentWorld::recover_copy(path,replay);
  {PersistentWorld recovered(replay,Simulation(5153));
   ck(recovered.world().storage(destination)->goods_kg.at("aluminum_panels")==1&&
      recovered.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==0,
      "independent genesis replay changed cargo owner");}
  std::filesystem::remove(replay);
  // Missing contract rows or altered proof endpoints must be detected on reopen.
  const auto clone=[&](const std::string& filename,const std::string& sql){
   std::filesystem::remove(filename);sqlite3* srcdb=nullptr;sqlite3* dstdb=nullptr;
   ck(sqlite3_open(path.c_str(),&srcdb)==SQLITE_OK&&sqlite3_open(filename.c_str(),&dstdb)==SQLITE_OK,"cannot clone shipping journal");
   auto* backup=sqlite3_backup_init(dstdb,"main",srcdb,"main");
   ck(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,"cannot back up shipping history");
   sqlite3_close(srcdb);
   ck(sqlite3_exec(dstdb,sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"cannot inject shipping corruption");
   sqlite3_close(dstdb);
   reject([&]{PersistentWorld bad(filename,Simulation(5153));(void)bad;},"shipping forgery survived cold recovery");
   std::filesystem::remove(filename);
  };
  clone(base+".lost-contract","DROP TRIGGER sg_v56_immutable_d; DELETE FROM sg_v56_contract WHERE contract_id='frames-third-party';");
  clone(base+".lost-table","DROP TABLE sg_v56_contract;");
  clone(base+".bad-receipt","UPDATE resource_ledger SET destination_id='storage:999:aluminum_frames' WHERE command_id='9403';");
  clone(base+".bad-owner","DROP TRIGGER sg_v56_immutable_u; UPDATE sg_v56_contract SET receiving_owner='7' WHERE contract_id='frames-third-party';");
  clone(base+".missing-approval","DROP TRIGGER sg_v56_approval_immutable_delete; DELETE FROM sg_v56_approval WHERE contract_id='frames-third-party' AND role=3;");
  clone(base+".forged-approval","DROP TRIGGER sg_v56_approval_immutable_update; UPDATE sg_v56_approval SET principal='8' WHERE contract_id='frames-third-party' AND role=1;");
  clone(base+".lost-approvals","DROP TABLE sg_v56_approval;");
  std::cout<<"v56 three independent owners: source warehouse -> reserved third-party carrier -> jump -> receiving warehouse; crash/replay/tamper PASS\n";
  // Keep every independent source/receiver and negative tamper check;
  // release the live native SQLite owners before Windows file deletion.
 }catch(const std::exception& e){std::cerr<<"v56 third-party custody FAIL: "<<e.what()<<'\n';return 1;}
 for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
 return 0;
}
