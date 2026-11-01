#include "galaxy/RegionalPersistentWorldBridge.hpp"
#include <sqlite3.h>
#include <limits>
#include <cstdint>
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

// This is a *parking* reconciliation, not a live cold-world production model.
// The physical world snapshot remains sole owner of ore and manufactured goods.
// New cold-region food, water, cargo or population must not be copied from it.
struct JournalEvidence {std::uint64_t operations=0,movements=0;};
std::string column_text(sqlite3_stmt* q,int col){
    const auto* p=reinterpret_cast<const char*>(sqlite3_column_text(q,col));
    if(!p)throw std::runtime_error("null physical source journal column");
    return {p,static_cast<std::size_t>(sqlite3_column_bytes(q,col))};
}
JournalEvidence verify_detail_epoch(sqlite3* db,const WorldRow& baseline,const WorldRow& current){
    if(current.generation<baseline.generation)
        throw std::runtime_error("physical world generation rewound during detailed ownership");
    const auto prior=Simulation::from_snapshot(baseline.payload);
    const auto now=Simulation::from_snapshot(current.payload);
    if(prior.total_ore()!=now.total_ore() || prior.total_credits()!=now.total_credits())
        throw std::runtime_error("physical world mass/credit conservation failed");
    if(prior.tick()>now.tick())throw std::runtime_error("detailed timeline moved backwards");
    auto expected=baseline.generation+1;
    auto prev_tick=prior.tick();
    std::string last_hash=std::to_string(prior.state_hash());
    JournalEvidence evidence;
    Stmt operations(db,"SELECT generation,kind,tick,state_hash FROM operation_log "
        "WHERE generation>?1 AND generation<=?2 ORDER BY generation");
    operations.num(1,baseline.generation);operations.num(2,current.generation);
    int rc=0;
    while((rc=sqlite3_step(operations.q))==SQLITE_ROW){
        const auto gen=sqlite3_column_int64(operations.q,0);
        const auto kind=sqlite3_column_int(operations.q,1);
        const auto tick=sqlite3_column_int64(operations.q,2);
        if(gen<1 || static_cast<std::uint64_t>(gen)!=expected++ ||
           (kind!=1 && kind!=2) || tick<0)
            throw std::runtime_error("missing or unsupported physical operation generation");
        const auto t=static_cast<std::uint64_t>(tick);
        if((kind==1 && t!=prev_tick) ||
           (kind==2 && (prev_tick==std::numeric_limits<std::uint64_t>::max() || t!=prev_tick+1)))
            throw std::runtime_error("noncontiguous physical operation timeline");
        prev_tick=t;
        last_hash=column_text(operations.q,3);
        ++evidence.operations;
    }
    if(rc!=SQLITE_DONE||expected!=current.generation+1 || prev_tick!=now.tick() ||
       last_hash!=std::to_string(now.state_hash()))
        throw std::runtime_error("physical operation journal does not seal current state");
    // Only physical *executed* operations in this first supported slice. Ship
    // movement, destruction, external cargo, full factory routing, and social
    // consequences must receive their own later adapter instead of being guessed.
    Stmt effects(db,"SELECT action,outcome,transferred FROM historical_events "
        "WHERE generation>?1 AND generation<=?2");
    effects.num(1,baseline.generation);effects.num(2,current.generation);
    while((rc=sqlite3_step(effects.q))==SQLITE_ROW){
        const auto action=sqlite3_column_int(effects.q,0);
        const auto outcome=sqlite3_column_int(effects.q,1);
        const auto qty=sqlite3_column_int64(effects.q,2);
        if(qty<0 || (outcome==static_cast<int>(Outcome::Applied) &&
           action!=static_cast<int>(Action::Mine) &&
           action!=static_cast<int>(Action::RefineOre) &&
           action!=static_cast<int>(Action::FabricateModule)))
            throw std::runtime_error("physical operation not supported by parking reconciliation");
    }
    if(rc!=SQLITE_DONE)throw std::runtime_error("physical history read failed");
    // Verify the resource ledger is backed by immutable cargo incidents from
    // the SAME committed generation and a real applied historical command.
    Stmt material(db,"SELECT r.id,r.generation,r.tick,r.command_id,r.source_id,r.destination_id,"
      "r.amount,p.kind,p.generation,p.tick,p.command_id,p.source,p.destination,p.amount,"
      "h.action,h.outcome FROM resource_ledger r "
      "LEFT JOIN physical_incidents p ON p.incident_id=('cargo:'||r.id) "
      "LEFT JOIN historical_events h ON h.command_id=r.command_id "
      "WHERE r.generation>?1 AND r.generation<=?2 AND r.source_id IS NOT NULL ORDER BY r.id");
    material.num(1,baseline.generation);material.num(2,current.generation);
    while((rc=sqlite3_step(material.q))==SQLITE_ROW){
        const auto gen=sqlite3_column_int64(material.q,1);
        const auto tick=sqlite3_column_int64(material.q,2);
        const auto amount=sqlite3_column_int64(material.q,6);
        if(gen<1||tick<0||amount<=0||sqlite3_column_type(material.q,3)==SQLITE_NULL||
           sqlite3_column_type(material.q,7)==SQLITE_NULL ||
           sqlite3_column_int(material.q,7)!=static_cast<int>(PhysicalKind::CargoMovement)||
           sqlite3_column_int64(material.q,8)!=gen||sqlite3_column_int64(material.q,9)!=tick||
           column_text(material.q,3)!=column_text(material.q,10)||
           column_text(material.q,4)!=column_text(material.q,11)||
           column_text(material.q,5)!=column_text(material.q,12)||
           sqlite3_column_int64(material.q,13)!=amount||
           sqlite3_column_type(material.q,14)==SQLITE_NULL||
           sqlite3_column_int(material.q,15)!=static_cast<int>(Outcome::Applied))
            throw std::runtime_error("unproven physical material movement");
        const int action=sqlite3_column_int(material.q,14);
        if(action!=static_cast<int>(Action::Mine)&&action!=static_cast<int>(Action::RefineOre)&&
           action!=static_cast<int>(Action::FabricateModule))
            throw std::runtime_error("unhandled physical material movement action");
        ++evidence.movements;
    }
    if(rc!=SQLITE_DONE)throw std::runtime_error("physical material ledger read failed");
    Stmt unmanaged(db,"SELECT COUNT(*) FROM physical_incidents p "
      "LEFT JOIN resource_ledger r ON p.incident_id=('cargo:'||r.id) "
      "WHERE p.generation>?1 AND p.generation<=?2 "
      "AND (p.kind!=1 OR r.id IS NULL OR r.generation!=p.generation)");
    unmanaged.num(1,baseline.generation);unmanaged.num(2,current.generation);
    if(sqlite3_step(unmanaged.q)!=SQLITE_ROW||sqlite3_column_int64(unmanaged.q,0)!=0)
        throw std::runtime_error("unsupported unjournaled detailed physical incident");
    return evidence;
}
void verify_parked_world(sqlite3* db,const std::string& scope,const WorldRow& now){
    Stmt q(db,"SELECT world_generation,world_snapshot FROM sg_world_cold_manifest WHERE scope=?1");
    q.str(1,scope);
    const auto rc=sqlite3_step(q.q);
    if(rc==SQLITE_DONE)return; // First-ever cold promotion has no parked manifest.
    if(rc!=SQLITE_ROW || sqlite3_column_int64(q.q,0)<1||
       static_cast<std::uint64_t>(sqlite3_column_int64(q.q,0))!=now.generation ||
       sqlite3_column_bytes(q.q,1)!=static_cast<int>(now.payload.size()))
        throw std::runtime_error("cold parked world generation/size changed");
    const auto* p=static_cast<const char*>(sqlite3_column_blob(q.q,1));
    if(!p || now.payload.compare(0,now.payload.size(),p,now.payload.size())!=0)
        throw std::runtime_error("cold parked physical world bytes changed");
    Stmt erase(db,"DELETE FROM sg_world_cold_manifest WHERE scope=?1");
    erase.str(1,scope);erase.done();
    if(sqlite3_changes(db)!=1)throw std::runtime_error("parked manifest race");
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
        exec(c.db,"CREATE TABLE IF NOT EXISTS sg_world_cold_manifest ("
             "scope TEXT PRIMARY KEY, handoff_id TEXT NOT NULL,"
             "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
             "world_snapshot BLOB NOT NULL, operations INTEGER NOT NULL CHECK(operations>=0),"
             "physical_movements INTEGER NOT NULL CHECK(physical_movements>=0))");
        exec(c.db,"CREATE TABLE IF NOT EXISTS sg_world_reconciled_movements ("
             "scope TEXT NOT NULL, handoff_id TEXT NOT NULL, incident_id TEXT NOT NULL UNIQUE,"
             "generation INTEGER NOT NULL CHECK(generation>0),"
             "source TEXT NOT NULL, destination TEXT NOT NULL,"
             "amount INTEGER NOT NULL CHECK(amount>0),"
             "PRIMARY KEY(scope,incident_id))");
        exec(c.db,"CREATE TRIGGER IF NOT EXISTS sg_world_reconcile_no_update "
             "BEFORE UPDATE ON sg_world_reconciled_movements "
             "BEGIN SELECT RAISE(ABORT,'reconciled physical movements immutable'); END");
        exec(c.db,"CREATE TRIGGER IF NOT EXISTS sg_world_reconcile_no_delete "
             "BEFORE DELETE ON sg_world_reconciled_movements "
             "BEGIN SELECT RAISE(ABORT,'reconciled physical movements immutable'); END");
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
                verify_parked_world(db,scope_,actual);
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

RegionalAuthorityState PersistentWorldBridge::demote_preserving_effects(const std::string& handoff){
    const auto old=authority_.state();
    if(old.owner!=SimulationOwner::Detailed||old.phase!=TransitionPhase::Stable)
        throw std::runtime_error("effect demotion requires stable detailed ownership");
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
                const auto now=current_world(db);
                Stmt anchor(db,"SELECT world_generation,world_snapshot FROM sg_world_detail_anchor "
                          "WHERE scope=?1");anchor.str(1,scope_);
                if(sqlite3_step(anchor.q)!=SQLITE_ROW)
                    throw std::runtime_error("no independently staged detailed world anchor");
                const auto gen=sqlite3_column_int64(anchor.q,0);
                const auto size=sqlite3_column_bytes(anchor.q,1);
                const auto* ptr=static_cast<const char*>(sqlite3_column_blob(anchor.q,1));
                if(gen<=0||size<=0||size>16*1024*1024||!ptr)
                    throw std::runtime_error("invalid durable detailed anchor");
                const WorldRow baseline{static_cast<std::uint64_t>(gen),
                                        std::string(ptr,static_cast<std::size_t>(size))};
                const auto proof=verify_detail_epoch(db,baseline,now);
                {
                    Stmt record(db,"INSERT INTO sg_world_cold_manifest "
                      "(scope,handoff_id,world_generation,world_snapshot,operations,physical_movements) "
                      "VALUES (?1,?2,?3,?4,?5,?6)");
                    record.str(1,scope_);record.str(2,handoff);record.num(3,now.generation);
                    record.blob(4,now.payload);record.num(5,proof.operations);
                    record.num(6,proof.movements);record.done();
                }
                {
                    Stmt ins(db,"INSERT INTO sg_world_reconciled_movements "
                      "(scope,handoff_id,incident_id,generation,source,destination,amount) "
                      "SELECT ?1,?2,p.incident_id,p.generation,p.source,p.destination,p.amount "
                      "FROM physical_incidents p WHERE p.generation>?3 AND p.generation<=?4 "
                      "AND p.kind=1");
                    ins.str(1,scope_);ins.str(2,handoff);
                    ins.num(3,baseline.generation);ins.num(4,now.generation);ins.done();
                }
                {
                    Stmt erase(db,"DELETE FROM sg_world_detail_anchor WHERE scope=?1");
                    erase.str(1,scope_);erase.done();
                    if(sqlite3_changes(db)!=1)throw std::runtime_error("detailed anchor disappeared");
                }
                // scheduler is intentionally unchanged: it must not mint copies
                // of the real world's ore, material goods, NPCs, or carrier cargo.
            });
    }catch(...){with_rollback_abort(authority_,pending,handoff);throw;}
}
} // namespace galaxy::regional_events
