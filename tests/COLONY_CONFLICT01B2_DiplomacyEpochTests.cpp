#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CONFLICT01B2: ")+#x+" line "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& db){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(db+suffix);
}
static void sql(sqlite3* db,const std::string& command){
    char* error=nullptr;
    if(sqlite3_exec(db,command.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string msg=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error(msg);
    }
}
static std::vector<std::uint64_t> residents(const Simulation& sim,EntityId colony){
    std::vector<std::uint64_t> out;
    for(const auto& [id,npc]:sim.npc_records())
        if(npc.home_ship==colony && npc.location==colony)out.push_back(id);
    return out;
}
static std::string incident_for(const PersistentWorld& world,std::uint64_t command){
    for(const auto& e:world.recent_physical_incidents(100))
        if(e.command_id==command && e.kind==PhysicalKind::ShipDestroyed)
            return e.incident_id;
    throw std::runtime_error("physical ship destruction missing");
}
static std::uint64_t knowledge_for(const PersistentWorld& w,
    std::uint64_t recipient,const std::string& event,bool transmitted){
    for(const auto& k:w.recent_knowledge(100))
        if(k.recipient_npc==recipient && k.incident_id==event &&
           (k.parent_id!=0)==transmitted)return k.id;
    throw std::runtime_error("missing actual NPC witness/received claim");
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_conflict_v84_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto copy=db+".copy",tampered=db+".tampered";
    EntityId A=0,B=0,actor=0,victim=0,victim2=0;
    std::uint64_t leaderA=0,guardA=0,leaderB=0,guardB=0;
    std::uint64_t hostile_epoch=0;
    {
        Simulation sim(8401);
        A=sim.add_colony_ship(7,{0,0},100,2);
        B=sim.add_colony_ship(10,{0,0},100,2);
        actor=sim.add_ship(8,{1000,0},100);
        victim=sim.add_ship(9,{1100,0},100);
        victim2=sim.add_ship(9,{1200,0},100);
        sim.provision_colony_genesis(A,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(B,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(db,std::move(sim));
        const auto a=residents(world.world(),A);
        const auto b=residents(world.world(),B);
        CHECK(a.size()==2 && b.size()==2);
        leaderA=a[0];guardA=a[1];leaderB=b[0];guardB=b[1];
        world.establish_colony_conflict_agenda(
            8401,A,leaderA,colony_conflict::Agenda::Cooperative);
        world.establish_colony_conflict_agenda(
            8402,B,leaderB,colony_conflict::Agenda::Cooperative);
        // Existing 01A intent predates any incident and MUST stay neutral.
        world.record_colony_watch_contact(8501,A,guardA,actor);
        const auto old=world.record_colony_conflict_intent(8601,A,8501);
        CHECK(old.posture==colony_conflict::Posture::Observe);
        CHECK(old.relation_event_id==0);
        CHECK(world.submit_testing_destroy_ship(actor,victim,8701));
        world.step();
        const auto incident=incident_for(world,8701);
        CHECK(world.record_local_observation(incident,guardA));
        CHECK(world.record_local_observation(incident,guardB));
        CHECK(world.transmit_claim(
            knowledge_for(world,guardA,incident,false),leaderA,
            ClaimFidelity::Faithful,-90,100,"this ship owner endangers us"));
        CHECK(world.transmit_claim(
            knowledge_for(world,guardB,incident,false),leaderB,
            ClaimFidelity::Faithful,90,100,"this ship owner rescued our interests"));
        const auto grudge=world.record_colony_relationship_revision(
            8803,A,knowledge_for(world,leaderA,incident,true));
        const auto affinity=world.record_colony_relationship_revision(
            8802,B,knowledge_for(world,leaderB,incident,true));
        CHECK(grudge.new_score==-90);
        CHECK(affinity.new_score==90);
        hostile_epoch=grudge.event_id;
        // No same-generation time travel: claims take effect on next gen.
        world.record_colony_watch_contact(8901,A,guardA,actor);
        const auto bounded=world.assess_colony_diplomatic_intent(A,8901);
        CHECK(bounded.relation_event_id==0);
        CHECK(bounded.posture==colony_conflict::Posture::Observe);
        world.step();
        world.record_colony_watch_contact(8902,A,guardA,actor);
        world.record_colony_watch_contact(8903,B,guardB,actor);
        const auto wary=world.assess_colony_diplomatic_intent(A,8902);
        const auto friend_decision=world.assess_colony_diplomatic_intent(B,8903);
        CHECK(wary.relation_event_id==hostile_epoch);
        CHECK(wary.relation_score==-90);
        CHECK(wary.posture==colony_conflict::Posture::Warn);
        CHECK(wary.motive==colony_conflict::Motive::Security);
        CHECK(friend_decision.relation_event_id==affinity.event_id);
        CHECK(friend_decision.relation_score==90);
        CHECK(friend_decision.posture==colony_conflict::Posture::Observe);
        const auto durable=world.record_colony_diplomatic_intent(9003,A,8902);
        CHECK(durable.posture==colony_conflict::Posture::Warn);
        const auto other=world.record_colony_diplomatic_intent(9002,B,8903);
        CHECK(other.posture==colony_conflict::Posture::Observe);
        bool refused=false;
        try{(void)world.record_colony_diplomatic_intent(9091,A,8501);}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused); // already used by old strategic planner
        refused=false;
        try{(void)world.record_colony_conflict_intent(9092,A,8902);}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused); // already used by new planner
        CHECK(world.world().find(actor)->type==EntityType::Ship);
        CHECK(world.world().npc_casualties().empty());
        // New physical event changes A's opinion, but MUST NOT alter
        // the historical hostile response to the first grievance.
        CHECK(world.submit_testing_destroy_ship(actor,victim2,8702));
        world.step();
        const auto incident2=incident_for(world,8702);
        CHECK(world.record_local_observation(incident2,guardA));
        CHECK(world.transmit_claim(
            knowledge_for(world,guardA,incident2,false),leaderA,
            ClaimFidelity::Faithful,100,100,"new evidence revises my position"));
        const auto changed=world.record_colony_relationship_revision(
            8801,A,knowledge_for(world,leaderA,incident2,true));
        CHECK(changed.old_score==-90 && changed.new_score==10);
        world.step();
        world.record_colony_watch_contact(8904,A,guardA,actor);
        const auto calm=world.record_colony_diplomatic_intent(9001,A,8904);
        CHECK(calm.relation_event_id==changed.event_id);
        CHECK(calm.relation_score==10);
        CHECK(calm.posture==colony_conflict::Posture::Observe);
        world.steps(60-world.world().tick());
        CHECK(world.world().npc_casualties().empty());
    }
    {
        PersistentWorld reopened(db,Simulation(8402));
        CHECK(reopened.world().tick()==60);
        CHECK(reopened.colony_relationship(A,8).new_score==10);
        CHECK(reopened.colony_relationship(B,8).new_score==90);
        CHECK(reopened.world().find(actor)->type==EntityType::Ship);
    }
    PersistentWorld::recover_copy(db,copy);
    {
        PersistentWorld check(copy,Simulation(8403));
        CHECK(check.colony_relationship(A,8).new_score==10);
        CHECK(check.world().npc_casualties().empty());
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_diplomatic_intent_no_update");
        sql(raw,"UPDATE sg_colony_diplomatic_intent SET relation_score=0 "
            "WHERE event_id=9003");
        sql(raw,"CREATE TRIGGER sg_diplomatic_intent_no_update "
            "BEFORE UPDATE ON sg_colony_diplomatic_intent "
            "BEGIN SELECT RAISE(ABORT,'immutable diplomatic intent'); END");
        sqlite3_close(raw);
        bool rejected=false;
        try{PersistentWorld altered(tampered,Simulation(8404));}
        catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
    }
    clean(db);clean(copy);clean(tampered);
    std::cout<<"PASS CONFLICT01B2: neutral original policy, opposite prior-gen "
        "diplomacy choices, later revised opinion without retroactive change, "
        "restart, recovered copy and forged dated score rejected\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
