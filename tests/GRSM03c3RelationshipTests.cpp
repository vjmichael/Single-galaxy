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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM03c3 line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp {
    fs::path path;
    Temp():path(fs::temp_directory_path()/("sg03c3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {fs::create_directories(path);}
    ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
static void sql(const fs::path& db,const std::string& statement){
    sqlite3* raw=nullptr;CHECK(sqlite3_open(db.string().c_str(),&raw)==SQLITE_OK);
    char* error=nullptr;const auto status=sqlite3_exec(raw,statement.c_str(),nullptr,nullptr,&error);
    const std::string msg(error?error:"");sqlite3_free(error);sqlite3_close(raw);
    if(status!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+msg);
}
static bool refuses(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(999));return false;}
    catch(const std::exception&){return true;}
}
static void directed_actions(const fs::path& root){
    auto db=root/"relationship.sqlite",copy=root/"recovered.sqlite";
    Simulation sim(51);
    const auto home=sim.add_colony_ship(4,{0,0},100,4);
    sim.add_colony_ship(6,{25000,0},100,4);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.recent_social_interactions().empty());
        CHECK(w.directed_rapport(1,2)==0);
        CHECK(w.schedule_social_encounters(1,1)==1);
        const auto c=w.recent_social_encounters().front();
        CHECK(c.npc_low==1 && c.npc_high==2);
        CHECK(!w.record_social_interaction(0,1,2,SocialInteractionKind::Assistance));
        CHECK(!w.record_social_interaction(c.id,1,1,SocialInteractionKind::Assistance));
        CHECK(!w.record_social_interaction(c.id,1,5,SocialInteractionKind::Assistance));
        CHECK(!w.record_social_interaction(c.id,1,2,static_cast<SocialInteractionKind>(99)));
        CHECK(!w.record_social_interaction(c.id,2,3,SocialInteractionKind::Assistance));
        CHECK(w.directed_rapport(1,2)==0); // proximity is not a relationship score
        CHECK(w.recent_social_ties().empty());
        CHECK(w.record_social_interaction(c.id,1,2,SocialInteractionKind::Assistance));
        CHECK(w.directed_rapport(1,2)==8);
        CHECK(w.directed_rapport(2,1)==0); // no reciprocal magic
        CHECK(!w.record_social_interaction(c.id,1,2,SocialInteractionKind::Harm));
        CHECK(w.record_social_interaction(c.id,2,1,SocialInteractionKind::Harm));
        CHECK(w.directed_rapport(2,1)==-12);
        CHECK(w.recent_social_interactions().size()==2);
        CHECK(w.recent_knowledge().empty());CHECK(w.recent_social_ties().empty());
        CHECK(w.recent_physical_incidents().empty());
        CHECK(w.world().colony_ship(home)->crew==4);
        w.steps(8);
        CHECK(!w.record_social_interaction(c.id,1,2,SocialInteractionKind::Harm)); // stale encounter
        for(int i=0;i<6;++i)CHECK(w.schedule_social_encounters(64,8)<=8);
        const auto encounters=w.recent_social_encounters();
        std::uint64_t newer=0;
        for(const auto& e:encounters)if(e.npc_low==1&&e.npc_high==2&&e.id!=c.id){newer=e.id;break;}
        CHECK(newer!=0);
        CHECK(w.record_social_interaction(newer,1,2,SocialInteractionKind::Harm));
        CHECK(w.directed_rapport(1,2)==-4);
        CHECK(w.directed_rapport(2,1)==-12);
        CHECK(w.recent_social_interactions().front().prior_id==1);
        CHECK(w.recent_social_interactions().front().rapport==-4);
        w.steps(6); // old full checkpoints compact; history remains append-only
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& p:{db,copy}){
        PersistentWorld w(p.string(),Simulation(52));
        CHECK(w.directed_rapport(1,2)==-4);
        CHECK(w.directed_rapport(2,1)==-12);
        CHECK(w.recent_social_interactions().size()==3);
        CHECK(w.world().colony_ship(home)->crew==4);
        CHECK(w.recent_social_ties().empty());CHECK(w.recent_knowledge().empty());
    }
    for(const auto& [name,attack]:std::vector<std::pair<std::string,std::string>>{
        {"alter", "DROP TRIGGER social_interactions_no_update; UPDATE social_interactions SET rapport=80 WHERE id=1;"},
        {"wrong_actor", "DROP TRIGGER social_interactions_no_update; UPDATE social_interactions SET actor='5' WHERE id=1;"},
        {"false_encounter", "DROP TRIGGER social_interactions_no_update; UPDATE social_interactions SET encounter_id=999 WHERE id=1;"},
        {"deleted_leaf", "DROP TRIGGER social_interactions_no_delete; DELETE FROM social_interactions WHERE id=3;"},
        {"metadata", "UPDATE interaction_meta SET event_count=999 WHERE singleton=1;"},
        {"wrong_prior", "DROP TRIGGER social_interactions_no_update; UPDATE social_interactions SET prior_id=0 WHERE id=3;"},
    }){
        const auto bad=root/(name+".sqlite");fs::copy_file(db,bad);
        sql(bad,attack);CHECK(refuses(bad));
    }
}
static void genuine_v11_migration(const fs::path& root){
    auto db=root/"old_v11.sqlite";
    Simulation sim(52);sim.add_colony_ship(7,{0,0},100,4);sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.schedule_social_encounters(1,1)==1);
        CHECK(w.recent_social_encounters().size()==1);
    }
    // Genuine v11 contains real encounters but no v12 interaction table.
    sql(db,"DROP TABLE npc_mining_executions; DROP TABLE npc_mining_execution_meta; DROP TABLE npc_mining_assignments; DROP TABLE npc_mining_assignment_meta; DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; PRAGMA user_version=11;");
    {
        PersistentWorld w(db.string(),Simulation(999));
        CHECK(w.recent_social_encounters().size()==1);
        CHECK(w.recent_social_interactions().empty());
        CHECK(w.directed_rapport(1,2)==0);
        const auto id=w.recent_social_encounters().front().id;
        CHECK(w.record_social_interaction(id,1,2,SocialInteractionKind::Assistance));
    }
    PersistentWorld reloaded(db.string(),Simulation(999));
    CHECK(reloaded.directed_rapport(1,2)==8);
    CHECK(reloaded.recent_social_interactions().size()==1);
}
int main(){Temp t;try{
    directed_actions(t.path);
    std::cout<<"PASS explicit evidence-linked directional rapport, no gossip, recovery and tamper rejection\n";
    genuine_v11_migration(t.path);
    std::cout<<"PASS real v11 to v12 migration without fabricated interactions\n";
    std::cout<<"GRSM-03c.3 2/2 scenario gates passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-03c.3 FAIL: "<<e.what()<<'\n';return 1;}
}
