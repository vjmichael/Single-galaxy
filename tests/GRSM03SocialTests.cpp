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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM-03 line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp{
    fs::path path;
    Temp():path(fs::temp_directory_path()/("sg_social_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(path);}
    ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
static void sql(const fs::path& db,const std::string& statement){
    sqlite3* raw=nullptr;
    CHECK(sqlite3_open(db.string().c_str(),&raw)==SQLITE_OK);
    char* error=nullptr;const auto rc=sqlite3_exec(raw,statement.c_str(),nullptr,nullptr,&error);
    const std::string reason(error?error:"");sqlite3_free(error);sqlite3_close(raw);
    if(rc!=SQLITE_OK)throw std::runtime_error("SQL failed "+reason);
}
static bool fails_to_restore(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(7));return false;}
    catch(const std::runtime_error&){return true;}
}
static void overlapping_groups_and_recovery(const fs::path& root){
    const auto db=root/"social.sqlite",copy=root/"recovery.sqlite";
    Simulation sim(31);
    const auto ship=sim.add_colony_ship(7,{0,0},100,24);
    sim.add_colony_ship(8,{25000,0},100,24);
    sim.enable_individual_workforce();
    CHECK(sim.npc_records().size()==48);
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.recent_social_groups().empty());
        CHECK(w.found_social_group("",{2,3})==0);
        CHECK(w.found_social_group("Bad duplicate",{2,2})==0);
        CHECK(w.found_social_group("Nonexistent",{2,999})==0);
        CHECK(w.found_social_group("Remote fabricated circle",{2,25})==0);
        const auto crew=w.found_social_group("Crew Council",{4,2,3});
        const auto aid=w.found_social_group("Mutual Aid",{5,2,4});
        CHECK(crew==1 && aid==2);
        const auto groups=w.recent_social_groups();
        CHECK(groups.size()==2&&groups[0].members==std::vector<std::uint64_t>({2,4,5}));
        CHECK(groups[1].members==std::vector<std::uint64_t>({2,3,4}));
        CHECK(w.submit_testing_crew_loss(ship,1,100));w.step();
        CHECK(w.world().colony_ship(ship)->crew==23);
        CHECK(w.record_local_observation("death:100:1",2));
        CHECK(w.recent_knowledge().size()==1);
        CHECK(!w.relay_group_claim(crew,1,25,ClaimFidelity::Faithful,-40,60,"Remote witness"));
        CHECK(!w.relay_group_claim(aid,1,3,ClaimFidelity::Faithful,-40,60,"Not a member"));
        CHECK(!w.relay_group_claim(crew,1,2,ClaimFidelity::Faithful,-40,60,"No self-talk"));
        CHECK(!w.relay_group_claim(999,1,3,ClaimFidelity::Faithful,-40,60,"Fictional group"));
        CHECK(!w.record_group_view(crew,1,GroupStance::Admiring,90,"No real relay yet"));
        CHECK(w.relay_group_claim(crew,1,3,ClaimFidelity::Faithful,-70,95,
                                 "Crew member lost in a real accident"));
        CHECK(w.record_group_view(crew,2,GroupStance::Condemning,90,
                                  "Our workforce mourns the fatal incident"));
        CHECK(w.recent_knowledge().size()==2); // group view did NOT broadcast knowledge
        CHECK(w.relay_group_claim(aid,1,4,ClaimFidelity::Contrary,80,30,
                                 "I was told no one actually died"));
        CHECK(w.record_group_view(aid,3,GroupStance::Admiring,40,
                                  "We doubt the accusations against the operator"));
        CHECK(w.recent_knowledge().size()==3); // NPC 5 does not learn by membership
        CHECK(w.relay_group_claim(aid,3,5,ClaimFidelity::Embellished,60,20,
                                 "I heard it was all a staged distraction"));
        CHECK(w.record_group_view(aid,4,GroupStance::Mixed,20,
                                  "There are incompatible stories in our mutual aid circle"));
        CHECK(w.recent_group_views().size()==3);
        CHECK(w.recent_group_views()[0].stance==GroupStance::Mixed);
        CHECK(w.recent_group_views()[1].stance==GroupStance::Admiring);
        CHECK(w.recent_group_views()[2].stance==GroupStance::Condemning);
        CHECK(w.recent_knowledge().size()==4);
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.world().npc_records().size()==47);
        w.step(); // a delayed retroactive "group meeting" is not permitted
        CHECK(!w.record_group_view(crew,2,GroupStance::Admiring,80,"Retroactive vote"));
        CHECK(w.recent_group_views().size()==3);
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& path:{db,copy}){
        PersistentWorld w(path.string(),Simulation(88));
        CHECK(w.recent_social_groups().size()==2);
        CHECK(w.recent_group_views().size()==3);
        CHECK(w.recent_knowledge().size()==4);
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.world().colony_ship(ship)->crew==23);
        CHECK(w.world().npc_records().size()==47);
        CHECK(w.recent_group_views()[1].group_id==2);
    }
    for(const auto& [name,attack]:std::vector<std::pair<std::string,std::string>>{
        {"group_changed","DROP TRIGGER social_groups_no_update; UPDATE social_groups SET members='2,3,99' WHERE id=1;"},
        {"view_changed","DROP TRIGGER group_views_no_update; UPDATE group_views SET confidence=99 WHERE id=1;"},
        {"relay_deleted","DROP TRIGGER group_relays_no_delete; DELETE FROM group_relays WHERE id=1;"},
        {"view_deleted","DROP TRIGGER group_views_no_delete; DELETE FROM group_views WHERE id=3;"},
        {"group_deleted","PRAGMA foreign_keys=OFF; DROP TRIGGER social_groups_no_delete; DELETE FROM social_groups WHERE id=2;"}
    }){
        const auto target=root/(name+".sqlite");fs::copy_file(db,target);
        sql(target,attack);CHECK(fails_to_restore(target));
    }
}
static void migration_and_stale_commit(const fs::path& root){
    const auto db=root/"migration.sqlite";
    Simulation sim(27);const auto ship=sim.add_colony_ship(7,{0,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.submit_testing_crew_loss(ship,1,500));w.step();
        CHECK(w.record_local_observation("death:500:1",2));
        CHECK(w.recent_knowledge().size()==1);
    }
    // Exact original v7 metadata shape: v8 tables absent, physically real NPC
    // knowledge intact. Migration must not fabricate a group or an opinion.
    sql(db,"DROP TABLE npc_fabrication_executions; DROP TABLE npc_fabrication_execution_meta; DROP TABLE npc_fabrication_assignments; DROP TABLE npc_fabrication_assignment_meta; DROP TABLE npc_mining_executions; DROP TABLE npc_mining_execution_meta; DROP TABLE npc_mining_assignments; DROP TABLE npc_mining_assignment_meta; DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; DROP TABLE group_membership_events; DROP TABLE membership_meta; "
                       "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
                       "DROP TABLE group_views; DROP TABLE group_relays; DROP TABLE social_groups; "
           "DROP TABLE social_meta; PRAGMA user_version=7;");
    {
        PersistentWorld w(db.string(),Simulation(99));
        CHECK(w.recent_social_groups().empty());CHECK(w.recent_group_views().empty());
        CHECK(w.recent_knowledge().size()==1);
        CHECK(w.found_social_group("Real survivors",{2,3})==1);
    }
    PersistentWorld w(db.string(),Simulation(999));
    CHECK(w.recent_social_groups().size()==1);
    CHECK(w.recent_group_views().empty());
    PersistentWorld stale(db.string(),Simulation(999));
    w.step();
    bool refused=false;
    try{(void)stale.found_social_group("Stale council",{2,3});}
    catch(const std::runtime_error&){refused=true;}
    CHECK(refused);
    CHECK(w.recent_social_groups().size()==1);
}
int main(){Temp tmp;try{
    overlapping_groups_and_recovery(tmp.path);
    std::cout<<"PASS overlapping groups, bounded human contact, rival narratives, recover, tamper\n";
    migration_and_stale_commit(tmp.path);
    std::cout<<"PASS v7 migration and stale generation rejection\n";
    std::cout<<"GRSM-03 2/2 native scenario gates passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"GRSM-03 FAILED: "<<e.what()<<"\n";return 1;}}
