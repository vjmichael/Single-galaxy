#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalShipmentReconciliation.hpp"
#include "galaxy/Simulation.hpp"
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
void reject_active_region_berths(sqlite3* db,const std::string& scope){
    // v49: operating crew have committed physical transfer receipts, but do
    // not yet have a trusted staff->region identity binding. Until one exists,
    // any crew still physically absent from its assigned ship is a GLOBAL
    // conservative fence on cold/detailed ownership transitions. Do not
    // promote an industry runtime that would manufacture their labor.
    Statement native(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                        "AND name='sg_split_crew_execution'");
    if(sqlite3_step(native.stmt)==SQLITE_ROW){
        Statement latest(db,"SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1");
        if(sqlite3_step(latest.stmt)!=SQLITE_ROW)
            throw std::runtime_error("crew rescue has no canonical world snapshot");
        const auto* bytes=sqlite3_column_blob(latest.stmt,0);
        const auto size=sqlite3_column_bytes(latest.stmt,0);
        if(!bytes||size<=0||size>16*1024*1024)
            throw std::runtime_error("crew rescue world snapshot corrupt");
        const auto sim=Simulation::from_snapshot(std::string(
            static_cast<const char*>(bytes),static_cast<std::size_t>(size)));
        Statement crew(db,"SELECT DISTINCT npc_id FROM sg_split_crew_execution");
        int row=sqlite3_step(crew.stmt);
        for(;row==SQLITE_ROW;row=sqlite3_step(crew.stmt)){
            const auto* raw=sqlite3_column_text(crew.stmt,0);
            if(!raw)throw std::runtime_error("crew rescue identity missing");
            const std::string identifier(reinterpret_cast<const char*>(raw));
            if(identifier.empty()||identifier.find_first_not_of("0123456789")!=std::string::npos)
                throw std::runtime_error("noncanonical crew identity");
            const auto id=std::stoull(identifier);
            if(!id||std::to_string(id)!=identifier)
                throw std::runtime_error("noncanonical crew identity");
            const auto person=sim.npc_records().find(id);
            if(person==sim.npc_records().end()||person->second.transport_id||
               person->second.location!=person->second.assigned_site)
                throw std::runtime_error(
                    "unbound rescued operating crew blocks detailed region handoff");
        }
        if(row!=SQLITE_DONE)throw std::runtime_error("crew rescue custody SQL failed");
    }
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                       "AND name='sg_dependent_travel'");
    if(sqlite3_step(table.stmt)!=SQLITE_ROW)return;
    Statement bindings(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                          "AND name='sg_dependent_region_binding'");
    if(sqlite3_step(bindings.stmt)!=SQLITE_ROW){
        Statement active(db,"SELECT 1 FROM sg_dependent_travel WHERE status=1 LIMIT 1");
        if(sqlite3_step(active.stmt)==SQLITE_ROW)
            throw std::runtime_error("unscoped legacy child custody blocks all handoffs");
        return;
    }
    Statement active(db,"SELECT 1 FROM sg_dependent_travel d LEFT JOIN "
        "sg_dependent_region_binding b ON b.family_scope=d.family_scope "
        "WHERE d.status=1 AND (b.region_scope=? OR b.region_scope IS NULL) LIMIT 1");
    active.str(1,scope);
    if(sqlite3_step(active.stmt)==SQLITE_ROW)
        throw std::runtime_error("region owns active or unscoped dependent berth");
}
// After the host stage, prove that an enrolled real factory actually crossed
// the SAME owner boundary. A no-op durable_stage must never flip ownership
// while leaving WIP and materials on the old side of the handoff.
void validate_factory_stage(sqlite3* db,const std::string& scope,
                            const RegionalAuthorityState& previous,
                            const std::string& id){
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v50_factory'");
    if(sqlite3_step(table.stmt)!=SQLITE_ROW)return;
    Statement cold(db,"SELECT payload FROM sg_v50_factory WHERE scope=?1");cold.str(1,scope);
    if(sqlite3_step(cold.stmt)!=SQLITE_ROW)return;
    const auto* raw=sqlite3_column_text(cold.stmt,0);
    if(!raw)throw std::runtime_error("factory checkpoint missing during owner transfer");
    const std::string cold_payload(reinterpret_cast<const char*>(raw));
    Statement effect(db,"SELECT kind,before_payload,after_payload FROM sg_v50_factory_event "
        "WHERE scope=?1 AND event_id=?2");
    effect.str(1,scope);effect.str(2,id);
    if(sqlite3_step(effect.stmt)!=SQLITE_ROW)
        throw std::runtime_error("owner flip missing factory handoff receipt");
    auto text_at=[&](int col){
        const auto* v=sqlite3_column_text(effect.stmt,col);
        if(!v)throw std::runtime_error("factory handoff receipt malformed");
        return std::string(reinterpret_cast<const char*>(v));
    };
    const auto kind=text_at(0),before=text_at(1),after=text_at(2);
    Statement warm(db,"SELECT payload,generation FROM sg_warm_active WHERE scope=?1");
    warm.str(1,scope);const auto rc=sqlite3_step(warm.stmt);
    if(previous.phase==TransitionPhase::ToDetailed){
        if(kind!="promote" || before!=after || cold_payload!=after ||
           rc!=SQLITE_ROW || sqlite3_column_int64(warm.stmt,1)!=
                            static_cast<sqlite3_int64>(previous.generation+1))
            throw std::runtime_error("factory detailed promotion lacks materialized owner state");
        const auto* val=sqlite3_column_text(warm.stmt,0);
        if(!val||after!=std::string(reinterpret_cast<const char*>(val)))
            throw std::runtime_error("factory promoted to mismatched material/WIP state");
    }else if(previous.phase==TransitionPhase::ToCold){
        if(kind!="demote" || before!=after || after!=cold_payload ||rc!=SQLITE_DONE)
            throw std::runtime_error("factory cold return lacks reconciled material state");
    }else throw std::runtime_error("unrecognized factory owner handoff phase");
}
// v51: the entire warehouse + machine graph must cross the owner boundary,
// not merely the legacy single-machine checkpoint.
void validate_network_stage(sqlite3* db,const std::string& scope,
                            const RegionalAuthorityState& prior,const std::string& id){
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v51_network'");
    if(sqlite3_step(table.stmt)!=SQLITE_ROW)return;
    Statement enrolled(db,"SELECT payload FROM sg_v51_network WHERE scope=?1");
    enrolled.str(1,scope);if(sqlite3_step(enrolled.stmt)!=SQLITE_ROW)return;
    auto col=[](sqlite3_stmt* stmt,int i)->std::string{
        const auto* ptr=sqlite3_column_text(stmt,i);
        if(!ptr)throw std::runtime_error("network handoff missing payload");
        return reinterpret_cast<const char*>(ptr);
    };
    const auto cold=col(enrolled.stmt,0);
    Statement effect(db,"SELECT kind,before_payload,after_payload FROM sg_v51_event WHERE scope=?1 AND event_id=?2");
    effect.str(1,scope);effect.str(2,id);
    if(sqlite3_step(effect.stmt)!=SQLITE_ROW)throw std::runtime_error("network handoff effect absent");
    const auto kind=col(effect.stmt,0),before=col(effect.stmt,1),after=col(effect.stmt,2);
    Statement active(db,"SELECT payload,generation FROM sg_v51_warm WHERE scope=?1");
    active.str(1,scope);const auto row=sqlite3_step(active.stmt);
    if(prior.phase==TransitionPhase::ToDetailed){
        if(kind!="promote"||before!=after||after!=cold||row!=SQLITE_ROW||
           sqlite3_column_int64(active.stmt,1)!=static_cast<sqlite3_int64>(prior.generation+1)||
           col(active.stmt,0)!=after)
            throw std::runtime_error("network promotion did not stage entire graph");
    }else if(prior.phase==TransitionPhase::ToCold){
        if(kind!="demote"||before!=after||after!=cold||row!=SQLITE_DONE)
            throw std::runtime_error("network demotion failed material reconciliation");
    }else throw std::runtime_error("network handoff phase invalid");
}
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
    // Region-scoped child ownership fence. An unbound legacy child still
    // blocks ALL handoffs: absence of custody provenance never grants access.
    reject_active_region_berths(store_.db_,store_.scope_);
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
    // Region-scoped child ownership fence. An unbound legacy child still
    // blocks ALL handoffs: absence of custody provenance never grants access.
    reject_active_region_berths(store_.db_,store_.scope_);
    auto candidate=cp->scheduler;
    const auto original_bytes=store_.encode(candidate);
    // Only host-owned SQLite changes, no external side effects, can share the
    // atomic authority flip. Even test adapters must explicitly stage data.
    stage(store_.db_,candidate);
    validate_factory_stage(store_.db_,store_.scope_,s,id);
    validate_network_stage(store_.db_,store_.scope_,s,id);
    // Atomically bind world cargo journal prefix to this regional owner flip.
    galaxy::shipping::seal_regional_shipment_boundary(store_.db_,store_.scope_,id,s.generation,rev,
        s.phase==TransitionPhase::ToDetailed?1:0);
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
    // A failed Detailed->Cold cutover can be cancelled, but its live factory
    // materialization must receive the same rotated owner generation. Otherwise
    // the valid detailed owner could never resume work after an abort.
    if(s.owner==SimulationOwner::Detailed&&s.phase==TransitionPhase::ToCold){
        Statement table(store_.db_,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v50_factory'");
        if(sqlite3_step(table.stmt)==SQLITE_ROW){
            Statement enrolled(store_.db_,"SELECT 1 FROM sg_v50_factory WHERE scope=?1");
            enrolled.str(1,store_.scope_);
            if(sqlite3_step(enrolled.stmt)==SQLITE_ROW){
                Statement warm(store_.db_,"UPDATE sg_warm_active SET generation=?2 "
                    "WHERE scope=?1 AND generation=?3");
                warm.str(1,store_.scope_);warm.num(2,gen+1);warm.num(3,gen);warm.step_done();
                if(sqlite3_changes(store_.db_)!=1)
                    throw std::runtime_error("factory warm generation rotation failed");
            }
        }
        // v51 production-network materialization follows the same aborted
        // demotion owner-token rotation as a single line.
        Statement net_table(store_.db_,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v51_network'");
        if(sqlite3_step(net_table.stmt)==SQLITE_ROW){
            Statement bound(store_.db_,"SELECT 1 FROM sg_v51_network WHERE scope=?1");
            bound.str(1,store_.scope_);
            if(sqlite3_step(bound.stmt)==SQLITE_ROW){
                Statement warm(store_.db_,"UPDATE sg_v51_warm SET generation=?2 WHERE scope=?1 AND generation=?3");
                warm.str(1,store_.scope_);warm.num(2,gen+1);warm.num(3,gen);warm.step_done();
                if(sqlite3_changes(store_.db_)!=1)
                    throw std::runtime_error("network active generation rotation failed");
            }
        }
    }
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

namespace galaxy {
// Checked by PersistentWorld on EVERY reopen, plus independent recover_copy.
void verify_dependent_region_bindings(const std::string& path,
                                      std::uint64_t max_generation){
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close(db);
        throw std::runtime_error("cannot inspect dependent region custody");
    }
    try{
        auto present=[&](const char* kind,const char* name){
            sqlite3_stmt* q=nullptr;
            if(sqlite3_prepare_v2(db,"SELECT 1 FROM sqlite_master WHERE type=? AND name=?",-1,&q,nullptr)!=SQLITE_OK)
                throw std::runtime_error("region binding sqlite schema lookup failed");
            sqlite3_bind_text(q,1,kind,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(q,2,name,-1,SQLITE_TRANSIENT);
            const bool yes=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);return yes;
        };
        const bool binding=present("table","sg_dependent_region_binding");
        bool anchor=false;
        sqlite3_stmt* cols=nullptr;
        if(sqlite3_prepare_v2(db,"PRAGMA table_info(world_meta)",-1,&cols,nullptr)!=SQLITE_OK)
            throw std::runtime_error("region binding metadata unavailable");
        while(sqlite3_step(cols)==SQLITE_ROW){
            const auto* text=sqlite3_column_text(cols,1);
            if(text && std::string(reinterpret_cast<const char*>(text))==
               "dependent_region_binding_count")anchor=true;
        }
        sqlite3_finalize(cols);
        if(binding!=anchor)throw std::runtime_error("dependent region binding anchor missing");
        if(binding){
            for(const auto* suffix:{"_immutable_update","_immutable_delete"})
                if(!present("trigger",(std::string("sg_dependent_region_binding")+suffix).c_str()))
                    throw std::runtime_error("dependent region binding immutability missing");
            sqlite3_stmt* q=nullptr;
            if(sqlite3_prepare_v2(db,"SELECT family_scope,region_scope,first_generation,first_command "
               "FROM sg_dependent_region_binding",-1,&q,nullptr)!=SQLITE_OK)
                throw std::runtime_error("region binding read error");
            std::uint64_t actual=0;
            while(sqlite3_step(q)==SQLITE_ROW){
                ++actual;
                auto string=[&](int i){const auto* v=sqlite3_column_text(q,i);
                    if(!v)throw std::runtime_error("null dependent region binding");
                    return std::string(reinterpret_cast<const char*>(v));};
                const auto family=string(0),scope=string(1),command=string(3);
                const auto gen=sqlite3_column_int64(q,2);
                if(family.empty()||scope.empty()||command.empty()||gen<1||
                   static_cast<std::uint64_t>(gen)>max_generation)
                    throw std::runtime_error("bad dependent region binding record");
                sqlite3_stmt* record=nullptr;
                if(sqlite3_prepare_v2(db,"SELECT 1 FROM sg_dependent_transport_batch "
                    "WHERE family_scope=? AND command_id=? AND phase=1 AND world_generation=?",-1,
                    &record,nullptr)!=SQLITE_OK)
                    throw std::runtime_error("missing dependent transport receipt table");
                sqlite3_bind_text(record,1,family.c_str(),-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(record,2,command.c_str(),-1,SQLITE_TRANSIENT);
                sqlite3_bind_int64(record,3,gen);
                const bool found=sqlite3_step(record)==SQLITE_ROW;
                sqlite3_finalize(record);
                if(!found)throw std::runtime_error("region binding lacks original boarding receipt");
            }
            sqlite3_finalize(q);
            sqlite3_stmt* count=nullptr;
            if(sqlite3_prepare_v2(db,"SELECT dependent_region_binding_count FROM world_meta "
                "WHERE singleton=1",-1,&count,nullptr)!=SQLITE_OK)
                throw std::runtime_error("region binding counter unreadable");
            if(sqlite3_step(count)!=SQLITE_ROW ||
               sqlite3_column_int64(count,0)<0 ||
               static_cast<std::uint64_t>(sqlite3_column_int64(count,0))!=actual)
                throw std::runtime_error("region binding counter corrupted");
            sqlite3_finalize(count);
        }
        sqlite3_close(db);
    }catch(...){sqlite3_close(db);throw;}
}
} // namespace galaxy
