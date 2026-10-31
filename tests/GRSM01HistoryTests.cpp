#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("GRSM01 failed: ")+ #x + " line "+std::to_string(__LINE__));}while(0)
static Command command(const PersistentWorld& w, std::uint64_t id, EntityId ship,
                       EntityId target, std::int64_t amount=4) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=Action::Mine;c.amount=amount;
    return c;
}
static void sql(const fs::path& path,const std::string& statement) {
    sqlite3* db=nullptr;
    if(sqlite3_open(path.string().c_str(),&db)!=SQLITE_OK)throw std::runtime_error("open sqlite");
    char* err=nullptr;
    const int code=sqlite3_exec(db,statement.c_str(),nullptr,nullptr,&err);
    const std::string message=err?err:"";
    sqlite3_free(err);sqlite3_close(db);
    if(code!=SQLITE_OK) throw std::runtime_error("sqlite: "+message);
}
static bool fails_open(const fs::path& path) {
    try { PersistentWorld bad(path.string(),Simulation(99)); return false; }
    catch (const std::runtime_error&) { return true; }
}
int main() {
    const auto base=fs::temp_directory_path()/("galaxy_grsm01_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base);
    const auto db=base/"world.sqlite", copy=base/"good_copy.sqlite";
    const auto tampered=base/"tampered.sqlite", missing=base/"missing.sqlite";
    const auto legacy=base/"legacy.sqlite";
    try {
        Simulation sim(2601);
        const EntityId ship=sim.add_ship(7,{0,0},100);
        const EntityId rock=sim.add_asteroid({1000,0},100);
        const auto conserved=sim.total_material_kg();
        {
            PersistentWorld w(db.string(),std::move(sim));
            CHECK(w.generation()==1);
            CHECK(w.recent_history().empty());
            const auto c1=command(w,1,ship,rock,7);
            CHECK(w.submit(c1));
            CHECK(w.recent_history().empty());  // submission is not execution
            w.step();
            auto facts=w.recent_history();
            CHECK(facts.size()==1);
            CHECK(facts[0].event_id=="command:1" && facts[0].command_id==1);
            CHECK(facts[0].actor==ship && facts[0].target==rock);
            CHECK(facts[0].action==Action::Mine && facts[0].result==Outcome::Applied);
            CHECK(facts[0].tick==1 && facts[0].transferred==7);
            CHECK(w.world().total_material_kg()==conserved);
            CHECK(!w.submit(c1)); // cannot duplicate an already executed command
            auto c2=command(w,2,ship,0,8); // invalid target, still an executed fact
            CHECK(w.submit(c2)); w.step();
            facts=w.recent_history();
            CHECK(facts.size()==2 && facts[0].command_id==2);
            CHECK(facts[0].result!=Outcome::Applied && facts[0].transferred==0);
            CHECK(w.world().total_material_kg()==conserved);
            const auto c3=command(w,3,ship,rock,5);
            CHECK(w.submit(c3));
            const auto generation_before=w.generation();
            bool failed=false;
            try {w.step(FailPoint::AfterReceiptWrite);}catch(const std::runtime_error&){failed=true;}
            CHECK(failed && w.generation()==generation_before);
            CHECK(w.recent_history().size()==2); // DB and memory both rolled back
            w.step();
            facts=w.recent_history();
            CHECK(facts.size()==3 && facts[0].command_id==3);
            CHECK(facts[0].result==Outcome::Applied && facts[0].transferred==5);
            CHECK(w.world().total_material_kg()==conserved);
            CHECK(w.recent_history(1).size()==1);
            failed=false;
            try {(void)w.recent_history(257);}catch(const std::invalid_argument&){failed=true;}
            CHECK(failed); // bounded query protects APS from unbounded scanning
        }
        {
            PersistentWorld w(db.string(),Simulation(99));
            CHECK(w.recent_history().size()==3);
            CHECK(w.recent_history()[2].event_id=="command:1");
            CHECK(w.world().total_material_kg()==conserved);
        }
        PersistentWorld::recover_copy(db.string(),copy.string());
        {
            PersistentWorld w(copy.string(),Simulation(9));
            CHECK(w.recent_history().size()==3); // recovery copied historical facts
        }
        fs::copy_file(copy,tampered);
        sql(tampered,"DROP TRIGGER historical_no_update; "
            "UPDATE historical_events SET action=action+1 WHERE command_id='1'");
        CHECK(fails_open(tampered));
        fs::copy_file(copy,missing);
        sql(missing,"DROP TRIGGER historical_no_delete; "
            "DELETE FROM historical_events WHERE command_id='1'");
        CHECK(fails_open(missing));
        // Simulate a v4 database to test nondestructive upgrade: do not invent
        // historical facts for already-executed commands.
        fs::copy_file(copy,legacy);
        sql(legacy,"DROP TRIGGER historical_no_update; DROP TRIGGER historical_no_delete; "
                   "DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; DROP TABLE group_membership_events; DROP TABLE membership_meta; "
                       "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
                       "DROP TABLE group_views; DROP TABLE group_relays; DROP TABLE social_groups; "
                       "DROP TABLE social_meta; "
                       "DROP TRIGGER knowledge_no_update; DROP TRIGGER knowledge_no_delete; "
                   "DROP TABLE npc_knowledge; DROP TABLE knowledge_meta; "
                   "DROP TRIGGER physical_no_update; DROP TRIGGER physical_no_delete; "
                   "DROP TABLE physical_incidents; DROP TABLE physical_meta; "
                   "DROP TABLE historical_events; DROP TABLE historical_meta; PRAGMA user_version=4");
        {
            PersistentWorld w(legacy.string(),Simulation(999));
            CHECK(w.world().tick()==3 && w.recent_history().empty());
            const auto c4=command(w,4,ship,rock,1);
            CHECK(w.submit(c4)); w.step();
            CHECK(w.recent_history().size()==1);
            CHECK(w.recent_history()[0].command_id==4);
            CHECK(w.world().total_material_kg()==conserved);
        }
        {
            PersistentWorld w(legacy.string(),Simulation(999));
            CHECK(w.recent_history().size()==1);
            CHECK(w.recent_history()[0].command_id==4);
        }
        fs::remove_all(base);
        std::cout<<"GRSM-01 PASS: atomic command truth, rejected-action facts, rollback, "
                 <<"restart, recovery, tamper/deletion detection, v4 migration, conservation\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr<<"GRSM-01 FAILED: "<<ex.what()<<"\n";
        fs::remove_all(base);return 1;
    }
}
