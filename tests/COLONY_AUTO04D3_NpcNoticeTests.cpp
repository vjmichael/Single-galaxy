#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <iostream>
using namespace galaxy;
#define CHECK(v) do{if(!(v))throw std::runtime_error(std::string("AUTO04D3 failed: ")+ #v+" line "+std::to_string(__LINE__));}while(false)
namespace {
Simulation genesis(){
    Simulation w(104703); const auto ship=w.add_colony_ship(7,{0,0},100,4);
    CHECK(ship==1);
    w.provision_colony_genesis(ship,100,100,100,200,500);
    w.enable_individual_workforce();
    CHECK(w.npc_records().size()==4);
    return w;
}
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    const auto rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){
        std::string msg=err?err:"SQL failed";sqlite3_free(err);throw std::runtime_error(msg);
    }
}
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("sg_auto04d3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    {
        PersistentWorld w(path,genesis());
        CHECK(w.colony_notification_opportunity_count()==0);
        CHECK(w.colony_notification_acknowledgement_count()==0);
        w.steps(21*20);
        CHECK(w.colony_response_event_count()==1);
        CHECK(w.colony_notification_opportunity_count()==4);
        auto p=w.pending_colony_notifications();
        CHECK(p.size()==4);
        for(std::size_t i=0;i<p.size();++i){
            CHECK(p[i].event_id==1);
            CHECK(p[i].npc_id==i+1);
            CHECK(p[i].site==1 && p[i].risk==1);
        }
        bool forged=false;
        try{(void)w.acknowledge_colony_notification(1,999);}
        catch(const std::runtime_error&){forged=true;}
        CHECK(forged);
        CHECK(w.colony_notification_acknowledgement_count()==0);
        CHECK(w.acknowledge_colony_notification(1,1));
        CHECK(!w.acknowledge_colony_notification(1,1)); // idempotent retry
        CHECK(w.colony_notification_acknowledgement_count()==1);
        CHECK(w.pending_colony_notifications().size()==3);
        w.steps(3*20);
        CHECK(w.colony_response_event_count()==2);
        CHECK(w.colony_notification_opportunity_count()==8);
        w.steps(20);
        CHECK(w.colony_response_event_count()==3);
        CHECK(w.colony_notification_opportunity_count()==12);
        CHECK(w.pending_colony_notifications().size()==11);
        const auto gen=w.generation(),tick=w.world().tick();
        bool rollback=false;
        try{w.step(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){rollback=true;}
        CHECK(rollback);
        CHECK(w.generation()==gen && w.world().tick()==tick);
        CHECK(w.colony_notification_opportunity_count()==12);
        CHECK(w.colony_notification_acknowledgement_count()==1);
    }
    {
        PersistentWorld restored(path,genesis());
        CHECK(restored.world().tick()==500);
        CHECK(restored.colony_notification_opportunity_count()==12);
        CHECK(restored.colony_notification_acknowledgement_count()==1);
        CHECK(restored.pending_colony_notifications().size()==11);
        CHECK(restored.acknowledge_colony_notification(1,2));
        CHECK(restored.colony_notification_acknowledgement_count()==2);
        restored.step();
        CHECK(restored.colony_notification_opportunity_count()==12);
        CHECK(restored.pending_colony_notifications().size()==10);
        CHECK(restored.world().npc_records().size()==4);
    }
    sqlite3* db=nullptr;
    CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    bool immutable=false;
    try{exec(db,"DELETE FROM sg_colony_npc_opportunities WHERE event_id=1 AND npc_id=1");}
    catch(const std::runtime_error&){immutable=true;}
    CHECK(immutable);
    // A privileged tamperer bypassing a trigger still cannot reopen this save
    // without the count anchor proving a missing delivery opportunity.
    exec(db,"DROP TRIGGER sg_npc_opportunity_no_delete");
    exec(db,"DELETE FROM sg_colony_npc_opportunities WHERE event_id=2 AND npc_id=4");
    CHECK(sqlite3_close(db)==SQLITE_OK);
    bool refused=false;
    try{PersistentWorld corrupt(path,genesis());(void)corrupt;}
    catch(const std::runtime_error&){refused=true;}
    CHECK(refused);
    std::filesystem::remove(path);
    std::filesystem::remove(path+"-wal");
    std::filesystem::remove(path+"-shm");
    std::cout<<"PASS 04D3 named NPC local opportunities, independent delivery, idempotency, crash rollback and tamper refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
