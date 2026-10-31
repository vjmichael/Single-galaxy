#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM-01b line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct TempDir {
    fs::path dir;
    TempDir():dir(fs::temp_directory_path()/("grsm01b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {fs::create_directories(dir);}
    ~TempDir(){std::error_code e;fs::remove_all(dir,e);}
};
static void sql(const fs::path& db,const std::string& q){
    sqlite3* p=nullptr;
    if(sqlite3_open(db.string().c_str(),&p)!=SQLITE_OK)throw std::runtime_error("sqlite open");
    char* e=nullptr;const auto rc=sqlite3_exec(p,q.c_str(),nullptr,nullptr,&e);
    const std::string msg=e?e:"";sqlite3_free(e);sqlite3_close(p);
    if(rc!=SQLITE_OK)throw std::runtime_error("sqlite failure "+msg);
}
static bool invalid(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(72));return false;}
    catch(const std::runtime_error&){return true;}
}
static Event send(PersistentWorld& w,std::uint64_t id,EntityId ship,EntityId target,
                  Action a,std::int64_t amount=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=a;c.amount=amount;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static bool kind_is(const PersistentWorld& w,PhysicalKind kind){
    for(const auto& incident:w.recent_physical_incidents(256))if(incident.kind==kind)return true;
    return false;
}
static std::size_t count_kind(const PersistentWorld& w,PhysicalKind kind){
    std::size_t count=0;
    for(const auto& i:w.recent_physical_incidents(256))if(i.kind==kind)++count;
    return count;
}
static void cargo_journey_and_recovery(const fs::path& root){
    const auto db=root/"travel.sqlite",copy=root/"travel_copy.sqlite";
    Simulation s(9001);s.add_system(2);
    const auto ship=s.add_ship(7,{0,0},60);
    const auto escort=s.add_ship(8,{0,0},20);
    const auto ore=s.add_asteroid({1000,0},35);
    const auto gate=s.add_gate(1,{0,0},2,{5000,0});
    s.add_gate(2,{17000,0},1,{0,0});
    const auto initial=s.total_ore();
    {
        PersistentWorld w(db.string(),std::move(s));
        CHECK(send(w,ship==0?0:101,ship,ore,Action::Mine,12).transferred==12);
        CHECK(kind_is(w,PhysicalKind::CargoMovement));
        CHECK(w.recent_physical_incidents().front().amount==12);
        CHECK(w.world().find(ship)->cargo==12);
        CHECK(send(w,102,ship,gate,Action::RequestJump).result==Outcome::Applied);
        CHECK(kind_is(w,PhysicalKind::JourneyStarted));
        CHECK(!kind_is(w,PhysicalKind::JourneyCompleted));
        w.steps(2);
        CHECK(w.world().find(ship)->system_id==2);
        CHECK(kind_is(w,PhysicalKind::JourneyCompleted));
        CHECK(w.world().total_ore()==initial);
        CHECK(count_kind(w,PhysicalKind::JourneyStarted)==1);
        CHECK(count_kind(w,PhysicalKind::JourneyCompleted)==1);
        // The witness ship follows and later destroys the same completed
        // traveler, retaining the cargo and completed journey in history.
        CHECK(send(w,103,escort,gate,Action::RequestJump,0,8).result==Outcome::Applied);
        w.steps(8); // arrival + cloak expiration
        CHECK(w.world().find(escort)->system_id==2);
        CHECK(w.submit_testing_destroy_ship(escort,ship,104));w.step();
        CHECK(w.world().find(ship)->type==EntityType::Wreck && w.world().find(ship)->cargo==12);
        CHECK(count_kind(w,PhysicalKind::JourneyStarted)==2);
        CHECK(count_kind(w,PhysicalKind::JourneyCompleted)==2);
        CHECK(count_kind(w,PhysicalKind::ShipDestroyed)==1);
        bool matched=false;
        for(const auto& e:w.recent_physical_incidents())if(e.kind==PhysicalKind::JourneyCompleted && e.command_id==102){
            CHECK(e.source=="1" && e.destination=="2" && e.amount==12);
            matched=true;
        }
        CHECK(matched);
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& path:{db,copy}){
        PersistentWorld w(path.string(),Simulation(11));
        CHECK(w.world().find(ship)->cargo==12 && w.world().find(ship)->system_id==2 &&
              w.world().find(ship)->type==EntityType::Wreck);
        CHECK(count_kind(w,PhysicalKind::CargoMovement)==1);
        CHECK(count_kind(w,PhysicalKind::JourneyStarted)==2);
        CHECK(count_kind(w,PhysicalKind::JourneyCompleted)==2);
        CHECK(count_kind(w,PhysicalKind::ShipDestroyed)==1);
    }
    sql(db,"DROP TRIGGER physical_no_update; UPDATE physical_incidents SET amount=888 "
           "WHERE kind=2;");
    CHECK(invalid(db));
}
static void wreck_and_people(const fs::path& root){
    const auto db=root/"damage.sqlite",copy=root/"damage_copy.sqlite";
    Simulation s(707);
    const auto colony=s.add_colony_ship(7,{0,0},100,24);
    s.enable_individual_workforce();
    const auto attacker=s.add_ship(7,{0,0},80);
    const auto victim=s.add_ship(8,{1000,0},40);
    const auto rock=s.add_asteroid({2000,0},100);
    const auto original=s.total_ore();
    {
        PersistentWorld w(db.string(),std::move(s));
        CHECK(send(w,501,victim,rock,Action::Mine,20,8).result==Outcome::Applied);
        CHECK(w.world().find(victim)->cargo==20);
        Command spoof{};spoof.id=502;spoof.sequence=502;
        spoof.execute_tick=w.world().tick()+1;spoof.issuer=7;spoof.actor=attacker;
        spoof.target=victim;spoof.action=Action::HostDestroyShip;
        CHECK(!w.submit(spoof));
        spoof.issuer=Simulation::kServerAuthority;CHECK(!w.submit(spoof));
        CHECK(w.submit_testing_destroy_ship(attacker,victim,503));
        CHECK(w.world().find(victim)->type==EntityType::Ship);
        // An aborted step may NEVER publish a wreck or death.
        const auto before=w.world().canonical_state();
        try{w.step(FailPoint::AfterReceiptWrite);throw std::runtime_error("expected failpoint");}
        catch(const std::runtime_error& ex){CHECK(std::string(ex.what()).find("injected failure")!=std::string::npos);}
        CHECK(w.world().canonical_state()==before && !kind_is(w,PhysicalKind::ShipDestroyed));
        w.step();
        CHECK(w.world().find(victim)->type==EntityType::Wreck);
        CHECK(w.world().find(victim)->owner_id==8); // original victim provenance survives
        CHECK(w.world().find(victim)->cargo==20);
        CHECK(kind_is(w,PhysicalKind::ShipDestroyed));
        CHECK(w.world().total_ore()==original);
        const auto total_population=w.world().npc_records().size();
        CHECK(total_population==24);
        CHECK(w.submit_testing_crew_loss(colony,3,504));
        w.step();
        CHECK(w.world().colony_ship(colony)->crew==21);
        CHECK(w.world().npc_records().size()==21);
        CHECK(!w.world().npc_records().count(1) && !w.world().npc_records().count(2) &&
              !w.world().npc_records().count(3));
        CHECK(count_kind(w,PhysicalKind::NpcLost)==3);
        CHECK(count_kind(w,PhysicalKind::ShipDestroyed)==1);
        CHECK(w.world().total_ore()==original);
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& path:{db,copy}){
        PersistentWorld w(path.string(),Simulation(55));
        CHECK(w.world().colony_ship(colony)->crew==21 && w.world().npc_records().size()==21);
        CHECK(w.world().find(victim)->type==EntityType::Wreck && w.world().find(victim)->cargo==20);
        CHECK(count_kind(w,PhysicalKind::NpcLost)==3);
        CHECK(count_kind(w,PhysicalKind::ShipDestroyed)==1);
        CHECK(w.world().total_ore()==original);
        CHECK(!w.submit_testing_destroy_ship(attacker,victim,503));
    }
    sql(db,"DROP TRIGGER physical_no_delete; DELETE FROM physical_incidents WHERE kind=5;");
    CHECK(invalid(db));
}
static void migration_boundary(const fs::path& root){
    const auto db=root/"migration.sqlite";
    Simulation s(7);const auto ship=s.add_ship(7,{0,0},60),rock=s.add_asteroid({2000,0},100);
    {
        PersistentWorld w(db.string(),std::move(s));
        CHECK(send(w,301,ship,rock,Action::Mine,7).result==Outcome::Applied);
    }
    // Exact v5 downgrade simulation: remove ONLY the forward v6 tables/triggers;
    // retained older histories and snapshots remain intact.
    sql(db,"DROP TABLE physical_appraisals; DROP TABLE physical_appraisal_meta; DROP TABLE group_attributions; DROP TABLE attribution_meta; DROP TABLE social_interactions; DROP TABLE interaction_meta; DROP TABLE social_encounters; DROP TABLE encounter_scans; DROP TABLE encounter_meta; DROP TABLE group_membership_events; DROP TABLE membership_meta; "
                       "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
                       "DROP TABLE group_views; DROP TABLE group_relays; DROP TABLE social_groups; "
                       "DROP TABLE social_meta; "
                       "DROP TRIGGER knowledge_no_update; DROP TRIGGER knowledge_no_delete; "
           "DROP TABLE npc_knowledge; DROP TABLE knowledge_meta; "
           "DROP TRIGGER physical_no_update; DROP TRIGGER physical_no_delete; "
           "DROP TABLE physical_incidents; DROP TABLE physical_meta; PRAGMA user_version=5;");
    {
        PersistentWorld w(db.string(),Simulation(99));
        CHECK(w.recent_physical_incidents().empty());
        CHECK(w.world().find(ship)->cargo==7);
        CHECK(send(w,302,ship,rock,Action::Mine,6).result==Outcome::Applied);
        CHECK(count_kind(w,PhysicalKind::CargoMovement)==1);
        CHECK(w.world().find(ship)->cargo==13);
    }
    PersistentWorld w(db.string(),Simulation(14));
    CHECK(w.recent_physical_incidents().size()==1);
}
int main(){TempDir tmp;try{
    cargo_journey_and_recovery(tmp.dir);std::cout<<"PASS cargo + two-phase journey + recovery + tamper\n";
    wreck_and_people(tmp.dir);std::cout<<"PASS host-auth wreck + exact NPC losses + rollback + recovery + tamper\n";
    migration_boundary(tmp.dir);std::cout<<"PASS physical ledger v5 migration boundary + no fabricated history\n";
    std::cout<<"3/3 GRSM-01b physical scenario gates passed\n";return 0;
} catch(const std::exception& ex){std::cerr<<"GRSM-01b FAILED: "<<ex.what()<<"\n";return 1;}}
