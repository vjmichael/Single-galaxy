#include "galaxy/ColonySafetyNotice.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <limits>
#include <set>

namespace galaxy::colony_safety::notice {
namespace {
void exec(sqlite3* db,const char* sql) {
    char* err=nullptr;
    if(!db || sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string reason=err?err:"null or inaccessible world writer";
        sqlite3_free(err);throw std::runtime_error("colony safety outbox: "+reason);
    }
}
struct Query {
    sqlite3* db;
    sqlite3_stmt* stmt=nullptr;
    Query(sqlite3* d,const char* sql):db(d){
        if(!d||sqlite3_prepare_v2(d,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("colony safety outbox SQL prepare failed");
    }
    ~Query(){sqlite3_finalize(stmt);}
    void num(int column,std::uint64_t value){
        if(value>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(stmt,column,static_cast<sqlite3_int64>(value))!=SQLITE_OK)
            throw std::runtime_error("colony safety outbox number out of range");
    }
    bool row(){
        const auto result=sqlite3_step(stmt);
        if(result==SQLITE_ROW)return true;
        if(result!=SQLITE_DONE)throw std::runtime_error("colony safety outbox row failed");
        return false;
    }
    void done(){
        if(sqlite3_step(stmt)!=SQLITE_DONE)
            throw std::runtime_error(std::string("colony safety outbox mutation failed: ")+sqlite3_errmsg(db));
    }
};
std::uint64_t integer(sqlite3_stmt* stmt,int column){
    if(sqlite3_column_type(stmt,column)!=SQLITE_INTEGER ||
       sqlite3_column_int64(stmt,column)<0)
        throw std::runtime_error("invalid outbox integer");
    return static_cast<std::uint64_t>(sqlite3_column_int64(stmt,column));
}
std::uint64_t count(sqlite3* db,const char* sql){
    Query q(db,sql);
    if(!q.row())throw std::runtime_error("missing outbox count");
    const auto n=integer(q.stmt,0);
    if(q.row())throw std::runtime_error("multiple outbox counts");
    return n;
}
int schema(sqlite3* db){
    Query q(db,"SELECT name FROM sqlite_master WHERE type='table' AND name IN "
      "('sg_colony_npc_opportunities','sg_colony_npc_acks','sg_colony_npc_notice_meta')");
    int n=0;while(q.row())++n;
    if(n && n!=3)throw std::runtime_error("incomplete NPC notification schema");
    return n;
}
void create(sqlite3* db){
    exec(db,"CREATE TABLE sg_colony_npc_notice_meta("
      "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
      "generation INTEGER NOT NULL CHECK(generation>0),"
      "opportunities INTEGER NOT NULL CHECK(opportunities>=0),"
      "acknowledged INTEGER NOT NULL CHECK(acknowledged>=0))");
    exec(db,"CREATE TABLE sg_colony_npc_opportunities("
      "event_id INTEGER NOT NULL, npc_id INTEGER NOT NULL CHECK(npc_id>0),"
      "generation INTEGER NOT NULL CHECK(generation>0),"
      "tick INTEGER NOT NULL CHECK(tick>=0),"
      "site INTEGER NOT NULL CHECK(site>0),risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3),"
      "PRIMARY KEY(event_id,npc_id),"
      "FOREIGN KEY(event_id) REFERENCES sg_colony_response_events(id))");
    exec(db,"CREATE TABLE sg_colony_npc_acks("
      "event_id INTEGER NOT NULL,npc_id INTEGER NOT NULL,"
      "delivery_generation INTEGER NOT NULL CHECK(delivery_generation>0),"
      "delivery_tick INTEGER NOT NULL CHECK(delivery_tick>=0),"
      "PRIMARY KEY(event_id,npc_id),"
      "FOREIGN KEY(event_id,npc_id) REFERENCES sg_colony_npc_opportunities(event_id,npc_id))");
    exec(db,"CREATE TRIGGER sg_npc_opportunity_no_update BEFORE UPDATE ON sg_colony_npc_opportunities "
      "BEGIN SELECT RAISE(ABORT,'immutable opportunity');END");
    exec(db,"CREATE TRIGGER sg_npc_opportunity_no_delete BEFORE DELETE ON sg_colony_npc_opportunities "
      "BEGIN SELECT RAISE(ABORT,'immutable opportunity');END");
    exec(db,"CREATE TRIGGER sg_npc_ack_no_update BEFORE UPDATE ON sg_colony_npc_acks "
      "BEGIN SELECT RAISE(ABORT,'immutable acknowledgment');END");
    exec(db,"CREATE TRIGGER sg_npc_ack_no_delete BEFORE DELETE ON sg_colony_npc_acks "
      "BEGIN SELECT RAISE(ABORT,'immutable acknowledgment');END");
}
void update(sqlite3* db,std::uint64_t generation){
    const auto n=count(db,"SELECT COUNT(*) FROM sg_colony_npc_opportunities");
    const auto a=count(db,"SELECT COUNT(*) FROM sg_colony_npc_acks");
    Query q(db,"UPDATE sg_colony_npc_notice_meta SET generation=?, opportunities=?,acknowledged=? "
                "WHERE singleton=1");
    q.num(1,generation);q.num(2,n);q.num(3,a);q.done();
    if(sqlite3_changes(db)!=1)throw std::runtime_error("notification metadata missing");
}
}
void verify(sqlite3* db,const Simulation& current,std::uint64_t generation){
    (void)current;
    if(!schema(db))return; // pre-v70 worlds
    Query m(db,"SELECT generation,opportunities,acknowledged FROM sg_colony_npc_notice_meta WHERE singleton=1");
    if(!m.row())throw std::runtime_error("NPC outbox missing metadata");
    const auto anchored_generation=integer(m.stmt,0);
    const auto expected=integer(m.stmt,1),acked=integer(m.stmt,2);
    if(m.row()||anchored_generation!=generation)
        throw std::runtime_error("NPC outbox anchored to wrong world generation");
    if(count(db,"SELECT COUNT(*) FROM sg_colony_npc_opportunities")!=expected||
       count(db,"SELECT COUNT(*) FROM sg_colony_npc_acks")!=acked||acked>expected)
        throw std::runtime_error("NPC outbox count anchor mismatch");
    // Source must be a real immutable, committed physical shortage transition.
    if(count(db,"SELECT COUNT(*) FROM sg_colony_npc_opportunities o "
        "LEFT JOIN sg_colony_response_events e ON e.id=o.event_id WHERE e.id IS NULL "
        "OR e.generation!=o.generation OR e.tick!=o.tick "
        "OR e.site!=o.site OR e.new_risk!=o.risk")!=0)
        throw std::runtime_error("NPC opportunity lacks exact transition provenance");
    if(count(db,"SELECT COUNT(*) FROM sg_colony_npc_acks a LEFT JOIN sg_colony_npc_opportunities o "
        "ON o.event_id=a.event_id AND o.npc_id=a.npc_id "
        "WHERE o.event_id IS NULL OR a.delivery_generation<o.generation "
        "OR a.delivery_generation> (SELECT generation FROM sg_colony_npc_notice_meta) "
        "OR a.delivery_tick<o.tick")!=0)
        throw std::runtime_error("NPC acknowledgment lacks outbox source");
}
void publish(sqlite3* db,const Simulation& state,std::uint64_t generation){
    if(!db||!generation)throw std::invalid_argument("NPC outbox requires committed generation");
    if(!schema(db)){
        create(db);
        Query start(db,"INSERT INTO sg_colony_npc_notice_meta VALUES(1,?,0,0)");
        start.num(1,generation);start.done();
    }
    // The exact physical roster at the threshold tick is what permits a local
    // observation OPPORTUNITY. It is not evidence of actual hearing, knowledge,
    // player blame, or social propagation.
    Query events(db,"SELECT id,tick,site,new_risk FROM sg_colony_response_events "
                    "WHERE generation=? ORDER BY id");
    events.num(1,generation);
    while(events.row()){
        const auto event_id=integer(events.stmt,0),tick=integer(events.stmt,1);
        const auto site=integer(events.stmt,2),risk=integer(events.stmt,3);
        if(risk==0)continue; // Recovery is a different notification protocol.
        for(const auto& [npc_id,npc]:state.npc_records()){
            if(!npc_id||npc.location!=site||npc.transport_id!=0)continue;
            Query insert(db,"INSERT OR IGNORE INTO sg_colony_npc_opportunities"
                          "(event_id,npc_id,generation,tick,site,risk) VALUES(?,?,?,?,?,?)");
            insert.num(1,event_id);insert.num(2,npc_id);insert.num(3,generation);
            insert.num(4,tick);insert.num(5,site);insert.num(6,risk);insert.done();
        }
    }
    update(db,generation);
    verify(db,state,generation);
}
std::vector<Pending> pending(sqlite3* db,std::uint64_t limit){
    std::vector<Pending> records;
    if(!schema(db)||limit==0)return records;
    if(limit>1000)throw std::invalid_argument("unbounded NPC notification query");
    Query q(db,"SELECT o.event_id,o.npc_id,o.generation,o.tick,o.site,o.risk "
        "FROM sg_colony_npc_opportunities o LEFT JOIN sg_colony_npc_acks a "
        "ON a.event_id=o.event_id AND a.npc_id=o.npc_id WHERE a.event_id IS NULL "
        "ORDER BY o.event_id,o.npc_id LIMIT ?");
    q.num(1,limit);
    while(q.row()){
        records.push_back({integer(q.stmt,0),integer(q.stmt,1),
                           integer(q.stmt,2),integer(q.stmt,3),
                           integer(q.stmt,4),static_cast<int>(integer(q.stmt,5))});
    }
    return records;
}
bool acknowledge(sqlite3* db,const Simulation& current,std::uint64_t generation,
                 std::uint64_t event_id,std::uint64_t npc_id){
    if(!db||!generation||!event_id||!npc_id)
        throw std::invalid_argument("notification acknowledgment requires real NPC and event");
    auto it=current.npc_records().find(npc_id);
    if(it==current.npc_records().end())
        throw std::runtime_error("NPC notification cannot be delivered to nonliving/nonexistent NPC");
    Query source(db,"SELECT 1 FROM sg_colony_npc_opportunities WHERE event_id=? AND npc_id=?");
    source.num(1,event_id);source.num(2,npc_id);
    if(!source.row())throw std::runtime_error("NPC was not locally eligible for this event");
    Query exists(db,"SELECT 1 FROM sg_colony_npc_acks WHERE event_id=? AND npc_id=?");
    exists.num(1,event_id);exists.num(2,npc_id);
    if(exists.row())return false; // delivery retries idempotent
    Query insert(db,"INSERT INTO sg_colony_npc_acks VALUES(?,?,?,?)");
    insert.num(1,event_id);insert.num(2,npc_id);
    insert.num(3,generation);insert.num(4,current.tick());insert.done();
    update(db,generation);
    verify(db,current,generation);
    return true;
}
std::uint64_t opportunity_count(sqlite3* db){
    if(!schema(db))return 0;
    return count(db,"SELECT COUNT(*) FROM sg_colony_npc_opportunities");
}
std::uint64_t acknowledgement_count(sqlite3* db){
    if(!schema(db))return 0;
    return count(db,"SELECT COUNT(*) FROM sg_colony_npc_acks");
}
} // namespace galaxy::colony_safety::notice
