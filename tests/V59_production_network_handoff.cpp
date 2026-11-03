#include "galaxy/RegionalProductionNetwork.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace re=galaxy::regional_events;
namespace ind=galaxy::industry;
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F>static void reject(F run,const char* reason){bool thrown=false;try{run();}catch(const std::exception&){thrown=true;}check(thrown,reason);}
static void advance_world(PersistentWorld& world,std::uint64_t ticks){for(std::uint64_t i=0;i<ticks;++i)world.step();}
static void start_region(re::CheckpointStore& region){re::Scheduler s;cold_colony::State c;c.population=6;c.habitat.food_kg=100;c.habitat.water_kg=100;c.habitat.oxygen_kg=100;s.add_colony("home",c);check(region.save(s,0)==1,"region seed failed");}
static ind::NetworkState seed(EntityId home){
 ind::NetworkState n;n.initial_mass=10000;n.warehouse.capacity_grams=50000;n.warehouse.grams["ore"]=10000;
 ind::NetworkMachine smelt;smelt.id=10;smelt.worksite=home;smelt.workers={1,2};
 smelt.recipe={"smelt-ingot","smelt",{{"ore",1000}},{"aluminum",950},{"tailings",50}};
 smelt.machine={1,"smelt",true,true,true};
 smelt.state.feed.capacity_grams=25000;smelt.state.finished.capacity_grams=25000;smelt.state.scrap.capacity_grams=25000;
 ind::NetworkMachine assemble;assemble.id=20;assemble.worksite=home;assemble.workers={3,4};
 assemble.recipe={"make-plates","assemble",{{"aluminum",950}},{"plates",900},{"offcuts",50}};
 assemble.machine={1,"assemble",true,true,true};
 assemble.state.feed.capacity_grams=25000;assemble.state.finished.capacity_grams=25000;assemble.state.scrap.capacity_grams=25000;
 n.machines={smelt,assemble};return n;
}
static ind::NetworkTransfer transfer(std::uint64_t src,ind::NetworkBin from,std::uint64_t dst,ind::NetworkBin to,const char* item,std::int64_t amount){return {src,dst,from,to,item,amount};}
static void equal(const ind::NetworkState& lhs,const ind::NetworkState& rhs,const char* why){
 check(lhs.tick==rhs.tick&&lhs.initial_mass==rhs.initial_mass&&lhs.warehouse.grams==rhs.warehouse.grams&&lhs.machines.size()==rhs.machines.size(),why);
 for(std::size_t i=0;i<lhs.machines.size();++i){const auto& a=lhs.machines[i].state;const auto& b=rhs.machines[i].state;
  check(a.feed.grams==b.feed.grams&&a.finished.grams==b.finished.grams&&a.scrap.grams==b.scrap.grams&&a.completed_batches==b.completed_batches&&a.elapsed_ticks==b.elapsed_ticks&&a.ticks_remaining==b.ticks_remaining&&a.reserved_inputs.size()==b.reserved_inputs.size()&&a.has_work_in_progress==b.has_work_in_progress,why);
 }
}
int main(){
 const auto path=(std::filesystem::temp_directory_path()/"sg_v51_network_handoff.db").string();std::filesystem::remove(path);
 try{
  Simulation initial(9919);
  const EntityId home=initial.add_colony_ship(7,{0,0},100,6);
  const EntityId ferry=initial.add_colony_ship(7,{0,0},100,6);
  initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();initial.enable_medical_workforce_rules();
  PersistentWorld world(path,std::move(initial));
  for(const auto& family:std::vector<std::pair<std::string,std::vector<std::uint64_t>>>{{"east",{1,2,3}},{"west",{4,5,6}}}){
   NpcAppearanceStore store(path,family.first,9919,[&](auto id){return world.world().npc_records().count(id)!=0;});
   store.register_settlement(home,3);for(auto id:family.second)store.enroll_family_founder(id,home);
  }
  const std::vector<std::string> scopes{"east","west"};
  re::CheckpointStore region(path,"net-real");start_region(region);re::RegionalModeAuthority owner(region);owner.enroll();
  re::CheckpointStore control(path,"net-control");start_region(control);re::RegionalModeAuthority control_owner(control);control_owner.enroll();
  ind::RegionalProductionNetwork net(world,region,owner,path,"home",scopes);
  ind::RegionalProductionNetwork cmp(world,control,control_owner,path,"home",scopes);
  auto beginning=seed(home);
  reject([&]{auto bad=beginning;bad.machines[1].workers={2,3};net.bootstrap(bad,"seed-invalid-workers");},"duplicate worker admitted across network machines");
  net.bootstrap(beginning,"stock-receipt-net");cmp.bootstrap(beginning,"stock-receipt-control");
  // Reciprocal provenance: even the older v50 single-line adapter must not
  // seed a second factory from the same physical warehouse source receipt.
  re::CheckpointStore legacy_region(path,"legacy-stolen-stock");start_region(legacy_region);
  re::RegionalModeAuthority legacy_owner(legacy_region);legacy_owner.enroll();
  ind::RegionalFactoryHandoff legacy(world,legacy_region,legacy_owner,path,"home",scopes);
  ind::DurableFactory legacy_seed;legacy_seed.home_ship=home;legacy_seed.workers={1,2};
  legacy_seed.recipe=beginning.machines[0].recipe;legacy_seed.machine=beginning.machines[0].machine;
  legacy_seed.state=beginning.machines[0].state;legacy_seed.state.feed.grams["ore"]=10000;
  legacy_seed.initial_mass=10000;
  reject([&]{legacy.bootstrap(legacy_seed,"stock-receipt-net");},
      "older factory adapter reused already escrowed network stock receipt");
  // Ownership record alone cannot promote the network without a material stage.
  auto unsafe=owner.state();
  auto prepared=owner.prepare("empty-network-stage",re::SimulationOwner::Detailed,
      unsafe.generation,unsafe.checkpoint_revision,unsafe.boundary_tick);
  reject([&]{(void)owner.commit("empty-network-stage",prepared.generation,
      prepared.checkpoint_revision,[](sqlite3*,re::Scheduler&){});},
      "owner flag switched without full warehouse and machine state");
  owner.abort("empty-network-stage",prepared.generation);
  re::CheckpointStore duplicate_region(path,"net-invalid-stock");start_region(duplicate_region);re::RegionalModeAuthority duplicate_owner(duplicate_region);duplicate_owner.enroll();
  ind::RegionalProductionNetwork dup(world,duplicate_region,duplicate_owner,path,"home",scopes);
  reject([&]{dup.bootstrap(beginning,"stock-receipt-net");},"warehouse source stock receipt reused");
  reject([&]{(void)region.commit_command("illegal-background","other","advance",region.load()->revision,[](re::Scheduler& s){s.advance_to(1);return std::uint64_t(0);});},"uncoordinated region command bypassed network");
  auto ore=transfer(0,ind::NetworkBin::Warehouse,10,ind::NetworkBin::Feed,"ore",4000);
  reject([&]{net.move("ore-crash",ore,ind::NetworkFault::AfterPayload);},"failed depot withdrawal created ore");
  check(net.active().warehouse.amount("ore")==10000,"failed depot withdrawal leaked material");
  net.move("feed-ore",ore);cmp.move("feed-ore",ore);
  reject([&]{net.move("oversubscribed-ore",transfer(0,ind::NetworkBin::Warehouse,10,ind::NetworkBin::Feed,"ore",7000));},"duplicate warehouse withdrawal was accepted without shortage");
  advance_world(world,2);
  check(net.advance_cold(2,"net-first-wip")==0,"smelter prematurely completed WIP");
  check(cmp.advance_cold(2,"ctrl-first-wip")==0,"control WIP incorrect");
  equal(net.active(),cmp.active(),"partial production differs before handoff");
  const auto warm_before=owner.state();
  reject([&]{(void)net.promote("net-promote",ind::NetworkFault::AfterPayload);},"failed network promotion published active inventory");
  check(owner.state().phase==re::TransitionPhase::ToDetailed,"network promotion failure lost prepared owner state");
  reject([&]{(void)net.advance_cold(1,"illegal-while-prepared");},"network cold tick bypassed prepared owner");
  auto committed=net.promote("net-promote");
  check(committed.owner==re::SimulationOwner::Detailed&&committed.generation>warm_before.generation,"network detailed owner did not commit");
  reject([&]{(void)net.advance_cold(1,"cold-owner-bypass");},"cold network writer ran under detailed owner");
  advance_world(world,3);
  reject([&]{(void)net.advance_detailed(3,"warm-crash",ind::NetworkFault::AfterEvent);},"half-written detailed batch committed");
  check(net.active().machines[0].state.completed_batches==0,"failed warm batch changed output");
  check(net.advance_detailed(3,"warm-three")==1,"first smelter batch missing");
  check(cmp.advance_cold(3,"ctrl-three")==1,"control first batch mismatch");
  equal(net.active(),cmp.active(),"detailed and cold first batch disagree");
  auto al=transfer(10,ind::NetworkBin::Finished,20,ind::NetworkBin::Feed,"aluminum",950);
  reject([&]{net.move("wrong-material",transfer(10,ind::NetworkBin::Finished,20,ind::NetworkBin::Feed,"ore",950));},"invalid factory-to-factory material accepted");
  net.move("transfer-ingot-1",al);cmp.move("transfer-ingot-1",al);
  advance_world(world,5);
  check(net.advance_detailed(5,"warm-five")==2,"two-machine detailed batch count wrong");
  check(cmp.advance_cold(5,"ctrl-five")==2,"two-machine control batch count wrong");
  equal(net.active(),cmp.active(),"warm/cold two-line production diverged");
  auto finished=transfer(20,ind::NetworkBin::Finished,0,ind::NetworkBin::Warehouse,"plates",900);
  net.move("warehouse-plates",finished);cmp.move("warehouse-plates",finished);
  auto secondary=transfer(10,ind::NetworkBin::Finished,20,ind::NetworkBin::Feed,"aluminum",950);
  net.move("ingot-2",secondary);cmp.move("ingot-2",secondary);
  check(net.active().warehouse.amount("plates")==900,"finished warehouse did not receive manufactured plates");
  // Real ferry custody makes the smelter stop; the unrelated assembler continues.
  Command boarding{};boarding.id=888;boarding.sequence=888;boarding.execute_tick=world.world().tick()+1;
  boarding.issuer=Simulation::kServerAuthority;boarding.action=Action::HostEmbarkPassengers;
  boarding.actor=ferry;boarding.target=home;boarding.transport_id=8801;boarding.passenger_ids={1,2};
  check(world.submit_host_passenger_action(boarding),"worker ferry departure refused");world.step();advance_world(world,4);
  check(net.advance_detailed(5,"warm-away")==1,"only assembly line should produce during smelter worker absence");
  check(cmp.advance_cold(5,"ctrl-away")==1,"control did not block absent smelter crew");
  equal(net.active(),cmp.active(),"network workforce absence changed production between modes");
  check(net.active().machines[0].state.completed_batches==2 && net.active().machines[1].state.completed_batches==2,"wrong per-machine availability across ferry trip");
  Command arrive=boarding;arrive.id=889;arrive.sequence=889;arrive.execute_tick=world.world().tick()+1;arrive.action=Action::HostDisembarkPassengers;
  check(world.submit_host_passenger_action(arrive),"worker ferry arrival refused");world.step();advance_world(world,4);
  check(net.advance_detailed(5,"warm-restore")==1,"returning smelter workers did not resume production");
  check(cmp.advance_cold(5,"ctrl-restore")==1,"cold smelter workers did not resume production");
  equal(net.active(),cmp.active(),"network not deterministic after workers returned");
  const auto before_demote=owner.state();
  reject([&]{(void)net.demote("network-demote",ind::NetworkFault::AfterEvent);},"demotion event crash changed cold factory");
  check(owner.state().phase==re::TransitionPhase::ToCold,"failed network demotion lost frozen intent");
  // Abort prepared demotion and prove the active warehouse receives a rotated owner token.
  auto aborted=owner.abort("network-demote",owner.state().generation);
  check(aborted.generation==before_demote.generation+1 && aborted.owner==re::SimulationOwner::Detailed,"network ownership abort did not rotate generation");
  net.verify();
  committed=net.demote("network-demote-retry");
  check(committed.owner==re::SimulationOwner::Cold && committed.phase==re::TransitionPhase::Stable,"network did not finish demotion");
  equal(net.active(),cmp.active(),"cold network after detailed demotion differs from always-cold network");
  check(region.load()->scheduler.tick()==20&&control.load()->scheduler.tick()==20,"regional life support clock diverged during demotion");
  net.verify();cmp.verify();
  {PersistentWorld reopened(path,Simulation(9919));re::CheckpointStore rg(path,"net-real");re::RegionalModeAuthority own(rg);
   ind::RegionalProductionNetwork restored(reopened,rg,own,path,"home",scopes);restored.verify();equal(restored.active(),net.active(),"cold restart lost warehouse or WIP");}
  sqlite3* db=nullptr;check(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"audit SQLite cannot open");
  check(sqlite3_exec(db,"DROP TRIGGER sg_v51_event_immutable_del; DELETE FROM sg_v51_event WHERE scope='net-real' AND event_id='warehouse-plates'; CREATE TRIGGER sg_v51_event_immutable_del BEFORE DELETE ON sg_v51_event BEGIN SELECT RAISE(ABORT,'immutable production effect');END;",nullptr,nullptr,nullptr)==SQLITE_OK,"tamper removal failed");sqlite3_close(db);
  reject([&]{net.verify();},"removed inter-machine warehouse receipt not detected");
  std::cout<<"v51 production network: two actual machines, physical warehouse, atomic routing, workforce, cold/detailed conservation, restart, tamper refusal PASS\n";
  std::filesystem::remove(path);return 0;
 }catch(const std::exception& e){std::cerr<<"v51 network failure: "<<e.what()<<'\n';std::filesystem::remove(path);return 1;}
}
