#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace cs=galaxy::colony_safety;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04B check failed: ")+ #c+" at "+std::to_string(__LINE__));}while(false)
namespace{
Simulation genesis(){
    Simulation w(10466); const auto ship=w.add_colony_ship(7,{0,0},100,4);
    CHECK(ship==1);w.provision_colony_genesis(ship,100,100,100,200,500);
    return w;
}
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string why=err?err:"sqlite error";sqlite3_free(err);throw std::runtime_error(why);
    }
}
std::int64_t row(sqlite3* db,const char* sql){
    sqlite3_stmt* stmt=nullptr;
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare failed");
    const auto rc=sqlite3_step(stmt);
    const auto n=rc==SQLITE_ROW?sqlite3_column_int64(stmt,0):-1;
    sqlite3_finalize(stmt);
    CHECK(n>=0);return n;
}
}
int main(){
try{
    const auto file=std::filesystem::temp_directory_path()/
      ("sg_auto04b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    const auto path=file.string();
    std::int64_t baseline_ops=0;
    {
        PersistentWorld w(path,genesis());
        sqlite3* initial=nullptr;CHECK(sqlite3_open(path.c_str(),&initial)==SQLITE_OK);
        baseline_ops=row(initial,"SELECT COUNT(*) FROM operation_log");
        CHECK(sqlite3_close(initial)==SQLITE_OK);
        CHECK(w.colony_safety_evidence_count()==0);
        const auto owner=w.record_colony_safety_preflight(1,1,7,cs::Order::SetLocalProduction);
        CHECK(owner.allowed && !owner.deliberate_hazard_attempt);
        CHECK(w.world().life_support(1)->food_kg==100);
        CHECK(w.colony_safety_evidence_count()==1);
        const auto other=w.record_colony_safety_preflight(1,1,8,cs::Order::SetLocalProduction);
        CHECK(!other.allowed && !other.deliberate_hazard_attempt);
        for(const auto bad : {cs::Order::SealOccupiedExit,cs::Order::DisableDistress,
                              cs::Order::DumpOccupiedAtmosphere,cs::Order::PreventEmergencyRepair}){
            const auto result=w.record_colony_safety_preflight(1,1,7,bad);
            CHECK(!result.allowed && result.deliberate_hazard_attempt);
        }
        CHECK(w.colony_safety_evidence_count()==6);
        CHECK(w.world().colony_ship(1)->crew==4);
        sqlite3* probe=nullptr;CHECK(sqlite3_open(path.c_str(),&probe)==SQLITE_OK);
        CHECK(row(probe,"SELECT COUNT(*) FROM sg_colony_safety_evidence WHERE attempted_hazard=1")==4);
        CHECK(row(probe,"SELECT event_count FROM sg_colony_safety_meta")==6);
        CHECK(row(probe,"SELECT COUNT(*) FROM operation_log")==baseline_ops); // evidence not fake world commands
        exec(probe,"CREATE TRIGGER sg_safety_injected_failure BEFORE INSERT ON sg_colony_safety_evidence "
                   "BEGIN SELECT RAISE(ABORT,'injected safety failure');END;");
        bool rejected=false;
        try{(void)w.record_colony_safety_preflight(1,1,7,cs::Order::SealOccupiedExit);}
        catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected && w.colony_safety_evidence_count()==6);
        CHECK(row(probe,"SELECT event_count FROM sg_colony_safety_meta")==6);
        exec(probe,"DROP TRIGGER sg_safety_injected_failure");
        CHECK(sqlite3_close(probe)==SQLITE_OK);
        w.step(); // ordinary world snapshot and generation proceed correctly
        CHECK(w.colony_safety_evidence_count()==6);
        const auto repeated=w.record_colony_safety_preflight(1,1,7,cs::Order::SealOccupiedExit);
        CHECK(repeated.deliberate_hazard_attempt && w.colony_safety_evidence_count()==7);
    }
    {
        PersistentWorld recovered(path,genesis());
        CHECK(recovered.colony_safety_evidence_count()==7);
        CHECK(recovered.world().colony_ship(1)->crew==4);
        CHECK(recovered.world().life_support(1)->food_kg==100);
    }
    // SQLite trigger protects ordinary row deletion; deliberate tamper that
    // removes trigger and a row must be detected by the durable count anchor.
    sqlite3* db=nullptr;CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    exec(db,"DROP TRIGGER sg_safety_no_delete");
    exec(db,"DELETE FROM sg_colony_safety_evidence WHERE event_id=7");
    CHECK(row(db,"SELECT COUNT(*) FROM sg_colony_safety_evidence")==6);
    CHECK(sqlite3_close(db)==SQLITE_OK);
    bool refused=false;
    try{PersistentWorld corrupt(path,genesis());(void)corrupt;}
    catch(const std::runtime_error&){refused=true;}
    CHECK(refused);
    std::filesystem::remove(file);std::filesystem::remove(path+"-wal");std::filesystem::remove(path+"-shm");
    std::cout<<"PASS AUTO04B canonical SQLite owner, denied occupied safety commands, atomic failure rollback, restart and deletion detection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
