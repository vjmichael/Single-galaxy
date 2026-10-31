#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("GRSM-03c line ")+std::to_string(__LINE__)+": " #x);}while(0)
struct Temp {
    fs::path path;
    Temp():path(fs::temp_directory_path()/("sg_membership_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(path);}
    ~Temp(){std::error_code error;fs::remove_all(path,error);}
};
static void sql(const fs::path& db,const std::string& text){
    sqlite3* conn=nullptr;CHECK(sqlite3_open(db.string().c_str(),&conn)==SQLITE_OK);
    char* msg=nullptr;auto rc=sqlite3_exec(conn,text.c_str(),nullptr,nullptr,&msg);
    std::string error=msg?msg:"";sqlite3_free(msg);sqlite3_close(conn);
    if(rc!=SQLITE_OK)throw std::runtime_error("SQL error: "+error);
}
static bool refuses(const fs::path& db){
    try{PersistentWorld w(db.string(),Simulation(1));return false;}
    catch(const std::exception&){return true;}
}
static void membership_and_knowledge(const fs::path& root){
    const auto db=root/"main.sqlite",copy=root/"recovered.sqlite";
    Simulation sim(31);
    const auto ship=sim.add_colony_ship(7,{0,0},100,24);
    sim.add_colony_ship(9,{25000,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        const auto guild=w.found_social_group("Engineers",{2,3});CHECK(guild==1);
        CHECK(w.recent_group_membership().empty());
        CHECK(!w.change_group_membership(guild,2,GroupMembershipKind::Join,3));
        CHECK(!w.change_group_membership(guild,4,GroupMembershipKind::Join,5));
        CHECK(!w.change_group_membership(guild,25,GroupMembershipKind::Join,2));
        CHECK(!w.change_group_membership(guild,4,GroupMembershipKind::Join,4));
        CHECK(!w.change_group_membership(guild,4,GroupMembershipKind::Leave));
        CHECK(w.change_group_membership(guild,3,GroupMembershipKind::Leave));
        CHECK(!w.change_group_membership(guild,3,GroupMembershipKind::Leave));
        CHECK(w.change_group_membership(guild,4,GroupMembershipKind::Join,2));
        CHECK(w.active_social_group_members(guild)==(std::vector<std::uint64_t>{2,4}));
        CHECK(w.recent_social_groups()[0].members==(std::vector<std::uint64_t>{2,4}));
        CHECK(w.recent_knowledge().empty()); // social join/exit grants NO incident awareness
        CHECK(w.submit_testing_crew_loss(ship,1,200));w.step();
        CHECK(w.record_local_observation("death:200:1",2));
        CHECK(!w.change_group_membership(guild,1,GroupMembershipKind::Join,2));
        CHECK(!w.relay_group_claim(guild,1,3,ClaimFidelity::Faithful,-5,50,"Former member excluded"));
        CHECK(w.relay_group_claim(guild,1,4,ClaimFidelity::Faithful,-5,50,"A real casualty"));
        CHECK(w.recent_knowledge().size()==2);
        CHECK(w.record_group_view(guild,2,GroupStance::Mixed,50,"Our view is unsettled"));
        CHECK(w.change_group_membership(guild,4,GroupMembershipKind::Leave));
        CHECK(!w.relay_group_claim(guild,2,2,ClaimFidelity::Faithful,-5,50,"Former member excluded"));
        CHECK(!w.record_group_view(guild,2,GroupStance::Mixed,20,"No longer eligible"));
        CHECK(w.change_group_membership(guild,4,GroupMembershipKind::Join,2));
        CHECK(!w.relay_group_claim(guild,2,3,ClaimFidelity::Faithful,10,40,"Still not a member"));
        CHECK(w.change_group_membership(guild,3,GroupMembershipKind::Join,4));
        CHECK(w.relay_group_claim(guild,2,3,ClaimFidelity::Contrary,30,40,"Conflicting account"));
        CHECK(w.record_group_view(guild,3,GroupStance::Admiring,40,"Another viewpoint"));
        CHECK(w.recent_group_membership().size()==5);
        CHECK(w.recent_group_membership()[0].epoch==5);
        CHECK(w.recent_social_groups()[0].members==(std::vector<std::uint64_t>{2,3,4}));
        CHECK(w.recent_physical_incidents().size()==1);
        CHECK(w.world().colony_ship(ship)->crew==23);
        w.steps(5);
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& file:{db,copy}){
        PersistentWorld w(file.string(),Simulation(199));
        CHECK(w.recent_group_membership().size()==5);
        CHECK(w.recent_knowledge().size()==3);
        CHECK(w.recent_group_views().size()==2);
        CHECK(w.active_social_group_members(1)==(std::vector<std::uint64_t>{2,3,4}));
        CHECK(w.world().colony_ship(ship)->crew==23);
    }
    for(const auto& [name,attack]:std::vector<std::pair<std::string,std::string>>{
        {"changed_epoch","DROP TRIGGER membership_no_update; UPDATE group_membership_events SET epoch=30 WHERE id=2;"},
        {"changed_sponsor","DROP TRIGGER membership_no_update; UPDATE group_membership_events SET sponsor_id='25' WHERE id=2;"},
        {"deleted_leaf","DROP TRIGGER membership_no_delete; DELETE FROM group_membership_events WHERE id=5;"},
        {"deleted_middle","DROP TRIGGER membership_no_delete; DELETE FROM group_membership_events WHERE id=3;"},
        {"forged_relay_epoch","DROP TRIGGER group_relays_no_update; UPDATE group_relays SET membership_seq=0 WHERE id=1;"}
    }){
        const auto tampered=root/(name+".sqlite");fs::copy_file(db,tampered);
        sql(tampered,attack);CHECK(refuses(tampered));
    }
}
static void legacy_v9_migration(const fs::path& root){
    const auto db=root/"legacy.sqlite";
    Simulation sim(30);const auto ship=sim.add_colony_ship(7,{0,0},100,24);
    sim.enable_individual_workforce();
    {
        PersistentWorld w(db.string(),std::move(sim));
        CHECK(w.found_social_group("Old guild",{2,3})==1);
        CHECK(w.submit_testing_crew_loss(ship,1,919));w.step();
        CHECK(w.record_local_observation("death:919:1",2));
        CHECK(w.relay_group_claim(1,1,3,ClaimFidelity::Faithful,-10,80,"V9 testimony"));
        CHECK(w.record_group_view(1,2,GroupStance::Condemning,60,"V9 institutional interpretation"));
    }
    // Emulate actual v9: no membership tables or epoch column.
    sql(db,"DROP TABLE group_membership_events; DROP TABLE membership_meta; "
       "CREATE TABLE original_group_relays AS SELECT id,group_id,knowledge_id,digest FROM group_relays; "
       "DROP TRIGGER group_relays_no_update; DROP TRIGGER group_relays_no_delete; "
       "DROP TABLE group_relays; "
       "CREATE TABLE group_relays(id INTEGER PRIMARY KEY,group_id INTEGER NOT NULL REFERENCES social_groups(id),"
       "knowledge_id INTEGER NOT NULL UNIQUE REFERENCES npc_knowledge(id),digest TEXT NOT NULL); "
       "INSERT INTO group_relays SELECT * FROM original_group_relays; DROP TABLE original_group_relays; "
       "CREATE TRIGGER group_relays_no_update BEFORE UPDATE ON group_relays "
       "BEGIN SELECT RAISE(ABORT,'social history immutable'); END; "
       "CREATE TRIGGER group_relays_no_delete BEFORE DELETE ON group_relays "
       "BEGIN SELECT RAISE(ABORT,'social history immutable'); END; "
       "PRAGMA user_version=9;");
    {
        PersistentWorld w(db.string(),Simulation(42));
        CHECK(w.recent_group_membership().empty());
        CHECK(w.active_social_group_members(1)==(std::vector<std::uint64_t>{2,3}));
        CHECK(w.recent_knowledge().size()==2);
        CHECK(w.recent_group_views().size()==1);
        CHECK(w.change_group_membership(1,4,GroupMembershipKind::Join,2));
        CHECK(w.relay_group_claim(1,2,4,ClaimFidelity::Embellished,-10,70,"V10 testimony"));
    }
    PersistentWorld w(db.string(),Simulation(40));
    CHECK(w.recent_group_membership().size()==1);
    CHECK(w.recent_knowledge().size()==3);
    CHECK(w.recent_group_views().size()==1);
    CHECK(w.world().colony_ship(ship)->crew==23);
    CHECK(w.active_social_group_members(1)==(std::vector<std::uint64_t>{2,3,4}));
}
int main(){Temp tmp;try{
    membership_and_knowledge(tmp.path);
    std::cout<<"PASS founder epochs, join/leave/rejoin, receipts, relays, recovery, corruption\n";
    legacy_v9_migration(tmp.path);
    std::cout<<"PASS non-inventing v9 migration\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM-03c FAILED: "<<e.what()<<'\n';return 1;}}
