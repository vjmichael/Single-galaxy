#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CONFLICT01B4A: ")+#x+" at "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& p){for(const auto* e:{"","-wal","-shm"})std::filesystem::remove(p+e);}
static void sql(sqlite3* db,const std::string& command){
    char* error=nullptr;
    if(sqlite3_exec(db,command.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        std::string reason=error?error:"sqlite error";sqlite3_free(error);
        throw std::runtime_error(reason);
    }
}
template <class F>static void refused(F&& f){
    bool rejected=false;
    try{f();}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
}
static std::uint64_t leader_for(const Simulation& world,EntityId source){
    for(const auto& [id,npc]:world.npc_records())
        if(npc.home_ship==source && npc.location==source)return id;
    throw std::runtime_error("missing local NPC founder");
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_b4a_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",corrupt=db+".corrupt";
    EntityId threatened=0,cooperative=0,remote=0;
    std::uint64_t old_leader=0,first_nominee=0;
    {
        Simulation sim(8601);
        threatened=sim.add_colony_ship(7,{0,0},100,4);
        cooperative=sim.add_colony_ship(10,{0,0},100,3);
        remote=sim.add_colony_ship(11,{0,0},100,1);
        sim.provision_colony_genesis(threatened,100,100,100,100,600);
        sim.provision_colony_genesis(cooperative,100,100,100,100,600);
        sim.provision_colony_genesis(remote,100,100,100,100,600);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(db,std::move(sim));
        old_leader=leader_for(world.world(),threatened);
        world.establish_colony_conflict_agenda(
            8501,threatened,old_leader,colony_conflict::Agenda::Predatory);
        world.establish_colony_conflict_agenda(
            8502,cooperative,leader_for(world.world(),cooperative),
            colony_conflict::Agenda::Cooperative);
        world.establish_colony_conflict_agenda(
            8503,remote,leader_for(world.world(),remote),
            colony_conflict::Agenda::Expansionist);
        CHECK(!world.assess_autonomous_colony_petition(threatened));
        world.steps(980-world.world().tick());
        CHECK(world.world().tick()==980);
        CHECK(static_cast<int>(colony_safety::assess(world.world(),threatened,threatened).risk)>=2);
        const auto proposed=world.assess_autonomous_colony_petition(threatened);
        CHECK(proposed.has_value());
        CHECK(proposed->world_generation==world.generation());
        CHECK(proposed->incumbent_id==old_leader);
        CHECK(proposed->incumbent_agenda==colony_conflict::Agenda::Predatory);
        CHECK(proposed->present_caucus.size()==3);
        CHECK(proposed->nominee_id==proposed->present_caucus.front());
        CHECK(proposed->nominee_id!=old_leader);
        CHECK(proposed->requested_agenda==colony_conflict::Agenda::Cooperative);
        CHECK(proposed->requested_policy==colony_conflict::DiplomaticPolicy::Pragmatic);
        CHECK(!world.assess_autonomous_colony_petition(cooperative));
        CHECK(!world.assess_autonomous_colony_petition(remote));
        first_nominee=proposed->nominee_id;
        const auto publication=world.record_autonomous_colony_petition(
            8601,threatened);
        CHECK(publication.event_id==8601);
        CHECK(publication.present_caucus==proposed->present_caucus);
        CHECK(world.colony_governance(threatened).leader_id==old_leader);
        CHECK(world.colony_governance(threatened).agenda==
              colony_conflict::Agenda::Predatory);
        // Petition is a record of conditions, NOT approval, a false ballot
        // or an immediate transfer of political authority.
        refused([&]{(void)world.record_autonomous_colony_petition(8602,threatened);});
        refused([&]{(void)world.record_autonomous_colony_petition(8603,cooperative);});
        refused([&]{(void)world.record_autonomous_colony_petition(8604,remote);});
        refused([&]{world.record_colony_governance_transition(
            8611,threatened,first_nominee,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Pragmatic,
            colony_conflict::TransitionKind::Appointment,10);});
        // The player/owner may independently approve the requested reforms
        // through the existing authenticated B3 handoff mechanism.
        const auto approved=world.record_colony_governance_transition(
            8611,threatened,first_nominee,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Pragmatic,
            colony_conflict::TransitionKind::Appointment,7);
        CHECK(approved.event_id==8611 && approved.leader_id==first_nominee);
        CHECK(world.colony_governance(threatened).leader_id==old_leader);
        world.step();
        CHECK(world.colony_governance(threatened).leader_id==first_nominee);
        CHECK(world.colony_governance(threatened).agenda==
              colony_conflict::Agenda::Cooperative);
        CHECK(!world.assess_autonomous_colony_petition(threatened));
        world.steps(1100-world.world().tick());
        CHECK(world.world().tick()==1100);
        CHECK(world.world().npc_casualties().empty());
    }
    {
        PersistentWorld fresh(db,Simulation(8602));
        CHECK(fresh.world().tick()==1100);
        CHECK(fresh.colony_governance(threatened).leader_id==first_nominee);
        CHECK(fresh.colony_governance(threatened).ordinal==1);
        CHECK(fresh.world().npc_casualties().empty());
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld copy(recovered,Simulation(8603));
        CHECK(copy.colony_governance(threatened).leader_id==first_nominee);
    }
    PersistentWorld::recover_copy(db,corrupt);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(corrupt.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_petition_no_update");
        sql(raw,"UPDATE sg_colony_political_petitions SET nominee='999999' "
            "WHERE event_id=8601");
        sql(raw,"CREATE TRIGGER sg_colony_petition_no_update "
            "BEFORE UPDATE ON sg_colony_political_petitions "
            "BEGIN SELECT RAISE(ABORT,'immutable NPC pressure petition'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld forged(corrupt,Simulation(8604));});
    }
    clean(db);clean(recovered);clean(corrupt);
    std::cout<<"PASS CONFLICT01B4A: crisis-only finite physical NPC caucus, "
        "no fake vote, no automatic government takeover, strict owner approval, "
        "independent original-snapshot reconstruction and tamper refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
