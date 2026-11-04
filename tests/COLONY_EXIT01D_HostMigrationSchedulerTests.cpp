#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("EXIT01B: ")+#x+" line "+std::to_string(__LINE__));}while(false)
template<class F>static void refused(F&& f){bool ok=false;try{f();}catch(const std::exception&){ok=true;}CHECK(ok);}
static void clean(const std::string& path){for(const auto* ext:{"","-wal","-shm"})std::filesystem::remove(path+ext);}
static void sql(sqlite3* db,const std::string& statement){
    char* error=nullptr;
    if(sqlite3_exec(db,statement.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        std::string msg=error?error:"unknown SQL error";sqlite3_free(error);
        throw std::runtime_error(msg);
    }
}
static Command command(const PersistentWorld& w,std::uint64_t id,
    PlayerId issuer,EntityId source,EntityId dest,Action action,
    bool ferry_passenger=false){
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=source;c.target=dest;c.action=action;
    if(ferry_passenger){c.transport_id=900;c.passenger_ids={2};}
    return c;
}
static void executed(PersistentWorld& w,std::uint64_t id){
    w.step();
    CHECK(!w.world().events().empty());
    const auto& e=w.world().events().back();
    CHECK(e.command_id==id && e.result==Outcome::Applied);
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_exit_v89_scheduler_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",spoof=db+".spoof",bad_receipt=db+".badreceipt",
               false_ferry=db+".falseferry",stripped_reason=db+".strippedreason";
    EntityId origin=0,dest=0,ferry=0,alternate_ferry=0;
    std::uint64_t last_origin_diary=0;
    {
        Simulation initial(8701);initial.add_system(2);
        origin=initial.add_colony_ship(7,{0,0},100,6);
        ferry=initial.add_colony_ship(7,{0,0},100,6);
        dest=initial.add_colony_ship(10,{0,0},100,6);
        alternate_ferry=initial.add_colony_ship(7,{0,0},100,2);
        const auto gate=initial.add_gate(1,{0,0},2,{5000,0});
        initial.provision_colony_genesis(origin,100,100,100,100,600);
        initial.provision_colony_genesis(ferry,100000,100000,100000,100000,100000);
        initial.provision_colony_genesis(dest,100000,100000,100000,100000,100000);
        initial.provision_colony_genesis(alternate_ferry,100000,100000,100000,100000,100000);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        initial.enable_passenger_life_support();
        PersistentWorld w(db,std::move(initial));
        CHECK(w.world().npc_records().at(1).home_ship==origin);
        CHECK(w.world().npc_records().at(2).home_ship==origin);
        CHECK(w.world().npc_records().at(13).home_ship==dest);
        NpcAppearanceStore family(db,"voluntary",8701,[&](auto id){
            return w.world().npc_records().count(id)!=0;});
        family.register_settlement(origin,6);
        family.register_settlement(dest,6);
        family.enroll_family_founder(2,origin);
        family.enroll_family_founder(13,dest);
        re::CheckpointStore region(db,"voluntary-region");
        re::Scheduler scheduler;
        cold_colony::State population{};population.population=6;
        scheduler.add_colony("origin",population);
        scheduler.add_colony("destination",population);
        CHECK(region.save(scheduler,0)==1);
        re::RegionalModeAuthority regional_owner(region);
        CHECK(regional_owner.enroll().generation==1);
        w.establish_colony_conflict_agenda(
            8501,origin,1,colony_conflict::Agenda::Predatory);
        w.establish_colony_conflict_agenda(
            8502,dest,13,colony_conflict::Agenda::Cooperative);
        // The receiving owner physically relocates its own colony to system 2.
        CHECK(w.submit(command(w,100,10,dest,gate,Action::RequestJump)));
        executed(w,100);
        w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
        CHECK(w.world().find(dest)->system_id==2);
        w.steps(980-w.world().tick());
        CHECK(w.world().tick()==980);
        CHECK(colony_safety::assess(w.world(),origin,origin).risk>=
              colony_safety::Risk::Critical);
        CHECK(colony_safety::assess(w.world(),dest,dest).risk==
              colony_safety::Risk::Safe);
        // The host never receives a user-selected NPC here: the supervisor
        // finds genuine named residents by rotating over physical occupancy.
        PersistentWorld::HostVoluntaryDepartureSchedule pulse{};
        pulse.origin=origin;pulse.scan_budget=8;
        const auto too_early=w.supervise_voluntary_migration(pulse);
        CHECK(too_early.status==
              PersistentWorld::HostVoluntaryDepartureStatus::NotDue);
        // Ground PERSON-SPECIFIC contrasting social histories in genuine
        // same-tick physical encounters: harm to #2, assistance to #3.
        CHECK(w.schedule_social_encounters(64,8)>=2);
        std::uint64_t harm_contact=0,help_contact=0;
        for(const auto& contact:w.recent_social_encounters(128)){
            if(contact.npc_low==1 && contact.npc_high==2)harm_contact=contact.id;
            if(contact.npc_low==1 && contact.npc_high==3)help_contact=contact.id;
        }
        CHECK(harm_contact && help_contact);
        CHECK(w.record_social_interaction(
            harm_contact,2,1,SocialInteractionKind::Harm));
        CHECK(w.record_social_interaction(
            help_contact,3,1,SocialInteractionKind::Assistance));
        w.step(); // source interactions become prior-generation memories
        w.steps(984-w.world().tick());
        CHECK(w.world().tick()==984);
        auto first_report=w.supervise_voluntary_migration(pulse);
        CHECK(first_report.examined==6);
        CHECK(first_report.observed==5); // excludes the incumbent #1
        const auto first=*w.colony_personal_outlook(origin,2);
        const auto helped=*w.colony_personal_outlook(origin,3);
        CHECK(first.leader_rapport<0 && helped.leader_rapport>0);
        CHECK(first.discontent>helped.discontent);
        CHECK(!first.considering_departure && !helped.considering_departure);
        // A crash or repeated host callback in the same generation must
        // NOT double count one bad period or create a second personal event.
        auto replay=w.supervise_voluntary_migration(pulse);
        CHECK(replay.observed==0);
        CHECK(w.colony_personal_outlook(origin,2)->event_id==first.event_id);
        w.steps(8);
        CHECK(w.world().tick()==992);
        auto second_report=w.supervise_voluntary_migration(pulse);
        CHECK(second_report.examined==6 && second_report.observed==5);
        const auto second=*w.colony_personal_outlook(origin,2);
        const auto second_helped=*w.colony_personal_outlook(origin,3);
        CHECK(second.prior_event_id==first.event_id);
        CHECK(second_helped.prior_event_id==helped.event_id);
        CHECK(second.considering_departure);
        CHECK(!second_helped.considering_departure);
        CHECK(second.discontent>second_helped.discontent);
        CHECK(second_report.status==
              PersistentWorld::HostVoluntaryDepartureStatus::SeekingDestination);
        CHECK(w.world().colony_ship(origin)->crew==6);
        // Without a RECEIVING OWNER offer, nobody is made to emigrate.
        w.steps(8);
        CHECK(w.world().tick()==1000);
        PersistentWorld::HostVoluntaryImmigrationOffer authorized{};
        authorized.destination=dest;authorized.ferry=ferry;
        authorized.receiving_owner=10; // host-authenticated receiver
        authorized.admission_event_id=8701;authorized.pressure_event_id=8601;
        authorized.transport_id=900;authorized.boarding_command_id=101;
        auto wrong_owner=authorized;wrong_owner.receiving_owner=7;
        pulse.approved_offer=wrong_owner;
        auto unauthorized=w.supervise_voluntary_migration(pulse);
        CHECK(unauthorized.status==
              PersistentWorld::HostVoluntaryDepartureStatus::PhysicalRouteBlocked);
        CHECK(w.world().colony_ship(origin)->crew==6);
        CHECK(!w.world().has_command_id(101));
        // Same tick retry with proper broker-supplied authorization does
        // not reappraise people a second time or fabricate any passenger.
        pulse.approved_offer=authorized;
        auto accepted=w.supervise_voluntary_migration(pulse);
        CHECK(accepted.status==
              PersistentWorld::HostVoluntaryDepartureStatus::BoardingQueued);
        CHECK(accepted.selected_npc==2);
        CHECK(accepted.admission_event_id==8701);
        CHECK(accepted.observed==0);
        last_origin_diary=w.colony_personal_outlook(origin,2)->event_id;
        CHECK(w.colony_personal_outlook(origin,2)->considering_departure);
        CHECK(!w.colony_personal_outlook(origin,3)->considering_departure);
        auto retry=w.supervise_voluntary_migration(pulse);
        CHECK(retry.status==
              PersistentWorld::HostVoluntaryDepartureStatus::ExistingAdmission);
        CHECK(retry.observed==0);
        CHECK(w.world().has_command_id(101));
        // The host has only QUEUED boarding: the same authoritative next
        // native world tick still must execute the physical embark command.
        CHECK(w.world().npc_records().at(2).location==origin);
        CHECK(w.world().colony_ship(origin)->crew==6);
        executed(w,101);
        CHECK(w.world().npc_records().at(2).transport_id==900);
        CHECK(w.world().npc_records().at(2).location==ferry);
        CHECK(!w.verified_voluntary_immigration(8701));
        CHECK(!w.submit_host_passenger_action(command(
            w,102,Simulation::kServerAuthority,origin,dest,
            Action::HostSettleMigrant,true)));
        CHECK(w.submit(command(w,103,7,ferry,gate,Action::RequestJump)));
        executed(w,103);
        w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
        CHECK(w.world().find(ferry)->system_id==2);
        CHECK(w.submit_host_passenger_action(command(
            w,104,Simulation::kServerAuthority,ferry,dest,
            Action::HostDisembarkPassengers,true)));
        executed(w,104);
        CHECK(w.world().voyage_status(900)==2);
        CHECK(w.world().npc_records().at(2).location==dest);
        CHECK(w.world().npc_records().at(2).home_ship==origin);
        CHECK(family.current_residence(2)==origin);
        CHECK(w.submit_host_passenger_action(command(
            w,105,Simulation::kServerAuthority,origin,dest,
            Action::HostSettleMigrant,true)));
        SocietyMigrationPlan plan{};
        plan.expected_world_generation=w.generation();
        plan.command_id=105;plan.npc_id=2;plan.transport_id=900;
        plan.origin=origin;plan.destination=dest;
        plan.expected_region_revision=1;plan.expected_owner_generation=1;
        plan.family_scope="voluntary";plan.region_scope="voluntary-region";
        plan.origin_colony="origin";plan.destination_colony="destination";
        plan.family_seed=8701;
        CHECK(!w.verified_voluntary_immigration(8701));
        refused([&]{w.step();}); // can't bypass ART02/ECON03
        refused([&]{w.step_with_atomic_migration(plan);}); // can't bypass voluntary receipt
        refused([&]{w.step_with_voluntary_immigration(9999,plan);});
        auto wrong=plan;wrong.npc_id=3;
        refused([&]{w.step_with_voluntary_immigration(8701,wrong);});
        const auto before=w.world().canonical_state();
        refused([&]{w.step_with_voluntary_immigration(
            8701,plan,FailPoint::AfterSnapshotWrite);});
        CHECK(w.world().canonical_state()==before);
        CHECK(family.current_residence(2)==origin);
        CHECK(region.load()->revision==1);
        CHECK(!w.verified_voluntary_immigration(8701));
        w.step_with_voluntary_immigration(8701,plan);
        CHECK(w.verified_voluntary_immigration(8701));
        CHECK(w.world().npc_records().at(2).id==2);
        CHECK(w.world().npc_records().at(2).home_ship==dest);
        CHECK(w.world().npc_records().at(2).location==dest);
        CHECK(w.world().colony_ship(origin)->crew==5);
        CHECK(w.world().colony_ship(dest)->crew==7);
        CHECK(family.current_residence(2)==dest);
        CHECK(family.family_census(origin).current_population==5);
        CHECK(family.family_census(dest).current_population==7);
        CHECK(region.load()->revision==2);
        CHECK(region.load()->scheduler.colony("origin").population==5);
        CHECK(region.load()->scheduler.colony("destination").population==7);
        CHECK(w.colony_governance(origin).leader_id==1);
        // Immigration does not erase a resident's life history. The new
        // stable colony relieves the old-home escape intention, but the
        // individual's underlying discontent fades by normal observations.
        const auto newcomer=w.record_personal_outlook(8615,dest,2);
        CHECK(newcomer.prior_event_id==last_origin_diary);
        CHECK(newcomer.leader_id==13);
        CHECK(newcomer.discontent<second.discontent);
        CHECK(!newcomer.considering_departure);
        CHECK(w.colony_personal_outlook(origin,2)->event_id==last_origin_diary);
        CHECK(w.colony_personal_outlook(dest,2)->event_id==8615);
        CHECK(w.world().npc_casualties().empty());
        refused([&]{w.step_with_voluntary_immigration(8701,plan);});
    }
    {
        PersistentWorld w(db,Simulation(8702));
        CHECK(w.verified_voluntary_immigration(8701));
        CHECK(w.world().npc_records().at(2).home_ship==dest);
        CHECK(w.world().colony_ship(origin)->crew==5);
        CHECK(w.world().colony_ship(dest)->crew==7);
        CHECK(w.colony_governance(origin).leader_id==1);
        CHECK(w.colony_personal_outlook(dest,2)->prior_event_id==last_origin_diary);
        CHECK(!w.colony_personal_outlook(dest,2)->considering_departure);
        CHECK(w.world().npc_casualties().empty());
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld w(recovered,Simulation(8703));
        CHECK(w.verified_voluntary_immigration(8701));
        CHECK(w.world().npc_records().at(2).home_ship==dest);
        CHECK(w.colony_personal_outlook(dest,2)->event_id==8615);
        NpcAppearanceStore family(recovered,"voluntary",8701,[&](auto id){
            return w.world().npc_records().count(id)!=0;});
        CHECK(family.current_residence(2)==dest);
    }
    PersistentWorld::recover_copy(db,spoof);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(spoof.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_voluntary_immigration_no_update");
        sql(raw,"UPDATE sg_colony_voluntary_immigration SET receiving_owner='999' "
            "WHERE event_id=8701");
        sql(raw,"CREATE TRIGGER sg_voluntary_immigration_no_update "
            "BEFORE UPDATE ON sg_colony_voluntary_immigration "
            "BEGIN SELECT RAISE(ABORT,'immutable receiving colony admission'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld forged(spoof,Simulation(8704));});
    }
    PersistentWorld::recover_copy(db,bad_receipt);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(bad_receipt.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_voluntary_immigration_settled_no_update");
        sql(raw,"UPDATE sg_colony_voluntary_immigration_settled "
            "SET migration_command='999' WHERE admission_event=8701");
        sql(raw,"CREATE TRIGGER sg_voluntary_immigration_settled_no_update "
            "BEFORE UPDATE ON sg_colony_voluntary_immigration_settled "
            "BEGIN SELECT RAISE(ABORT,'immutable completed immigration'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld forged(bad_receipt,Simulation(8705));});
    }
    // A forged permit still naming a REAL eligible alternate ferry must be
    // rejected if that ferry was not the one completing the canonical voyage.
    PersistentWorld::recover_copy(db,false_ferry);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(false_ferry.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_voluntary_immigration_no_update");
        sql(raw,"UPDATE sg_colony_voluntary_immigration SET ferry='"+
            std::to_string(alternate_ferry)+"' WHERE event_id=8701");
        sql(raw,"CREATE TRIGGER sg_voluntary_immigration_no_update "
            "BEFORE UPDATE ON sg_colony_voluntary_immigration "
            "BEGIN SELECT RAISE(ABORT,'immutable receiving colony admission'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld forged(false_ferry,Simulation(8706));});
    }
    // An attacker cannot turn a v88 personal-history admission into an
    // inherited v87 scarcity-only permit by erasing the decision reference.
    PersistentWorld::recover_copy(db,stripped_reason);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(stripped_reason.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_voluntary_immigration_no_update");
        sql(raw,"UPDATE sg_colony_voluntary_immigration "
            "SET personal_intent_event=0 WHERE event_id=8701");
        sql(raw,"CREATE TRIGGER sg_voluntary_immigration_no_update "
            "BEFORE UPDATE ON sg_colony_voluntary_immigration "
            "BEGIN SELECT RAISE(ABORT,'immutable receiving colony admission'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld forged(stripped_reason,Simulation(8707));});
    }
    clean(db);clean(recovered);clean(spoof);clean(bad_receipt);
    clean(false_ferry);clean(stripped_reason);
    std::cout<<"PASS EXIT01D: autonomously selected individual based on timed personal appraisal, real cross-owner solo immigration, actual "
        "gate ferry boarding/arrival, source pressure, native immutable ART02/ECON03 "
        "world+census+region conservation, fault rollback, restart, recover_copy "
        "and admission/settlement forgery refusal; no fake population\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"EXIT01B failure: "<<e.what()<<'\n';return 1;}
}
