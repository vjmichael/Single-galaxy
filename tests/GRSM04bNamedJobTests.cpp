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
#define CHECK(x) do{if(!(x))throw std::runtime_error("GRSM04b.B1 line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp{
    fs::path dir;
    Temp():dir(fs::temp_directory_path()/("grsm04bB_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(dir);}
    ~Temp(){std::error_code ec;fs::remove_all(dir,ec);}
};
void sql(const fs::path& p,const std::string& script){
    sqlite3* db=nullptr;CHECK(sqlite3_open(p.string().c_str(),&db)==SQLITE_OK);
    char* err=nullptr;const auto rc=sqlite3_exec(db,script.c_str(),nullptr,nullptr,&err);
    const std::string msg(err?err:"");sqlite3_free(err);sqlite3_close(db);
    if(rc!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+msg);
}
bool rejects(const fs::path& file){
    try{PersistentWorld w(file.string(),Simulation(900));return false;}
    catch(const std::runtime_error&){return true;}
}
void exercised_named_execution(const fs::path& folder){
    const auto file=folder/"normal.sqlite",copy=folder/"recovered.sqlite";
    Simulation s(3);
    const auto ship=s.add_colony_ship(7,{0,0},50,5);
    s.add_colony_ship(8,{40000,0},50,3);
    const auto rock=s.add_asteroid({1000,0},70);
    s.enable_individual_workforce();
    {
        PersistentWorld w(file.string(),std::move(s));
        CHECK(!w.submit_npc_mining_work(6,ship,rock,5,101)); // other ship's crew
        CHECK(!w.submit_npc_mining_work(999,ship,rock,5,101));
        CHECK(!w.submit_npc_mining_work(1,ship,rock,0,101));
        CHECK(!w.submit_npc_mining_work(1,ship,rock,5,0));
        CHECK(!w.submit_npc_mining_work(1,ship,rock,5000,101));
        CHECK(w.recent_npc_mining_assignments().empty());
        const auto before=w.world().canonical_state();
        try{w.submit_npc_mining_work(1,ship,rock,5,99,FailPoint::AfterReceiptWrite);CHECK(false);}
        catch(const std::runtime_error&){}
        CHECK(before==w.world().canonical_state());
        CHECK(w.recent_npc_mining_assignments().empty());
        CHECK(w.submit_npc_mining_work(1,ship,rock,6,101));
        CHECK(!w.submit_npc_mining_work(2,ship,rock,6,101));
        CHECK(w.recent_npc_mining_assignments().size()==1);
        CHECK(w.recent_npc_mining_executions().empty());
        CHECK(!w.verified_npc_mining_execution(101,1));
        w.step();
        CHECK(w.world().find(ship)->cargo==6);
        CHECK(w.world().find(rock)->ore==64);
        const auto e=w.recent_npc_mining_executions();
        CHECK(e.size()==1&&e[0].npc_id==1&&e[0].command_id==101&&e[0].amount==6);
        CHECK(e[0].incident_id.rfind("cargo:",0)==0);
        CHECK(w.verified_npc_mining_execution(101,1));
        CHECK(!w.verified_npc_mining_execution(101,2));
        CHECK(!w.is_verified_incident_role(e[0].incident_id,1,VerifiedNpcRole::Victim));
        // An ordinary ship command remains an ordinary ship command. Merely
        // being on its roster grants NONE of the crew executor status.
        Command plain{};plain.id=102;plain.sequence=102;plain.execute_tick=w.world().tick()+1;
        plain.issuer=7;plain.actor=ship;plain.target=rock;plain.action=Action::Mine;plain.amount=4;
        CHECK(w.submit(plain));w.step();
        CHECK(w.world().find(ship)->cargo==10);
        CHECK(w.recent_npc_mining_executions().size()==1);
        CHECK(!w.verified_npc_mining_execution(102,1));
        // A pending named job with a bogus rock is not false proof of work.
        CHECK(w.submit_npc_mining_work(2,ship,999999,5,103));w.step();
        CHECK(w.recent_npc_mining_assignments().size()==2);
        CHECK(w.recent_npc_mining_executions().size()==1);
        CHECK(!w.verified_npc_mining_execution(103,2));
        CHECK(w.world().total_ore()==70);
        w.steps(4);
        PersistentWorld::recover_copy(file.string(),copy.string());
    }
    for(const auto& p:{file,copy}){
        PersistentWorld w(p.string(),Simulation(999));
        CHECK(w.verified_npc_mining_execution(101,1));
        CHECK(!w.verified_npc_mining_execution(103,2));
        CHECK(w.recent_npc_mining_assignments().size()==2);
        CHECK(w.recent_npc_mining_executions().size()==1);
        CHECK(w.world().find(ship)->cargo==10);
    }
    const std::vector<std::pair<std::string,std::string>> attacks{
        {"forged_executor","DROP TRIGGER npc_mining_execution_no_update; UPDATE npc_mining_executions SET npc_id='2' WHERE id=1;"},
        {"wrong_result","DROP TRIGGER npc_mining_execution_no_update; UPDATE npc_mining_executions SET amount=9 WHERE id=1;"},
        {"false_incident","PRAGMA foreign_keys=OFF; DROP TRIGGER npc_mining_execution_no_update; UPDATE npc_mining_executions SET incident_id='cargo:9000000' WHERE id=1;"},
        {"false_job","DROP TRIGGER npc_mining_assignment_no_update; UPDATE npc_mining_assignments SET npc_id='9' WHERE id=1;"},
        {"deleted_execution","DROP TRIGGER npc_mining_execution_no_delete; DELETE FROM npc_mining_executions WHERE id=1;"},
        {"bad_execution_tip","UPDATE npc_mining_execution_meta SET tip='corrupt' WHERE singleton=1;"},
        {"bad_assignment_count","UPDATE npc_mining_assignment_meta SET event_count=50 WHERE singleton=1;"}
    };
    for(const auto& [name,attack]:attacks){
        const auto forged=folder/(name+".sqlite");fs::copy_file(file,forged);
        sql(forged,attack);CHECK(rejects(forged));
    }
}
void clean_v15_migration(const fs::path& folder){
    const auto file=folder/"migration.sqlite";
    Simulation s(12);const auto ship=s.add_colony_ship(7,{0,0},40,3);
    const auto rock=s.add_asteroid({150,0},60);s.enable_individual_workforce();
    {
        PersistentWorld w(file.string(),std::move(s));
        Command mine{};mine.id=301;mine.sequence=301;mine.execute_tick=1;
        mine.issuer=7;mine.actor=ship;mine.target=rock;mine.action=Action::Mine;mine.amount=2;
        CHECK(w.submit(mine));w.step();
        CHECK(w.recent_npc_mining_executions().empty());
    }
    sql(file,"DROP TABLE npc_fabrication_executions; DROP TABLE npc_fabrication_execution_meta; DROP TABLE npc_fabrication_assignments; DROP TABLE npc_fabrication_assignment_meta; DROP TABLE npc_mining_executions; DROP TABLE npc_mining_execution_meta; "
             "DROP TABLE npc_mining_assignments; DROP TABLE npc_mining_assignment_meta; DROP TABLE IF EXISTS sg_native_npc_commands; PRAGMA user_version=15;");
    {
        PersistentWorld w(file.string(),Simulation(2));
        CHECK(w.recent_npc_mining_assignments().empty());
        CHECK(w.recent_npc_mining_executions().empty());
        CHECK(!w.verified_npc_mining_execution(301,1));
        CHECK(w.submit_npc_mining_work(2,ship,rock,4,302));w.step();
        CHECK(w.verified_npc_mining_execution(302,2));
        CHECK(w.world().find(ship)->cargo==6);
    }
    PersistentWorld w(file.string(),Simulation(3));
    CHECK(w.recent_npc_mining_executions().size()==1);
    CHECK(w.recent_npc_mining_assignments().size()==1);
}
int main(){Temp tmp;try{
    exercised_named_execution(tmp.dir);
    std::cout<<"PASS named-work mining with real ore effects, no roster blame, recovery/tamper\n";
    clean_v15_migration(tmp.dir);
    std::cout<<"PASS v15->v16 migration invents no old executor and preserves new work\n";
    std::cout<<"GRSM-04b.B1: 2/2 scenarios passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-04b.B1 FAIL: "<<e.what()<<'\n';return 1;}}
