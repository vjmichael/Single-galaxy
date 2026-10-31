#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM03c2 line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp{
    fs::path root;
    Temp():root(fs::temp_directory_path()/("sg_encounters_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(root);}
    ~Temp(){std::error_code e;fs::remove_all(root,e);}
};
static void sql(const fs::path& db,const std::string& statement){
    sqlite3* raw=nullptr;CHECK(sqlite3_open(db.string().c_str(),&raw)==SQLITE_OK);
    char* err=nullptr;const auto rc=sqlite3_exec(raw,statement.c_str(),nullptr,nullptr,&err);
    const std::string error(err?err:"");sqlite3_free(err);sqlite3_close(raw);
    if(rc!=SQLITE_OK)throw std::runtime_error("SQL failed: "+error);
}
static bool refused(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(99));return false;}
    catch(const std::exception&){return true;}
}
static void bounded_scheduling(const fs::path& root){
    const auto db=root/"contacts.sqlite",copy=root/"copy.sqlite";
    Simulation sim(37);
    const auto home=sim.add_colony_ship(3,{0,0},100,4);
    sim.add_colony_ship(7,{19000,0},100,4);
    sim.enable_individual_workforce();
    std::uint64_t first=0;
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.world().npc_records().size()==8);
        CHECK(w.recent_social_encounters().empty());
        CHECK(w.recent_knowledge().empty());CHECK(w.recent_social_ties().empty());
        for(const auto [scan,contact]:{std::pair<std::size_t,std::size_t>{0,1},{65,1},{1,0},{1,9}}){
            bool rejected=false;try{(void)w.schedule_social_encounters(scan,contact);}catch(const std::invalid_argument&){rejected=true;}
            CHECK(rejected);
        }
        CHECK(w.schedule_social_encounters(1,1)==1);
        const auto first_record=w.recent_social_encounters().front();
        CHECK(first_record.npc_low<first_record.npc_high);
        CHECK(first_record.ordinal==1);first=first_record.id;
        CHECK(w.schedule_social_encounters(1,1)==1);
        CHECK(w.recent_social_encounters().size()==2);
        // Exhaust every pair in the small roster at most once this tick.
        for(int i=0;i<5;++i)CHECK(w.schedule_social_encounters(64,8)<=8);
        const auto before=w.recent_social_encounters();
        CHECK(before.size()==12); // 4 choose 2 in each of two colonies
        for(const auto& c:before)CHECK((c.npc_low>=1 && c.npc_high<=4) ||
                                    (c.npc_low>=5 && c.npc_high<=8));
        CHECK(w.schedule_social_encounters(64,8)==0); // cooldown, not more gossip
        CHECK(w.recent_social_encounters().size()==12);
        CHECK(w.recent_social_ties().empty());CHECK(w.recent_knowledge().empty());
        CHECK(w.world().colony_ship(home)->crew==4);
        w.steps(7);
        CHECK(w.schedule_social_encounters(64,8)==0);
        w.step(); // minimum eight world ticks between repeat meetings
        std::size_t new_meetings=0;
        for(int i=0;i<5;++i)new_meetings+=w.schedule_social_encounters(64,8);
        CHECK(new_meetings==12);
        const auto again=w.recent_social_encounters();
        CHECK(again.size()==24);
        for(std::size_t i=0;i<12;++i)CHECK(again[i].ordinal==2);
        CHECK(w.world().colony_ship(home)->crew==4);
        CHECK(w.recent_physical_incidents().empty());
        CHECK(w.recent_knowledge().empty());
        CHECK(w.recent_social_groups().empty());
        CHECK(w.recent_social_ties().empty());
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& path:{db,copy}){
        PersistentWorld w(path.string(),Simulation(90));
        CHECK(w.recent_social_encounters().size()==24);
        CHECK(w.recent_social_encounters().front().ordinal==2);
        CHECK(w.world().colony_ship(home)->crew==4);
        CHECK(w.schedule_social_encounters(64,8)==0);
        CHECK(w.recent_social_ties().empty());
    }
    for(const auto& [label,attack]:std::vector<std::pair<std::string,std::string>>{
        {"false_npc","DROP TRIGGER social_encounters_no_update; UPDATE social_encounters SET npc_high='999' WHERE id=1;"},
        {"false_slot","DROP TRIGGER social_encounters_no_update; UPDATE social_encounters SET slot=20 WHERE id=1;"},
        {"delete_leaf","DROP TRIGGER social_encounters_no_delete; DELETE FROM social_encounters WHERE id=24;"},
        {"delete_cycle","DROP TRIGGER encounter_scans_no_delete; PRAGMA foreign_keys=OFF; DELETE FROM encounter_scans WHERE id=(SELECT MAX(id) FROM encounter_scans);"},
        {"counter","UPDATE encounter_meta SET contact_count=25 WHERE singleton=1;"},
        {"cursor","UPDATE encounter_meta SET cursor=5 WHERE singleton=1;"}
    }){
        const auto bad=root/(label+".sqlite");fs::copy_file(db,bad);
        sql(bad,attack);CHECK(refused(bad));
    }
    (void)first;
}
static void migrate_v10_without_fake_encounters(const fs::path& root){
    const auto db=root/"v10.sqlite";
    Simulation sim(38);sim.add_colony_ship(7,{0,0},100,4);sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.found_social_group("Crew committee",{2,3})==1);
    }
    // Recreate genuine schema-v10 shape rather than lying with the version flag.
    sql(db,"DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; "
           "PRAGMA user_version=10;");
    {
        PersistentWorld w(db.string(),Simulation(40));
        CHECK(w.recent_social_encounters().empty());
        CHECK(w.recent_social_groups().size()==1);
        CHECK(w.schedule_social_encounters(1,1)==1);
    }
    PersistentWorld restored(db.string(),Simulation(41));
    CHECK(restored.recent_social_encounters().size()==1);
}
int main(){Temp t;try{
    bounded_scheduling(t.root);
    std::cout<<"PASS bounded deterministic contacts, cooldown, recovery, tamper, and physical isolation\n";
    migrate_v10_without_fake_encounters(t.root);
    std::cout<<"PASS non-inventing schema v10 -> v11 migration\n";
    std::cout<<"GRSM-03c.2 2/2 scenario gates passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM03c.2 FAIL: "<<e.what()<<'\n';return 1;}}
