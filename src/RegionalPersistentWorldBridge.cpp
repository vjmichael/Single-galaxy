#include "galaxy/RegionalPersistentWorldBridge.hpp"
#include <sqlite3.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::regional_events {
namespace {
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string msg=err?err:sqlite3_errmsg(db);
        sqlite3_free(err);
        throw std::runtime_error("live-world bridge SQL: "+msg);
    }
}
struct Stmt {
    sqlite3* db;
    sqlite3_stmt* q=nullptr;
    Stmt(sqlite3* d,const char* sql):db(d){
        if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("bridge query failed: ")+sqlite3_errmsg(db));
    }
    ~Stmt(){sqlite3_finalize(q);}
    void str(int i,const std::string& s){
        if(s.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())||
           sqlite3_bind_text(q,i,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
           throw std::runtime_error("bridge string bind failed");
    }
    void num(int i,std::uint64_t n){
        if(n>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())||
            sqlite3_bind_int64(q,i,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
            throw std::runtime_error("bridge uint64 over SQLite range");
    }
    void blob(int i,const std::string& s){
        if(s.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())||
           sqlite3_bind_blob(q,i,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
           throw std::runtime_error("bridge snapshot bind failed");
    }
    void done(){if(sqlite3_step(q)!=SQLITE_DONE)
        throw std::runtime_error(std::string("bridge SQL write: ")+sqlite3_errmsg(db));}
};
struct Connection {
    sqlite3* db=nullptr;
    explicit Connection(const std::string& file){
        if(sqlite3_open_v2(file.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const auto err=db?sqlite3_errmsg(db):"open failed";
            const std::string msg=err;
            sqlite3_close(db);db=nullptr;throw std::runtime_error("bridge open: "+msg);
        }
        sqlite3_busy_timeout(db,3000);
    }
    ~Connection(){sqlite3_close(db);}
};
struct WorldRow {std::uint64_t generation=0;std::string payload;};
WorldRow current_world(sqlite3* db){
    Stmt q(db,"SELECT generation,state FROM snapshots ORDER BY generation DESC LIMIT 1");
    if(sqlite3_step(q.q)!=SQLITE_ROW)throw std::runtime_error("PersistentWorld has no saved snapshot");
    const auto n=sqlite3_column_int64(q.q,0);
    const auto* p=static_cast<const char*>(sqlite3_column_blob(q.q,1));
    const auto len=sqlite3_column_bytes(q.q,1);
    if(n<=0||len<=0||!p||len>16*1024*1024)
        throw std::runtime_error("invalid physical world snapshot size/generation");
    return {static_cast<std::uint64_t>(n),std::string(p,static_cast<std::size_t>(len))};
}
void assert_real_world(sqlite3* db,const PersistentWorld& live){
    const auto saved=current_world(db);
    if(saved.generation!=live.generation()||saved.payload!=live.world().canonical_state())
        throw std::runtime_error("live PersistentWorld differs from same-DB durable source");
    const auto parsed=Simulation::from_snapshot(saved.payload);
    if(parsed.canonical_state()!=saved.payload || parsed.state_hash()!=live.world().state_hash() ||
       parsed.total_ore()!=live.world().total_ore()||parsed.total_credits()!=live.world().total_credits())
        throw std::runtime_error("real-world snapshot validation failed");
}
void empty_region(const Scheduler& scheduler){
    const auto c=scheduler.custody();
    if(scheduler.colony_count()||!scheduler.shipments().empty()||scheduler.pending_events()||
       !scheduler.pending_notices().empty()||c.food_kg||c.water_kg||c.oxygen_kg)
       throw std::runtime_error("physical world bridge cannot duplicate nonempty cold colonies, cargo or needs");
}
void with_rollback_abort(RegionalModeAuthority& authority, const RegionalAuthorityState& pending,
                         const std::string& handoff){
    try {authority.abort(handoff,pending.generation);}catch(...){}
}
} // namespace

PersistentWorldBridge::PersistentWorldBridge(const std::string& file,const std::string& scope,
                                             PersistentWorld& live)
 :path_(file),scope_(scope),live_(live),store_(file,scope),authority_(store_){
    if(file.empty()||scope.empty()||scope.size()>256)
        throw std::invalid_argument("bridge requires bounded region scope and database path");
}
RegionalAuthorityState PersistentWorldBridge::enroll(){
    // Fail closed for this world from the moment registration commits. A half
    // installed gate can be retried; this method never silently creates stock.
    if(!store_.load())store_.save(Scheduler{},0);
    auto cp=store_.load();
    if(!cp)throw std::runtime_error("missing cold checkpoint");
    empty_region(cp->scheduler);
    Connection c(path_);
    exec(c.db,"BEGIN IMMEDIATE");
    try{
        assert_real_world(c.db,live_);
        exec(c.db,"CREATE TABLE IF NOT EXISTS sg_world_region_gate ("
             "singleton INTEGER PRIMARY KEY CHECK(singleton=1), scope TEXT NOT NULL UNIQUE)");
        {
            Stmt q(c.db,"SELECT scope FROM sg_world_region_gate WHERE singleton=1");
            const int rc=sqlite3_step(q.q);
            if(rc==SQLITE_ROW){
                const auto* s=reinterpret_cast<const char*>(sqlite3_column_text(q.q,0));
                if(!s || scope_!=s)throw std::runtime_error("world already linked to a different region");
            }else if(rc==SQLITE_DONE){
                Stmt ins(c.db,"INSERT INTO sg_world_region_gate(singleton,scope) VALUES(1,?1)");
                ins.str(1,scope_);ins.done();
            }else throw std::runtime_error("world gate lookup failed");
        }
        exec(c.db,"CREATE TABLE IF NOT EXISTS sg_world_detail_anchor ("
             "scope TEXT PRIMARY KEY, handoff_id TEXT NOT NULL,"
             "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
             "world_snapshot BLOB NOT NULL)");
        // No guard for player non-snapshot metadata yet; every physical game
        // command & tick publishes an authoritative snapshot via Store::commit.
        exec(c.db,"CREATE TRIGGER IF NOT EXISTS sg_world_snapshot_authority "
             "BEFORE INSERT ON snapshots WHEN EXISTS ("
             "SELECT 1 FROM sg_world_region_gate g LEFT JOIN sg_regional_mode_authority a "
             "ON a.scope=g.scope WHERE a.scope IS NULL OR a.owner!=1 OR a.phase!=0) "
             "BEGIN SELECT RAISE(ABORT,'world not owned by detailed simulation'); END");
        exec(c.db,"COMMIT");
    }catch(...){try{exec(c.db,"ROLLBACK");}catch(...){}throw;}
    return authority_.enroll();
}
RegionalAuthorityState PersistentWorldBridge::abort_pending(const std::string& handoff,
                                                      std::uint64_t expected_generation){
    return authority_.abort(handoff,expected_generation);
}
RegionalAuthorityState PersistentWorldBridge::promote(const std::string& handoff){
    const auto old=authority_.state();
    if(old.owner!=SimulationOwner::Cold||old.phase!=TransitionPhase::Stable)
        throw std::runtime_error("promotion requires stable cold ownership");
    const auto cp=store_.load();
    if(!cp||cp->revision!=old.checkpoint_revision)throw std::runtime_error("stale cold checkpoint");
    empty_region(cp->scheduler);
    const auto pending=authority_.prepare(handoff,SimulationOwner::Detailed,
                        old.generation,cp->revision,cp->scheduler.tick());
    try{
        return authority_.commit(handoff,pending.generation,cp->revision,
            [&](sqlite3* db,Scheduler& scheduler){
                empty_region(scheduler);
                assert_real_world(db,live_);
                const auto actual=current_world(db);
                Stmt q(db,"INSERT INTO sg_world_detail_anchor "
                     "(scope,handoff_id,world_generation,world_snapshot) VALUES(?1,?2,?3,?4)");
                q.str(1,scope_);q.str(2,handoff);q.num(3,actual.generation);
                q.blob(4,actual.payload);q.done();
            });
    }catch(...){with_rollback_abort(authority_,pending,handoff);throw;}
}
RegionalAuthorityState PersistentWorldBridge::demote_if_unchanged(const std::string& handoff){
    const auto old=authority_.state();
    if(old.owner!=SimulationOwner::Detailed||old.phase!=TransitionPhase::Stable)
        throw std::runtime_error("demotion requires stable detailed ownership");
    const auto cp=store_.load();
    if(!cp||cp->revision!=old.checkpoint_revision)throw std::runtime_error("stale regional checkpoint");
    empty_region(cp->scheduler);
    const auto pending=authority_.prepare(handoff,SimulationOwner::Cold,
                          old.generation,cp->revision,cp->scheduler.tick());
    try{
        return authority_.commit(handoff,pending.generation,cp->revision,
            [&](sqlite3* db,Scheduler& scheduler){
                empty_region(scheduler);
                assert_real_world(db,live_);
                Stmt read(db,"SELECT world_generation,world_snapshot FROM sg_world_detail_anchor "
                             "WHERE scope=?1");read.str(1,scope_);
                if(sqlite3_step(read.q)!=SQLITE_ROW)
                    throw std::runtime_error("no durable detailed snapshot anchor");
                const auto gen=sqlite3_column_int64(read.q,0);
                const auto len=sqlite3_column_bytes(read.q,1);
                const auto* p=static_cast<const char*>(sqlite3_column_blob(read.q,1));
                const auto now=current_world(db);
                if(gen<=0||!p||len<=0||static_cast<std::size_t>(len)!=now.payload.size()||
                   now.generation!=static_cast<std::uint64_t>(gen)||
                   now.payload.compare(0,now.payload.size(),p,static_cast<std::size_t>(len))!=0)
                    throw std::runtime_error("detailed world changed; authoritative material/effect reconciliation required");
                Stmt erase(db,"DELETE FROM sg_world_detail_anchor WHERE scope=?1");
                erase.str(1,scope_);erase.done();
            });
    }catch(...){with_rollback_abort(authority_,pending,handoff);throw;}
}
} // namespace galaxy::regional_events
