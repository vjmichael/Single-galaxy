#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM04a line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp {
    fs::path root;
    Temp():root(fs::temp_directory_path()/("grsm04a_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(root);}
    ~Temp(){std::error_code e;fs::remove_all(root,e);}
};
static void sql(const fs::path& file,const std::string& stmt){
    sqlite3* db=nullptr;CHECK(sqlite3_open(file.string().c_str(),&db)==SQLITE_OK);
    char* error=nullptr;const int rc=sqlite3_exec(db,stmt.c_str(),nullptr,nullptr,&error);
    const auto why=std::string(error?error:"");sqlite3_free(error);sqlite3_close(db);
    if(rc!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+why);
}
static bool rejects(const fs::path& file){
    try{PersistentWorld w(file.string(),Simulation(99));return false;}catch(const std::runtime_error&){return true;}
}
static void contextual_witnessed_physical_incident(const fs::path& root){
    const auto db=root/"appraisal.sqlite", copy=root/"recovery.sqlite";
    Simulation sim(22);
    const auto home=sim.add_colony_ship(7,{0,0},100,8);
    sim.add_colony_ship(8,{25000,0},100,4);
    const auto asteroid=sim.add_asteroid({1000,0},90);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        const auto miners=w.found_social_group("Miners",{1,2});
        const auto relief=w.found_social_group("Relief Volunteers",{1,3});
        const auto remote=w.found_social_group("Remote Inspectors",{9,10});
        CHECK(miners==1&&relief==2&&remote==3);
        CHECK(w.recent_physical_appraisals().empty());
        Command mine{};mine.id=50;mine.sequence=50;mine.execute_tick=w.world().tick()+1;
        mine.issuer=7;mine.actor=home;mine.target=asteroid;mine.action=Action::Mine;mine.amount=5;
        CHECK(w.submit(mine));w.step();
        std::string incident;
        for(const auto& fact:w.recent_physical_incidents())if(fact.kind==PhysicalKind::CargoMovement){
            incident=fact.incident_id;CHECK(fact.amount==5);break;
        }
        CHECK(!incident.empty());
        CHECK(w.world().find(home)->cargo==5);
        CHECK(!w.record_local_observation(incident,9)); // remote cannot see cargo
        CHECK(w.record_local_observation(incident,1));
        CHECK(w.record_local_observation(incident,3));
        const auto claims=w.recent_knowledge();CHECK(claims.size()==2);
        const auto o1=claims[1].id,o3=claims[0].id;
        CHECK(claims[1].recipient_npc==1&&claims[0].recipient_npc==3);
        CHECK(!w.appraise_witnessed_incident(0,miners,PhysicalAppraisalStance::Beneficial,90));
        CHECK(!w.appraise_witnessed_incident(777,miners,PhysicalAppraisalStance::Beneficial,90));
        CHECK(!w.appraise_witnessed_incident(o1,remote,PhysicalAppraisalStance::Beneficial,90));
        CHECK(!w.appraise_witnessed_incident(o3,miners,PhysicalAppraisalStance::Damaging,80));
        CHECK(!w.appraise_witnessed_incident(o1,miners,PhysicalAppraisalStance::Beneficial,101));
        CHECK(!w.appraise_witnessed_incident(o1,miners,static_cast<PhysicalAppraisalStance>(99),40));
        // The same actual mined material can serve opposite group interests:
        // a miner celebrates supplies, a relief volunteer worries about impact.
        CHECK(w.appraise_witnessed_incident(o1,miners,PhysicalAppraisalStance::Beneficial,90));
        CHECK(!w.appraise_witnessed_incident(o1,miners,PhysicalAppraisalStance::Damaging,90));
        CHECK(w.appraise_witnessed_incident(o1,relief,PhysicalAppraisalStance::Damaging,65));
        CHECK(w.appraise_witnessed_incident(o3,0,PhysicalAppraisalStance::Uncertain,35));
        CHECK(w.recent_physical_appraisals().size()==3);
        const auto records=w.recent_physical_appraisals();
        CHECK(records[0].context_group==0&&records[0].stance==PhysicalAppraisalStance::Uncertain);
        CHECK(records[1].context_group==relief&&records[1].stance==PhysicalAppraisalStance::Damaging);
        CHECK(records[2].context_group==miners&&records[2].stance==PhysicalAppraisalStance::Beneficial);
        for(const auto& e:records)CHECK(e.incident_id==incident);
        CHECK(w.transmit_claim(o1,2,ClaimFidelity::Faithful,10,40,"Ore was mined"));
        CHECK(!w.appraise_witnessed_incident(w.recent_knowledge().front().id,miners,
                                            PhysicalAppraisalStance::Beneficial,80));
        CHECK(w.recent_group_attributions().empty());
        CHECK(w.recent_social_interactions().empty());
        CHECK(w.recent_social_ties().empty());
        CHECK(w.world().find(home)->cargo==5);
        w.steps(6);
        CHECK(!w.appraise_witnessed_incident(o1,0,PhysicalAppraisalStance::Beneficial,80));
        CHECK(w.change_group_membership(miners,1,GroupMembershipKind::Leave));
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& file:{db,copy}){
        PersistentWorld w(file.string(),Simulation(99));
        CHECK(w.recent_physical_appraisals().size()==3);
        CHECK(w.recent_physical_appraisals()[2].context_group==1);
        CHECK(w.active_social_group_members(1).size()==1);
        CHECK(w.world().find(home)->cargo==5);
        CHECK(w.recent_social_interactions().empty());
    }
    for(const auto& [name,attack]:std::vector<std::pair<std::string,std::string>>{
        {"wrong_stance","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET stance=2 WHERE id=1;"},
        {"false_observer","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET observer='9' WHERE id=1;"},
        {"fabricated_incident","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET incident_id='fake' WHERE id=1;"},
        {"foreign_claim","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET observation_id=3 WHERE id=1;"},
        {"false_epoch","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET membership_seq=999 WHERE id=1;"},
        {"changed_confidence","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET confidence=0 WHERE id=1;"},
        {"wrong_group","DROP TRIGGER physical_appraisals_no_update; UPDATE physical_appraisals SET context_group=3 WHERE id=1;"},
        {"deleted","DROP TRIGGER physical_appraisals_no_delete; DELETE FROM physical_appraisals WHERE id=3;"},
        {"broken_tip","UPDATE physical_appraisal_meta SET tip='fake' WHERE singleton=1;"},
        {"broken_count","UPDATE physical_appraisal_meta SET event_count=999 WHERE singleton=1;"}
    }){
        auto bad=root/(name+".sqlite");fs::copy_file(db,bad);
        // Writing illegal FK references requires disabling FK checks for this
        // intentionally hostile fixture. Normal API writes cannot do this.
        if(name=="fabricated_incident")sql(bad,"PRAGMA foreign_keys=OFF; "+attack);
        else sql(bad,attack);
        CHECK(rejects(bad));
    }
}
static void genuine_v13_migration(const fs::path& root){
    const auto db=root/"old_v13.sqlite";
    Simulation sim(33);
    const auto home=sim.add_colony_ship(7,{0,0},100,5);
    sim.add_asteroid({1000,0},80);sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.found_social_group("Old Crew",{1,2})==1);
        CHECK(w.submit_testing_crew_loss(home,1,201));w.step();
        CHECK(w.record_local_observation("death:201:1",2));
    }
    sql(db,"DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; PRAGMA user_version=13;");
    {
        PersistentWorld w(db.string(),Simulation(111));
        CHECK(w.recent_physical_appraisals().empty()); // old witnesses are not given invented views
        w.step(); // after the witnessed tick, the old incident cannot gain a new appraisal
        CHECK(!w.appraise_witnessed_incident(1,1,PhysicalAppraisalStance::Damaging,90));
        CHECK(w.submit_testing_crew_loss(home,1,202));w.step();
        CHECK(w.record_local_observation("death:202:2",3));
        const auto id=w.recent_knowledge().front().id;
        CHECK(w.appraise_witnessed_incident(id,0,PhysicalAppraisalStance::Damaging,100));
    }
    PersistentWorld reloaded(db.string(),Simulation(44));
    CHECK(reloaded.recent_physical_appraisals().size()==1);
    CHECK(reloaded.recent_physical_appraisals()[0].incident_id=="death:202:2");
}
int main(){Temp t;try{
    contextual_witnessed_physical_incident(t.root);
    std::cout<<"PASS physical facts -> firsthand witnessing -> contextual appraisals, recovery and tamper refusal\n";
    genuine_v13_migration(t.root);
    std::cout<<"PASS v13 -> v14 migration with no fabricated earlier attitudes\n";
    std::cout<<"GRSM-04a 2/2 scenarios passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-04a FAIL: "<<e.what()<<'\n';return 1;}
}
