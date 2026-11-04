#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafetyJournal.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D2 failed: ")+ #c+" at "+std::to_string(__LINE__));}while(false)
namespace {
Simulation genesis(){
    Simulation w(104692);
    const auto ship=w.add_colony_ship(7,{0,0},100,4);
    CHECK(ship==1);
    w.provision_colony_genesis(ship,100,100,100,200,500);
    return w;
}
void sql(sqlite3* db,const char* statement){
    char* error=nullptr;
    if(sqlite3_exec(db,statement,nullptr,nullptr,&error)!=SQLITE_OK){
        std::string why=error?error:"SQL error";
        sqlite3_free(error);throw std::runtime_error(why);
    }
}
std::uint64_t scalar(sqlite3* db,const char* statement){
    sqlite3_stmt* q=nullptr;
    CHECK(sqlite3_prepare_v2(db,statement,-1,&q,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(q)==SQLITE_ROW);
    const auto n=sqlite3_column_int64(q,0);
    CHECK(sqlite3_finalize(q)==SQLITE_OK);
    CHECK(n>=0);return static_cast<std::uint64_t>(n);
}
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("sg_auto04d2_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    std::uint64_t committed_generation=0;
    {
        PersistentWorld w(path,genesis());
        CHECK(w.colony_response_event_count()==0);
        CHECK(w.world().tick()==0);
        w.steps(21*20);
        CHECK(w.world().life_support(1)->food_kg==16);
        CHECK(w.colony_response_event_count()==1);
        CHECK(w.world().colony_ship(1)->crew==4);
        w.steps(3*20);
        CHECK(w.world().life_support(1)->food_kg==4);
        CHECK(w.colony_response_event_count()==2);
        w.steps(20);
        CHECK(w.world().life_support(1)->food_kg==0);
        CHECK(w.colony_response_event_count()==3);
        CHECK(w.world().colony_ship(1)->crew==4);
        committed_generation=w.generation();
        const auto tick=w.world().tick();
        bool failed=false;
        try{w.step(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){failed=true;}
        CHECK(failed);
        CHECK(w.generation()==committed_generation);
        CHECK(w.world().tick()==tick);
        CHECK(w.colony_response_event_count()==3);
    }
    {
        PersistentWorld resumed(path,genesis());
        CHECK(resumed.generation()==committed_generation);
        CHECK(resumed.world().tick()==500);
        CHECK(resumed.colony_response_event_count()==3);
        resumed.step(); // ongoing emergency does NOT duplicate notices
        CHECK(resumed.colony_response_event_count()==3);
        CHECK(resumed.world().colony_ship(1)->crew==4);
    }
    sqlite3* db=nullptr;
    CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    CHECK(scalar(db,"SELECT COUNT(*) FROM sg_colony_response_events")==3);
    CHECK(scalar(db,"SELECT COUNT(*) FROM sg_colony_response_events WHERE old_risk!=new_risk")==3);
    CHECK(scalar(db,"SELECT event_count FROM sg_colony_response_meta")==3);
    CHECK(scalar(db,"SELECT COUNT(DISTINCT generation) FROM sg_colony_response_events")==3);
    CHECK(scalar(db,"SELECT COUNT(*) FROM sg_colony_response_sites")==1);
    // Inject ordinary mutation attempt. Immutable event history must reject it.
    bool protected_row=false;
    try{sql(db,"UPDATE sg_colony_response_events SET population=999 WHERE id=1");}
    catch(const std::runtime_error&){protected_row=true;}
    CHECK(protected_row);
    // Simulate privileged tamper: removal of immutable trigger and one record.
    // Restart must detect mismatch against count + chained tip.
    sql(db,"DROP TRIGGER sg_response_events_immutable_delete");
    sql(db,"DELETE FROM sg_colony_response_events WHERE id=3");
    CHECK(sqlite3_close(db)==SQLITE_OK);
    bool refused=false;
    try{PersistentWorld corrupt(path,genesis());(void)corrupt;}
    catch(const std::runtime_error&){refused=true;}
    CHECK(refused);
    std::filesystem::remove(path);
    std::filesystem::remove(path+"-wal");
    std::filesystem::remove(path+"-shm");
    std::cout<<"PASS AUTO04D2 durable thresholds, no duplicated notices, rollback, restart and tamper refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
