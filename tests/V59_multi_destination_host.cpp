#include "galaxy/RegionalProductionNetwork.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include "galaxy/RegionalShipmentReconciliation.hpp"
#include "galaxy/CanonicalShippingHost.hpp"
#include "galaxy/ShipmentStatusView.hpp"
#include "galaxy/ClientContract.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <filesystem>
#include <cstdlib>
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
 const char* run=std::getenv("SG_SHIPPING_RUN_ID");
 const std::string suffix=run?std::string(run):std::string{};
 ck(suffix.size()<=32&&suffix.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,
    "invalid test fixture run identifier");
 const auto base=(std::filesystem::temp_directory_path()/("sg_v58_multi_destination"+(suffix.empty()?std::string{}:"_"+suffix))).string();
 const auto path=base+".db",copy=base+".replay",corrupt=base+".tampered",badledger=base+".badledger";
 for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
 try{
  Simulation first(5153);first.add_system(2);
  const auto gate=first.add_gate(1,{0,0},2,{5000,0});
  const auto back_gate=first.add_gate(2,{-6000,0},1,{0,0});
  auto home=first.add_colony_ship(7,{0,0},300,6);
  auto carrier=first.add_colony_ship(8,{0,0},300,2);
  auto receiver=first.add_colony_ship(9,{0,0},300,6);
  auto alternate_receiver=first.add_colony_ship(10,{0,0},300,6);
  first.provision_colony_genesis(home,1000,3000,1000,1000,1000);
  first.provision_colony_genesis(carrier,1000,3000,1000,1000,1000);
  first.provision_colony_genesis(receiver,1000,3000,1000,1000,1000);
  first.provision_colony_genesis(alternate_receiver,1000,3000,1000,1000,1000);
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
  // Independent alternate receiver: builds a THIRD physical warehouse in the
  // destination system, with ownership independent of the booked receiver.
  send_as(w,9310,10,alternate_receiver,Action::Mine,receiver_ore,25);
  send_as(w,9311,10,alternate_receiver,Action::RefineOre,0,25);
  send_as(w,9312,10,alternate_receiver,Action::FabricateModule,0,10);
  send_as(w,9313,10,alternate_receiver,Action::RequestJump,gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(alternate_receiver)->system_id==2,"replacement receiver did not cross systems");
  for(int i=0;i<1;++i)send_as(w,9320+static_cast<std::uint64_t>(i),10,alternate_receiver,Action::Thrust,0,1,1);
  for(int i=0;i<1;++i)send_as(w,9324+static_cast<std::uint64_t>(i),10,alternate_receiver,Action::Thrust,0,1,-1);
  ck(w.world().find(alternate_receiver)->velocity.x==0,"replacement warehouse founding ship did not brake");
  send_as(w,9330,10,alternate_receiver,Action::ConstructOutpost);
  EntityId alternate_site=0;
  for(const auto& [id,b]:w.world().built_outposts())if(b.source_colony_ship==alternate_receiver)alternate_site=id;
  ck(alternate_site&&alternate_site!=destination&&w.world().find(alternate_site)->owner_id==10,
    "independent replacement destination not constructed");
  send_as(w,9331,10,alternate_receiver,Action::ConstructStorage,alternate_site);
  ck(w.world().storage(alternate_site)->source_ship==alternate_receiver,"alternate store has no real construction witness");
  std::cerr<<"v58 original destination x="<<w.world().find(destination)->position.x
           <<" alternate destination x="<<w.world().find(alternate_site)->position.x<<"\n";
  PlayerId active=0;
  auto authenticate=[&](PlayerId caller){return caller==active;};
  shipping::ThirdPartyRegistry contracts(w,path,authenticate);
  shipping::ThirdPartyBooking frames{"frames-third-party",home,carrier,receiver,site,destination,7,8,9,2,2,1,9102,9401,9403};
  shipping::ThirdPartyBooking panels{"panels-third-party",home,carrier,receiver,site,destination,7,8,9,2,1,1,9106,9402,9404};
  frames.require_dispatch=true;panels.require_dispatch=true;

  // All principal calls are checked against an injected authenticated session
  // stub. This fixture does not claim cryptographic multiplayer login.
  reject([&]{contracts.book(frames);},"unauthenticated shipment booking accepted");
  shipping::ProductionShippingHost production_host(w,net,contracts,path);
  shipping::CanonicalShippingHost game_host(w,region,owner,net,contracts,path,authenticate);
  {Command forgery{};forgery.id=9399;forgery.sequence=9399;forgery.execute_tick=w.world().tick()+1;
   forgery.issuer=8;forgery.actor=carrier;forgery.action=Action::Thrust;
   active=7;reject([&]{(void)game_host.player_command(forgery,7);},"game-host session let source impersonate carrier");
   active=0;reject([&]{(void)game_host.player_command(forgery,8);},"anonymous carrier session admitted");
  }
  active=7;
  reject([&]{auto spoof=frames;spoof.factory_return_command=9106;production_host.book_factory_goods(spoof);},
    "production host accepted the wrong finished material provenance");
  game_host.book(frames,7);game_host.book(panels,7);
  if(const char* fixture=std::getenv("SG_SHIPPING_PENDING_FIXTURE")){
    sqlite3 *from=nullptr,*to=nullptr;
    ck(sqlite3_open_v2(path.c_str(),&from,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
       "cannot open pending-freight fixture source");
    ck(sqlite3_open_v2(fixture,&to,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK,
       "cannot open pending-freight fixture destination");
    auto* backup=sqlite3_backup_init(to,"main",from,"main");
    ck(backup!=nullptr,"cannot start pending-freight backup");
    ck(sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,
       "pending-freight backup incomplete");
    sqlite3_close(to);sqlite3_close(from);
  }
  reject([&]{contracts.load(frames.id);},"unapproved shipment loaded");
  active=8;reject([&]{production_host.dispatch_factory_goods(frames.id,8,"frames-dispatch");},"unapproved dispatch accepted");
  for(const auto& id:{frames.id,panels.id}){
    active=7;game_host.approve_booking(id,7,1);
    active=8;game_host.approve_booking(id,8,2);
    active=9;game_host.approve_booking(id,9,3);
  }
  active=0;reject([&]{contracts.dispatch(frames.id,8,"fake-dispatch");},"unauthenticated carrier dispatch accepted");
  active=7;reject([&]{contracts.dispatch(frames.id,7,"wrong-role-dispatch");},"warehouse owner dispatched unrelated ship");
  active=8;
  reject([&]{contracts.load(frames.id);},"approved but undispatched goods loaded");
  game_host.dispatch(frames.id,8,"frames-dispatch");
  reject([&]{contracts.dispatch(frames.id,8,"frames-dispatch-duplicate");},"carrier dispatched twice");
  game_host.dispatch(panels.id,8,"panels-dispatch");
  active=7;reject([&]{game_host.load(frames.id,7);},"source-owner session moved carrier freight");
  active=9;reject([&]{game_host.load(frames.id,9);},"receiver session moved carrier freight");
  active=10;reject([&]{game_host.load(frames.id,10);},"alternate receiver session moved carrier freight");
  active=8;
  for(const auto& [id,code]:std::vector<std::pair<std::string,std::string>>{{frames.id,"aluminum_frames"},{panels.id,"aluminum_panels"}}){
    auto before=w.world().tick();
    for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
      reject([&]{contracts.load(id,fail);},"interrupted load was accepted");
      ck(w.world().tick()==before && w.world().storage(site)->goods_kg.at(code)==1,
         "interrupted load lost origin stock");
    }
    game_host.load(id,8);
    ck(w.world().ship_inventory_snapshot(carrier).amount("good/"+code)==1,
       "dispatch did not physically load finished goods");
  }
  // v59: real owner cutovers while both physical lots are still aboard
  // the independent carrier. Ownership flips must not reissue either cargo.
  const auto loaded_mass=w.world().total_material_kg();
  reject([&]{(void)net.promote("v59-transit-promote",ind::NetworkFault::AfterPayload);},
         "failed cold-to-detailed commit was accepted");
  ck(owner.state().phase==re::TransitionPhase::ToDetailed,"prepared transition was not retained");
  active=8;reject([&]{game_host.dispatch(panels.id,8,"illegal-pending-dispatch");},
          "host shipping command ran while ownership transition was prepared");
  reject([&]{(void)game_host.transition_region("different-handoff",re::SimulationOwner::Detailed);},
         "stale conflicting host ownership transition accepted");
  (void)game_host.transition_region("v59-transit-promote",re::SimulationOwner::Detailed);
  reject([&]{(void)net.demote("v59-transit-demote",ind::NetworkFault::AfterEvent);},
         "failed detailed-to-cold commit was accepted");
  ck(owner.state().phase==re::TransitionPhase::ToCold,"demotion intent vanished during injected failure");
  (void)game_host.transition_region("v59-transit-demote",re::SimulationOwner::Cold);
  shipping::verify_regional_shipment_boundaries(path);
  { re::CheckpointStore unrelated(path,"remote-unrelated"); region_seed(unrelated);
    re::RegionalModeAuthority remote_owner(unrelated);remote_owner.enroll();
    const auto st=remote_owner.state();
    auto pending=remote_owner.prepare("remote-shipment-check",re::SimulationOwner::Detailed,
       st.generation,st.checkpoint_revision,st.boundary_tick);
    (void)remote_owner.commit("remote-shipment-check",pending.generation,pending.checkpoint_revision,
        [](sqlite3*,re::Scheduler&){});
    sqlite3* db=nullptr;ck(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"regional isolation DB failed");
    sqlite3_stmt* q=nullptr;
    ck(sqlite3_prepare_v2(db,"SELECT loaded,transit FROM sg_v59_shipment_handoff WHERE scope='remote-unrelated'",-1,&q,nullptr)==SQLITE_OK,"regional isolation query failed");
    ck(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==0&&sqlite3_column_int(q,1)==0,
       "unrelated region inherited physical shipment ledger");
    sqlite3_finalize(q);sqlite3_close(db);
    shipping::verify_regional_shipment_boundaries(path);
  }
  ck(w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==1&&
     w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_panels")==1&&
     w.world().total_material_kg()==loaded_mass,"cold/detailed transition duplicated in-flight cargo");
  active=8;reject([&]{game_host.acknowledge_delivery(frames.id,8,2);},"unshipped receipt settled before completion");
  // Original receiver and replacement receiver are different people and
  // different constructed sites. All four roles must consent to diversion.
  active=9;reject([&]{contracts.propose_alternate(frames.id,8,alternate_receiver,alternate_site,10,2,9470);},
       "original receiver impersonated carrier for amendment");
  active=8;game_host.propose_redirect(frames.id,8,alternate_receiver,alternate_site,10,2,9470);
  reject([&]{contracts.request_return(frames.id,8,9471);},"amended destination accepted a return-to-origin plan");
  reject([&]{contracts.unload(frames.id);},"diverted cargo allowed old destination unload");
  reject([&]{contracts.unload_alternate(frames.id);},"unsigned alternate route unloaded");
  active=7;game_host.approve_redirect(frames.id,7,1);
  active=8;reject([&]{game_host.approve_redirect(frames.id,7,2);},"source forged carrier approval");
  game_host.approve_redirect(frames.id,8,2);
  active=9;game_host.approve_redirect(frames.id,9,3);
  active=10;game_host.approve_redirect(frames.id,10,4);
  reject([&]{contracts.approve_alternate(frames.id,10,4);},"duplicate amended-route vote accepted");
  active=0;
  reject([&]{contracts.unload_alternate(frames.id);},"replacement delivery teleported before inter-system voyage");
  send_as(w,9405,8,carrier,Action::RequestJump,gate);
  w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
  ck(w.world().find(carrier)->system_id==2,"carrier never completed outbound jump");
  // The carrier must actually enter range of receiver 9's warehouse.
  for(int i=0;i<10;++i)send_as(w,9600+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,-1);
  for(int i=0;i<10;++i)send_as(w,9610+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,1);
  ck(w.world().find(carrier)->velocity.x==0,"carrier did not brake at receiver");
  for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    const auto before=w.world().tick();
    reject([&]{contracts.unload(panels.id,fail);},"interrupted original-destination unload succeeded");
    ck(w.world().tick()==before&&w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_panels")==1,
       "rollback after original unload lost panels");
  }
  active=7;reject([&]{game_host.unload(panels.id,7);},"warehouse owner issued carrier unload");
  active=9;reject([&]{game_host.unload(panels.id,9);},"receiver owner issued carrier unload");
  active=8;game_host.unload(panels.id,8);
  ck(w.world().storage(destination)->goods_kg.at("aluminum_panels")==1,"original receiver lacks delivered panels");
  active=8;game_host.acknowledge_delivery(panels.id,8,2);
  active=9;game_host.acknowledge_delivery(panels.id,9,3);
  active=0;
  // The first destination cannot receive the redirected frames even after
  // delivery of another item: storage and settlement remain destination-scoped.
  reject([&]{contracts.unload(frames.id);},"alternate consignment delivered to stale receiver");
  reject([&]{contracts.acknowledge_settlement(frames.id,8,2);},"old destination settlement bypassed diversion");
  const auto sx=w.world().find(carrier)->position.x,tx=w.world().find(alternate_site)->position.x;
  std::cerr<<"v58 carrier at x="<<sx<<" replacement x="<<tx<<"\n";
  // Move the carrier to replacement warehouse; cargo stays aboard until
  // local distance, finite storage capacity and the canonical world commit.
  for(int i=0;i<10;++i)send_as(w,9630+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,1);
  for(int i=0;i<10;++i)send_as(w,9640+static_cast<std::uint64_t>(i),8,carrier,Action::Thrust,0,1,-1);
  std::cerr<<"v58 carrier after approach x="<<w.world().find(carrier)->position.x<<" replacement x="<<tx<<"\n";
  for(const auto fail:{FailPoint::AfterReceiptWrite,FailPoint::AfterSnapshotWrite}){
    const auto before=w.world().tick();
    reject([&]{production_host.deliver_redirected_goods(frames.id,fail);},"failed redirected unload altered inventory");
    ck(w.world().tick()==before&&w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==1,
       "redirected unload rollback lost in-transit frames");
  }
  active=7;reject([&]{game_host.deliver_redirected(frames.id,7);},"source owner issued carrier delivery");
  active=9;reject([&]{game_host.deliver_redirected(frames.id,9);},"former receiver issued carrier delivery");
  active=10;reject([&]{game_host.deliver_redirected(frames.id,10);},"replacement receiver issued carrier delivery");
  active=8;game_host.deliver_redirected(frames.id,8);
  active=8;game_host.deliver_redirected(frames.id,8); // idempotent exact retry
  ck(w.world().storage(alternate_site)->goods_kg.at("aluminum_frames")==1&&
     w.world().storage(destination)->goods_kg.at("aluminum_panels")==1&&
     w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_frames")==0&&
     w.world().ship_inventory_snapshot(carrier).amount("good/aluminum_panels")==0,
     "reassigned physical cargo was lost or duplicated");
  ck(w.world().storage(site)->network_escrow_kg==8&&w.world().total_material_kg()==conserved,
     "redispatch invented or destroyed total physical matter");
  active=9;reject([&]{contracts.acknowledge_alternate_settlement(frames.id,9,4);},"original receiver signed replacement receipt");
  active=8;game_host.acknowledge_redirected_delivery(frames.id,8,2);
  active=10;game_host.acknowledge_redirected_delivery(frames.id,10,4);
  reject([&]{contracts.acknowledge_alternate_settlement(frames.id,10,4);},"duplicate replacement settlement accepted");
  active=0;contracts.verify();production_host.verify();
  // Same handoff after final settlement must see each destination exactly
  // once, not replay two historical cargo arrivals as cold macro events.
  (void)game_host.transition_region("v59-delivered-promote",re::SimulationOwner::Detailed);
  (void)game_host.transition_region("v59-delivered-demote",re::SimulationOwner::Cold);
  shipping::verify_regional_shipment_boundaries(path);
  {sqlite3* db=nullptr;ck(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"v59 boundary audit DB unavailable");
   sqlite3_stmt* q=nullptr;
   ck(sqlite3_prepare_v2(db,"SELECT loaded,transit,original,returned,alternate FROM sg_v59_shipment_handoff WHERE handoff_id=?",-1,&q,nullptr)==SQLITE_OK,"v59 boundary query failed");
   const auto assert_boundary=[&](const char* id,int transit,int original,int alternate){
    sqlite3_reset(q);sqlite3_clear_bindings(q);sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);
    ck(sqlite3_step(q)==SQLITE_ROW,"v59 handoff missing");
    ck(sqlite3_column_int(q,0)==2 && sqlite3_column_int(q,1)==transit &&
       sqlite3_column_int(q,2)==original && sqlite3_column_int(q,3)==0 &&
       sqlite3_column_int(q,4)==alternate,"v59 owner cutover changed physical cargo history");
   };
   assert_boundary("v59-transit-promote",2,0,0);
   assert_boundary("v59-transit-demote",2,0,0);
   assert_boundary("v59-delivered-promote",0,1,1);
   assert_boundary("v59-delivered-demote",0,1,1);
   sqlite3_finalize(q);sqlite3_close(db);
  }
  {PersistentWorld reopen(path,Simulation(5153));shipping::verify_third_party_registry(path);
    ck(reopen.world().storage(alternate_site)->goods_kg.at("aluminum_frames")==1&&
       reopen.world().storage(destination)->goods_kg.at("aluminum_panels")==1,
       "world restart changed redirect results");}
  std::filesystem::remove(copy);
  PersistentWorld::recover_copy(path,copy);
  {PersistentWorld replay(copy,Simulation(5153));shipping::verify_third_party_registry(copy);
    ck(replay.world().storage(alternate_site)->goods_kg.at("aluminum_frames")==1&&
       replay.world().storage(destination)->goods_kg.at("aluminum_panels")==1,
       "independent genesis recovery lost redirected finished goods");}
  const auto forge=[&](const std::string& suffix,const std::string& stmt){
    auto dst=base+suffix;std::filesystem::remove(dst);
    sqlite3* a=nullptr;sqlite3* b=nullptr;
    ck(sqlite3_open(path.c_str(),&a)==SQLITE_OK&&sqlite3_open(dst.c_str(),&b)==SQLITE_OK,"tamper fixture open failed");
    auto* backup=sqlite3_backup_init(b,"main",a,"main");
    ck(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,"tamper fixture backup failed");sqlite3_close(a);
    ck(sqlite3_exec(b,stmt.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"tamper modification failed");sqlite3_close(b);
    reject([&]{PersistentWorld broken(dst,Simulation(5153));(void)broken;},"tampered alternate registry recovered as valid");
    std::filesystem::remove(dst);
  };
  forge(".altered-route","DROP TRIGGER sg_v58_alternate_no_update; UPDATE sg_v58_alternate SET receiver_site='999' WHERE contract_id='frames-third-party';");
  forge(".missing-approval","DROP TRIGGER sg_v58_alternate_approval_no_delete; DELETE FROM sg_v58_alternate_approval WHERE contract_id='frames-third-party' AND role=4;");
  forge(".missing-settlement","DROP TRIGGER sg_v58_settlement_no_delete; DELETE FROM sg_v58_settlement WHERE contract_id='frames-third-party';");
  forge(".false-receipt","UPDATE resource_ledger SET destination_id='storage:999:aluminum_frames' WHERE command_id='9470';");
  forge(".missing-route","DROP TRIGGER sg_v58_alternate_no_delete; DELETE FROM sg_v58_alternate WHERE contract_id='frames-third-party';");
  forge(".missing-handoff","DROP TRIGGER sg_v59_no_delete; DELETE FROM sg_v59_shipment_handoff WHERE handoff_id='v59-transit-promote';");
  forge(".forged-handoff","DROP TRIGGER sg_v59_no_update; UPDATE sg_v59_shipment_handoff SET digest='0000000000000000' WHERE handoff_id='v59-delivered-promote';");
  forge(".forged-owner","UPDATE sg_regional_mode_authority SET owner=1 WHERE scope='world-return-net';");
  // Read-only shipment projection: no arbitrary owner-scoped queries, no
  // shadow cargo balance, and four-party alternate receivers see only their
  // own consignments. Derived from canonical physical ledger receipts.
  {
    shipping::ShipmentStatusView status(path);
    reject([&]{(void)status.for_principal(0);},"unauthenticated status principal admitted");
    ck(status.for_principal(11).empty(),"unrelated player learned a shipment");
    const auto source_rows=status.for_principal(7);
    const auto carrier_rows=status.for_principal(8);
    const auto original_rows=status.for_principal(9);
    const auto new_rows=status.for_principal(10);
    ck(source_rows.size()==2&&carrier_rows.size()==2&&original_rows.size()==2&&new_rows.size()==1,
      "shipping view leaked another owner's manifest or omitted a participant");
    ck(source_rows[0].settled&&source_rows[1].settled,"world-settled receipts missing in shipment view");
    ck(new_rows[0].id==frames.id&&new_rows[0].phase=="redirected_settled"&&
       new_rows[0].delivered&&new_rows[0].destination_site==alternate_site,
       "alternate receiving owner did not see exact committed warehouse receipt");
    ck(original_rows[0].role=="receiver"&&new_rows[0].role=="alternate_receiver",
       "status view misidentified signed-in player's role");
    const auto json=shipping::ShipmentStatusView::to_json(new_rows);
    ck(json.find("redirected_settled")!=std::string::npos&&json.find("aluminum_frames")!=std::string::npos,
       "status JSON omitted canonical material or terminal phase");
  }
  // The actual game presentation contract and the freight settlement world
  // share one galaxy::Simulation; never instantiate a second transport world.
  ServerViewBridge presentation(w.world(), 6060, 0);
  ClientView local_view;
  auto frame=decode_frame(encode_frame(presentation.snapshot()));
  ck(local_view.apply(frame)==ApplyResult::Applied,
     "original game client failed to load a shipment-settled world");
  ck(local_view.world().tick==w.world().tick() &&
     local_view.world().entities.count(home)!=0,
     "game client projection diverged from authoritative shipment world");
  w.step(); // one committed host tick is required by the original delta contract
  auto empty_delta=decode_frame(encode_frame(presentation.after_committed_step(w.world())));
  ck(local_view.apply(empty_delta)==ApplyResult::Applied,
     "original game client failed to consume canonical shipping-world delta");
  std::cout<<"v59 verified regional owner/cargo snapshots and v58 production-host source, alternate constructed receiver, four-party consent, physical inter-system delivery and replay PASS\n";
  // Optional live-transport fixture: use SQLite backup to export a committed
  // single-file snapshot (not copying an active WAL database by filename).
  // The real integration test opens this with the read-only status executable.
  if(const char* fixture=std::getenv("SG_SHIPPING_STATUS_FIXTURE")){
    sqlite3 *from=nullptr,*to=nullptr;
    ck(sqlite3_open_v2(path.c_str(),&from,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
      "cannot open shipping transport fixture source");
    ck(sqlite3_open_v2(fixture,&to,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK,
      "cannot open shipping transport fixture destination");
    auto* backup=sqlite3_backup_init(to,"main",from,"main");
    ck(backup!=nullptr,"cannot start transport fixture backup");
    ck(sqlite3_backup_step(backup,-1)==SQLITE_DONE&&sqlite3_backup_finish(backup)==SQLITE_OK,
      "transport fixture backup incomplete");
    sqlite3_close(to);sqlite3_close(from);
  }
  for(const auto& file:{path,copy,corrupt,badledger})std::filesystem::remove(file);
  return 0;
 }catch(const std::exception& e){std::cerr<<"v58 alternate destination and production host FAIL: "<<e.what()<<'\n';return 1;}
}
