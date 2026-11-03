#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy {
namespace {
struct DB {
    sqlite3* db=nullptr;
    explicit DB(const std::string& path){
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const std::string why=db?sqlite3_errmsg(db):"open error";
            sqlite3_close(db);throw std::runtime_error("guardian SQLite: "+why);
        }
        sqlite3_busy_timeout(db,5000);
        sqlite3_exec(db,"PRAGMA foreign_keys=ON",nullptr,nullptr,nullptr);
    }
    ~DB(){sqlite3_close(db);}
    DB(const DB&)=delete;DB& operator=(const DB&)=delete;
};
void exec(sqlite3* db,const std::string& query){
    char* err=nullptr;
    if(sqlite3_exec(db,query.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string what=err?err:sqlite3_errmsg(db);
        sqlite3_free(err);throw std::runtime_error("guardian SQL: "+what);
    }
}
struct Q {
    sqlite3* db;
    sqlite3_stmt* q=nullptr;
    Q(sqlite3* database,const char* statement):db(database){
        if(sqlite3_prepare_v2(db,statement,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("guardian prepare: ")+sqlite3_errmsg(db));
    }
    ~Q(){sqlite3_finalize(q);}
    void s(int i,const std::string& value){
        if(sqlite3_bind_text(q,i,value.c_str(),static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("guardian bind string");
    }
    void n(int i,std::uint64_t value){
        if(value>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(q,i,static_cast<sqlite3_int64>(value))!=SQLITE_OK)
            throw std::runtime_error("guardian bind generation");
    }
    bool row(){
        const int rc=sqlite3_step(q);
        if(rc==SQLITE_ROW)return true;
        if(rc==SQLITE_DONE)return false;
        throw std::runtime_error(std::string("guardian query: ")+sqlite3_errmsg(db));
    }
    void done(){if(sqlite3_step(q)!=SQLITE_DONE)
        throw std::runtime_error(std::string("guardian insert: ")+sqlite3_errmsg(db));}
    std::string str(int i)const{
        const auto* data=sqlite3_column_text(q,i);
        if(!data)throw std::runtime_error("guardian missing text field");
        return reinterpret_cast<const char*>(data);
    }
    std::uint64_t id(int i)const{
        const auto v=str(i);
        if(v.empty()||v.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("guardian invalid decimal ID");
        const auto parsed=std::stoull(v);
        if(std::to_string(parsed)!=v)throw std::runtime_error("noncanonical guardian ID");
        return parsed;
    }
    std::uint64_t number(int i)const{
        const auto value=sqlite3_column_int64(q,i);
        if(value<0)throw std::runtime_error("guardian invalid negative value");
        return static_cast<std::uint64_t>(value);
    }
};
bool exists(sqlite3* db,const std::string& kind,const std::string& name){
    Q q(db,"SELECT 1 FROM sqlite_master WHERE type=? AND name=?");
    q.s(1,kind);q.s(2,name);return q.row();
}
bool column(sqlite3* db,const char* target){
    Q q(db,"PRAGMA table_info(world_meta)");
    while(q.row())if(q.str(1)==target)return true;
    return false;
}
bool valid(const std::string& s){
    return !s.empty()&&s.size()<=128&&
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==std::string::npos;
}
void ordered_ids(const std::vector<std::uint64_t>& ids){
    if(ids.empty()||ids.size()>15||ids[0]==0||
       !std::is_sorted(ids.begin(),ids.end())||
       std::adjacent_find(ids.begin(),ids.end())!=ids.end())
        throw std::invalid_argument("guardian needs full, sorted, unique dependent manifest");
}
std::string joined(const std::vector<std::uint64_t>& ids){
    ordered_ids(ids);
    std::string result;
    for(const auto id:ids){if(!result.empty())result.push_back(',');result+=std::to_string(id);}
    return result;
}
struct Outcome {
    std::string family;
    EntityId ferry=0;
    std::uint64_t voyage=0,rescue_generation=0;
    std::vector<std::uint64_t> child_ids;
    std::vector<std::uint64_t> parents;
};
Outcome outcome(sqlite3* db,const std::string& outcome_id){
    Outcome r{};
    Q o(db,"SELECT family_scope,rescue_transport,receiving_ferry,rescue_generation,member_count "
           "FROM sg_dependent_rescue_outcome WHERE outcome_id=?");
    o.s(1,outcome_id);
    if(!o.row())throw std::runtime_error("no verified prior rescue outcome");
    r.family=o.str(0);r.voyage=o.id(1);r.ferry=o.id(2);
    r.rescue_generation=o.number(3);const auto count=o.number(4);
    if(o.row())throw std::runtime_error("duplicate rescue outcome");
    Q m(db,"SELECT child_id,guardian_id,co_guardian_id FROM "
           "sg_dependent_rescue_outcome_member WHERE outcome_id=? ORDER BY child_id");
    m.s(1,outcome_id);
    while(m.row()){
        const auto child=m.id(0),primary=m.id(1),secondary=m.id(2);
        if(primary==0||primary==secondary||child==primary||child==secondary)
            throw std::runtime_error("rescue parentage invalid");
        std::vector<std::uint64_t> present={primary};if(secondary)present.push_back(secondary);
        std::sort(present.begin(),present.end());
        if(r.parents.empty())r.parents=present;
        if(r.parents!=present)
            throw std::runtime_error("mixed-family guardian changes require a different protocol");
        r.child_ids.push_back(child);
    }
    ordered_ids(r.child_ids);
    if(r.child_ids.size()!=count || r.child_ids.size()+r.parents.size()>16)
        throw std::runtime_error("rescue household roster incomplete");
    return r;
}
struct Snapshot {
    Simulation sim;
    explicit Snapshot(const Simulation& in):sim(in){}
};
Snapshot snapshot(sqlite3* db,std::uint64_t generation){
    Q q(db,"SELECT state,checksum FROM snapshots WHERE generation=?");q.n(1,generation);
    if(!q.row()||sqlite3_column_type(q.q,0)!=SQLITE_BLOB)
        throw std::runtime_error("guardian historical snapshot missing");
    const auto* data=static_cast<const char*>(sqlite3_column_blob(q.q,0));
    const auto n=sqlite3_column_bytes(q.q,0);
    if(!data||n<=0||n>16*1024*1024)
        throw std::runtime_error("guardian invalid historical snapshot");
    const auto sim=Simulation::from_snapshot(std::string(data,static_cast<std::size_t>(n)));
    const auto checksum=q.str(1);
    if(checksum!=std::to_string(sim.state_hash()))
        throw std::runtime_error("guardian historical snapshot checksum mismatch");
    Q op(db,"SELECT state_hash,tick FROM operation_log WHERE generation=?");op.n(1,generation);
    if(!op.row()||op.str(0)!=checksum||op.number(1)!=sim.tick()||op.row())
        throw std::runtime_error("guardian historical snapshot lacks canonical operation");
    return Snapshot(sim);
}
void aboard(const Simulation& sim,const Outcome& o,std::uint64_t id){
    const auto it=sim.npc_records().find(id);
    if(it==sim.npc_records().end()||it->second.location!=o.ferry||
       it->second.transport_id!=o.voyage)
        throw std::runtime_error("receiving adult not physically present on rescue ferry");
}
void children_aboard(sqlite3* db,const Outcome& o,std::uint64_t gen){
    for(const auto child:o.child_ids){
        Q c(db,"SELECT COUNT(*) FROM sg_dependent_travel "
               "WHERE family_scope=? AND child_id=? AND transport_id=? AND ferry=? "
               "AND board_generation<=? AND (arrival_generation IS NULL OR arrival_generation>?)");
        c.s(1,o.family);c.s(2,std::to_string(child));c.s(3,std::to_string(o.voyage));
        c.s(4,std::to_string(o.ferry));c.n(5,gen);c.n(6,gen);
        if(!c.row()||c.number(0)!=1)
            throw std::runtime_error("dependent is not physically aboard at transfer generation");
    }
}
void confirm(sqlite3* db,const Outcome& o,std::uint64_t gen,std::uint64_t receiver){
    if(gen<o.rescue_generation)throw std::runtime_error("guardian predates rescue");
    if(std::binary_search(o.parents.begin(),o.parents.end(),receiver))
        throw std::runtime_error("new legal guardian must not be a birth parent");
    const auto state=snapshot(db,gen);
    aboard(state.sim,o,receiver);
    for(const auto parent:o.parents)aboard(state.sim,o,parent);
    children_aboard(db,o,gen);
}
std::uint64_t current_gen(sqlite3* db){
    Q q(db,"SELECT MAX(generation) FROM snapshots");
    if(!q.row())throw std::runtime_error("guardian no current snapshot");
    return q.number(0);
}
std::uint64_t tick_for(sqlite3* db,std::uint64_t generation){
    Q q(db,"SELECT tick FROM operation_log WHERE generation=?");q.n(1,generation);
    if(!q.row())throw std::runtime_error("guardian no world tick");
    return q.number(0);
}
void triggers(sqlite3* db,const std::string& table){
    for(const auto* suffix:{"_immutable_update","_immutable_delete"}){
        const std::string name=table+suffix;
        const std::string operation=std::string(suffix)=="_immutable_update"?"UPDATE":"DELETE";
        exec(db,"CREATE TRIGGER IF NOT EXISTS "+name+" BEFORE "+operation+" ON "+table+
            " BEGIN SELECT RAISE(ABORT,'legal guardian event immutable'); END");
    }
}
void schema(sqlite3* db){
    if(!column(db,"dependent_guardian_approval_count"))
        exec(db,"ALTER TABLE world_meta ADD COLUMN dependent_guardian_approval_count "
            "INTEGER NOT NULL DEFAULT 0 CHECK(dependent_guardian_approval_count>=0)");
    if(!column(db,"dependent_guardian_transfer_count"))
        exec(db,"ALTER TABLE world_meta ADD COLUMN dependent_guardian_transfer_count "
            "INTEGER NOT NULL DEFAULT 0 CHECK(dependent_guardian_transfer_count>=0)");
    exec(db,"CREATE TABLE IF NOT EXISTS sg_guardian_approval ("
        "authorization_id TEXT PRIMARY KEY,outcome_id TEXT NOT NULL,family_scope TEXT NOT NULL,"
        "parent_id TEXT NOT NULL,receiving_guardian TEXT NOT NULL,"
        "child_manifest TEXT NOT NULL,approved_generation INTEGER NOT NULL,approved_tick INTEGER NOT NULL,"
        "UNIQUE(outcome_id,parent_id))");
    exec(db,"CREATE TABLE IF NOT EXISTS sg_guardian_transfer ("
        "transfer_id TEXT PRIMARY KEY,outcome_id TEXT NOT NULL UNIQUE,family_scope TEXT NOT NULL,"
        "guardian_id TEXT NOT NULL,world_generation INTEGER NOT NULL,world_tick INTEGER NOT NULL,"
        "member_count INTEGER NOT NULL CHECK(member_count BETWEEN 1 AND 15),"
        "approval_count INTEGER NOT NULL CHECK(approval_count BETWEEN 1 AND 2))");
    exec(db,"CREATE TABLE IF NOT EXISTS sg_guardian_transfer_auth ("
        "transfer_id TEXT NOT NULL,authorization_id TEXT NOT NULL UNIQUE,"
        "PRIMARY KEY(transfer_id,authorization_id))");
    exec(db,"CREATE TABLE IF NOT EXISTS npc_family_guardianship ("
        "scope TEXT NOT NULL,event_id TEXT NOT NULL,child_id TEXT NOT NULL,"
        "guardian_id TEXT NOT NULL,transfer_id TEXT NOT NULL,"
        "parent_a TEXT NOT NULL,parent_b TEXT NOT NULL,"
        "generation INTEGER NOT NULL,tick INTEGER NOT NULL,"
        "PRIMARY KEY(scope,event_id), UNIQUE(scope,child_id,transfer_id))");
    for(const auto* t:{"sg_guardian_approval","sg_guardian_transfer",
                       "sg_guardian_transfer_auth","npc_family_guardianship"})triggers(db,t);
}
void validate_approval(sqlite3* db,const std::string& id,std::uint64_t bound){
    Q a(db,"SELECT outcome_id,family_scope,parent_id,receiving_guardian,child_manifest,"
           "approved_generation,approved_tick FROM sg_guardian_approval WHERE authorization_id=?");
    a.s(1,id);
    if(!a.row())throw std::runtime_error("missing parental authorization receipt");
    const auto rescue_id=a.str(0),family=a.str(1),manifest=a.str(4);
    const auto parent=a.id(2),receiver=a.id(3),gen=a.number(5),tick=a.number(6);
    if(a.row()||gen>bound||!gen||tick!=tick_for(db,gen))
        throw std::runtime_error("invalid guardian authorization history");
    const auto o=outcome(db,rescue_id);
    if(family!=o.family||manifest!=joined(o.child_ids)||
       !std::binary_search(o.parents.begin(),o.parents.end(),parent))
        throw std::runtime_error("guardian authorization does not match rescue family");
    confirm(db,o,gen,receiver);
}
void validate_transfer(sqlite3* db,const std::string& id,std::uint64_t bound){
    Q t(db,"SELECT outcome_id,family_scope,guardian_id,world_generation,world_tick,"
           "member_count,approval_count FROM sg_guardian_transfer WHERE transfer_id=?");
    t.s(1,id);
    if(!t.row())throw std::runtime_error("missing legal guardianship transfer");
    const auto outcome_id=t.str(0),family=t.str(1);
    const auto receiver=t.id(2),gen=t.number(3),tick=t.number(4),
        member_count=t.number(5),auth_count=t.number(6);
    if(t.row()||!gen||gen>bound||tick!=tick_for(db,gen))
        throw std::runtime_error("invalid guardian transfer generation");
    const auto o=outcome(db,outcome_id);
    if(o.family!=family||o.child_ids.size()!=member_count||o.parents.size()!=auth_count)
        throw std::runtime_error("guardian family/rescue transfer roster mismatch");
    confirm(db,o,gen,receiver);
    std::vector<std::uint64_t> parents;
    Q grants(db,"SELECT authorization_id FROM sg_guardian_transfer_auth WHERE transfer_id=? "
                "ORDER BY authorization_id");
    grants.s(1,id);
    std::size_t n=0;
    while(grants.row()){
        ++n;const auto approval_id=grants.str(0);
        validate_approval(db,approval_id,bound);
        Q approval(db,"SELECT outcome_id,family_scope,parent_id,receiving_guardian "
                      "FROM sg_guardian_approval WHERE authorization_id=?");
        approval.s(1,approval_id);
        if(!approval.row()||approval.str(0)!=outcome_id||approval.str(1)!=family||
           approval.id(3)!=receiver)
            throw std::runtime_error("wrong authorization bound to guardian transfer");
        parents.push_back(approval.id(2));
    }
    std::sort(parents.begin(),parents.end());
    if(n!=auth_count||parents!=o.parents)
        throw std::runtime_error("both original parents must authorize new guardian");
    Q children(db,"SELECT child_id,guardian_id,parent_a,parent_b,generation,tick "
                  "FROM npc_family_guardianship WHERE scope=? AND transfer_id=? ORDER BY child_id");
    children.s(1,family);children.s(2,id);
    std::vector<std::uint64_t> found;
    while(children.row()){
        const auto child=children.id(0),guardian=children.id(1),a=children.id(2),b=children.id(3);
        if(guardian!=receiver||children.number(4)!=gen||children.number(5)!=tick||
           std::vector<std::uint64_t>{a,b}!=
               std::vector<std::uint64_t>{o.parents[0],o.parents.size()>1?o.parents[1]:0})
            throw std::runtime_error("nonparent transfer changed original parentage");
        found.push_back(child);
    }
    if(found!=o.child_ids)throw std::runtime_error("legal custody missing a child");
}
// A physical handoff binds the *current berth* and an authorized legal
// designation without changing the original parents in the ferry journal.
void physical_schema(sqlite3* db){
    if(!column(db,"dependent_physical_handoff_count"))
        exec(db,"ALTER TABLE world_meta ADD COLUMN dependent_physical_handoff_count "
                "INTEGER NOT NULL DEFAULT 0 CHECK(dependent_physical_handoff_count>=0)");
    exec(db,"CREATE TABLE IF NOT EXISTS sg_dependent_physical_handoff ("
        "handoff_id TEXT PRIMARY KEY,legal_transfer_id TEXT NOT NULL UNIQUE,"
        "family_scope TEXT NOT NULL,guardian_id TEXT NOT NULL,transport_id TEXT NOT NULL,"
        "ferry TEXT NOT NULL,generation INTEGER NOT NULL,tick INTEGER NOT NULL,"
        "member_count INTEGER NOT NULL CHECK(member_count BETWEEN 1 AND 15))");
    exec(db,"CREATE TABLE IF NOT EXISTS sg_dependent_physical_member ("
        "handoff_id TEXT NOT NULL,child_id TEXT NOT NULL,"
        "PRIMARY KEY(handoff_id,child_id),UNIQUE(child_id,handoff_id))");
    triggers(db,"sg_dependent_physical_handoff");
    triggers(db,"sg_dependent_physical_member");
}
void physical_check(sqlite3* db,const std::string& id,std::uint64_t maxgen){
    Q h(db,"SELECT legal_transfer_id,family_scope,guardian_id,transport_id,"
           "ferry,generation,tick,member_count FROM sg_dependent_physical_handoff "
           "WHERE handoff_id=?");h.s(1,id);
    if(!h.row())throw std::runtime_error("missing physical custody handoff");
    const auto legal=h.str(0),family=h.str(1);
    const auto guardian=h.id(2),transport=h.id(3),
        ferry=h.id(4),gen=h.number(5),tick=h.number(6),count=h.number(7);
    if(h.row()||!gen||gen>maxgen||tick!=tick_for(db,gen)||count<1||count>15)
        throw std::runtime_error("invalid physical custody provenance");
    validate_transfer(db,legal,maxgen);
    Q t(db,"SELECT outcome_id,family_scope,guardian_id,world_generation FROM "
           "sg_guardian_transfer WHERE transfer_id=?");t.s(1,legal);
    if(!t.row())throw std::runtime_error("physical transfer legal authorization missing");
    const auto outcome_id=t.str(0);
    if(t.str(1)!=family||t.id(2)!=guardian||t.number(3)>gen||t.row())
        throw std::runtime_error("physical transfer lacks prior matching legal authorization");
    const auto o=outcome(db,outcome_id);
    if(o.family!=family||o.voyage!=transport||o.ferry!=ferry||
       o.child_ids.size()!=count)
        throw std::runtime_error("physical transfer mismatch with actual rescue voyage");
    confirm(db,o,gen,guardian);
    Q members(db,"SELECT child_id FROM sg_dependent_physical_member "
                 "WHERE handoff_id=? ORDER BY child_id");members.s(1,id);
    std::vector<std::uint64_t> actual;
    while(members.row())actual.push_back(members.id(0));
    if(actual!=o.child_ids)throw std::runtime_error("physical custody has missing child");
    // All children must be accounted for on a real berth, possibly on a
    // successor rescue voyage or having arrived. The new legal escort must be
    // physically at the SAME custody location as every child at replay.
    const auto now=snapshot(db,maxgen).sim;
    for(const auto child:actual){
        Q legs(db,"SELECT transport_id,ferry,destination,status FROM sg_dependent_travel "
                  "WHERE family_scope=? AND child_id=? ORDER BY board_generation DESC LIMIT 1");
        legs.s(1,family);legs.s(2,std::to_string(child));
        if(!legs.row())throw std::runtime_error("physical child custody leg vanished");
        const auto live_transport=legs.id(0),live_ferry=legs.id(1),destination=legs.id(2);
        const auto status=legs.number(3);
        if(legs.row()||(status!=1&&status!=2))
            throw std::runtime_error("invalid current dependent custody status");
        auto person=now.npc_records().find(guardian);
        if(person==now.npc_records().end()||
           (status==1 && (person->second.location!=live_ferry ||
                           person->second.transport_id!=live_transport)) ||
           (status==2 && (person->second.location!=destination ||
                           person->second.transport_id!=0)))
            throw std::runtime_error("legal escort separated from dependent passenger");
    }
}
void physical_validate(sqlite3* db,std::uint64_t maxgen){
    const bool meta=column(db,"dependent_physical_handoff_count");
    const bool head=exists(db,"table","sg_dependent_physical_handoff");
    const bool child=exists(db,"table","sg_dependent_physical_member");
    if(!meta&&!head&&!child)return;
    if(!meta||!head||!child)
        throw std::runtime_error("physical custody activated ledger missing");
    for(const auto* table:{"sg_dependent_physical_handoff","sg_dependent_physical_member"})
        for(const auto* suffix:{"_immutable_update","_immutable_delete"})
            if(!exists(db,"trigger",std::string(table)+suffix))
                throw std::runtime_error("physical custody immutable receipt trigger removed");
    Q anchor(db,"SELECT dependent_physical_handoff_count FROM world_meta WHERE singleton=1");
    if(!anchor.row())throw std::runtime_error("physical custody count anchor missing");
    const auto expected=anchor.number(0);
    Q row(db,"SELECT handoff_id FROM sg_dependent_physical_handoff ORDER BY handoff_id");
    std::uint64_t actual=0;
    while(row.row()){physical_check(db,row.str(0),maxgen);++actual;}
    if(actual!=expected)throw std::runtime_error("physical custody handoff receipt count tampered");
    Q orphan(db,"SELECT COUNT(*) FROM sg_dependent_physical_member m "
                "LEFT JOIN sg_dependent_physical_handoff h ON m.handoff_id=h.handoff_id "
                "WHERE h.handoff_id IS NULL");
    if(!orphan.row()||orphan.number(0))
        throw std::runtime_error("physical child custody orphan member");
}
void validate_database(sqlite3* db,std::uint64_t maxgen){
    const bool ac=column(db,"dependent_guardian_approval_count");
    const bool tc=column(db,"dependent_guardian_transfer_count");
    const bool a=exists(db,"table","sg_guardian_approval");
    const bool t=exists(db,"table","sg_guardian_transfer");
    const bool x=exists(db,"table","sg_guardian_transfer_auth");
    const bool f=exists(db,"table","npc_family_guardianship");
    if(!ac&&!tc&&!a&&!t&&!x&&!f)return; // legacy database
    if(!ac||!tc||!a||!t||!x||!f)
        throw std::runtime_error("activated legal guardian ledger incomplete");
    for(const auto* tab:{"sg_guardian_approval","sg_guardian_transfer",
                         "sg_guardian_transfer_auth","npc_family_guardianship"})
        for(const auto* suffix:{"_immutable_update","_immutable_delete"})
            if(!exists(db,"trigger",std::string(tab)+suffix))
                throw std::runtime_error("legal guardian immutability trigger missing");
    Q count(db,"SELECT dependent_guardian_approval_count,dependent_guardian_transfer_count "
               "FROM world_meta WHERE singleton=1");
    if(!count.row())throw std::runtime_error("legal guardian anchor missing");
    const auto approvals=count.number(0),transfers=count.number(1);
    Q arows(db,"SELECT authorization_id FROM sg_guardian_approval ORDER BY authorization_id");
    std::uint64_t seen=0;
    while(arows.row()){++seen;validate_approval(db,arows.str(0),maxgen);}
    if(seen!=approvals)throw std::runtime_error("parental consent anchor/count changed");
    Q trows(db,"SELECT transfer_id FROM sg_guardian_transfer ORDER BY transfer_id");
    seen=0;
    while(trows.row()){++seen;validate_transfer(db,trows.str(0),maxgen);}
    if(seen!=transfers)throw std::runtime_error("nonparent guardian anchor/count changed");
    Q extras(db,"SELECT COUNT(*) FROM sg_guardian_transfer_auth l LEFT JOIN sg_guardian_transfer t "
                "ON t.transfer_id=l.transfer_id WHERE t.transfer_id IS NULL");
    if(!extras.row()||extras.number(0)!=0)
        throw std::runtime_error("orphan legal guardian approval association");
    Q orphan(db,"SELECT COUNT(*) FROM npc_family_guardianship g LEFT JOIN sg_guardian_transfer t "
                "ON t.transfer_id=g.transfer_id WHERE t.transfer_id IS NULL");
    if(!orphan.row()||orphan.number(0)!=0)
        throw std::runtime_error("orphan legal guardian family receipt");
    physical_validate(db,maxgen);
}
} // namespace

// Called by the authoritative world writer with its *same transaction*.
// If a later ferry step separates an assigned escort from a child, reject
// BEFORE committing or publishing the new world state, not just on restart.
void enforce_physical_guardianship_at_commit(sqlite3* db,std::uint64_t generation){
    physical_validate(db,generation);
}

void verify_nonparent_guardianship_ledger(const std::string& path,
                                          std::uint64_t max_generation){
    DB db(path);validate_database(db.db,max_generation);
}

void PersistentWorld::record_nonparent_guardian_approval(
    const NonParentGuardianApprovalPlan& p,FailPoint fail){
    if(!valid(p.authorization_id)||!valid(p.rescue_outcome_id)||!valid(p.family_scope)||
       !p.expected_world_generation||p.expected_world_generation!=generation()||
       !p.approving_parent_id||!p.receiving_guardian_id)
        throw std::invalid_argument("bad or stale host guardianship approval");
    ordered_ids(p.child_ids);
    DB db(db_path_);exec(db.db,"BEGIN IMMEDIATE");
    try{
        if(current_gen(db.db)!=p.expected_world_generation)
            throw std::runtime_error("stale legal guardian world generation");
        const auto o=outcome(db.db,p.rescue_outcome_id);
        if(o.family!=p.family_scope||o.child_ids!=p.child_ids||
           !std::binary_search(o.parents.begin(),o.parents.end(),p.approving_parent_id))
            throw std::runtime_error("parent not authorized by family rescue history");
        confirm(db.db,o,generation(),p.receiving_guardian_id);
        schema(db.db);
        validate_database(db.db,generation());
        Q add(db.db,"INSERT INTO sg_guardian_approval VALUES(?,?,?,?,?,?,?,?)");
        add.s(1,p.authorization_id);add.s(2,p.rescue_outcome_id);add.s(3,p.family_scope);
        add.s(4,std::to_string(p.approving_parent_id));
        add.s(5,std::to_string(p.receiving_guardian_id));
        add.s(6,joined(p.child_ids));add.n(7,generation());add.n(8,tick_for(db.db,generation()));
        add.done();
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected parental approval rollback");
        exec(db.db,"UPDATE world_meta SET dependent_guardian_approval_count="
                   "dependent_guardian_approval_count+1 WHERE singleton=1");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected parental approval anchor rollback");
        validate_database(db.db,generation());
        exec(db.db,"COMMIT");
    }catch(...){sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}

void PersistentWorld::settle_nonparent_guardianship(
    const NonParentGuardianshipPlan& p,FailPoint fail){
    if(!valid(p.transfer_id)||!valid(p.rescue_outcome_id)||!valid(p.family_scope)||
       !p.expected_world_generation||p.expected_world_generation!=generation()||
       !p.receiving_guardian_id)
        throw std::invalid_argument("bad or stale nonparent guardianship transfer");
    ordered_ids(p.child_ids);
    if(p.authorizations.empty()||p.authorizations.size()>2||
       !std::is_sorted(p.authorizations.begin(),p.authorizations.end())||
       std::adjacent_find(p.authorizations.begin(),p.authorizations.end())!=p.authorizations.end())
        throw std::invalid_argument("guardian transfer requires ordered distinct approvals");
    DB db(db_path_);exec(db.db,"BEGIN IMMEDIATE");
    try{
        if(current_gen(db.db)!=p.expected_world_generation)
            throw std::runtime_error("guardian transfer stale world generation");
        schema(db.db);validate_database(db.db,generation());
        const auto o=outcome(db.db,p.rescue_outcome_id);
        if(o.family!=p.family_scope||o.child_ids!=p.child_ids||
           p.authorizations.size()!=o.parents.size())
            throw std::runtime_error("guardian transfer lacks complete rescue family");
        confirm(db.db,o,generation(),p.receiving_guardian_id);
        std::vector<std::uint64_t> parents;
        for(const auto& authorization:p.authorizations){
            validate_approval(db.db,authorization,generation());
            Q q(db.db,"SELECT outcome_id,family_scope,parent_id,receiving_guardian "
                      "FROM sg_guardian_approval WHERE authorization_id=?");
            q.s(1,authorization);
            if(!q.row()||q.str(0)!=p.rescue_outcome_id||q.str(1)!=p.family_scope||
               q.id(3)!=p.receiving_guardian_id)
                throw std::runtime_error("consent for different child/recipient");
            parents.push_back(q.id(2));
        }
        std::sort(parents.begin(),parents.end());
        if(parents!=o.parents)throw std::runtime_error("both parent approvals required");
        Q add(db.db,"INSERT INTO sg_guardian_transfer VALUES(?,?,?,?,?,?,?,?)");
        add.s(1,p.transfer_id);add.s(2,p.rescue_outcome_id);add.s(3,p.family_scope);
        add.s(4,std::to_string(p.receiving_guardian_id));
        add.n(5,generation());add.n(6,tick_for(db.db,generation()));
        add.n(7,p.child_ids.size());add.n(8,p.authorizations.size());add.done();
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected guardian settlement header rollback");
        for(const auto& authorization:p.authorizations){
            Q link(db.db,"INSERT INTO sg_guardian_transfer_auth VALUES(?,?)");
            link.s(1,p.transfer_id);link.s(2,authorization);link.done();
        }
        for(const auto child:p.child_ids){
            Q previous(db.db,"SELECT 1 FROM npc_family_guardianship "
                "WHERE scope=? AND child_id=?");
            previous.s(1,p.family_scope);previous.s(2,std::to_string(child));
            if(previous.row())throw std::runtime_error("dependent already has nonparent guardian");
            Q family(db.db,"INSERT INTO npc_family_guardianship VALUES(?,?,?,?,?,?,?,?,?)");
            family.s(1,p.family_scope);family.s(2,p.transfer_id+":"+std::to_string(child));
            family.s(3,std::to_string(child));family.s(4,std::to_string(p.receiving_guardian_id));
            family.s(5,p.transfer_id);family.s(6,std::to_string(o.parents[0]));
            family.s(7,std::to_string(o.parents.size()>1?o.parents[1]:0));
            family.n(8,generation());family.n(9,tick_for(db.db,generation()));family.done();
        }
        exec(db.db,"UPDATE world_meta SET dependent_guardian_transfer_count="
                   "dependent_guardian_transfer_count+1 WHERE singleton=1");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected guardian transfer members rollback");
        validate_database(db.db,generation());
        exec(db.db,"COMMIT");
    }catch(...){sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}

VerifiedNonParentGuardianship PersistentWorld::verify_nonparent_guardianship(
    const std::string& transfer_id) const{
    if(!valid(transfer_id))throw std::invalid_argument("invalid guardian transfer ID");
    verify_nonparent_guardianship_ledger(db_path_,generation());
    DB db(db_path_);
    validate_transfer(db.db,transfer_id,generation());
    Q t(db.db,"SELECT outcome_id,guardian_id,world_generation FROM sg_guardian_transfer "
              "WHERE transfer_id=?");t.s(1,transfer_id);
    if(!t.row())throw std::runtime_error("unknown guardian transfer");
    VerifiedNonParentGuardianship result{};
    result.transfer_id=transfer_id;result.rescue_outcome_id=t.str(0);
    result.guardian_id=t.id(1);result.world_generation=t.number(2);
    result.child_ids=outcome(db.db,result.rescue_outcome_id).child_ids;
    return result;
}

void PersistentWorld::transfer_dependent_physical_custody(
    const DependentPhysicalHandoffPlan& p,FailPoint fail){
    if(!valid(p.handoff_id)||!valid(p.legal_transfer_id)||!valid(p.family_scope)||
       !p.expected_world_generation||p.expected_world_generation!=generation()||
       !p.receiving_guardian_id||!p.transport_id||!p.ferry)
        throw std::invalid_argument("invalid physical guardian handoff");
    ordered_ids(p.child_ids);
    DB db(db_path_);exec(db.db,"BEGIN IMMEDIATE");
    try{
        if(current_gen(db.db)!=generation())
            throw std::runtime_error("stale world physical custody generation");
        validate_database(db.db,generation());
        validate_transfer(db.db,p.legal_transfer_id,generation());
        Q t(db.db,"SELECT outcome_id,family_scope,guardian_id FROM sg_guardian_transfer "
                  "WHERE transfer_id=?");t.s(1,p.legal_transfer_id);
        if(!t.row()||t.str(1)!=p.family_scope||t.id(2)!=p.receiving_guardian_id)
            throw std::runtime_error("physical escort has no authorized legal designation");
        const auto o=outcome(db.db,t.str(0));
        if(o.voyage!=p.transport_id||o.ferry!=p.ferry||o.child_ids!=p.child_ids)
            throw std::runtime_error("physical escort is not aboard authorized rescue vessel");
        confirm(db.db,o,generation(),p.receiving_guardian_id);
        physical_schema(db.db);
        Q insert(db.db,"INSERT INTO sg_dependent_physical_handoff VALUES(?,?,?,?,?,?,?,?,?)");
        insert.s(1,p.handoff_id);insert.s(2,p.legal_transfer_id);
        insert.s(3,p.family_scope);insert.s(4,std::to_string(p.receiving_guardian_id));
        insert.s(5,std::to_string(p.transport_id));insert.s(6,std::to_string(p.ferry));
        insert.n(7,generation());insert.n(8,tick_for(db.db,generation()));
        insert.n(9,p.child_ids.size());insert.done();
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected physical handoff header rollback");
        for(const auto child:p.child_ids){
            Q m(db.db,"INSERT INTO sg_dependent_physical_member VALUES(?,?)");
            m.s(1,p.handoff_id);m.s(2,std::to_string(child));m.done();
        }
        exec(db.db,"UPDATE world_meta SET dependent_physical_handoff_count="
                   "dependent_physical_handoff_count+1 WHERE singleton=1");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected physical handoff member rollback");
        validate_database(db.db,generation());
        exec(db.db,"COMMIT");
    }catch(...){sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}

VerifiedDependentPhysicalHandoff PersistentWorld::verify_dependent_physical_custody(
    const std::string& handoff_id) const{
    if(!valid(handoff_id))throw std::invalid_argument("invalid physical custody lookup ID");
    verify_nonparent_guardianship_ledger(db_path_,generation());
    DB db(db_path_);physical_check(db.db,handoff_id,generation());
    Q h(db.db,"SELECT legal_transfer_id,guardian_id,transport_id,ferry,generation "
              "FROM sg_dependent_physical_handoff WHERE handoff_id=?");h.s(1,handoff_id);
    if(!h.row())throw std::runtime_error("physical custody receipt missing");
    VerifiedDependentPhysicalHandoff result{};
    result.handoff_id=handoff_id;result.legal_transfer_id=h.str(0);
    result.receiving_guardian_id=h.id(1);result.transport_id=h.id(2);
    result.ferry=h.id(3);result.generation=h.number(4);
    Q members(db.db,"SELECT child_id FROM sg_dependent_physical_member "
                    "WHERE handoff_id=? ORDER BY child_id");members.s(1,handoff_id);
    while(members.row())result.child_ids.push_back(members.id(0));
    return result;
}

} // namespace galaxy
