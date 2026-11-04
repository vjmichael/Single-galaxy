#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("EXIT01A: ")+#x+" line "+std::to_string(__LINE__));}while(false)
template <typename F>static void refuses(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
static void clean(const std::string& p){for(const auto* e:{"","-wal","-shm"})std::filesystem::remove(p+e);}
static void sql(sqlite3* db,const std::string& q){
    char* err=nullptr;
    if(sqlite3_exec(db,q.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        std::string m=err?err:"sqlite failure";sqlite3_free(err);
        throw std::runtime_error(m);
    }
}
static std::vector<std::uint64_t> residents(const Simulation& world,EntityId colony){
    std::vector<std::uint64_t> out;
    for(const auto& [id,npc]:world.npc_records())
        if(npc.home_ship==colony && npc.location==colony)out.push_back(id);
    return out;
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_exit_v86_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",corrupted=db+".corrupted";
    EntityId threatened=0,stable=0,tiny=0;
    std::uint64_t founder=0,exiting=0,second_exiting=0;
    {
        Simulation simulation(8601);
        threatened=simulation.add_colony_ship(7,{0,0},100,4);
        stable=simulation.add_colony_ship(10,{0,0},100,3);
        tiny=simulation.add_colony_ship(11,{0,0},100,1);
        simulation.provision_colony_genesis(threatened,100,100,100,100,600);
        simulation.provision_colony_genesis(stable,100000,100000,100000,100000,100000);
        simulation.provision_colony_genesis(tiny,100,100,100,100,600);
        simulation.enable_individual_workforce();
        simulation.upgrade_assignment_snapshot_format();
        simulation.enable_passenger_life_support();
        PersistentWorld world(db,std::move(simulation));
        const auto source=residents(world.world(),threatened);
        CHECK(source.size()==4);
        founder=source.front();exiting=source[1];second_exiting=source[2];
        world.establish_colony_conflict_agenda(
            8501,threatened,founder,colony_conflict::Agenda::Predatory);
        world.establish_colony_conflict_agenda(
            8502,stable,residents(world.world(),stable).front(),
            colony_conflict::Agenda::Cooperative);
        world.establish_colony_conflict_agenda(
            8503,tiny,residents(world.world(),tiny).front(),
            colony_conflict::Agenda::Isolationist);
        CHECK(!world.assess_colony_exit_pressure(threatened,exiting));
        world.steps(980-world.world().tick());
        CHECK(world.world().tick()==980);
        CHECK(static_cast<int>(colony_safety::assess(
            world.world(),threatened,threatened).risk)>=2);
        CHECK(static_cast<int>(colony_safety::assess(
            world.world(),stable,stable).risk)<2);
        CHECK(!world.assess_colony_exit_pressure(threatened,founder));
        CHECK(!world.assess_colony_exit_pressure(threatened,99999999));
        CHECK(!world.assess_colony_exit_pressure(stable,residents(world.world(),stable)[1]));
        CHECK(!world.assess_colony_exit_pressure(tiny,residents(world.world(),tiny)[0]));
        CHECK(!world.assess_colony_exit_pressure(stable,exiting));
        const auto pressure=world.assess_colony_exit_pressure(threatened,exiting);
        CHECK(pressure.has_value());
        CHECK(pressure->source_colony==threatened);
        CHECK(pressure->source_owner==7);
        CHECK(pressure->npc_id==exiting);
        CHECK(pressure->world_generation==world.generation());
        CHECK(pressure->governance_event_id==0);
        CHECK(static_cast<int>(pressure->source_risk)>=2);
        const auto generation=world.generation();
        const auto observed=world.record_colony_exit_pressure(8601,threatened,exiting);
        CHECK(observed.event_id==8601);
        CHECK(observed.world_generation==generation);
        // Multiple distinct actual residents can experience the same harm;
        // this is not a fake vote, nor does it imply a second NPC consented
        // to any travel. One per NPC/governing term, enforced by SQLite.
        const auto second=world.record_colony_exit_pressure(
            8602,threatened,second_exiting);
        CHECK(second.npc_id==second_exiting);
        refuses([&]{(void)world.record_colony_exit_pressure(8603,threatened,exiting);});
        refuses([&]{(void)world.record_colony_exit_pressure(8604,threatened,founder);});
        refuses([&]{(void)world.record_colony_exit_pressure(
            8605,stable,residents(world.world(),stable)[1]);});
        CHECK(world.generation()==generation);
        CHECK(world.colony_governance(threatened).leader_id==founder);
        CHECK(world.colony_governance(threatened).agenda==colony_conflict::Agenda::Predatory);
        CHECK(residents(world.world(),threatened).size()==4);
        CHECK(world.world().npc_casualties().empty());
        world.steps(1100-world.world().tick());
        CHECK(world.world().tick()==1100);
        CHECK(residents(world.world(),threatened).size()==4);
    }
    {
        PersistentWorld replay(db,Simulation(8602));
        CHECK(replay.world().tick()==1100);
        CHECK(replay.colony_governance(threatened).leader_id==founder);
        CHECK(residents(replay.world(),threatened).size()==4);
        CHECK(replay.world().npc_casualties().empty());
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld saved(recovered,Simulation(8603));
        CHECK(saved.world().tick()==1100);
        CHECK(residents(saved.world(),threatened).size()==4);
    }
    PersistentWorld::recover_copy(db,corrupted);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(corrupted.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_npc_exit_pressure_no_update");
        sql(raw,"UPDATE sg_colony_npc_exit_pressure SET npc='99999999' "
            "WHERE event_id=8601");
        sql(raw,"CREATE TRIGGER sg_npc_exit_pressure_no_update "
            "BEFORE UPDATE ON sg_colony_npc_exit_pressure "
            "BEGIN SELECT RAISE(ABORT,'immutable real NPC exit pressure'); END");
        sqlite3_close(raw);
        refuses([&]{PersistentWorld forged(corrupted,Simulation(8604));});
    }
    clean(db);clean(recovered);clean(corrupted);
    std::cout<<"PASS EXIT01A: individual real-resident scarcity pressure, "
        "no petitions or ballots, no imagined departure, real player owner, "
        "one event per NPC per term, stable society unaffected, "
        "independent restart and recovery plus recovered tamper refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
