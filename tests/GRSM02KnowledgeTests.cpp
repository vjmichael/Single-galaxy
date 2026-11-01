#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("GRSM-02 failed at line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp { fs::path root;
    Temp():root(fs::temp_directory_path()/("grsm02_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(root);
    }
    ~Temp(){std::error_code e;fs::remove_all(root,e);}
};
static void sql(const fs::path& file,const std::string& stmt) {
    sqlite3* raw=nullptr;
    if(sqlite3_open(file.string().c_str(),&raw)!=SQLITE_OK)throw std::runtime_error("sqlite open");
    char* err=nullptr;const auto rc=sqlite3_exec(raw,stmt.c_str(),nullptr,nullptr,&err);
    const auto why=std::string(err?err:"");sqlite3_free(err);sqlite3_close(raw);
    if(rc!=SQLITE_OK)throw std::runtime_error("sqlite statement: "+why);
}
static bool invalid(const fs::path& file){
    try {PersistentWorld w(file.string(),Simulation(12));return false;}
    catch(const std::runtime_error&){return true;}
}
static void testimony_and_recovery(const fs::path& root){
    const auto db=root/"witness.sqlite",copy=root/"recovered.sqlite",altered=root/"altered.sqlite",
               missing=root/"missing.sqlite",ancestor=root/"ancestor.sqlite";
    Simulation sim(22);
    const auto home=sim.add_colony_ship(7,{0,0},100,24);
    const auto remote=sim.add_colony_ship(8,{25000,0},100,24);
    (void)remote;
    sim.enable_individual_workforce();
    CHECK(sim.npc_records().size()==48);
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.recent_knowledge().empty());
        CHECK(w.submit_testing_crew_loss(home,1,100));
        w.step();
        CHECK(w.world().npc_records().size()==47);
        const std::string fact="death:100:1";
        CHECK(w.recent_physical_incidents().front().incident_id==fact);
        CHECK(!w.record_local_observation(fact,1)); // dead NPC cannot testify
        CHECK(!w.record_local_observation(fact,25)); // other colony out of range
        CHECK(!w.record_local_observation("death:100:999",2));
        CHECK(w.record_local_observation(fact,2));
        CHECK(!w.record_local_observation(fact,2)); // one firsthand claim per event/person
        CHECK(w.recent_knowledge().size()==1);
        CHECK(w.recent_knowledge()[0].parent_id==0);
        CHECK(w.recent_knowledge()[0].recipient_npc==2);
        CHECK(w.recent_knowledge()[0].fidelity==ClaimFidelity::LocalObservation);
        CHECK(!w.transmit_claim(55,3,ClaimFidelity::Faithful,0,60,"Did not hear this"));
        CHECK(!w.transmit_claim(1,25,ClaimFidelity::Faithful,0,50,"Remote gossip"));
        CHECK(!w.transmit_claim(1,2,ClaimFidelity::Faithful,0,50,"Talk to myself"));
        CHECK(!w.transmit_claim(1,3,ClaimFidelity::LocalObservation,0,50,"False direct witness"));
        CHECK(!w.transmit_claim(1,3,ClaimFidelity::Faithful,101,50,"Invalid valence"));
        CHECK(!w.transmit_claim(1,3,ClaimFidelity::Faithful,0,50,""));
        CHECK(w.transmit_claim(1,3,ClaimFidelity::Faithful,-80,90,
              "I saw an NPC die aboard the colony ship"));
        CHECK(w.transmit_claim(2,4,ClaimFidelity::Embellished,-100,35,
              "I heard hundreds died in the colony"));
        CHECK(w.transmit_claim(1,4,ClaimFidelity::Contrary,75,20,
              "It was a staged accident and no one died"));
        CHECK(w.recent_knowledge().size()==4);
        auto claims=w.recent_knowledge();
        CHECK(claims[0].parent_id==1 && claims[0].recipient_npc==4);
        CHECK(claims[1].parent_id==2 && claims[1].recipient_npc==4);
        CHECK(claims[1].fidelity==ClaimFidelity::Embellished && claims[0].fidelity==ClaimFidelity::Contrary);
        CHECK(claims[0].incident_id==fact && claims[1].incident_id==fact);
        CHECK(!w.transmit_claim(3,25,ClaimFidelity::Embellished,-40,50,"Another distant listener"));
        CHECK(w.recent_physical_incidents().front().kind==PhysicalKind::NpcLost);
        CHECK(w.world().npc_records().size()==47); // the claims don't change physical truth
        CHECK(w.world().colony_ship(home)->crew==23);
        bool invalid_limit=false;
        try{(void)w.recent_knowledge(257);}catch(const std::invalid_argument&){invalid_limit=true;}
        CHECK(invalid_limit);
        w.step(); // old events cannot be newly "witnessed" after time advances
        CHECK(!w.record_local_observation(fact,5));
        CHECK(w.transmit_claim(3,5,ClaimFidelity::Embellished,-50,40,
               "Rumor persists even after the witnessing window"));
        CHECK(w.recent_knowledge().size()==5);
        CHECK(w.recent_history().size()==1); // one executed physical outcome
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& file:{db,copy}) {
        PersistentWorld w(file.string(),Simulation(88));
        CHECK(w.recent_knowledge().size()==5);
        CHECK(w.recent_knowledge()[0].speaker_npc==4);
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.world().colony_ship(home)->crew==23);
        CHECK(w.world().npc_records().size()==47);
    }
    fs::copy_file(copy,altered);
    sql(altered,"DROP TRIGGER knowledge_no_update; UPDATE npc_knowledge SET confidence=99 WHERE id=3;");
    CHECK(invalid(altered));
    fs::copy_file(copy,missing);
    sql(missing,"DROP TRIGGER knowledge_no_delete; DELETE FROM npc_knowledge WHERE id=5;");
    CHECK(invalid(missing));
    fs::copy_file(copy,ancestor);
    // An invalid claim ancestry must never be accepted, even if its author tries
    // to present it as somebody else's firsthand knowledge.
    sql(ancestor,"DROP TRIGGER knowledge_no_update; UPDATE npc_knowledge SET parent_id=1 WHERE id=3;");
    CHECK(invalid(ancestor));
}
static void physical_witness_ranges(const fs::path& root){
    const auto db=root/"ranges.sqlite";
    Simulation sim(18);
    const auto near=sim.add_colony_ship(7,{0,0},100,24);
    sim.add_colony_ship(8,{28000,0},100,24);
    const auto miner=sim.add_asteroid({1000,0},90);
    const auto raider=sim.add_ship(7,{1200,0},40);
    const auto victim=sim.add_ship(8,{2300,0},40);
    sim.enable_individual_workforce();
    PersistentWorld w(db.string(),std::move(sim));
    Command mine{};mine.id=50;mine.sequence=50;mine.execute_tick=w.world().tick()+1;
    mine.issuer=7;mine.actor=near;mine.target=miner;mine.action=Action::Mine;mine.amount=5;
    CHECK(w.submit(mine));w.step();
    std::string cargo;
    for(const auto& fact:w.recent_physical_incidents())
        if(fact.kind==PhysicalKind::CargoMovement){cargo=fact.incident_id;break;}
    CHECK(!cargo.empty());
    CHECK(w.record_local_observation(cargo,2));
    CHECK(!w.record_local_observation(cargo,25));
    CHECK(w.world().find(near)->cargo==5);
    CHECK(w.submit_testing_destroy_ship(raider,victim,51));w.step();
    const std::string wreck="wreck:51";
    CHECK(w.world().find(victim)->type==EntityType::Wreck);
    CHECK(w.record_local_observation(wreck,2));
    CHECK(!w.record_local_observation(wreck,25));
    CHECK(w.recent_knowledge().size()==2);
    CHECK(w.recent_physical_incidents().size()==2);
}
static void legacy_boundary(const fs::path& root){
    const auto db=root/"legacy.sqlite";
    Simulation sim(15);const auto home=sim.add_colony_ship(7,{0,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.submit_testing_crew_loss(home,1,401));w.step();
        CHECK(w.recent_physical_incidents().size()==1);
    }
    sql(db,"DROP TABLE npc_fabrication_executions; DROP TABLE npc_fabrication_execution_meta; DROP TABLE npc_fabrication_assignments; DROP TABLE npc_fabrication_assignment_meta; DROP TABLE npc_mining_executions; DROP TABLE npc_mining_execution_meta; DROP TABLE npc_mining_assignments; DROP TABLE npc_mining_assignment_meta; DROP TABLE incident_role_receipts; DROP TABLE incident_role_meta; DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; DROP TABLE group_membership_events; DROP TABLE membership_meta; "
                       "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
                       "DROP TABLE group_views; DROP TABLE group_relays; DROP TABLE social_groups; "
           "DROP TABLE social_meta; "
           "DROP TRIGGER knowledge_no_update; DROP TRIGGER knowledge_no_delete; "
           "DROP TABLE npc_knowledge; DROP TABLE knowledge_meta; PRAGMA user_version=6;");
    {
        PersistentWorld w(db.string(),Simulation(44));
        CHECK(w.world().colony_ship(home)->crew==23);
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.recent_knowledge().empty());
        CHECK(!w.record_local_observation("death:401:1",2)); // no fabricated prior testimony
        CHECK(w.submit_testing_crew_loss(home,1,402));w.step();
        CHECK(w.record_local_observation("death:402:2",3));
        CHECK(w.recent_knowledge().size()==1);
    }
    PersistentWorld w(db.string(),Simulation(33));
    CHECK(w.world().colony_ship(home)->crew==22);
    CHECK(w.recent_knowledge().size()==1);
}
int main(){Temp tmp;try{
    testimony_and_recovery(tmp.root);
    std::cout<<"PASS local witnesses + contradictory multihop claims + recovery + tamper\n";
    physical_witness_ranges(tmp.root);
    std::cout<<"PASS cargo + nearby wreck witnesses; distant colony denied\n";
    legacy_boundary(tmp.root);
    std::cout<<"PASS v6 migration + no false backfill\n";
    std::cout<<"GRSM-02 3/3 scenario gates passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
