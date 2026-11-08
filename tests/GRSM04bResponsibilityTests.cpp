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
#define CHECK(expr) do{if(!(expr))throw std::runtime_error(std::string("GRSM04b line ")+std::to_string(__LINE__)+": " #expr);}while(0)
struct Temp{
    fs::path root;
    Temp():root(fs::temp_directory_path()/("grsm04b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(root);}
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
void sql(const fs::path& p,const std::string& script){
    sqlite3* db=nullptr;CHECK(sqlite3_open(p.string().c_str(),&db)==SQLITE_OK);
    char* err=nullptr;const int rc=sqlite3_exec(db,script.c_str(),nullptr,nullptr,&err);
    const std::string error(err?err:"");sqlite3_free(err);sqlite3_close(db);
    if(rc!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+error);
}
bool rejects(const fs::path& file){
    try{PersistentWorld w(file.string(),Simulation(999));return false;}
    catch(const std::runtime_error&){return true;}
}
void physically_proven_roles(const fs::path& root){
    const auto db=root/"roles.sqlite",copy=root/"recovery.sqlite";
    Simulation s(30);
    const auto home=s.add_colony_ship(7,{0,0},100,8);
    s.add_colony_ship(8,{22000,0},100,4);
    const auto asteroid=s.add_asteroid({1000,0},100);
    s.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(s));
        Command mine{};mine.id=101;mine.sequence=101;mine.execute_tick=1;
        mine.issuer=7;mine.actor=home;mine.target=asteroid;mine.action=Action::Mine;mine.amount=5;
        CHECK(w.submit(mine));w.step();
        std::string cargo;
        for(const auto& p:w.recent_physical_incidents())if(p.kind==PhysicalKind::CargoMovement){cargo=p.incident_id;break;}
        CHECK(!cargo.empty());
        CHECK(!w.attest_incident_role(cargo,1,VerifiedNpcRole::Victim));
        CHECK(!w.attest_incident_role(cargo,1,static_cast<VerifiedNpcRole>(3)));
        CHECK(!w.attest_incident_role(cargo,9,VerifiedNpcRole::FirsthandObserver,999));
        CHECK(!w.record_local_observation(cargo,9));
        CHECK(w.record_local_observation(cargo,2));
        const auto eyewitness=w.recent_knowledge().front().id;
        CHECK(!w.attest_incident_role(cargo,3,VerifiedNpcRole::FirsthandObserver,eyewitness));
        CHECK(!w.attest_incident_role(cargo,2,VerifiedNpcRole::FirsthandObserver,0));
        CHECK(w.attest_incident_role(cargo,2,VerifiedNpcRole::FirsthandObserver,eyewitness));
        CHECK(!w.attest_incident_role(cargo,2,VerifiedNpcRole::FirsthandObserver,eyewitness));
        CHECK(w.is_verified_incident_role(cargo,2,VerifiedNpcRole::FirsthandObserver));
        CHECK(!w.is_verified_incident_role(cargo,2,VerifiedNpcRole::Victim));
        CHECK(w.transmit_claim(eyewitness,3,ClaimFidelity::Faithful,30,70,"Ore really moved"));
        const auto secondhand=w.recent_knowledge().front().id;
        CHECK(!w.attest_incident_role(cargo,3,VerifiedNpcRole::FirsthandObserver,secondhand));
        CHECK(!w.attest_incident_role("cargo:999999",2,VerifiedNpcRole::FirsthandObserver,eyewitness));
        CHECK(w.recent_incident_roles().size()==1);
        CHECK(w.world().find(home)->cargo==5);
        CHECK(w.submit_testing_crew_loss(home,1,201));w.step();
        const auto lost=std::string("death:201:1");
        CHECK(!w.attest_incident_role(lost,2,VerifiedNpcRole::Victim));
        CHECK(w.attest_incident_role(lost,1,VerifiedNpcRole::Victim));
        CHECK(!w.attest_incident_role(lost,1,VerifiedNpcRole::Victim));
        CHECK(!w.attest_incident_role(lost,1,VerifiedNpcRole::FirsthandObserver,eyewitness));
        CHECK(w.record_local_observation(lost,2));
        const auto death_observation=w.recent_knowledge().front().id;
        CHECK(w.attest_incident_role(lost,2,VerifiedNpcRole::FirsthandObserver,death_observation));
        CHECK(w.recent_incident_roles().size()==3);
        CHECK(w.is_verified_incident_role(lost,1,VerifiedNpcRole::Victim));
        CHECK(!w.is_verified_incident_role(lost,3,VerifiedNpcRole::Victim));
        CHECK(w.recent_physical_appraisals().empty());
        CHECK(w.recent_social_interactions().empty());
        CHECK(w.world().find(home)->cargo==5);
        w.steps(5);
        CHECK(!w.attest_incident_role(lost,3,VerifiedNpcRole::Victim));
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& file:{db,copy}){
        PersistentWorld w(file.string(),Simulation(19));
        CHECK(w.recent_incident_roles().size()==3);
        CHECK(w.is_verified_incident_role("death:201:1",1,VerifiedNpcRole::Victim));
        CHECK(w.is_verified_incident_role("death:201:1",2,VerifiedNpcRole::FirsthandObserver));
        CHECK(w.world().find(home)->cargo==5);
    }
    const std::vector<std::pair<std::string,std::string>> attacks{
        {"culprit_forged","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET role=3 WHERE id=2;"},
        {"false_victim","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET npc_id='3' WHERE id=2;"},
        {"fake_witness","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET npc_id='9' WHERE id=1;"},
        {"wrong_observation","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET observation_id=2 WHERE id=1;"},
        {"wrong_incident","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET incident_id='death:201:1' WHERE id=1;"},
        {"wrong_tick","DROP TRIGGER incident_role_no_update; UPDATE incident_role_receipts SET tick=tick+1 WHERE id=1;"},
        {"role_deleted","DROP TRIGGER incident_role_no_delete; DELETE FROM incident_role_receipts WHERE id=3;"},
        {"broken_tip","UPDATE incident_role_meta SET tip='corrupt' WHERE singleton=1;"},
        {"broken_count","UPDATE incident_role_meta SET event_count=9 WHERE singleton=1;"}
    };
    for(const auto& [name,script]:attacks){
        const auto altered=root/(name+".sqlite");fs::copy_file(db,altered);
        if(name=="culprit_forged" || name=="wrong_incident"){
            // These payloads are purposefully invalid; checks may reject them
            // at the database constraint layer before restore is reached.
            try{sql(altered,"PRAGMA foreign_keys=OFF; "+script);}catch(const std::runtime_error&){continue;}
        }else sql(altered,script);
        CHECK(rejects(altered));
    }
}
void real_v14_migration(const fs::path& root){
    const auto db=root/"old_v14.sqlite";
    Simulation s(35);const auto home=s.add_colony_ship(7,{0,0},100,5);
    s.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(s));
        CHECK(w.submit_testing_crew_loss(home,1,301));w.step();
        CHECK(w.record_local_observation("death:301:1",2));
        const auto obs=w.recent_knowledge().front().id;
        CHECK(w.appraise_witnessed_incident(obs,0,PhysicalAppraisalStance::Damaging,95));
        CHECK(w.recent_physical_appraisals().size()==1);
    }
    sql(db,"DROP TABLE npc_fabrication_executions; DROP TABLE npc_fabrication_execution_meta; DROP TABLE npc_fabrication_assignments; DROP TABLE npc_fabrication_assignment_meta; DROP TABLE npc_mining_executions; DROP TABLE npc_mining_execution_meta; DROP TABLE npc_mining_assignments; DROP TABLE npc_mining_assignment_meta; DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE IF EXISTS sg_native_npc_commands; PRAGMA user_version=14;");
    {
        PersistentWorld w(db.string(),Simulation(44));
        CHECK(w.recent_incident_roles().empty());
        CHECK(w.recent_physical_appraisals().size()==1);
        w.step(); // Can't invent attribution of old casualty after migration.
        CHECK(!w.attest_incident_role("death:301:1",1,VerifiedNpcRole::Victim));
        CHECK(w.submit_testing_crew_loss(home,1,302));w.step();
        CHECK(w.attest_incident_role("death:302:2",2,VerifiedNpcRole::Victim));
        CHECK(w.is_verified_incident_role("death:302:2",2,VerifiedNpcRole::Victim));
    }
    PersistentWorld w(db.string(),Simulation(123));
    CHECK(w.recent_incident_roles().size()==1);
    CHECK(w.recent_physical_appraisals().size()==1);
}
int main(){Temp temp;try{
    physically_proven_roles(temp.root);
    std::cout<<"PASS objective victim vs firsthand observer, no phantom perpetrator, replay/restore/corruption\n";
    real_v14_migration(temp.root);
    std::cout<<"PASS v14 -> v15 no invented prior roles, surviving appraisal history\n";
    std::cout<<"GRSM-04b.A: 2/2 scenarios passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-04b.A FAIL: "<<e.what()<<'\n';return 1;}
}
