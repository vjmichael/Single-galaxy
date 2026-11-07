#pragma once
// SHIP-STATUS-07I: owner-bound, crash-durable original-world security notices.
// HailJournal inserts these in the SAME SQLite transaction as original
// committed call/reply/harassment evidence. At-least-once delivery: an SSE
// emission is not acknowledgement; only the authenticated native ACK is.
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::ship_notifications {
struct Notice {
    std::uint64_t sequence=0,source_event=0,peer_ship=0,peer_owner=0;
    std::uint64_t related=0,world_tick=0;
    std::uint32_t kind=0;
};
class Outbox {
    sqlite3* db_=nullptr;
    static void sql(sqlite3* db,const char* statement){
        char* error=nullptr;
        if(sqlite3_exec(db,statement,nullptr,nullptr,&error)!=SQLITE_OK){
            const std::string why=error?error:"outbox SQLite failure";
            sqlite3_free(error);throw std::runtime_error(why);
        }
    }
    static sqlite3_int64 num(std::uint64_t n){
        if(n>std::uint64_t(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("security notification exceeds signed SQLite");
        return static_cast<sqlite3_int64>(n);
    }
    static std::uint64_t val(sqlite3_stmt* s,int n){
        const auto v=sqlite3_column_int64(s,n);
        if(v<0)throw std::runtime_error("negative outbox identity");
        return std::uint64_t(v);
    }
    struct Statement{
        sqlite3_stmt* ptr=nullptr;
        Statement(sqlite3* db,const char* query){
            if(sqlite3_prepare_v2(db,query,-1,&ptr,nullptr)!=SQLITE_OK)
                throw std::runtime_error("outbox SQL prepare failed");
        }
        ~Statement(){sqlite3_finalize(ptr);}
        Statement(const Statement&)=delete;
        Statement& operator=(const Statement&)=delete;
    };
    static void insert(sqlite3* db,std::uint64_t owner,
                       std::uint32_t kind,std::uint64_t event,
                       std::uint64_t peer_ship,std::uint64_t peer_owner,
                       std::uint64_t related,std::uint64_t tick){
        if(!owner || !event || !peer_ship || kind<1 || kind>7)
            throw std::invalid_argument("outbox requires original owner, hull and event");
        Statement s(db,"INSERT INTO sg_ship_notifications07i "
            "(owner,kind,source_event,peer_ship,peer_owner,related,world_tick) "
            "VALUES(?,?,?,?,?,?,?)");
        const std::uint64_t values[]{
            owner,kind,event,peer_ship,peer_owner,related,tick};
        for(int i=0;i<7;++i)
            if(sqlite3_bind_int64(s.ptr,i+1,num(values[i]))!=SQLITE_OK)
                throw std::runtime_error("outbox bind failed");
        if(sqlite3_step(s.ptr)!=SQLITE_DONE)
            throw std::runtime_error("durable owner notification insert failed");
    }
public:
    // Safe to call during an original security-journal connection's setup.
    // Append operations must run inside that journal's BEGIN/COMMIT.
    static void ensure_schema(sqlite3* db){
        sql(db,"CREATE TABLE IF NOT EXISTS sg_ship_notifications07i ("
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
        sql(db,"CREATE INDEX IF NOT EXISTS sg_ship_notifications07i_pending "
            "ON sg_ship_notifications07i(owner,acked,seq)");
        sql(db,"CREATE TRIGGER IF NOT EXISTS sg_ship_notifications07i_immutable "
            "BEFORE UPDATE ON sg_ship_notifications07i WHEN "
            "NEW.seq!=OLD.seq OR NEW.owner!=OLD.owner OR "
            "NEW.kind!=OLD.kind OR NEW.source_event!=OLD.source_event OR "
            "NEW.peer_ship!=OLD.peer_ship OR NEW.peer_owner!=OLD.peer_owner OR "
            "NEW.related!=OLD.related OR NEW.world_tick!=OLD.world_tick OR "
            "NEW.acked!=1 OR OLD.acked!=0 "
            "BEGIN SELECT RAISE(ABORT,'immutable owner notification'); END");
        sql(db,"CREATE TRIGGER IF NOT EXISTS sg_ship_notifications07i_no_delete "
            "BEFORE DELETE ON sg_ship_notifications07i "
            "BEGIN SELECT RAISE(ABORT,'durable notification cannot be deleted'); END");
    }
    static void record_comms(sqlite3* db,int original_kind,
        std::uint64_t caller,std::uint64_t recipient,
        std::uint64_t event,std::uint64_t caller_ship,
        std::uint64_t recipient_ship,std::uint64_t identified_controller,
        std::uint64_t related,std::uint64_t tick){
        if(!caller || !recipient || caller==recipient)
            throw std::invalid_argument("outbox original owners missing or identical");
        // A declined or unreturned hail never reveals a recipient's native
        // owner to the caller, despite the internal durable delivery address.
        switch(original_kind){
        case 1: // first physical call
            insert(db,caller,1,event,recipient_ship,0,related,tick);
            insert(db,recipient,2,event,caller_ship,caller,related,tick);
            break;
        case 2: case 3: // Identify / Decline
            insert(db,recipient,3,event,caller_ship,caller,related,tick);
            insert(db,caller,4,event,recipient_ship,
                original_kind==2?identified_controller:0,related,tick);
            break;
        case 4: // blocked nuisance attempt (including compact strikes)
            insert(db,caller,5,event,recipient_ship,0,related,tick);
            insert(db,recipient,6,event,caller_ship,caller,related,tick);
            break;
        default:throw std::invalid_argument("unknown original communications event");
        }
    }
    // The independently authorized agency uses the SAME transaction as its
    // original verified referral. It never delivers a police warrant.
    static void record_agency(sqlite3* db,std::uint64_t owner,
        std::uint64_t referral,std::uint64_t colony,
        std::uint64_t complaint,std::uint64_t tick){
        insert(db,owner,7,referral,colony,0,complaint,tick);
    }
    explicit Outbox(const std::string& existing_world){
        try{
            if(sqlite3_open_v2(existing_world.c_str(),&db_,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
                throw std::runtime_error("outbox requires existing original SQLite");
            sqlite3_busy_timeout(db_,10000);
            {
                Statement check(db_,
                    "SELECT 1 FROM sqlite_master WHERE "
                    "type='table' AND name='snapshots'");
                if(sqlite3_step(check.ptr)!=SQLITE_ROW)
                    throw std::runtime_error("outbox refuses independent fake world");
            }
            ensure_schema(db_);
        }catch(...){if(db_)sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~Outbox(){if(db_)sqlite3_close(db_);}
    Outbox(const Outbox&)=delete;
    Outbox& operator=(const Outbox&)=delete;
    std::vector<Notice> pending(std::uint64_t owner,
                                std::size_t limit=32) const{
        if(!owner || limit==0 || limit>64)
            throw std::invalid_argument("invalid owner notification request");
        Statement q(db_,"SELECT seq,kind,source_event,peer_ship,peer_owner,"
            "related,world_tick FROM sg_ship_notifications07i "
            "WHERE owner=? AND acked=0 ORDER BY seq LIMIT ?");
        sqlite3_bind_int64(q.ptr,1,num(owner));
        sqlite3_bind_int64(q.ptr,2,static_cast<sqlite3_int64>(limit));
        std::vector<Notice> result;
        while(true){
            const int step=sqlite3_step(q.ptr);
            if(step==SQLITE_DONE)break;
            if(step!=SQLITE_ROW)throw std::runtime_error("outbox replay failed");
            Notice row{};
            row.sequence=val(q.ptr,0);row.kind=static_cast<std::uint32_t>(val(q.ptr,1));
            row.source_event=val(q.ptr,2);row.peer_ship=val(q.ptr,3);
            row.peer_owner=val(q.ptr,4);row.related=val(q.ptr,5);
            row.world_tick=val(q.ptr,6);
            if(!row.sequence || row.kind<1 || row.kind>7 ||
               !row.source_event || !row.peer_ship)
                throw std::runtime_error("noncanonical durable notification");
            result.push_back(row);
        }
        return result;
    }
    bool acknowledge(std::uint64_t owner,std::uint64_t sequence){
        if(!owner || !sequence)return false;
        sql(db_,"BEGIN IMMEDIATE");
        try{
            int previous=-1;
            {
                Statement lookup(db_,"SELECT acked FROM sg_ship_notifications07i "
                    "WHERE seq=? AND owner=?");
                sqlite3_bind_int64(lookup.ptr,1,num(sequence));
                sqlite3_bind_int64(lookup.ptr,2,num(owner));
                const auto step=sqlite3_step(lookup.ptr);
                if(step==SQLITE_ROW)
                    previous=sqlite3_column_int(lookup.ptr,0);
                else if(step!=SQLITE_DONE)
                    throw std::runtime_error("outbox ACK lookup failed");
            }
            if(previous<0){
                sql(db_,"ROLLBACK");return false;
            }
            if(previous==0){
                Statement change(db_,"UPDATE sg_ship_notifications07i "
                    "SET acked=1 WHERE seq=? AND owner=? AND acked=0");
                sqlite3_bind_int64(change.ptr,1,num(sequence));
                sqlite3_bind_int64(change.ptr,2,num(owner));
                if(sqlite3_step(change.ptr)!=SQLITE_DONE ||
                    sqlite3_changes(db_)!=1)
                    throw std::runtime_error("outbox acknowledgement CAS failed");
            }else if(previous!=1)
                throw std::runtime_error("invalid original ACK state");
            sql(db_,"COMMIT");return true; // idempotent same-owner ACK
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;
        }
    }
};
} // namespace galaxy::ship_notifications
