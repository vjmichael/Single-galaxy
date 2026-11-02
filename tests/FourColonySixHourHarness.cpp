// ECON-03c8 scenario verification: native regional kernel, NOT a complete playable-world test.
// Four 1M-person macro-cohorts grouped into two native regional schedulers.
// Synthetic carrier/worker receipts and preexisting supply inventory; mining/farming
// and a million-NPC detailed PersistentWorld are NOT implemented in this harness.
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalHostCommands.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalInterestManager.hpp"
#include <sqlite3.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
using namespace galaxy::regional_events;
namespace fs=std::filesystem;

static constexpr std::uint64_t TPS=5, HOURS=6, END=TPS*HOURS*3600, STEP=900;
static constexpr std::uint64_t PEOPLE=1000000, SWITCH_ONE=117*STEP, SWITCH_MID=3*3600*TPS;
static constexpr std::int64_t DELIVERY=100000, TRADE=250000;
static constexpr std::int64_t HABITAT=4000000, DEPOT=40000000;
static constexpr std::uint64_t COOLDOWN=9000;

static void insist(bool condition, const std::string& message){if(!condition)throw std::runtime_error(message);}
static void sql(sqlite3* db,const char* statement){
 char* err=nullptr;
 const int rc=sqlite3_exec(db,statement,nullptr,nullptr,&err);
 if(rc!=SQLITE_OK){const std::string why=err?err:sqlite3_errmsg(db);sqlite3_free(err);throw std::runtime_error("SQL: "+why);}
}
static void sql_file(const std::string& path,const char* statement){
 sqlite3* db=nullptr; if(sqlite3_open(path.c_str(),&db)!=SQLITE_OK)throw std::runtime_error("open DB failed");
 try{sql(db,statement);sqlite3_close(db);}catch(...){sqlite3_close(db);throw;}
}
static std::int64_t stockmass(const ColonyLifeSupport& s){
 return s.food_kg+s.water_kg+s.oxygen_kg+s.organic_waste_kg+s.wastewater_kg+s.co2_kg;
}
static std::int64_t total(const Scheduler& s,const std::string& a,const std::string& b){
 auto c=s.custody();std::int64_t sum=c.food_kg+c.water_kg+c.oxygen_kg;
 for(const auto& id:{a,b}){const auto& x=s.colony(id);sum+=stockmass(x.depot)+stockmass(x.habitat);}
 return sum;
}
static std::string snapshot(const Scheduler& s,const std::string& a,const std::string& b){
 std::ostringstream o;
 o<<s.tick()<<":"<<s.pending_events()<<":"<<s.pending_notices().size()<<":"<<s.shipments().size();
 for(const auto& id:{a,b}){
  const auto& c=s.colony(id);
  o<<":"<<id<<":"<<c.population<<":"<<c.processed_tick;
  for(const auto& v:{c.depot,c.habitat})o<<":"<<v.food_kg<<":"<<v.water_kg<<":"<<v.oxygen_kg
     <<":"<<v.organic_waste_kg<<":"<<v.wastewater_kg<<":"<<v.co2_kg<<":"<<v.shortages;
 }
 for(const auto& [id,x]:s.shipments())o<<":"<<id<<":"<<static_cast<int>(x.stage)<<":"<<x.origin<<":"<<x.destination<<":"<<x.departure_tick<<":"<<x.arrival_tick<<":"<<x.cargo.food_kg<<":"<<x.cargo.water_kg<<":"<<x.cargo.oxygen_kg;
 for(const auto& n:s.pending_notices())o<<":notice:"<<n.sequence<<":"<<n.tick<<":"<<static_cast<int>(n.kind)<<":"<<n.colony<<":"<<n.reference<<":"<<n.shortage_periods;
 return o.str();
}
static Scheduler seed(const std::string& a,const std::string& b){
 Scheduler s(COOLDOWN);
 cold_colony::State c;
 c.population=PEOPLE;
 c.habitat.food_kg=HABITAT;c.habitat.water_kg=HABITAT;c.habitat.oxygen_kg=HABITAT;
 c.depot.food_kg=DEPOT;c.depot.water_kg=DEPOT;c.depot.oxygen_kg=DEPOT;
 s.add_colony(a,c);s.add_colony(b,c);return s;
}
struct Region {
 std::string id,a,b,db;
 RegionBounds bounds;
 std::unique_ptr<CheckpointStore> store;
 std::unique_ptr<RegionalModeAuthority> auth;
 std::unique_ptr<HostCommands> host;
 std::optional<Scheduler> detailed;
 std::int64_t initial_mass=0;
 std::uint64_t shipments=0,deliveries=0, cold_commands=0,handoffs=0, stale_rejections=0;
 std::uint64_t max_inflight_at_switch=0;
 explicit Region(std::string name,std::string colonyA,std::string colonyB,std::string file,PositionMeters center)
 :id(std::move(name)),a(std::move(colonyA)),b(std::move(colonyB)),db(std::move(file)),
  bounds{id,center,1000.0}{
  auto s=seed(a,b);initial_mass=total(s,a,b);
  store=std::make_unique<CheckpointStore>(db,id);
  insist(store->save(s,0)==1,"seed persist failed");
  auth=std::make_unique<RegionalModeAuthority>(*store);
  insist(auth->enroll().owner==SimulationOwner::Cold,"seed ownership wrong");
  host=std::make_unique<HostCommands>(*store,[](const HostCommand& c){
   return c.actor=="scenario-fixture-host" && (c.op==HostOp::Advance ||
          (c.carrier_receipt=="SYNTHETIC_TEST_RECEIPT_NOT_PHYSICAL_PROOF"));
  });
 }
 bool is_detailed() const{return auth->state().owner==SimulationOwner::Detailed;}
 Scheduler read() const{
   if(is_detailed()){insist(detailed.has_value(),"detailed owner missing state");return *detailed;}
   auto cp=store->load();insist(cp.has_value(),"checkpoint missing");return cp->scheduler;
 }
 void assert_consistent(std::uint64_t tick){
   const auto cur=read();
   insist(cur.tick()==tick,id+" clock mismatch");
   insist(cur.colony(a).population==PEOPLE && cur.colony(b).population==PEOPLE,id+" population drift");
   insist(cur.colony(a).habitat.shortages==0 && cur.colony(b).habitat.shortages==0,id+" shortage");
   insist(total(cur,a,b)==initial_mass,id+" resource duplication / destruction");
   auto st=auth->state();insist(st.phase==TransitionPhase::Stable,"pending handoff leaked");
   if(!is_detailed()){
      const auto persisted=store->load();insist(persisted->revision==st.checkpoint_revision,"authority CAS desync");
      insist(st.boundary_tick==cur.tick(),"authority tick desync");
   }
 }
 void stage(sqlite3* dbh,const char* action,const Scheduler& s){
   sql(dbh,"CREATE TABLE IF NOT EXISTS fixture_handoffs(scope TEXT NOT NULL, action TEXT NOT NULL, tick INTEGER NOT NULL)");
   sqlite3_stmt* q=nullptr;
   insist(sqlite3_prepare_v2(dbh,"INSERT INTO fixture_handoffs(scope,action,tick) VALUES(?1,?2,?3)",-1,&q,nullptr)==SQLITE_OK,"stage SQL prepare");
   sqlite3_bind_text(q,1,id.c_str(),-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(q,2,action,-1,SQLITE_TRANSIENT);
   sqlite3_bind_int64(q,3,static_cast<sqlite3_int64>(s.tick()));
   const int status=sqlite3_step(q);sqlite3_finalize(q);insist(status==SQLITE_DONE,"stage SQL failed");
 }
 void promote(std::uint64_t tick){
   insist(!is_detailed(),id+" already warm");
   const auto cp=store->load();const auto before=snapshot(cp->scheduler,a,b);
   insist(cp->scheduler.tick()==tick,"promote clock mismatch");
   const auto owner=auth->state();
   const auto hid=id+"-promote-"+std::to_string(tick);
   auth->prepare(hid,SimulationOwner::Detailed,owner.generation,cp->revision,tick);
   const auto completed=auth->commit(hid,owner.generation,cp->revision,[&](sqlite3* dbh,Scheduler& candidate){
      insist(snapshot(candidate,a,b)==before,"promotion altered snapshot");
      stage(dbh,"promote",candidate);
      detailed=candidate; // synthetic detailed scheduler, NOT PersistentWorld entities
   });
   insist(completed.owner==SimulationOwner::Detailed,"promotion owner mismatch");
   insist(snapshot(*detailed,a,b)==before,"promotion state mismatch");
   // Stale cold executor may no longer update this region.
   HostCommand old;old.op=HostOp::Advance;old.actor="scenario-fixture-host";
   old.command_id="stale-"+id+"-"+std::to_string(tick);old.at_tick=tick+1;
   try{host->execute(old,cp->revision,owner.generation);throw std::runtime_error("stale cold command accepted");}
   catch(const std::runtime_error& e){
     if(std::string(e.what())=="stale cold command accepted")throw;
     stale_rejections++;
   }
   handoffs++;
 }
 void test_failed_demote(std::uint64_t tick){
   insist(is_detailed() && detailed && detailed->tick()==tick,"fault setup not detailed");
   const auto before=snapshot(*detailed,a,b);const auto cp=store->load();
   const auto old=auth->state();
   const auto hid=id+"-fault-demote-"+std::to_string(tick);
   auth->prepare(hid,SimulationOwner::Cold,old.generation,cp->revision,cp->scheduler.tick());
   sql_file(db,"CREATE TRIGGER fixture_fail_demote BEFORE INSERT ON fixture_handoffs "
           "WHEN NEW.action='demote' BEGIN SELECT RAISE(ABORT,'injected handoff write failure'); END");
   bool failed=false;
   try{auth->commit(hid,old.generation,cp->revision,[&](sqlite3* dbh,Scheduler& candidate){
        candidate=*detailed;stage(dbh,"demote",candidate);
       });}catch(const std::exception&){failed=true;}
   insist(failed,"injected SQL failure did not prevent handoff");
   insist(auth->state().owner==SimulationOwner::Detailed && auth->state().phase==TransitionPhase::ToCold,
          "failure stole authoritative owner");
   insist(store->load()->revision==cp->revision && snapshot(store->load()->scheduler,a,b)==snapshot(cp->scheduler,a,b),
          "failed transition damaged checkpoint");
   insist(snapshot(*detailed,a,b)==before,"failed handoff altered warm inventory");
   auth->abort(hid,old.generation);
   insist(auth->state().owner==SimulationOwner::Detailed && auth->state().phase==TransitionPhase::Stable,
          "failed handoff abort failed");
   sql_file(db,"DROP TRIGGER fixture_fail_demote");
 }
 void demote(std::uint64_t tick){
   insist(is_detailed(),id+" is not detailed");
   insist(detailed && detailed->tick()==tick,"detailed clock mismatch");
   auto before=snapshot(*detailed,a,b);
   auto cp=store->load();auto owner=auth->state();
   const auto hid=id+"-demote-"+std::to_string(tick);
   const auto custody=detailed->custody();
   if(custody.food_kg>0)max_inflight_at_switch=std::max(max_inflight_at_switch,static_cast<std::uint64_t>(custody.food_kg));
   auth->prepare(hid,SimulationOwner::Cold,owner.generation,cp->revision,cp->scheduler.tick());
   const auto result=auth->commit(hid,owner.generation,cp->revision,[&](sqlite3* dbh,Scheduler& candidate){
      insist(candidate.tick()<=detailed->tick(),"cold advanced beyond detailed");
      candidate=*detailed; // fixture reconciles a Scheduler copy, not physical world entities
      stage(dbh,"demote",candidate);
   });
   insist(result.owner==SimulationOwner::Cold && result.boundary_tick==tick,"demotion owner/tick mismatch");
   insist(snapshot(store->load()->scheduler,a,b)==before,"demotion changed inventory/in-flight shipment/notice data");
   detailed.reset();handoffs++;
   // Persisted data must match after reopening SQLite handles.
   restart_cold(tick);
 }
 void restart_cold(std::uint64_t tick){
   insist(!is_detailed(),"cannot simulate cold reboot of active warm fixture");
   const auto before=snapshot(store->load()->scheduler,a,b);
   host.reset();auth.reset();store.reset();
   store=std::make_unique<CheckpointStore>(db,id);
   auth=std::make_unique<RegionalModeAuthority>(*store);
   host=std::make_unique<HostCommands>(*store,[](const HostCommand& c){
    return c.actor=="scenario-fixture-host" && (c.op==HostOp::Advance ||
           c.carrier_receipt=="SYNTHETIC_TEST_RECEIPT_NOT_PHYSICAL_PROOF");
   });
   insist(snapshot(store->load()->scheduler,a,b)==before,"cold reboot changed checkpoint");
   insist(auth->state().owner==SimulationOwner::Cold && auth->state().boundary_tick==tick,"cold reboot owner changed");
 }
 void command(HostCommand c){
   c.actor="scenario-fixture-host";
   c.carrier_receipt="SYNTHETIC_TEST_RECEIPT_NOT_PHYSICAL_PROOF";
   const auto cp=store->load();const auto owner=auth->state();
   insist(owner.owner==SimulationOwner::Cold,"command attempted while detailed");
   const auto r=host->execute(c,cp->revision,owner.generation);
   insist(!r.replayed,"unexpected command replay");cold_commands++;
 }
 void interval(std::uint64_t previous,std::uint64_t tick){
   insist(tick==previous+STEP,"invalid interval");
   const auto depart=previous+700;
   const auto deliver=previous+720;
   auto shipment=[&](const std::string& origin,const std::string& dest){
      if(detailed){detailed->schedule_shipment(origin,dest,{TRADE,TRADE,TRADE},depart,300);}
      else{HostCommand c;c.op=HostOp::ScheduleShipment;c.origin=origin;c.destination=dest;
       c.cargo={TRADE,TRADE,TRADE};c.at_tick=depart;c.travel_ticks=300;
       c.command_id=id+"-ship-"+std::to_string(previous)+"-"+origin;command(c);}
      shipments++;
   };
   auto feed=[&](const std::string& dest){
      if(detailed){detailed->schedule_habitat_delivery(dest,{DELIVERY,DELIVERY,DELIVERY},deliver);}
      else{HostCommand c;c.op=HostOp::ScheduleHabitatDelivery;c.destination=dest;
       c.cargo={DELIVERY,DELIVERY,DELIVERY};c.at_tick=deliver;
       c.command_id=id+"-feed-"+std::to_string(previous)+"-"+dest;command(c);}
      deliveries++;
   };
   shipment(a,b);shipment(b,a);feed(a);feed(b);
   if(detailed){detailed->advance_to(tick);}
   else{HostCommand c;c.op=HostOp::Advance;c.at_tick=tick;c.command_id=id+"-advance-"+std::to_string(tick);command(c);}
   assert_consistent(tick);
 }
};
static void verify_sqlite(const std::string& file){
 sqlite3* db=nullptr;insist(sqlite3_open(file.c_str(),&db)==SQLITE_OK,"SQLite reopen failed");
 sqlite3_stmt* q=nullptr;insist(sqlite3_prepare_v2(db,"PRAGMA quick_check",-1,&q,nullptr)==SQLITE_OK,"quickcheck prepare");
 insist(sqlite3_step(q)==SQLITE_ROW,"quickcheck missing result");
 const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(q,0));
 const bool okay=value && std::string(value)=="ok";
 sqlite3_finalize(q);sqlite3_close(db);insist(okay,"SQLite quick_check failed");
}
struct ScenarioResult {
 std::string name,description;
 std::uint64_t ticks=0,shipments=0,deliveries=0,transitions=0,cold_commands=0,stale_rejections=0;
 std::int64_t total_mass=0;
 std::uint64_t notices=0,active_inflight=0;
 double elapsed_ms=0;
 std::string final_semantic_digest;
 std::vector<std::string> events;
};
static ScenarioResult scenario(const fs::path& directory,const std::string& id,
                              const std::vector<std::uint64_t>& travel_ticks){
 fs::create_directories(directory);
 const auto start=std::chrono::steady_clock::now();
 const auto file=(directory/(id+".sqlite")).string();
 Region near("near","near-A","near-B",file,{0,0,0});
 Region far("far","far-A","far-B",file,{50000000,0,0});
 RegionalInterestManager interest({2000,3000,1800});
 std::vector<PositionMeters> observers{{0,0,0}};
 const auto initial_interest=interest.consider(near.bounds,observers,0);
 insist(initial_interest.desired==RegionMode::Detailed && initial_interest.transition_requested,"initial detailed interest not detected");
 near.promote(0);interest.acknowledge(near.id,RegionMode::Detailed,0);
 auto cold_decision=interest.consider(far.bounds,observers,0);
 insist(cold_decision.desired==RegionMode::Cold,"far should start cold");
 ScenarioResult result{id,id=="scenario1"?"Visit far pair near the six-hour end":"Visit far pair at 3h; return to near pair at 5h51m"};
 std::ofstream csv(directory/(id+"_snapshots.csv"));
 csv<<"scenario,tick,game_hours,region,mode,population,shipments,custody_kg,habitat_food_kg,habitat_water_kg,habitat_oxygen_kg,depot_food_kg,total_mass_kg,notices,checkpoint_revision,owner_generation\n";
 auto record=[&](std::uint64_t tick){
  for(Region* r:{&near,&far}){
   const auto s=r->read(); const auto c=s.custody();
   const auto& A=s.colony(r->a);const auto& B=s.colony(r->b);
   auto authority=r->auth->state();
   csv<<id<<","<<tick<<","<<std::fixed<<std::setprecision(3)<<(static_cast<double>(tick)/TPS/3600)<<","<<r->id<<","<<(r->is_detailed()?"warm":"cold")
      <<","<<A.population+B.population<<","<<s.shipments().size()<<","<<c.food_kg+c.water_kg+c.oxygen_kg
      <<","<<A.habitat.food_kg+B.habitat.food_kg<<","<<A.habitat.water_kg+B.habitat.water_kg
      <<","<<A.habitat.oxygen_kg+B.habitat.oxygen_kg<<","<<A.depot.food_kg+B.depot.food_kg
      <<","<<total(s,r->a,r->b)<<","<<s.pending_notices().size()<<","<<authority.checkpoint_revision<<","<<authority.generation<<"\n";
  }
 };
 record(0);
 std::size_t travel_index=0;
 for(std::uint64_t tick=STEP;tick<=END;tick+=STEP){
   near.interval(tick-STEP,tick);far.interval(tick-STEP,tick);
   if(tick%18000==0){ // cold-only process restart, while the detailed fixture is still owned in memory
     if(!near.is_detailed())near.restart_cold(tick);
     if(!far.is_detailed())far.restart_cold(tick);
   }
   if(travel_index<travel_ticks.size() && tick==travel_ticks[travel_index]){
      const bool going_far=(travel_index%2==0);
      auto& old=going_far?near:far;
      auto& next=going_far?far:near;
      observers={{going_far?50000000.0:0.0,0,0}};
      auto a=interest.consider(old.bounds,observers,tick);
      auto b=interest.consider(next.bounds,observers,tick);
      insist(a.desired==RegionMode::Cold && a.transition_requested,"warm->cold interest mismatch");
      insist(b.desired==RegionMode::Detailed && b.transition_requested,"cold->warm interest mismatch");
      const auto old_state=old.read(),next_state=next.read();
      const auto old_shipments=old_state.shipments().size();
      const auto old_custody=old_state.custody().food_kg;
      const auto next_custody=next_state.custody().food_kg;
      insist(old_custody>=2*TRADE && next_custody>=2*TRADE,"expected in-flight trade at transition");
      if(id=="scenario2" && tick==SWITCH_MID){
        old.test_failed_demote(tick);
        result.events.push_back("tick=54000 injected SQL failure rejected; original warm owner retained; pending intent explicitly aborted before retry");
      }
      old.demote(tick);interest.acknowledge(old.id,RegionMode::Cold,tick);
      next.promote(tick);interest.acknowledge(next.id,RegionMode::Detailed,tick);
      old.assert_consistent(tick);next.assert_consistent(tick);
      auto same_old=snapshot(old.read(),old.a,old.b)==snapshot(old_state,old.a,old.b);
      auto same_next=snapshot(next.read(),next.a,next.b)==snapshot(next_state,next.a,next.b);
      insist(same_old && same_next,"transfer lost state at boundary");
      result.events.push_back(std::string("tick=")+std::to_string(tick)+" travel "+old.id+"->"+next.id+
          "; old mode cold, next mode warm; in-flight origin cargo="+std::to_string(old_custody)+
          "kg food; destination in-flight="+std::to_string(next_custody)+"kg food; origin shipments="+std::to_string(old_shipments));
      travel_index++;
   }
   record(tick);
 }
 insist(travel_index==travel_ticks.size(),"not all journeys simulated");
 near.assert_consistent(END);far.assert_consistent(END);
 verify_sqlite(file);
 const auto x=near.read(), y=far.read();
 result.ticks=END;result.shipments=near.shipments+far.shipments;
 result.deliveries=near.deliveries+far.deliveries;
 result.transitions=near.handoffs+far.handoffs;
 result.cold_commands=near.cold_commands+far.cold_commands;
 result.stale_rejections=near.stale_rejections+far.stale_rejections;
 result.total_mass=total(x,near.a,near.b)+total(y,far.a,far.b);
 // FNV is a diagnostic equality digest, not a cryptographic integrity proof.
 auto combined=snapshot(x,near.a,near.b)+"|"+snapshot(y,far.a,far.b);
 std::uint64_t fingerprint=14695981039346656037ULL;
 for(unsigned char byte:combined){fingerprint^=byte;fingerprint*=1099511628211ULL;}
 std::ostringstream digest;digest<<std::hex<<fingerprint;
 result.final_semantic_digest=digest.str();
 result.notices=x.pending_notices().size()+y.pending_notices().size();
 result.active_inflight=std::max(near.max_inflight_at_switch,far.max_inflight_at_switch);
 result.elapsed_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 return result;
}
int main(int argc,char** argv){
 try{
  const fs::path out=argc>1?argv[1]:"/mnt/data/four_colony_6hr/out";
  fs::create_directories(out);
  auto a=scenario(out,"scenario1",{SWITCH_ONE});
  auto b=scenario(out,"scenario2",{SWITCH_MID,SWITCH_ONE});
  insist(a.final_semantic_digest==b.final_semantic_digest,"travel patterns diverged in canonical scenario state");
  std::ofstream report(out/"results.json");
  auto write=[&](const ScenarioResult& x,bool comma){
    report<<"  {\"name\":\""<<x.name<<"\",\"game_ticks\":"<<x.ticks<<",\"game_hours\":6,"
    <<"\"colonies\":4,\"residents_per_colony\":1000000,\"shipments\":"<<x.shipments
    <<",\"habitat_deliveries\":"<<x.deliveries<<",\"ownership_handoffs\":"<<x.transitions
    <<",\"cold_host_commands\":"<<x.cold_commands<<",\"stale_commands_rejected\":"<<x.stale_rejections
    <<",\"mass_kg\":"<<x.total_mass<<",\"final_semantic_digest\":\""<<x.final_semantic_digest<<"\",\"pending_notices\":"<<x.notices
    <<",\"max_inflight_food_kg_at_switch\":"<<x.active_inflight
    <<",\"elapsed_wall_ms\":"<<std::fixed<<std::setprecision(3)<<x.elapsed_ms<<",\"events\":[";
    for(size_t i=0;i<x.events.size();++i){if(i)report<<",";report<<"\""<<x.events[i]<<"\"";}
    report<<"]}"<<(comma?",":"")<<"\n";
  };
  report<<"{\"test_kind\":\"regional-native-scheduler-synthetic-warm-fixture\",\"passes_native_colony_trade_consumption_handoffs\":true,\"passes_autonomous_mining_farming\":false,\"passes_real_1m_npc_detailed_world\":false,\"scenarios\":[\n";
  write(a,true);write(b,false);report<<"]}\n";report.close();
  std::cout<<"PASS 2 x 6-hour accelerated-time regional scenarios; 4 x 1M cohort populations; pair trading; cold/warm handoff; in-flight escrow conservation; durable cold restart; SQLite integrity\n";
  for(const auto& x:{a,b}){
    std::cout<<x.name<<" ticks="<<x.ticks<<" shipments="<<x.shipments<<" deliveries="<<x.deliveries
      <<" transitions="<<x.transitions<<" cold_host_commands="<<x.cold_commands<<" stale_rejected="<<x.stale_rejections
      <<" total_mass="<<x.total_mass<<" digest="<<x.final_semantic_digest<<" elapsed_wall_ms="<<x.elapsed_ms<<"\n";
    for(const auto& e:x.events)std::cout<<"  "<<e<<"\n";
  }
  std::cout<<"NOT TESTED: real autonomous mining/farming, real NPC/carrier auth, million-person detailed PersistentWorld reconstruction, rendering, physical travel.\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL four colony scenario: "<<e.what()<<"\n";return 1;}
}
