#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define REQUIRE(c) do {if(!(c))throw std::runtime_error(std::string("FAIL line ")+std::to_string(__LINE__)+" " #c);}while(false)
struct Temp {
    fs::path path;
    Temp(){ static int n=0;path=fs::temp_directory_path()/("b62b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++n)+".sqlite"); }
    ~Temp(){std::error_code ec;fs::remove(path,ec);fs::remove(path.string()+"-wal",ec);fs::remove(path.string()+"-shm",ec);}
};
struct IDs {EntityId a,ship,rock,gate,b,returngate,foreignrock;};
Simulation seed(IDs& ids){
    Simulation s(48151623);
    s.add_system(2);
    ids.a=s.add_station({0,0});
    ids.ship=s.spawn_ship_docked(7,ids.a,30,100);
    ids.rock=s.add_asteroid({2000,0},20);
    ids.gate=s.add_gate(1,{1500,0},2,{5000,0});
    ids.b=s.add_station_in_system(2,{17000,0});
    ids.returngate=s.add_gate(2,{17000,0},1,{1500,0});
    ids.foreignrock=s.add_asteroid_in_system(2,{1500,0},99);
    s.configure_market(ids.a,2,1000);s.configure_market(ids.b,5,1000);
    return s;
}
Event do_cmd(PersistentWorld& w, std::uint64_t id,EntityId ship,EntityId target,Action action,std::int64_t amount=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;c.amount=amount;
    REQUIRE(w.submit(c));w.step();return w.world().events().back();
}
void until_undocked(PersistentWorld& w,const IDs& ids){
    REQUIRE(do_cmd(w,1,ids.ship,ids.a,Action::RequestUndock).result==Outcome::Applied);
    w.steps(2);
    REQUIRE(w.world().find(ids.ship)->dock_state==DockState::InSpace);
}
void jump_with_restart(){
    Temp tmp;IDs ids{};
    {
        PersistentWorld w(tmp.path.string(),seed(ids));
        until_undocked(w,ids);
        auto bad=do_cmd(w,2,ids.ship,ids.foreignrock,Action::Mine,5);
        REQUIRE(bad.result==Outcome::InvalidTarget); // remote rock at same coordinates
        REQUIRE(do_cmd(w,3,ids.ship,ids.rock,Action::Mine,10).transferred==10);
        REQUIRE(do_cmd(w,4,ids.ship,ids.returngate,Action::RequestJump).result==Outcome::WrongSystem);
        REQUIRE(do_cmd(w,5,ids.ship,ids.gate,Action::RequestJump,0,8).result==Outcome::Unauthorized);
        REQUIRE(do_cmd(w,6,ids.ship,ids.gate,Action::RequestJump).result==Outcome::Applied);
        REQUIRE(w.world().find(ids.ship)->jump_state==ShipJumpState::Jumping);
        REQUIRE(w.world().find(ids.ship)->system_id==1);
        REQUIRE(w.world().find(ids.ship)->cargo==10);
        const auto origin=w.world().entity_ids_in_system(1);
        const auto frontier=w.world().entity_ids_in_system(2);
        REQUIRE(std::find(origin.begin(),origin.end(),ids.ship)!=origin.end());
        REQUIRE(std::find(frontier.begin(),frontier.end(),ids.ship)==frontier.end());
        REQUIRE(w.world().total_ore()==119);
    }
    {
        PersistentWorld w(tmp.path.string(),Simulation(1));
        REQUIRE(w.world().find(ids.ship)->jump_state==ShipJumpState::Jumping);
        Command duplicate{};duplicate.id=6;duplicate.issuer=7;duplicate.actor=ids.ship;
        duplicate.execute_tick=w.world().tick()+1;duplicate.action=Action::RequestJump;duplicate.target=ids.gate;
        REQUIRE(!w.submit(duplicate));
        const auto initial=w.world().canonical_state();
        bool failed=false;
        try{w.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){failed=true;}
        REQUIRE(failed && w.world().canonical_state()==initial);
        w.step();
        REQUIRE(w.world().find(ids.ship)->system_id==1);
        REQUIRE(w.world().find(ids.ship)->jump_state==ShipJumpState::Jumping);
    }
    {
        PersistentWorld w(tmp.path.string(),Simulation(1));
        w.step(); // completion: one entity moves from system 1 to system 2
        const auto* ship=w.world().find(ids.ship);
        REQUIRE(ship && ship->system_id==2 && ship->cargo==10);
        REQUIRE(ship->position.x==17000 && ship->velocity.x==0);
        const auto origin=w.world().entity_ids_in_system(1);
        const auto frontier=w.world().entity_ids_in_system(2);
        REQUIRE(std::find(origin.begin(),origin.end(),ids.ship)==origin.end());
        REQUIRE(std::count(frontier.begin(),frontier.end(),ids.ship)==1);
        REQUIRE(ship->jump_state==ShipJumpState::ArrivalCloak);
        REQUIRE(w.world().protected_from_attack(ids.ship));
        REQUIRE(do_cmd(w,7,ids.ship,ids.b,Action::RequestDock).result==Outcome::InTransition);
        REQUIRE(do_cmd(w,8,ids.ship,ids.rock,Action::Mine,2).result==Outcome::InTransition);
        REQUIRE(do_cmd(w,9,ids.ship,ids.gate,Action::RequestJump).result==Outcome::InTransition);
        w.steps(3); // cloak expires
        REQUIRE(!w.world().protected_from_attack(ids.ship));
        REQUIRE(do_cmd(w,10,ids.ship,ids.a,Action::RequestDock).result==Outcome::WrongSystem);
        REQUIRE(do_cmd(w,11,ids.ship,ids.b,Action::RequestDock).result==Outcome::Applied);
        w.steps(2);
        REQUIRE(w.world().find(ids.ship)->dock_state==DockState::Docked);
        REQUIRE(do_cmd(w,12,ids.ship,ids.b,Action::UnloadOre,10).transferred==10);
        REQUIRE(do_cmd(w,13,ids.ship,ids.b,Action::SellOre,10).transferred==10);
        REQUIRE(w.world().wallet(7)==50);
        REQUIRE(w.world().market(ids.b)->ore==10);
        REQUIRE(w.world().market(ids.b)->credits==950);
        REQUIRE(w.world().hangar(ids.a,7)->ore==0);
        REQUIRE(w.world().hangar(ids.b,7)->ore==0);
        REQUIRE(w.world().total_ore()==119);
        REQUIRE(w.world().total_credits()==2000);
        REQUIRE(w.world().entities().count(ids.ship)==1);
        // Roundtrip: return through another valid gate without duplicating cargo
        // or corrupting historical jump records.
        REQUIRE(do_cmd(w,14,ids.ship,ids.b,Action::RequestUndock).result==Outcome::Applied);
        w.steps(2);
        REQUIRE(do_cmd(w,15,ids.ship,ids.returngate,Action::RequestJump).result==Outcome::Applied);
        w.steps(2);
        REQUIRE(w.world().find(ids.ship)->system_id==1);
        REQUIRE(w.world().total_ore()==119);
        REQUIRE(w.world().total_credits()==2000);
    }
    PersistentWorld w(tmp.path.string(),Simulation(1));
    REQUIRE(w.world().find(ids.ship)->system_id==1);
    REQUIRE(w.world().find(ids.ship)->jump_state==ShipJumpState::ArrivalCloak);
    REQUIRE(w.world().wallet(7)==50);
    REQUIRE(w.world().total_ore()==119);
}
void tamper_detection(){
    Temp tmp;IDs ids{};
    {
        PersistentWorld w(tmp.path.string(),seed(ids));
        until_undocked(w,ids);
        REQUIRE(do_cmd(w,2,ids.ship,ids.gate,Action::RequestJump).result==Outcome::Applied);
        w.steps(2);
        REQUIRE(w.world().find(ids.ship)->system_id==2);
    }
    auto sql=[&](const char* statement){
        sqlite3* db=nullptr;
        REQUIRE(sqlite3_open(tmp.path.string().c_str(),&db)==SQLITE_OK);
        char* error=nullptr;const auto rc=sqlite3_exec(db,statement,nullptr,nullptr,&error);
        sqlite3_free(error);sqlite3_close(db);REQUIRE(rc==SQLITE_OK);
    };
    sql("UPDATE jump_ledger SET dest_system=1 WHERE command_id='2'");
    bool rejected=false;
    try{PersistentWorld w(tmp.path.string(),Simulation(1));}catch(...){rejected=true;}
    REQUIRE(rejected);
    sql("UPDATE jump_ledger SET dest_system=2 WHERE command_id='2'");
    {PersistentWorld w(tmp.path.string(),Simulation(1)); REQUIRE(w.world().find(ids.ship)->system_id==2);}
    sql("DELETE FROM jump_ledger WHERE command_id='2'");
    rejected=false;
    try{PersistentWorld w(tmp.path.string(),Simulation(1));}catch(...){rejected=true;}
    REQUIRE(rejected);
}
void recovery_copy_after_jump(){
    Temp tmp;IDs ids{};
    {
        PersistentWorld w(tmp.path.string(),seed(ids));
        until_undocked(w,ids);
        REQUIRE(do_cmd(w,2,ids.ship,ids.gate,Action::RequestJump).result==Outcome::Applied);
        w.steps(2);
        REQUIRE(w.world().find(ids.ship)->system_id==2);
    }
    const auto recovered=tmp.path.string()+".recovered";
    std::string last;
    {PersistentWorld w(tmp.path.string(),Simulation(1));last=w.world().canonical_state();}
    sqlite3* db=nullptr;REQUIRE(sqlite3_open(tmp.path.string().c_str(),&db)==SQLITE_OK);
    char* error=nullptr;
    const auto rc=sqlite3_exec(db,"UPDATE snapshots SET state=X'00' WHERE generation=(SELECT MAX(generation) FROM snapshots)",nullptr,nullptr,&error);
    sqlite3_free(error);sqlite3_close(db);REQUIRE(rc==SQLITE_OK);
    bool rejected=false;try{PersistentWorld w(tmp.path.string(),Simulation(1));}catch(...){rejected=true;}
    REQUIRE(rejected);
    PersistentWorld::recover_copy(tmp.path.string(),recovered);
    {PersistentWorld w(recovered,Simulation(1));REQUIRE(w.world().canonical_state()==last);}
    rejected=false;try{PersistentWorld w(tmp.path.string(),Simulation(1));}catch(...){rejected=true;}
    REQUIRE(rejected); // no accidental change to original
    std::error_code ec;fs::remove(recovered,ec);fs::remove(recovered+"-wal",ec);fs::remove(recovered+"-shm",ec);
}
void rollback_after_departure(){
    Temp tmp;IDs ids{};
    PersistentWorld w(tmp.path.string(),seed(ids));
    until_undocked(w,ids);
    Command c{};c.id=2;c.sequence=2;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ids.ship;c.target=ids.gate;c.action=Action::RequestJump;
    REQUIRE(w.submit(c));
    auto before=w.world().canonical_state();
    bool rejected=false;
    try{w.step(FailPoint::AfterReceiptWrite);}catch(...){rejected=true;}
    REQUIRE(rejected && w.world().canonical_state()==before);
    // An aborted departure must not leave a ghost pending ledger entry.
    {PersistentWorld r(tmp.path.string(),Simulation(7));
        REQUIRE(r.world().canonical_state()==before);r.step();
        REQUIRE(r.world().find(ids.ship)->jump_state==ShipJumpState::Jumping);
        r.steps(2);REQUIRE(r.world().find(ids.ship)->system_id==2);}
}
void snapshot_validation(){
    IDs ids{};auto s=seed(ids);
    REQUIRE(Simulation::from_snapshot(s.canonical_state()).canonical_state()==s.canonical_state());
    auto raw=s.canonical_state();
    auto corrupt=raw;
    const auto pos=corrupt.find("system|2\n");REQUIRE(pos!=std::string::npos);
    corrupt.replace(pos,9,"system|3\n");
    bool rejected=false;try{(void)Simulation::from_snapshot(corrupt);}catch(...){rejected=true;}
    REQUIRE(rejected);
    // A gate cannot be created targeting an unregistered system.
    rejected=false;try{s.add_gate(1,{0,0},999,{0,0});}catch(...){rejected=true;}
    REQUIRE(rejected);
}
int main(){try{snapshot_validation();jump_with_restart();tamper_detection();rollback_after_departure();recovery_copy_after_jump();std::cout<<"PASS B6.2b durable cross-system ship, cargo, station locality, reload, rollback, idempotency\n";return 0;}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
