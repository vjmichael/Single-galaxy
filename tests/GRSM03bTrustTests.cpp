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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM-03b line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp{
    fs::path path;
    Temp():path(fs::temp_directory_path()/("sg_trust_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(path);}
    ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
static void sql(const fs::path& db,const std::string& statement){
    sqlite3* raw=nullptr;CHECK(sqlite3_open(db.string().c_str(),&raw)==SQLITE_OK);
    char* error=nullptr;const auto rc=sqlite3_exec(raw,statement.c_str(),nullptr,nullptr,&error);
    const std::string why(error?error:"");sqlite3_free(error);sqlite3_close(raw);
    if(rc!=SQLITE_OK)throw std::runtime_error("SQL failed "+why);
}
static bool refused_restore(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(1));return false;}
    catch(const std::exception&){return true;}
}
static void directed_trust_and_recovery(const fs::path& root){
    const auto db=root/"trust.sqlite",copy=root/"trust_recovered.sqlite";
    Simulation sim(27);
    const auto home=sim.add_colony_ship(7,{0,0},100,24);
    sim.add_colony_ship(9,{25000,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.recent_social_ties().empty());
        CHECK(!w.record_social_contact(2,2));
        CHECK(!w.record_social_contact(3,25));
        CHECK(!w.record_social_contact(3,999));
        CHECK(w.record_social_contact(3,2)); // trust 3 -> 2, not 2 -> 3
        CHECK(w.record_social_contact(4,3));
        CHECK(w.record_social_contact(5,4));
        CHECK(!w.record_social_contact(3,2)); // idempotence: no duplicate receipt
        CHECK(w.recent_social_ties().size()==3);
        CHECK(w.recent_social_ties()[0].trust==50);
        CHECK(!w.relay_trusted_claim(900,4,ClaimFidelity::Faithful,0,100,"Invented"));
        CHECK(w.submit_testing_crew_loss(home,1,200));w.step();
        CHECK(w.record_local_observation("death:200:1",2));
        CHECK(!w.relay_trusted_claim(1,25,ClaimFidelity::Faithful,0,100,"Remote"));
        CHECK(!w.relay_trusted_claim(1,4,ClaimFidelity::Faithful,0,100,"No trust 4 to 2"));
        CHECK(w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,-10,100,"I heard of the real casualty"));
        CHECK(w.recent_knowledge()[0].id==2);
        CHECK(w.recent_knowledge()[0].confidence==50);
        CHECK(!w.reassess_social_trust(3,2,1,25)); // direct observation is not received from 2
        CHECK(!w.reassess_social_trust(3,4,2,25)); // wrong source / no tie
        CHECK(!w.reassess_social_trust(3,2,2,26)); // too large
        CHECK(w.reassess_social_trust(3,2,2,25));
        CHECK(!w.reassess_social_trust(3,2,2,-25)); // one feedback per claim
        CHECK(w.recent_social_ties()[0].trust==75);
        CHECK(w.relay_trusted_claim(2,4,ClaimFidelity::Embellished,-5,100,"The number was exaggerated"));
        CHECK(w.recent_knowledge()[0].confidence==50); // limited by 3's received claim and trust 4->3
        CHECK(w.reassess_social_trust(4,3,3,-25));
        CHECK(w.recent_social_ties()[0].trust==25);
        CHECK(w.relay_trusted_claim(3,5,ClaimFidelity::Contrary,20,100,"Maybe nobody was hurt"));
        CHECK(w.recent_knowledge()[0].confidence==50);
        CHECK(w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,-20,100,"A second firsthand retelling"));
        CHECK(w.recent_knowledge()[0].confidence==75); // fresh trust cap
        const auto claim=w.recent_knowledge()[0].id;
        CHECK(w.reassess_social_trust(3,2,claim,-25));
        CHECK(w.recent_social_ties()[0].trust==50);
        CHECK(w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,-20,100,"Another firsthand retelling"));
        CHECK(w.recent_knowledge()[0].confidence==50);
        CHECK(w.reassess_social_trust(3,2,w.recent_knowledge()[0].id,-25));
        CHECK(w.recent_social_ties()[0].trust==25);
        CHECK(w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,-20,100,"The latest retelling"));
        CHECK(w.recent_knowledge()[0].confidence==25);
        CHECK(w.reassess_social_trust(3,2,w.recent_knowledge()[0].id,-25));
        CHECK(w.recent_social_ties()[0].trust==0);
        const auto knowledge_count=w.recent_knowledge().size();
        CHECK(!w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,0,100,"Rejected by distrust"));
        CHECK(w.recent_knowledge().size()==knowledge_count);
        CHECK(!w.relay_trusted_claim(2,2,ClaimFidelity::Faithful,0,100,"No self-talk"));
        CHECK(w.world().npc_records().size()==47);
        CHECK(w.world().colony_ship(home)->crew==23);
        CHECK(w.recent_physical_incidents().size()==1);
        w.steps(5); // checkpoint compaction must not erase social history
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& source:{db,copy}){
        PersistentWorld w(source.string(),Simulation(99));
        CHECK(w.recent_social_ties().size()==8);
        CHECK(w.recent_social_ties()[0].trust==0);
        CHECK(w.recent_knowledge().size()==7);
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.world().colony_ship(home)->crew==23);
        CHECK(!w.relay_trusted_claim(1,3,ClaimFidelity::Faithful,0,100,"Still blocked"));
    }
    for(const auto& [label,attack]:std::vector<std::pair<std::string,std::string>>{
        {"changed_trust","DROP TRIGGER social_tie_no_update; UPDATE social_tie_events SET trust=80 WHERE id=4;"},
        {"changed_observer","DROP TRIGGER social_tie_no_update; UPDATE social_tie_events SET observer='25' WHERE id=1;"},
        {"deleted_leaf","DROP TRIGGER social_tie_no_delete; DELETE FROM social_tie_events WHERE id=8;"},
        {"deleted_middle","DROP TRIGGER social_tie_no_delete; DELETE FROM social_tie_events WHERE id=3;"},
        {"wrong_evidence","DROP TRIGGER social_tie_no_update; UPDATE social_tie_events SET knowledge_id=1 WHERE id=4;"}
    }){
        const auto bad=root/(label+".sqlite");fs::copy_file(db,bad);
        sql(bad,attack);CHECK(refused_restore(bad));
    }
}
static void legacy_migration(const fs::path& root){
    const auto db=root/"legacy.sqlite";
    Simulation sim(29);sim.add_colony_ship(7,{0,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.found_social_group("Real crew",{2,3})==1);
    }
    // Construct a *true* v8 fixture, removing every object belonging to v9.
    sql(db,"DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; DROP TABLE group_membership_events; DROP TABLE membership_meta; "
           "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
           "CREATE TABLE old_group_relays AS SELECT id,group_id,knowledge_id,digest FROM group_relays; "
           "DROP TRIGGER group_relays_no_update; DROP TRIGGER group_relays_no_delete; "
           "DROP TABLE group_relays; "
           "CREATE TABLE group_relays(id INTEGER PRIMARY KEY, group_id INTEGER NOT NULL REFERENCES social_groups(id), "
           "knowledge_id INTEGER NOT NULL UNIQUE REFERENCES npc_knowledge(id),digest TEXT NOT NULL); "
           "INSERT INTO group_relays SELECT * FROM old_group_relays; DROP TABLE old_group_relays; "
           "CREATE TRIGGER group_relays_no_update BEFORE UPDATE ON group_relays "
           "BEGIN SELECT RAISE(ABORT,'social history immutable'); END; "
           "CREATE TRIGGER group_relays_no_delete BEFORE DELETE ON group_relays "
           "BEGIN SELECT RAISE(ABORT,'social history immutable'); END; "
           "PRAGMA user_version=8;");
    {
        PersistentWorld w(db.string(),Simulation(42));
        CHECK(w.recent_social_groups().size()==1);
        CHECK(w.recent_social_ties().empty()); // never invent prior contact
        CHECK(w.record_social_contact(3,2));
        CHECK(w.recent_social_ties().size()==1);
    }
    PersistentWorld reloaded(db.string(),Simulation(43));
    CHECK(reloaded.recent_social_ties().size()==1);
}
int main(){Temp tmp;try{
    directed_trust_and_recovery(tmp.path);
    std::cout<<"PASS directional ties, receiver caps, evidence feedback, distrust and corruption\n";
    legacy_migration(tmp.path);
    std::cout<<"PASS non-inventing schema v8 migration\n";
    std::cout<<"GRSM-03b 2/2 native scenario gates passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-03b FAILED: "<<e.what()<<"\n";return 1;}}
