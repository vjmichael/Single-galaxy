#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <sqlite3.h>
#include <chrono>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string("assertion failed: ")+ #x +" line="+std::to_string(__LINE__));}while(0)
struct Tmp {
    fs::path directory, db;
    Tmp() {directory=fs::temp_directory_path()/("sg_b53_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(directory);db=directory/"world.sqlite";}
    ~Tmp(){std::error_code ec;fs::remove_all(directory,ec);}
};
struct IDs {EntityId a=0, ship=0, rock=0, b=0;};
Simulation initial(IDs& ids, int bankB=1000) {
    Simulation s(456);
    ids.a=s.add_station({0,0});
    ids.ship=s.spawn_ship_docked(7,ids.a,20,100);
    ids.rock=s.add_asteroid({2500,700},40);
    ids.b=s.add_station({12000,0});
    s.configure_market(ids.a,2,1000);
    s.configure_market(ids.b,5,bankB);
    return s;
}
std::uint64_t commandId=100;
Event invoke(PersistentWorld& world, IDs ids, Action action, EntityId target=0, std::int64_t amount=0, int thrust=0, PlayerId issuer=7) {
    Command c;
    c.id=++commandId;c.sequence=c.id;c.execute_tick=world.world().tick()+1;c.actor=ids.ship;
    c.issuer=issuer;c.target=target;c.action=action;c.amount=amount;c.thrust_x=thrust;
    CHECK(world.submit(c));world.step();
    CHECK(world.world().events().back().command_id==c.id);
    return world.world().events().back();
}
void transition(PersistentWorld& w) {w.steps(2);}
void mining_to_hangar(PersistentWorld& w, IDs ids) {
    CHECK(invoke(w,ids,Action::RequestUndock).result==Outcome::Applied);
    transition(w);
    CHECK(invoke(w,ids,Action::StartMining,ids.rock,4).result==Outcome::Applied);
    w.steps(4);
    CHECK(w.world().find(ids.ship)->cargo==20);
    CHECK(w.world().find(ids.rock)->ore==20);
    CHECK(invoke(w,ids,Action::RequestDock,ids.a).result==Outcome::Applied);
    transition(w);
    CHECK(invoke(w,ids,Action::UnloadOre,ids.a,100).transferred==20);
    CHECK(w.world().hangar(ids.a,7)->ore==20);
    CHECK(w.world().find(ids.ship)->cargo==0);
}
void manual_to_B(PersistentWorld& w, IDs ids) {
    CHECK(invoke(w,ids,Action::RequestUndock).result==Outcome::Applied);transition(w);
    for(int i=0;i<6;++i) CHECK(invoke(w,ids,Action::Thrust,0,0,1).result==Outcome::Applied);
    for(int i=0;i<6;++i) CHECK(invoke(w,ids,Action::Thrust,0,0,-1).result==Outcome::Applied);
    CHECK(w.world().find(ids.ship)->velocity.x==0);
    CHECK(w.world().find(ids.ship)->position.x==10500);
}
void two_station_journey() {
    Tmp db;IDs ids;
    PersistentWorld w(db.db.string(),initial(ids));
    const auto initialCredits=w.world().total_credits(), initialOre=w.world().total_ore();
    CHECK(initialCredits==2000 && initialOre==40);
    CHECK(w.world().market(ids.a)->price_per_ore==2);
    CHECK(w.world().market(ids.b)->price_per_ore==5);
    CHECK(!w.world().hangar(ids.b,7));
    // No selling cargo directly, or loading while docked at the wrong station.
    CHECK(invoke(w,ids,Action::LoadOre,ids.b,5).result==Outcome::InvalidTarget);
    CHECK(invoke(w,ids,Action::LoadOre,ids.a,5).result==Outcome::NoResources);
    mining_to_hangar(w,ids);
    CHECK(invoke(w,ids,Action::LoadOre,ids.a,100).transferred==20);
    CHECK(w.world().hangar(ids.a,7)->ore==0);
    CHECK(w.world().find(ids.ship)->cargo==20);
    const auto original=w.world().canonical_state();
    {
        PersistentWorld restored(db.db.string(),Simulation(1));
        CHECK(restored.world().canonical_state()==original);
    }
    manual_to_B(w,ids);
    CHECK(invoke(w,ids,Action::LoadOre,ids.a,1).result==Outcome::NotDocked);
    CHECK(invoke(w,ids,Action::RequestDock,ids.b).result==Outcome::Applied);
    transition(w);
    CHECK(w.world().hangar(ids.b,7)->ore==0);
    CHECK(invoke(w,ids,Action::UnloadOre,ids.b,100).transferred==20);
    CHECK(invoke(w,ids,Action::SellOre,ids.a,20).result==Outcome::InvalidTarget);
    CHECK(invoke(w,ids,Action::SellOre,ids.b,20,0,99).result==Outcome::Unauthorized);
    CHECK(invoke(w,ids,Action::SellOre,ids.b,-1).result==Outcome::InvalidArgument);
    CHECK(invoke(w,ids,Action::SellOre,ids.b,20).transferred==20);
    CHECK(w.world().wallet(7)==100);
    CHECK(w.world().market(ids.b)->ore==20);
    CHECK(w.world().market(ids.b)->credits==900);
    CHECK(w.world().market(ids.a)->ore==0);
    CHECK(w.world().hangar(ids.a,7)->ore==0 && w.world().hangar(ids.b,7)->ore==0);
    CHECK(w.world().total_ore()==initialOre && w.world().total_credits()==initialCredits);
    CHECK(invoke(w,ids,Action::SellOre,ids.b,10).result==Outcome::NoResources);
    PersistentWorld reopened(db.db.string(),Simulation(10));
    CHECK(reopened.world().canonical_state()==w.world().canonical_state());
    ServerViewBridge bridge(w.world(),98);
    ClientView client;
    auto frame=decode_frame(encode_frame(bridge.snapshot()));
    CHECK(client.apply(frame)==ApplyResult::Applied);
    CHECK(client.world().wallets.at(7)==100 && client.world().markets.at(ids.b).ore==20);
}
void partial_and_insufficient_funds() {
    Tmp db;IDs ids;
    PersistentWorld w(db.db.string(),initial(ids,30));
    mining_to_hangar(w,ids);
    CHECK(invoke(w,ids,Action::SellOre,ids.a,5).transferred==5);
    CHECK(w.world().wallet(7)==10 && w.world().market(ids.a)->ore==5);
    CHECK(invoke(w,ids,Action::LoadOre,ids.a,15).transferred==15);
    manual_to_B(w,ids);
    CHECK(invoke(w,ids,Action::RequestDock,ids.b).result==Outcome::Applied);
    transition(w);
    CHECK(invoke(w,ids,Action::UnloadOre,ids.b,15).transferred==15);
    auto sold=invoke(w,ids,Action::SellOre,ids.b,15);
    CHECK(sold.result==Outcome::Applied && sold.transferred==6);
    CHECK(w.world().wallet(7)==40);
    CHECK(w.world().market(ids.b)->credits==0 && w.world().market(ids.b)->ore==6);
    CHECK(w.world().hangar(ids.b,7)->ore==9);
    CHECK(invoke(w,ids,Action::SellOre,ids.b,3).result==Outcome::InsufficientFunds);
    CHECK(w.world().total_credits()==1030 && w.world().total_ore()==40);
    PersistentWorld again(db.db.string(),Simulation(1));
    CHECK(again.world().wallet(7)==40 && again.world().hangar(ids.b,7)->ore==9);
}
void failpoint_and_tamper() {
    Tmp db;IDs ids;
    PersistentWorld w(db.db.string(),initial(ids));
    mining_to_hangar(w,ids);
    auto before=w.world().canonical_state();auto gen=w.generation();
    Command c;
    c.id=++commandId;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ids.ship;c.target=ids.a;c.action=Action::SellOre;c.amount=6;
    CHECK(w.submit(c));
    auto pending=w.world().canonical_state();
    try{w.step(FailPoint::AfterReceiptWrite);throw std::runtime_error("expected failure");}
    catch(const std::exception& ex){CHECK(std::string(ex.what())!="expected failure");}
    CHECK(w.world().canonical_state()==pending);
    CHECK(w.world().wallet(7)==0);
    w.step();
    CHECK(w.world().wallet(7)==12 && w.world().market(ids.a)->credits==988);
    CHECK(w.generation()>gen && pending!=before);
    PersistentWorld restored(db.db.string(),Simulation(1));
    CHECK(restored.world().canonical_state()==w.world().canonical_state());
    sqlite3* handle=nullptr;
    CHECK(sqlite3_open(db.db.string().c_str(),&handle)==SQLITE_OK);
    char* msg=nullptr;
    CHECK(sqlite3_exec(handle,"UPDATE credit_ledger SET credit_amount=credit_amount+1",nullptr,nullptr,&msg)==SQLITE_OK);
    sqlite3_close(handle);
    bool rejected=false;
    try{PersistentWorld corrupt(db.db.string(),Simulation(1));}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
}
void recover_copy_after_sale() {
    Tmp db;IDs ids;std::string expected;
    const auto recovered=db.directory/"recovered.sqlite";
    {
        PersistentWorld w(db.db.string(),initial(ids));
        mining_to_hangar(w,ids);
        CHECK(invoke(w,ids,Action::SellOre,ids.a,10).transferred==10);
        CHECK(w.world().wallet(7)==20);
        w.step();w.step();
        expected=w.world().canonical_state();
    }
    sqlite3* handle=nullptr;
    CHECK(sqlite3_open(db.db.string().c_str(),&handle)==SQLITE_OK);
    CHECK(sqlite3_exec(handle,"UPDATE snapshots SET state=x'424144' WHERE generation=(SELECT MAX(generation) FROM snapshots)",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(handle);
    bool rejected=false;
    try{PersistentWorld corrupt(db.db.string(),Simulation(1));}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
    PersistentWorld::recover_copy(db.db.string(),recovered.string());
    PersistentWorld r(recovered.string(),Simulation(1));
    CHECK(r.world().canonical_state()==expected);
    CHECK(r.world().wallet(7)==20);
    CHECK(r.world().market(ids.a)->credits==980);
    CHECK(r.world().total_ore()==40 && r.world().total_credits()==2000);
    rejected=false;
    try{PersistentWorld original(db.db.string(),Simulation(1));}catch(const std::exception&){rejected=true;}
    CHECK(rejected); // recovery never repairs the original in place
}
void run(const char* name,const std::function<void()>& fn,int& good){try{fn();++good;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
int main(){int good=0;run("two_station_journey",two_station_journey,good);run("partial_funds_and_local_prices",partial_and_insufficient_funds,good);run("sale_rollback_and_credit_journal_tamper",failpoint_and_tamper,good);run("recover_copy_of_v4_credit_state",recover_copy_after_sale,good);std::cout<<good<<"/4 B5.3 acceptance cases passed\n";return good==4?0:1;}
