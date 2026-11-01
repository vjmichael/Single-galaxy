#include "galaxy/RegionalPersistentWorldBridge.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
using namespace galaxy::regional_events;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("assertion failed @")+std::to_string(__LINE__)+": " + #x);}while(0)
struct Temp {
  fs::path dir;
  Temp():dir(fs::temp_directory_path()/("sg_c8_real_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){
      fs::create_directories(dir);
  }
  ~Temp(){std::error_code ec;fs::remove_all(dir,ec);}
  std::string path() const {return (dir/"actual-persistent-world.sqlite").string();}
};
template<class F>void must_throw(F&& f,const char* label){
    try{f();}catch(const std::exception&){return;}
    throw std::runtime_error(std::string("expected rejection: ")+label);
}
static std::uint64_t count_anchors(const std::string& path){
    sqlite3* db=nullptr;
    CHECK(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    sqlite3_stmt* q=nullptr;
    CHECK(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM sg_world_detail_anchor",-1,&q,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(q)==SQLITE_ROW);
    const auto out=sqlite3_column_int64(q,0);
    sqlite3_finalize(q);sqlite3_close(db);
    return static_cast<std::uint64_t>(out);
}
static void sql_exec(const std::string& path,const char* sql){
    sqlite3* db=nullptr;
    CHECK(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
    char* err=nullptr;
    const auto result=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(result!=SQLITE_OK){const std::string msg=err?err:"unknown";sqlite3_free(err);sqlite3_close(db);throw std::runtime_error(msg);}
    sqlite3_close(db);
}
static void zero_effect_roundtrip_and_restart(){
    Temp t;
    Simulation first(211);
    auto ship=first.add_colony_ship(7,{0,0},64,6);
    first.add_asteroid({1000,0},50);
    const auto source_mass=first.total_ore();
    {
        PersistentWorld world(t.path(),std::move(first));
        CHECK(world.generation()==1);
        auto before=world.world().canonical_state();
        PersistentWorldBridge bridge(t.path(),"primary",world);
        auto cold=bridge.enroll();
        CHECK(cold.owner==SimulationOwner::Cold&&cold.phase==TransitionPhase::Stable);
        // Real PersistentWorld ticks are fenced, not just a dummy scheduler.
        must_throw([&]{world.step();},"live world step when cold");
        CHECK(world.generation()==1 && world.world().canonical_state()==before);
        auto hot=bridge.promote("cold-to-detail-1");
        CHECK(hot.owner==SimulationOwner::Detailed && hot.generation>cold.generation);
        CHECK(count_anchors(t.path())==1);
        auto cold2=bridge.demote_if_unchanged("detail-to-cold-1");
        CHECK(cold2.owner==SimulationOwner::Cold);
        CHECK(cold2.generation>hot.generation);
        CHECK(count_anchors(t.path())==0);
        CHECK(bridge.checkpoint().load()->scheduler.custody().food_kg==0);
        CHECK(world.world().total_ore()==source_mass);
        must_throw([&]{world.step();},"fenced after demotion");
        CHECK(world.world().canonical_state()==before);
        CHECK(world.world().colony_ship(ship)->crew==6);
    }
    // Real restore path, not a synthetic SQLite fixture.
    {
        PersistentWorld restored(t.path(),Simulation(999));
        CHECK(restored.world().total_ore()==source_mass);
        PersistentWorldBridge reconnected(t.path(),"primary",restored);
        auto cold=reconnected.enroll();
        CHECK(cold.owner==SimulationOwner::Cold);
        CHECK(reconnected.promote("cold-to-detail-2").owner==SimulationOwner::Detailed);
        CHECK(reconnected.demote_if_unchanged("detail-to-cold-2").owner==SimulationOwner::Cold);
    }
}
static void changed_live_game_is_not_silently_reconciled(){
    Temp t;
    Simulation initial(42);
    auto ship=initial.add_colony_ship(7,{0,0},60,5);
    auto rock=initial.add_asteroid({1000,0},35);
    PersistentWorld world(t.path(),std::move(initial));
    PersistentWorldBridge bridge(t.path(),"primary",world);
    bridge.enroll();
    const auto before=world.world().canonical_state();
    auto old_gen=world.generation();
    // Authoritative command receipt cannot be recorded while cold.
    Command cmd{};cmd.id=77;cmd.sequence=77;cmd.execute_tick=1;cmd.issuer=7;
    cmd.actor=ship;cmd.target=rock;cmd.action=Action::Mine;cmd.amount=10;
    must_throw([&]{world.submit(cmd);},"physical command submission fenced when cold");
    CHECK(world.world().canonical_state()==before);
    CHECK(world.generation()==old_gen);
    auto detailed=bridge.promote("detail-1");
    CHECK(world.submit(cmd));world.step();
    CHECK(world.world().find(ship)->cargo==10);
    CHECK(world.world().total_ore()==world.resource_origin());
    CHECK(world.generation()>old_gen);
    must_throw([&]{bridge.demote_if_unchanged("cold-1");},"physical state changed");
    const auto after=bridge.state();
    CHECK(after.owner==SimulationOwner::Detailed && after.phase==TransitionPhase::Stable);
    CHECK(after.generation>detailed.generation); // failed transition was aborted, stale token fenced
    CHECK(count_anchors(t.path())==1);
    CHECK(world.world().find(ship)->cargo==10);
    // Every gate rechecks persisted current state, never trusts a passed object alone.
    PersistentWorld different((t.dir/"unrelated.sqlite").string(),Simulation(123));
    PersistentWorldBridge wrong(t.path(),"primary",different);
    must_throw([&]{wrong.demote_if_unchanged("wrong-world");},"wrong live source");
    CHECK(bridge.state().owner==SimulationOwner::Detailed);
    CHECK(bridge.state().phase==TransitionPhase::Stable);
    // Reopen the real world and confirm the physical command was never discarded.
    PersistentWorld restored(t.path(),Simulation(999));
    CHECK(restored.world().find(ship)->cargo==10);
}
static void no_mirroring_and_global_scope_fence(){
    Temp t;
    Simulation initial(21);
    initial.add_colony_ship(7,{0,0},30,3);
    PersistentWorld world(t.path(),std::move(initial));
    {
       CheckpointStore existing(t.path(),"primary");
       Scheduler s; cold_colony::State state{};
       state.population=1;
       s.add_colony("fake-stock-copy",state);
       CHECK(existing.save(s,0)==1);
    }
    PersistentWorldBridge forbidden(t.path(),"primary",world);
    must_throw([&]{forbidden.enroll();},"nonempty regional copy");
    {
       CheckpointStore clean(t.path(),"real");
       CHECK(clean.save(Scheduler{},0)==1);
    }
    PersistentWorldBridge real(t.path(),"real",world);
    CHECK(real.enroll().owner==SimulationOwner::Cold);
    must_throw([&]{forbidden.enroll();},"single world linked to incompatible scope");
}
static void interrupted_prepare_and_staged_failure_recovery(){
    Temp t;
    Simulation initial(300);
    initial.add_colony_ship(7,{0,0},20,2);
    PersistentWorld world(t.path(),std::move(initial));
    PersistentWorldBridge bridge(t.path(),"primary",world);
    auto cold=bridge.enroll();
    // Simulate a process dying after durable prepare but before commit.
    {
        CheckpointStore storage(t.path(),"primary");
        RegionalModeAuthority other(storage);
        auto saved=storage.load(); CHECK(saved.has_value());
        auto pending=other.prepare("interrupted-1",SimulationOwner::Detailed,
                  cold.generation,saved->revision,saved->scheduler.tick());
        CHECK(pending.phase==TransitionPhase::ToDetailed);
    }
    PersistentWorldBridge recovered(t.path(),"primary",world);
    CHECK(recovered.enroll().phase==TransitionPhase::ToDetailed);
    must_throw([&]{world.step();},"cold world still fenced while prepared");
    auto aborted=recovered.abort_pending("interrupted-1",cold.generation);
    CHECK(aborted.phase==TransitionPhase::Stable);
    CHECK(aborted.generation>cold.generation);
    // Force a real SQLite stage failure after the handoff was prepared.
    sql_exec(t.path(),"CREATE TRIGGER sg_c8_fail_stage BEFORE INSERT ON sg_world_detail_anchor "
                       "BEGIN SELECT RAISE(ABORT,'injected stage failure'); END");
    must_throw([&]{recovered.promote("injected-failure");},"atomic staging SQL failure");
    CHECK(recovered.state().owner==SimulationOwner::Cold);
    CHECK(recovered.state().phase==TransitionPhase::Stable);
    CHECK(count_anchors(t.path())==0);
    must_throw([&]{world.step();},"cold still fenced after rollback");
    sql_exec(t.path(),"DROP TRIGGER sg_c8_fail_stage");
    CHECK(recovered.promote("recovered-promotion").owner==SimulationOwner::Detailed);
    CHECK(count_anchors(t.path())==1);
}

int main(){
  try{
    zero_effect_roundtrip_and_restart();
    changed_live_game_is_not_silently_reconciled();
    no_mirroring_and_global_scope_fence();
    interrupted_prepare_and_staged_failure_recovery();
    std::cout << "ECON-03c8a: real PersistentWorld snapshot gate and refusal tests passed\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr << "ECON-03c8a FAILED: " << e.what()<<'\n';return 1;
  }
}
