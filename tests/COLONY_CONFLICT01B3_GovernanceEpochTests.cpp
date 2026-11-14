#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CONFLICT01B3 ")+#x+" line "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& path){
    for(const auto* s:{"","-wal","-shm"})std::filesystem::remove(path+s);
}
static void sql(sqlite3* db,const std::string& cmd){
    char* error=nullptr;
    if(sqlite3_exec(db,cmd.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        std::string msg=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error("SQL tamper failed: "+msg);
    }
}
template <typename F> static void refused(F&& f){
    bool rejected=false;
    try{f();}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
}
static std::vector<std::uint64_t> residents(const Simulation& sim,EntityId ship){
    std::vector<std::uint64_t> out;
    for(const auto& [id,npc]:sim.npc_records())
        if(npc.home_ship==ship && npc.location==ship)out.push_back(id);
    return out;
}
static std::string incident_for(const PersistentWorld& world,std::uint64_t cmd){
    for(const auto& e:world.recent_physical_incidents(100))
        if(e.command_id==cmd && e.kind==PhysicalKind::ShipDestroyed)
            return e.incident_id;
    throw std::runtime_error("expected real destruction incident missing");
}
static std::uint64_t knowledge_for(const PersistentWorld& w,
    std::uint64_t recipient,const std::string& incident,bool secondhand){
    for(const auto& k:w.recent_knowledge(100))
        if(k.recipient_npc==recipient && k.incident_id==incident &&
           (k.parent_id!=0)==secondhand)return k.id;
    throw std::runtime_error("missing observed/delivered GRSM knowledge");
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_conflict_v85_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".copy",corrupted=db+".corrupted";
    EntityId A=0,B=0,attacker=0,victim=0,victim2=0;
    std::uint64_t founder=0,successor=0,guard=0,other_colony_leader=0;
    std::uint64_t original_decision=0;
    {
        Simulation sim(8501);
        A=sim.add_colony_ship(7,{0,0},100,3);
        B=sim.add_colony_ship(10,{0,0},100,2);
        attacker=sim.add_ship(8,{1000,0},100);
        victim=sim.add_ship(9,{1100,0},100);
        victim2=sim.add_ship(9,{1200,0},100);
        sim.provision_colony_genesis(A,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(B,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(db,std::move(sim));
        const auto residentsA=residents(world.world(),A);
        const auto residentsB=residents(world.world(),B);
        CHECK(residentsA.size()==3 && residentsB.size()==2);
        founder=residentsA[0];successor=residentsA[1];guard=residentsA[2];
        other_colony_leader=residentsB[0];
        world.establish_colony_conflict_agenda(
            8501,A,founder,colony_conflict::Agenda::Expansionist);
        world.establish_colony_conflict_agenda(
            8502,B,other_colony_leader,colony_conflict::Agenda::Cooperative);
        auto founding=world.colony_governance(A);
        CHECK(founding.event_id==0 && founding.ordinal==0);
        CHECK(founding.leader_id==founder);
        CHECK(founding.agenda==colony_conflict::Agenda::Expansionist);
        CHECK(founding.policy==colony_conflict::DiplomaticPolicy::Pragmatic);
        // An actual foreign contact, before any leader turnover.
        world.record_colony_watch_contact(8601,A,guard,attacker);
        const auto before=world.record_colony_diplomatic_intent(8701,A,8601);
        CHECK(before.governance_event_id==0);
        CHECK(before.leader_id==founder);
        CHECK(before.posture==colony_conflict::Posture::Warn);
        CHECK(before.motive==colony_conflict::Motive::Territory);
        original_decision=8701;
        // First grudge is the old founder's genuine delivered eyewitness report.
        CHECK(world.submit_testing_destroy_ship(attacker,victim,8801));
        world.step();
        const auto incident=incident_for(world,8801);
        CHECK(world.record_local_observation(incident,guard));
        CHECK(world.transmit_claim(
            knowledge_for(world,guard,incident,false),founder,
            ClaimFidelity::Faithful,-90,100,
            "foreign owner has endangered this region"));
        const auto grievance=world.record_colony_relationship_revision(
            8901,A,knowledge_for(world,founder,incident,true));
        CHECK(grievance.new_score==-90);
        world.step();
        world.record_colony_watch_contact(8602,A,guard,attacker);
        const auto old_hostile=world.record_colony_diplomatic_intent(8702,A,8602);
        CHECK(old_hostile.leader_id==founder);
        CHECK(old_hostile.relation_event_id==grievance.event_id);
        CHECK(old_hostile.posture==colony_conflict::Posture::Warn);
        // No cross-owner appointments, outsider successors, sham vacancy or
        // same-leader appointments. The world must not advance for any refusal.
        const auto generation=world.generation();
        refused([&]{world.record_colony_governance_transition(
            9000,A,successor,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::Appointment,10);});
        refused([&]{world.record_colony_governance_transition(
            9000,A,other_colony_leader,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::Appointment,7);});
        refused([&]{world.record_colony_governance_transition(
            9000,A,successor,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::EmergencyVacancy,7);});
        refused([&]{world.record_colony_governance_transition(
            9000,A,founder,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::Appointment,7);});
        CHECK(world.generation()==generation);
        const auto transition=world.record_colony_governance_transition(
            9001,A,successor,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::Appointment,7);
        CHECK(transition.ordinal==1 && transition.prior_event_id==0);
        CHECK(transition.leader_id==successor);
        CHECK(transition.policy==colony_conflict::DiplomaticPolicy::Reconciliatory);
        // Pending handoff isn't magically effective in its own generation.
        CHECK(world.colony_governance(A).leader_id==founder);
        refused([&]{world.record_colony_governance_transition(
            9002,A,successor,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::Appointment,7);});
        world.record_colony_watch_contact(8603,A,guard,attacker);
        const auto pending=world.record_colony_diplomatic_intent(8703,A,8603);
        CHECK(pending.leader_id==founder);
        CHECK(pending.posture==colony_conflict::Posture::Warn);
        world.step();
        const auto active=world.colony_governance(A);
        CHECK(active.event_id==9001 && active.ordinal==1);
        CHECK(active.leader_id==successor);
        CHECK(active.agenda==colony_conflict::Agenda::Cooperative);
        world.record_colony_watch_contact(8604,A,guard,attacker);
        const auto conciliatory=world.record_colony_diplomatic_intent(8704,A,8604);
        CHECK(conciliatory.governance_event_id==9001);
        CHECK(conciliatory.leader_id==successor);
        CHECK(conciliatory.relation_event_id==grievance.event_id);
        CHECK(conciliatory.relation_score==-90);
        CHECK(conciliatory.diplomatic_policy==
              colony_conflict::DiplomaticPolicy::Reconciliatory);
        CHECK(conciliatory.posture==colony_conflict::Posture::Observe);
        CHECK(conciliatory.motive==colony_conflict::Motive::None);
        CHECK(world.colony_relationship(A,8).new_score==-90);
        CHECK(world.colony_governance(B).leader_id==other_colony_leader);
        // A SECOND actual wreck teaches the newly appointed leader rather
        // than miraculously delivering knowledge to the retired founder.
        CHECK(world.submit_testing_destroy_ship(attacker,victim2,8802));
        world.step();
        const auto later=incident_for(world,8802);
        CHECK(world.record_local_observation(later,guard));
        CHECK(world.transmit_claim(
            knowledge_for(world,guard,later,false),successor,
            ClaimFidelity::Faithful,100,100,
            "new evidence supports a second chance"));
        refused([&]{(void)world.assess_colony_relationship_revision(
            B,knowledge_for(world,successor,later,true));});
        const auto revision=world.record_colony_relationship_revision(
            8902,A,knowledge_for(world,successor,later,true));
        CHECK(revision.leader_id==successor);
        CHECK(revision.old_score==-90 && revision.new_score==10);
        // Same leader can now change strategic direction, but cannot write
        // a no-op term disguised as a constitutional event.
        refused([&]{world.record_colony_governance_transition(
            9002,A,successor,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Reconciliatory,
            colony_conflict::TransitionKind::PolicyRevision,7);});
        const auto reorientation=world.record_colony_governance_transition(
            9002,A,successor,colony_conflict::Agenda::Isolationist,
            colony_conflict::DiplomaticPolicy::SecurityFirst,
            colony_conflict::TransitionKind::PolicyRevision,7);
        CHECK(reorientation.ordinal==2 && reorientation.prior_event_id==9001);
        // Cannot invoke the retired founder-only API after succession.
        world.record_colony_watch_contact(8605,A,guard,attacker);
        refused([&]{world.record_colony_conflict_intent(8705,A,8605);});
        world.step();
        CHECK(world.colony_governance(A).ordinal==2);
        CHECK(world.colony_governance(A).leader_id==successor);
        CHECK(world.colony_governance(A).agenda==colony_conflict::Agenda::Isolationist);
        world.record_colony_watch_contact(8606,A,guard,attacker);
        const auto defensive=world.record_colony_diplomatic_intent(8706,A,8606);
        CHECK(defensive.governance_event_id==9002);
        CHECK(defensive.relation_score==10);
        CHECK(defensive.posture==colony_conflict::Posture::Warn);
        CHECK(defensive.motive==colony_conflict::Motive::Security);
        // Second real succession. The old founder returns to office with a
        // conciliatory non-expansionist policy; existing beliefs are not
        // fabricated, deleted or automatically reset.
        const auto restoration=world.record_colony_governance_transition(
            9003,A,founder,colony_conflict::Agenda::Cooperative,
            colony_conflict::DiplomaticPolicy::Pragmatic,
            colony_conflict::TransitionKind::Appointment,7);
        CHECK(restoration.ordinal==3 && restoration.prior_event_id==9002);
        world.step();
        world.record_colony_watch_contact(8607,A,guard,attacker);
        const auto returned=world.record_colony_diplomatic_intent(8707,A,8607);
        CHECK(returned.governance_event_id==9003);
        CHECK(returned.leader_id==founder);
        CHECK(returned.posture==colony_conflict::Posture::Observe);
        CHECK(world.colony_relationship(A,8).new_score==10);
        CHECK(world.world().npc_casualties().empty());
        CHECK(world.world().find(attacker)->type==EntityType::Ship);
        CHECK(world.world().find(victim)->type==EntityType::Wreck);
        CHECK(world.world().find(victim2)->type==EntityType::Wreck);
        world.steps(120-world.world().tick());
        CHECK(world.world().tick()==120);
        CHECK(world.generation()>generation);
    }
    {
        PersistentWorld reopened(db,Simulation(8502));
        CHECK(reopened.world().tick()==120);
        CHECK(reopened.colony_governance(A).ordinal==3);
        CHECK(reopened.colony_governance(A).leader_id==founder);
        CHECK(reopened.colony_governance(B).ordinal==0);
        CHECK(reopened.colony_relationship(A,8).revision==2);
        CHECK(reopened.colony_relationship(A,8).new_score==10);
        CHECK(reopened.world().npc_casualties().empty());
        CHECK(original_decision==8701);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld copy(recovered,Simulation(8503));
        CHECK(copy.colony_governance(A).ordinal==3);
        CHECK(copy.colony_governance(A).leader_id==founder);
        CHECK(copy.colony_relationship(A,8).new_score==10);
    }
    PersistentWorld::recover_copy(db,corrupted);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(corrupted.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_governance_no_update");
        sql(raw,"UPDATE sg_colony_governance_epochs SET leader='999999' "
            "WHERE event_id=9001");
        sql(raw,"CREATE TRIGGER sg_colony_governance_no_update "
            "BEFORE UPDATE ON sg_colony_governance_epochs "
            "BEGIN SELECT RAISE(ABORT,'immutable constitutional epoch'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld spoofed(corrupted,Simulation(8504));});
    }
    clean(db);clean(recovered);clean(corrupted);
    std::cout<<"PASS CONFLICT01B3: 3 consecutive governance epochs, strict owner,"
        " physical resident successor, no same-gen policy time travel,"
        " changed diplomacy without falsifying original GRSM facts,"
        " predecessor chain, retire old API, independent restart/recovery,"
        " nonmonotonic experience and tamper rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
