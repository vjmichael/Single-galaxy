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
 const auto base=(std::filesystem::temp_directory_path()/"sg_v57_dispatch_diversion_settlement").string();
 const auto path=base+".db",copy=base+".replay",corrupt=base+".tampered",badledger=base+".badledger";
 for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
 try{
  Simulation first(5153);first.add_system(2);
  const auto gate=first.add_gate(1,{0,0},2,{5000,0});
  const auto back_gate=first.add_gate(2,{-6000,0},1,{0,0});
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
  frames.require_dispatch=true;panels.require_dispatch=true;

  // All principal calls are checked against an injected authenticated session
  // stub. This fixture does not claim cryptographic multiplayer login.
  reject([&]{contracts.book(frames);},"unauthenticated shipment booking accepted");
  active=7;contracts.book(frames);contracts.book(panels);
  reject([&]{contracts.load(frames.id);},"unapproved shipment loaded");
  active=8;reject([&]{contracts.dispatch(frames.id,8,"frames-dispatch");},"unapproved dispatch accepted");
  for(const auto& id:{frames.id,panels.id}){
    active=7;contracts.approve(id,7,1);
    active=8;contracts.approve(id,8,2);
    active=9;contracts.approve(id,9,3);
  }
  active=0;reject([&]{contracts.dispatch(frames.id,8,"fake-dispatch");},"unauthenticated carrier dispatch accepted");
  active=7;reject([&]{contracts.dispatch(frames.id,7,"wrong-role-dispatch");},"warehouse owner dispatched unrelated ship");
  active=8;
  reject([&]{contracts.load(frames.id);},"approved but undispatched goods loaded");
  contracts.dispatch(frames.id,8,"frames-dispatch");
  reject([&]{contracts.dispatch(frames.id,8,"frames-dispatch-duplicate");},"carrier dispatched twice");
  contracts.dispatch(panels.id,8,"panels-dispatch");
  for(const auto& [id,code]:std::vector<std::pair<std::string,std::string>>{{frames.id,"aluminum_frames"},{panels.id,"aluminum_panels"}}){
    auto before=w.world().tick();
    for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
      reject([&]{contracts.load(id,fail);},"interrupted load was accepted");
      ck(w.world().tick()==before && w.world().storage(site)->goods_kg.at(code)==1,
         "interrupted load lost origin stock");
    }
    contracts.load(id);
    ck(w.world().ship_inventory_snapshot(carrier).amount("good/"+code)==1,
       "dispatch did not physically load finished goods");
  }
  reject([&]{contracts.acknowledge_settlement(frames.id,8,2);},"unshipped receipt settled before completion");
  active=9;reject([&]{contracts.request_return(frames.id,8,9470);},"unrelated login impersonated carrier for return");
  active=8;contracts.request_return(frames.id,8,9470);
  reject([&]{contracts.request_return(frames.id,8,9471);},"repeated diversion request accepted");
  reject([&]{contracts.unload(frames.id);},"diverted cargo reached abandoned destination");
  reject([&]{contracts.return_to_origin(frames.id);},"return without multi-party consent accepted");
  active=7;contracts.approve_return(frames.id,7,1);
  active=8;reject([&]{contracts.approve_return(frames.id,7,2);},"carrier forged source signer");
  contracts.approve_return(frames.id,8,2);
  active=9;contracts.approve_return(frames.id,9,3);
  reject([&]{contracts.approve_return(frames.id,9,3);},"duplicate return consent accepted");
  active=0;
  // Still beside the origin store, the carrier could physically return now.
  // Demonstrate the return order remains pending through a real outbound
  // voyage instead: no instant transfer while the carrier is system 2.
  send_as(w,9405,8,carrier,Action::RequestJump,gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(carrier)->system_id==2,"carrier never completed outbound jump");
  reject([&]{contracts.return_to_origin(frames.id);},"physical return bypassed cross-system travel");
  // Travel 25km to owner 9's real warehouse. A mere system ID is not enough.
  for(int i=0;i<10;++i)send_as(w,9600+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,-1);
  for(int i=0;i<10;++i)send_as(w,9610+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,1);
  ck(w.world().find(carrier)->velocity.x==0,"carrier did not brake at receiver");
  reject([&]{contracts.unload(frames.id);},"diverted frames were delivered to former receiver");
  for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    const auto before=w.world().tick();
    reject([&]{contracts.unload(panels.id,fail);},"interrupted destination unload succeeded");
    ck(w.world().tick()==before&&w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_panels")==1,
       "rollback after unload lost carrier panels");
  }
  contracts.unload(panels.id);
  ck(w.world().storage(destination)->goods_kg.at("aluminum_panels")==1,"receiver lacks completed delivery");
  reject([&]{contracts.return_to_origin(frames.id);},"cross-system return possible after delivery");
  // Successful delivery: carrier and physical receiver jointly attest the
  // exact committed world-ledger endpoint. Source owner cannot sign receipt.
  active=7;reject([&]{contracts.acknowledge_settlement(panels.id,7,1);},"non-receiver attested delivered cargo");
  active=8;contracts.acknowledge_settlement(panels.id,8,2);
  active=0;reject([&]{contracts.acknowledge_settlement(panels.id,9,3);},"anonymous receiver settled");
  active=9;contracts.acknowledge_settlement(panels.id,9,3);
  reject([&]{contracts.acknowledge_settlement(panels.id,9,3);},"duplicate delivery settlement permitted");
  active=0;
  // Reverse gate is physically located at the warehouse, and points back to
  // the source system's actual origin. The return remains in ship cargo until
  // completed travel, proximity, and a new world write all succeed.
  send_as(w,9621,8,carrier,Action::RequestJump,back_gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(carrier)->system_id==1,"carrier never returned to original system");
  for(int i=0;i<6;++i)send_as(w,9700+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,-1);
  for(int i=0;i<6;++i)send_as(w,9710+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,1);
  ck(w.world().find(carrier)->velocity.x==0,"carrier failed return approach braking");
  for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    const auto tick=w.world().tick();
    reject([&]{contracts.return_to_origin(frames.id,fail);},"interrupted return moved cargo");
    ck(w.world().tick()==tick&&w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==1,
       "return failure lost physical cargo");
  }
  contracts.return_to_origin(frames.id);
  contracts.return_to_origin(frames.id); // idempotent exact retry
  ck(w.world().storage(site)->goods_kg.at("aluminum_frames")==1&&
     w.world().storage(destination)->goods_kg.at("aluminum_panels")==1&&
     w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==0&&
     w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_panels")==0,
     "final two-warehouse physical custody mismatch");
  ck(w.world().storage(site)->network_escrow_kg==8&&w.world().total_material_kg()==conserved,
     "diversion or delivery lost/created factory matter");
  reject([&]{contracts.unload(frames.id);},"returned cargo could be re-delivered");
  active=9;reject([&]{contracts.acknowledge_settlement(frames.id,9,3);},"former receiver settled returned frames");
  active=7;contracts.acknowledge_settlement(frames.id,7,1);
  active=8;contracts.acknowledge_settlement(frames.id,8,2);
  reject([&]{contracts.acknowledge_settlement(frames.id,8,2);},"double return settlement accepted");
  active=0;contracts.verify();net.verify();
  {PersistentWorld reopen(path,Simulation(5153));shipping::verify_third_party_registry(path);
   ck(reopen.world().storage(site)->goods_kg.at("aluminum_frames")==1&&
      reopen.world().storage(destination)->goods_kg.at("aluminum_panels")==1,
      "reopen did not preserve two distinct settlement outcomes");}
  std::filesystem::remove(copy);
  PersistentWorld::recover_copy(path,copy);
  {PersistentWorld recovered(copy,Simulation(5153));shipping::verify_third_party_registry(copy);
   ck(recovered.world().storage(destination)->goods_kg.at("aluminum_panels")==1&&
      recovered.world().storage(site)->goods_kg.at("aluminum_frames")==1,
      "genesis journal replay changed delivered/returned inventory");}
  const auto forge=[&](const std::string& suffix,const std::string& stmt){
    auto dst=base+suffix;std::filesystem::remove(dst);
    sqlite3* a=nullptr;sqlite3* b=nullptr;
    ck(sqlite3_open(path.c_str(),&a)==SQLITE_OK&&sqlite3_open(dst.c_str(),&b)==SQLITE_OK,
       "failed to construct independent tamper fixture");
    auto* backup=sqlite3_backup_init(b,"main",a,"main");
    ck(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,
       "tamper fixture backup failed");sqlite3_close(a);
    ck(sqlite3_exec(b,stmt.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"tamper injection failed");sqlite3_close(b);
    reject([&]{PersistentWorld modified(dst,Simulation(5153));(void)modified;},"forged shipment state recovered as valid");
    std::filesystem::remove(dst);
  };
  forge(".missing-dispatch","DROP TRIGGER sg_v57_dispatch_no_delete; DELETE FROM sg_v57_dispatch WHERE contract_id='frames-third-party';");
  forge(".missing-policy","DROP TRIGGER sg_v57_policy_no_delete; DELETE FROM sg_v57_policy WHERE contract_id='frames-third-party';");
  forge(".missing-return-plan","DROP TRIGGER sg_v57_return_plan_no_delete; DELETE FROM sg_v57_return_plan WHERE contract_id='frames-third-party';");
  forge(".missing-return-consent","DROP TRIGGER sg_v57_return_approval_no_delete; DELETE FROM sg_v57_return_approval WHERE contract_id='frames-third-party' AND role=3;");
  forge(".receipt-forgery","DROP TRIGGER sg_v57_settlement_no_update; UPDATE sg_v57_settlement SET receipt_command='999999' WHERE contract_id='frames-third-party';");
  forge(".missing-settlement","DROP TRIGGER sg_v57_settlement_no_delete; DELETE FROM sg_v57_settlement WHERE contract_id='panels-third-party';");
  forge(".missing-settlement-vote","DROP TRIGGER sg_v57_vote_no_delete; DELETE FROM sg_v57_vote WHERE contract_id='panels-third-party' AND role=3;");
  forge(".bad-return-endpoint","UPDATE resource_ledger SET destination_id='storage:999:aluminum_frames' WHERE command_id='9470';");
  std::cout<<"v57 authenticated dispatch, actual inter-system delivery + physical return, two-party settlement, crash/replay and corrupt-evidence refusal PASS\n";
  for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
  return 0;
 }catch(const std::exception& e){std::cerr<<"v57 dispatch/diversion/settlement FAIL: "<<e.what()<<'\n';return 1;}
}
