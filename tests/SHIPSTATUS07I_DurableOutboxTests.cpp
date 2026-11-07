#include "galaxy/ShipHailJournal.hpp"
#include "galaxy/ShipNotificationOutbox.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static void execute(const std::string& db,const char* sql){
    sqlite3* native=nullptr;
    CHECK(sqlite3_open(db.c_str(),&native)==SQLITE_OK);
    char* error=nullptr;
    const auto rc=sqlite3_exec(native,sql,nullptr,nullptr,&error);
    if(error)sqlite3_free(error);
    sqlite3_close(native);
    CHECK(rc==SQLITE_OK);
}
int main(){
 const auto root=fs::temp_directory_path()/(
    "sg_status07i_"+std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(root);
    const auto db=(root/"world.sqlite").string();
    const auto copied=(root/"backup.sqlite").string();
    EntityId a=0,b=0;
    std::string canonical;
    std::uint64_t late_sequence=0;
    {
        Simulation initial(707100);
        a=initial.add_colony_ship(7,{0,0},100,6);
        b=initial.add_colony_ship(8,{800,0},100,6);
        for(const auto ship:{a,b})
            initial.provision_colony_genesis(
                ship,2000,2000,2000,2000,2000);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_passenger_life_support();
        PersistentWorld world(db,std::move(initial));
        colony_disposition::TerritoryJournal territory(db);
        ship_comms::Journal calls(db);
        ship_notifications::Outbox notice(db);
        CHECK(notice.pending(7).empty()&&notice.pending(8).empty());
        CHECK(!notice.acknowledge(7,999999));
        // Inject a transactional outbox failure AFTER the original hail
        // receipt insert. SQLite must roll back BOTH records atomically.
        execute(db,"CREATE TRIGGER sg_status07i_block BEFORE INSERT ON "
            "sg_ship_notifications07i WHEN NEW.owner=8 "
            "BEGIN SELECT RAISE(ABORT,'injected outbox failure'); END");
        bool denied=false;
        try{(void)calls.call(world,territory,7,a,b,9001);}
        catch(const std::exception&){denied=true;}
        CHECK(denied);
        CHECK(calls.history().empty());
        CHECK(notice.pending(7).empty()&&notice.pending(8).empty());
        execute(db,"DROP TRIGGER sg_status07i_block");
        CHECK(calls.call(world,territory,7,a,b,9001).status==
            ship_comms::Status::CallSent);
        CHECK(calls.history().size()==1);
        const auto out=notice.pending(7),in=notice.pending(8);
        CHECK(out.size()==1&&in.size()==1);
        CHECK(out.front().kind==1 && out.front().peer_ship==b &&
            out.front().peer_owner==0);
        CHECK(in.front().kind==2 && in.front().peer_ship==a &&
            in.front().peer_owner==7);
        CHECK(!notice.acknowledge(7,in.front().sequence));
        CHECK(notice.acknowledge(8,in.front().sequence));
        CHECK(notice.acknowledge(8,in.front().sequence));
        CHECK(notice.pending(8).empty());
        CHECK(notice.pending(7).size()==1);
        CHECK(calls.reply(world,8,b,9001,9002,false).status==
            ship_comms::Status::Declined);
        const auto caller=notice.pending(7);
        CHECK(caller.size()==2);
        const auto reply=std::find_if(caller.begin(),caller.end(),
            [](const auto& n){return n.kind==4;});
        CHECK(reply!=caller.end() && reply->peer_owner==0 &&
            reply->related==9001); // Decline NEVER reveals owner 8.
        const auto recipient=notice.pending(8);
        CHECK(recipient.size()==1 && recipient.front().kind==3 &&
              recipient.front().peer_owner==7);
        late_sequence=reply->sequence;
        // Independent clients may display and ACK in any order; neither
        // account may revoke someone else's record.
        CHECK(!notice.acknowledge(8,late_sequence));
        canonical=world.world().canonical_state();
        PersistentWorld::recover_copy(db,copied);
    }
    {
        PersistentWorld w(copied,Simulation(0));
        ship_comms::Journal calls(copied);
        ship_notifications::Outbox notice(copied);
        CHECK(w.world().canonical_state()==canonical);
        CHECK(calls.history().size()==2);
        CHECK(notice.pending(7).size()==2);
        CHECK(notice.pending(8).size()==1);
        CHECK(notice.pending(7).back().sequence==late_sequence);
        CHECK(notice.acknowledge(7,late_sequence));
        CHECK(notice.pending(7).size()==1);
        CHECK(!notice.acknowledge(9,late_sequence));
        // SQL tampering cannot make a committed owner notification change
        // principal, peer ID, original event, or acknowledgement state.
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(copied.c_str(),&raw)==SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "UPDATE sg_ship_notifications07i SET owner=9 WHERE seq=1",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "DELETE FROM sg_ship_notifications07i",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        sqlite3_close(raw);
    }
    // STATUS-07K migration: a genuine old CHECK(kind BETWEEN 1 AND 7)
    // table must retain immutable sequences, both ACK states, and future
    // ID allocation while admitting the new original territorial family.
    {
        const auto older=(root/"legacy.sqlite").string();
        Simulation initial(707101);
        const auto home=initial.add_colony_ship(7,{0,0},100,6);
        initial.provision_colony_genesis(home,2000,2000,2000,2000,2000);
        PersistentWorld w(older,std::move(initial));
        execute(older,"CREATE TABLE sg_ship_notifications07i ("
            "seq INTEGER PRIMARY KEY AUTOINCREMENT,"
            "owner INTEGER NOT NULL CHECK(owner>0),"
            "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 7),"
            "source_event INTEGER NOT NULL CHECK(source_event>0),"
            "peer_ship INTEGER NOT NULL CHECK(peer_ship>0),"
            "peer_owner INTEGER NOT NULL CHECK(peer_owner>=0),"
            "related INTEGER NOT NULL CHECK(related>=0),"
            "world_tick INTEGER NOT NULL CHECK(world_tick>=0),"
            "acked INTEGER NOT NULL DEFAULT 0 CHECK(acked IN (0,1)),"
            "UNIQUE(owner,source_event,kind))");
        execute(older,"INSERT INTO sg_ship_notifications07i "
            "(seq,owner,kind,source_event,peer_ship,peer_owner,related,"
            "world_tick,acked) VALUES "
            "(9,7,1,900,1,0,0,0,0),(10,7,1,901,1,0,0,0,1)");
        ship_notifications::Outbox migrated(older);
        const auto oldRows=migrated.pending(7);
        CHECK(oldRows.size()==1 && oldRows.front().sequence==9);
        CHECK(migrated.acknowledge(7,9));
        colony_disposition::TerritoryJournal territory(older);
        CHECK(territory.claim(w,7,home,1500,3000));
        const auto current=migrated.pending(7);
        CHECK(current.size()==1 && current.front().kind==8 &&
              current.front().source_event==1500 &&
              current.front().sequence>10);
        CHECK(migrated.acknowledge(7,current.front().sequence));
        CHECK(migrated.pending(7).empty());
    }
    fs::remove_all(root);
    std::cout<<"PASS STATUS-07I: original hail+outbox atomic rollback, "
        "owner privacy, at-least-once replay, owner ACK and canonical SQLite recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL STATUS-07I "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(root,ignored);return 1;
 }
}
