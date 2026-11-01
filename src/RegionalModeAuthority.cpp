#include "galaxy/RegionalModeAuthority.hpp"
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy::regional_events {
namespace {
constexpr auto max_sqlint=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string message=err?err:sqlite3_errmsg(db);
        sqlite3_free(err);
        throw std::runtime_error("regional ownership SQL: "+message);
    }
}
struct Transaction {
    sqlite3* db;
    bool ended=false;
    explicit Transaction(sqlite3* connection):db(connection){exec(db,"BEGIN IMMEDIATE");}
    ~Transaction(){if(!ended){try{exec(db,"ROLLBACK");}catch(...){}}}
    void commit(){exec(db,"COMMIT");ended=true;}
};
struct Statement {
    sqlite3_stmt* stmt=nullptr;
    sqlite3* db;
    Statement(sqlite3* conn,const char* sql):db(conn){
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("regional ownership prepare: "+std::string(sqlite3_errmsg(db)));
    }
    ~Statement(){sqlite3_finalize(stmt);}
    void str(int n,const std::string& v){
        if(sqlite3_bind_text(stmt,n,v.data(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("regional ownership bind string failed");
    }
    void num(int n,std::uint64_t v){
        if(v>max_sqlint || sqlite3_bind_int64(stmt,n,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
            throw std::runtime_error("regional ownership invalid SQLite integer");
    }
    void step_done(){if(sqlite3_step(stmt)!=SQLITE_DONE)
        throw std::runtime_error("regional ownership mutation failed: "+std::string(sqlite3_errmsg(db)));}
};
RegionalAuthorityState read(sqlite3* db,const std::string& scope){
    Statement q(db,"SELECT owner,phase,generation,checkpoint_revision,boundary_tick,handoff_id "
                   "FROM sg_regional_mode_authority WHERE scope=?1");
    q.str(1,scope);
    if(sqlite3_step(q.stmt)!=SQLITE_ROW)throw std::runtime_error("region not enrolled for authority handoff");
    auto own=sqlite3_column_int(q.stmt,0), phase=sqlite3_column_int(q.stmt,1);
    auto gen=sqlite3_column_int64(q.stmt,2), rev=sqlite3_column_int64(q.stmt,3);
    auto tick=sqlite3_column_int64(q.stmt,4);
    auto raw=reinterpret_cast<const char*>(sqlite3_column_text(q.stmt,5));
    auto n=sqlite3_column_bytes(q.stmt,5);
    if((own!=0&&own!=1)||(phase<0||phase>2)||gen<1||rev<1||tick<0||!raw||n<0||n>4096)
        throw std::runtime_error("corrupt regional authority state");
    RegionalAuthorityState out{static_cast<SimulationOwner>(own),static_cast<TransitionPhase>(phase),
        static_cast<std::uint64_t>(gen),static_cast<std::uint64_t>(rev),static_cast<std::uint64_t>(tick),
        std::string(raw,static_cast<std::size_t>(n))};
    if((phase==0)!=out.handoff_id.empty() ||
       (phase==1&&own!=0) || (phase==2&&own!=1))
        throw std::runtime_error("inconsistent regional ownership transition");
    return out;
}
void verify_id(const std::string& id){
    if(id.empty() || id.size()>4096)throw std::invalid_argument("handoff ID must be nonempty and bounded");
}
}
RegionalModeAuthority::RegionalModeAuthority(CheckpointStore& store):store_(store){
    exec(store_.db_,"CREATE TABLE IF NOT EXISTS sg_regional_mode_authority ("
       "scope TEXT PRIMARY KEY NOT NULL, owner INTEGER NOT NULL CHECK(owner IN (0,1)),"
       "phase INTEGER NOT NULL CHECK(phase BETWEEN 0 AND 2),"
       "generation INTEGER NOT NULL CHECK(generation>0),"
       "checkpoint_revision INTEGER NOT NULL CHECK(checkpoint_revision>0),"
       "boundary_tick INTEGER NOT NULL CHECK(boundary_tick>=0),"
       "handoff_id TEXT NOT NULL)");
}
RegionalAuthorityState RegionalModeAuthority::enroll(){
    Transaction tx(store_.db_);
    auto cp=store_.load();
    if(!cp)throw std::runtime_error("cannot enroll region without durable checkpoint");
    {
        Statement insert(store_.db_,"INSERT OR IGNORE INTO sg_regional_mode_authority "
            "(scope,owner,phase,generation,checkpoint_revision,boundary_tick,handoff_id) "
            "VALUES (?1,0,0,1,?2,?3,'')");
        insert.str(1,store_.scope_);
        insert.num(2,cp->revision);insert.num(3,cp->scheduler.tick());
        insert.step_done();
    }
    auto s=read(store_.db_,store_.scope_);
    tx.commit();return s;
}
RegionalAuthorityState RegionalModeAuthority::state() const {
    return read(store_.db_,store_.scope_);
}
RegionalAuthorityState RegionalModeAuthority::prepare(const std::string& id,
    SimulationOwner target,std::uint64_t gen,std::uint64_t rev,std::uint64_t tick){
    verify_id(id);
    if(gen==0 || gen>=max_sqlint || rev==0 || rev>max_sqlint || tick>max_sqlint)
        throw std::invalid_argument("invalid expected authority revision/tick");
    if(target!=SimulationOwner::Cold && target!=SimulationOwner::Detailed)
        throw std::invalid_argument("invalid owner target");
    Transaction tx(store_.db_);
    const auto s=read(store_.db_,store_.scope_);
    if(s.generation!=gen || s.phase!=TransitionPhase::Stable || s.owner==target)
        throw std::runtime_error("stale or invalid regional handoff intent");
    const auto cp=store_.load();
    if(s.checkpoint_revision!=rev || s.boundary_tick!=tick ||
       !cp || cp->revision!=rev || cp->scheduler.tick()!=tick)
        throw std::runtime_error("handoff requires exact durable checkpoint boundary");
    {
        Statement q(store_.db_,"UPDATE sg_regional_mode_authority SET phase=?2,"
             "checkpoint_revision=?3,boundary_tick=?4,handoff_id=?5 "
             "WHERE scope=?1 AND generation=?6 AND phase=0");
        q.str(1,store_.scope_);q.num(2,target==SimulationOwner::Detailed?1:2);
        q.num(3,rev);q.num(4,tick);q.str(5,id);q.num(6,gen);
        q.step_done();
        if(sqlite3_changes(store_.db_)!=1)throw std::runtime_error("regional handoff race");
    }
    const auto result=read(store_.db_,store_.scope_);
    tx.commit();return result;
}
RegionalAuthorityState RegionalModeAuthority::commit(const std::string& id,
    std::uint64_t gen,std::uint64_t rev,const DurableStage& stage){
    verify_id(id);
    if(!stage)throw std::invalid_argument("durable handoff adapter is required");
    Transaction tx(store_.db_);
    auto s=read(store_.db_,store_.scope_);
    if(s.generation!=gen || s.phase==TransitionPhase::Stable || s.handoff_id!=id ||
       s.checkpoint_revision!=rev || gen>=max_sqlint)
        throw std::runtime_error("stale or mismatched ownership commit");
    const auto cp=store_.load();
    if(!cp || cp->revision!=rev || cp->scheduler.tick()!=s.boundary_tick)
        throw std::runtime_error("checkpoint changed during ownership handoff");
    auto candidate=cp->scheduler;
    const auto original_bytes=store_.encode(candidate);
    // Only host-owned SQLite changes, no external side effects, can share the
    // atomic authority flip. Even test adapters must explicitly stage data.
    stage(store_.db_,candidate);
    std::uint64_t resulting_revision=rev;
    if(s.phase==TransitionPhase::ToDetailed){
        if(store_.encode(candidate)!=original_bytes)
            throw std::runtime_error("promotion must not silently change cold checkpoint");
    }else{
        if(rev>=max_sqlint)throw std::overflow_error("handoff checkpoint revision overflow");
        // This is a shadow of the actual detailed-world reconciliation: the
        // production adapter must independently prove the physical effects.
        // Never move the regional timeline backwards.
        if(candidate.tick()<cp->scheduler.tick())
            throw std::runtime_error("detailed flush would rewind regional timeline");
        const auto payload=store_.encode(candidate);
        Statement u(store_.db_,"UPDATE sg_regional_checkpoint SET revision=?3,payload=?4 "
                                 "WHERE scope=?1 AND revision=?2");
        u.str(1,store_.scope_);u.num(2,rev);u.num(3,rev+1);
        if(sqlite3_bind_blob(u.stmt,4,payload.data(),static_cast<int>(payload.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("bind reconciled snapshot failed");
        u.step_done();
        if(sqlite3_changes(store_.db_)!=1)throw std::runtime_error("regional handoff checkpoint stale");
        resulting_revision=rev+1;
    }
    {
        Statement q(store_.db_,"UPDATE sg_regional_mode_authority SET owner=?2,"
             "phase=0,generation=?3,handoff_id='',checkpoint_revision=?7,"
             "boundary_tick=?8 WHERE scope=?1 AND generation=?4 "
             "AND phase=?5 AND handoff_id=?6");
        q.str(1,store_.scope_);
        q.num(2,s.phase==TransitionPhase::ToDetailed?1:0);
        q.num(3,gen+1);q.num(4,gen);q.num(5,static_cast<int>(s.phase));q.str(6,id);
        q.num(7,resulting_revision);q.num(8,candidate.tick());
        q.step_done();
        if(sqlite3_changes(store_.db_)!=1)throw std::runtime_error("regional handoff commit lost race");
    }
    auto result=read(store_.db_,store_.scope_);
    tx.commit();return result;
}
RegionalAuthorityState RegionalModeAuthority::abort(const std::string& id,std::uint64_t gen){
    verify_id(id);
    Transaction tx(store_.db_);
    auto s=read(store_.db_,store_.scope_);
    if(s.generation!=gen || gen>=max_sqlint || s.phase==TransitionPhase::Stable || s.handoff_id!=id)
        throw std::runtime_error("stale or mismatched ownership abort");
    {
        Statement q(store_.db_,"UPDATE sg_regional_mode_authority SET phase=0,generation=?2,"
             "handoff_id='' WHERE scope=?1 AND generation=?3 AND handoff_id=?4");
        q.str(1,store_.scope_);q.num(2,gen+1);q.num(3,gen);q.str(4,id);
        q.step_done();
        if(sqlite3_changes(store_.db_)!=1)throw std::runtime_error("regional handoff abort lost race");
    }
    auto result=read(store_.db_,store_.scope_);
    tx.commit();return result;
}
} // namespace galaxy::regional_events
