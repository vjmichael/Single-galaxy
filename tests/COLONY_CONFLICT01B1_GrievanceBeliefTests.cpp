#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sqlite3.h>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CONFLICT01B1: ")+ #x+" at "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& db){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(db+suffix);
}
static void sql(sqlite3* db,const std::string& command){
    char* error=nullptr;
    if(sqlite3_exec(db,command.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string msg=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error("fixture tamper: "+msg);
    }
}
static std::vector<std::uint64_t> residents(const Simulation& world,EntityId colony){
    std::vector<std::uint64_t> out;
    for(const auto& [id,npc]:world.npc_records())
        if(npc.home_ship==colony && npc.location==colony)
            out.push_back(id);
    return out;
}
static std::uint64_t knowledge_for(const PersistentWorld& world,
                                    std::uint64_t id,const std::string& incident){
    for(const auto& r:world.recent_knowledge(100))
        if(r.recipient_npc==id && r.incident_id==incident &&
           r.parent_id==0)return r.id;
    throw std::runtime_error("missing actual firsthand GRSM observation");
}
static std::uint64_t opinion_for(const PersistentWorld& world,
                                  std::uint64_t leader,const std::string& incident){
    for(const auto& r:world.recent_knowledge(100))
        if(r.recipient_npc==leader && r.incident_id==incident &&
           r.parent_id!=0)return r.id;
    throw std::runtime_error("missing actual received NPC belief");
}
static std::string incident_for(const PersistentWorld& world,std::uint64_t command){
    for(const auto& e:world.recent_physical_incidents(100))
        if(e.command_id==command && e.kind==PhysicalKind::ShipDestroyed)
            return e.incident_id;
    throw std::runtime_error("missing actual physical ship destruction");
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_conflict_v83_"+std::to_string(std::chrono::steady_clock::now()
           .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",tampered=db+".tampered";
    EntityId colonyA=0,colonyB=0,attacker=0,victim=0,second_victim=0;
    // Nonmonotonic globally allocated IDs must not rewrite chronology:
    // revision order comes from the real prior chain, not numeric event IDs.
    const std::uint64_t first_event=9403,second_event=9402,third_event=9401;
    std::uint64_t leaderA=0,observerA=0,leaderB=0,observerB=0;
    {
        Simulation sim(8301);
        colonyA=sim.add_colony_ship(7,{0,0},100,2);
        colonyB=sim.add_colony_ship(10,{0,0},100,2);
        attacker=sim.add_ship(8,{1000,0},100);
        victim=sim.add_ship(9,{1100,0},100);
        second_victim=sim.add_ship(9,{1200,0},100);
        sim.provision_colony_genesis(colonyA,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(colonyB,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(db,std::move(sim));
        const auto a=residents(world.world(),colonyA);
        const auto b=residents(world.world(),colonyB);
        CHECK(a.size()==2 && b.size()==2);
        leaderA=a[0];observerA=a[1];leaderB=b[0];observerB=b[1];
        world.establish_colony_conflict_agenda(
            9101,colonyA,leaderA,colony_conflict::Agenda::Cooperative);
        world.establish_colony_conflict_agenda(
            9102,colonyB,leaderB,colony_conflict::Agenda::Isolationist);
        const auto neutral=world.colony_relationship(colonyA,8);
        CHECK(neutral.new_score==0 &&
              neutral.attitude==colony_conflict::Attitude::Neutral);
        CHECK(world.submit_testing_destroy_ship(attacker,victim,9301));
        world.step();
        CHECK(world.world().find(victim)->type==EntityType::Wreck);
        CHECK(world.world().find(attacker)->owner_id==8);
        const auto event=incident_for(world,9301);
        CHECK(world.record_local_observation(event,observerA));
        CHECK(world.record_local_observation(event,observerB));
        const auto sawA=knowledge_for(world,observerA,event);
        const auto sawB=knowledge_for(world,observerB,event);
        CHECK(world.transmit_claim(sawA,leaderA,ClaimFidelity::Faithful,
             90,100,"attacker defeated a threat to our interests"));
        CHECK(world.transmit_claim(sawB,leaderB,ClaimFidelity::Faithful,
             -90,100,"attacker destroyed a neutral ship; distrust them"));
        const auto claimA=opinion_for(world,leaderA,event);
        const auto claimB=opinion_for(world,leaderB,event);
        CHECK(claimA!=claimB);
        bool wrong=false;
        try{(void)world.assess_colony_relationship_revision(colonyA,claimB);}
        catch(const std::runtime_error&){wrong=true;}
        CHECK(wrong);
        const auto praise=world.assess_colony_relationship_revision(colonyA,claimA);
        const auto blame=world.assess_colony_relationship_revision(colonyB,claimB);
        CHECK(praise.target_owner==8 && blame.target_owner==8);
        CHECK(praise.incident_generation==blame.incident_generation);
        CHECK(praise.old_score==0 && praise.delta==90 && praise.new_score==90);
        CHECK(blame.old_score==0 && blame.delta==-90 && blame.new_score==-90);
        CHECK(praise.attitude==colony_conflict::Attitude::Allied);
        CHECK(blame.attitude==colony_conflict::Attitude::Hostile);
        const auto recordedA=world.record_colony_relationship_revision(
            first_event,colonyA,claimA);
        const auto recordedB=world.record_colony_relationship_revision(
            second_event,colonyB,claimB);
        CHECK(recordedA.event_id==first_event);
        CHECK(recordedB.event_id==second_event);
        CHECK(world.colony_relationship(colonyA,8).new_score==90);
        CHECK(world.colony_relationship(colonyB,8).new_score==-90);
        wrong=false;
        try{(void)world.record_colony_relationship_revision(9499,colonyA,claimA);}
        catch(const std::runtime_error&){wrong=true;}
        CHECK(wrong); // one incident may be debated but not counted twice by same colony
        CHECK(world.world().find(attacker)->type==EntityType::Ship);
        CHECK(world.world().npc_casualties().empty());
        CHECK(world.submit_testing_destroy_ship(attacker,second_victim,9302));
        world.step();
        CHECK(world.world().find(second_victim)->type==EntityType::Wreck);
        const auto later=incident_for(world,9302);
        CHECK(world.record_local_observation(later,observerA));
        CHECK(world.transmit_claim(knowledge_for(world,observerA,later),
            leaderA,ClaimFidelity::Faithful,-100,100,
            "second wreck shows attacker may endanger others"));
        const auto change=world.record_colony_relationship_revision(
            third_event,colonyA,opinion_for(world,leaderA,later));
        CHECK(change.revision==2);
        CHECK(change.prior_event_id==first_event);
        CHECK(change.old_score==90 && change.delta==-100 && change.new_score==-10);
        CHECK(change.attitude==colony_conflict::Attitude::Neutral);
        CHECK(world.colony_relationship(colonyA,8).new_score==-10);
        CHECK(world.colony_relationship(colonyB,8).new_score==-90);
        world.steps(160-world.world().tick());
        CHECK(world.world().tick()==160);
        CHECK(world.world().npc_records().size()==4);
        CHECK(world.world().npc_casualties().empty());
    }
    {
        PersistentWorld restarted(db,Simulation(8302));
        CHECK(restarted.world().tick()==160);
        CHECK(restarted.colony_relationship(colonyA,8).revision==2);
        CHECK(restarted.colony_relationship(colonyA,8).new_score==-10);
        CHECK(restarted.colony_relationship(colonyB,8).new_score==-90);
        CHECK(restarted.world().find(attacker)->type==EntityType::Ship);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld copy(recovered,Simulation(8303));
        CHECK(copy.colony_relationship(colonyA,8).new_score==-10);
        CHECK(copy.colony_relationship(colonyB,8).attitude==
              colony_conflict::Attitude::Hostile);
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_relation_no_update");
        sql(raw,"UPDATE sg_colony_relationship_revisions SET target_owner='9' "
            "WHERE event_id=9402");
        sql(raw,"CREATE TRIGGER sg_colony_relation_no_update "
                "BEFORE UPDATE ON sg_colony_relationship_revisions "
                "BEGIN SELECT RAISE(ABORT,'immutable colony diplomatic decision'); END");
        sqlite3_close(raw);
        bool failed=false;
        try{PersistentWorld invalid(tampered,Simulation(8304));}
        catch(const std::runtime_error&){failed=true;}
        CHECK(failed);
    }
    clean(db);clean(recovered);clean(tampered);
    std::cout<<"PASS CONFLICT01B1: two colonies interpret same real wreck oppositely; "
        "later negative incident reverses ally attitude; original facts remain; "
        "restart, copy and forged-owner rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
