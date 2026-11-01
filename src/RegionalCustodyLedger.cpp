#include "galaxy/RegionalCustodyLedger.hpp"
#include "galaxy/RegionalHostCommands.hpp"
#include <limits>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace galaxy::regional_events {
namespace {
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        std::string msg=err?err:sqlite3_errmsg(db);sqlite3_free(err);
        throw std::runtime_error("regional custody SQLite: "+msg);
    }
}
class Query {
public:
    Query(sqlite3* db,const char* sql):db_(db){
        if(sqlite3_prepare_v2(db_,sql,-1,&q_,nullptr)!=SQLITE_OK)
            throw std::runtime_error("prepare custody query: "+std::string(sqlite3_errmsg(db_)));
    }
    ~Query(){sqlite3_finalize(q_);}
    Query(const Query&)=delete;Query& operator=(const Query&)=delete;
    void s(int i,const std::string& v){
        if(sqlite3_bind_text(q_,i,v.data(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("bind custody text");
    }
    void n(int i,std::uint64_t v){
        if(v>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())||
           sqlite3_bind_int64(q_,i,static_cast<std::int64_t>(v))!=SQLITE_OK)
            throw std::runtime_error("bind custody integer");
    }
    int step(){return sqlite3_step(q_);}
    sqlite3_stmt* get()const{return q_;}
private:
    sqlite3* db_=nullptr;sqlite3_stmt* q_=nullptr;
};
void done(Query& q,const char* msg){if(q.step()!=SQLITE_DONE)throw std::runtime_error(msg);}
std::string str(sqlite3_stmt* s,int idx){
    const auto* p=sqlite3_column_text(s,idx);
    if(!p)throw std::runtime_error("invalid custody text");
    return std::string(reinterpret_cast<const char*>(p),static_cast<std::size_t>(sqlite3_column_bytes(s,idx)));
}
std::uint64_t num(sqlite3_stmt* s,int idx){
    const auto n=sqlite3_column_int64(s,idx);
    if(n<0)throw std::runtime_error("invalid custody nonnegative integer");
    return static_cast<std::uint64_t>(n);
}
CustodyEntry entry(sqlite3_stmt* s){
    CustodyEntry e;
    e.shipment_id=num(s,0);e.handoff_id=str(s,1);e.origin=str(s,2);e.destination=str(s,3);
    e.departure_tick=num(s,4);e.arrival_tick=num(s,5);
    e.cargo={static_cast<std::int64_t>(num(s,6)),static_cast<std::int64_t>(num(s,7)),static_cast<std::int64_t>(num(s,8))};
    const auto stage=num(s,9);
    if(stage>2 || !e.shipment_id)throw std::runtime_error("invalid custody stage/id");
    e.stage=static_cast<ShipmentStage>(stage);
    return e;
}
void match(const CustodyEntry& e,const Shipment& sh){
    if(e.shipment_id!=sh.id||e.origin!=sh.origin||e.destination!=sh.destination||
       e.departure_tick!=sh.departure_tick||e.arrival_tick!=sh.arrival_tick||
       e.cargo.food_kg!=sh.cargo.food_kg||e.cargo.water_kg!=sh.cargo.water_kg||
       e.cargo.oxygen_kg!=sh.cargo.oxygen_kg)
        throw std::runtime_error("physical custody does not match scheduler shipment");
}
} // namespace
CustodyLedger::CustodyLedger(const std::string& filename,const std::string& scope):scope_(scope){
    if(filename.empty()||scope.empty())throw std::invalid_argument("custody file and scope required");
    const int rc=sqlite3_open_v2(filename.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr);
    if(rc!=SQLITE_OK){std::string msg=db_?sqlite3_errmsg(db_):"open failed";
        sqlite3_close(db_);db_=nullptr;throw std::runtime_error(msg);}
    sqlite3_busy_timeout(db_,3000);
}
CustodyLedger::~CustodyLedger(){sqlite3_close(db_);}
bool CustodyLedger::enabled() const{
    Query q(db_,"SELECT 1 FROM sg_regional_custody_scope WHERE scope=?1");q.s(1,scope_);
    const auto rc=q.step();
    if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw std::runtime_error("custody enabled query failed");
    return rc==SQLITE_ROW;
}
void CustodyLedger::enable(const VersionedCheckpoint& current){
    if(!current.revision)throw std::invalid_argument("custody activation requires saved checkpoint");
    for(const auto& kv:current.scheduler.shipments()){
        if(kv.second.stage!=ShipmentStage::Delivered)
            throw std::invalid_argument("cannot opt in custody with unverified legacy shipment");
    }
    exec(db_,"BEGIN IMMEDIATE");
    try{
        Query q(db_,"SELECT revision FROM sg_regional_checkpoint WHERE scope=?1");q.s(1,scope_);
        if(q.step()!=SQLITE_ROW || num(q.get(),0)!=current.revision)
            throw std::runtime_error("stale checkpoint during physical custody activation");
        Query ins(db_,"INSERT OR IGNORE INTO sg_regional_custody_scope(scope) VALUES (?1)");
        ins.s(1,scope_);done(ins,"custody activation insert failed");
        exec(db_,"COMMIT");
    }catch(...){try{exec(db_,"ROLLBACK");}catch(...){}throw;}
}
std::optional<CustodyEntry> CustodyLedger::find(std::uint64_t id)const{
    Query q(db_,"SELECT shipment_id,handoff_id,origin,destination,depart_tick,arrive_tick,"
                "food,water,oxygen,stage FROM sg_regional_shipment_custody "
                "WHERE scope=?1 AND shipment_id=?2");
    q.s(1,scope_);q.n(2,id);
    const auto rc=q.step();
    if(rc==SQLITE_DONE)return std::nullopt;
    if(rc!=SQLITE_ROW)throw std::runtime_error("custody find failed");
    return entry(q.get());
}
void CustodyLedger::record(sqlite3* db,const HostCommand& cmd,
                          const Scheduler& before,const Scheduler& after,std::uint64_t outcome)const{
    if(!db)throw std::invalid_argument("missing same-transaction SQLite connection");
    const char* hostfile=sqlite3_db_filename(db,"main");
    const char* ledgerfile=sqlite3_db_filename(db_,"main");
    std::error_code ec;
    if(!hostfile||!ledgerfile ||
       !std::filesystem::equivalent(hostfile,ledgerfile,ec) || ec)
        throw std::runtime_error("physical custody ledger must share the checkpoint SQLite database");
    if(cmd.op==HostOp::ScheduleShipment){
        const auto& sh=after.shipment(outcome);
        if(sh.stage!=ShipmentStage::Reserved||sh.origin!=cmd.origin||
           sh.destination!=cmd.destination||sh.cargo.food_kg!=cmd.cargo.food_kg||
           sh.cargo.water_kg!=cmd.cargo.water_kg||sh.cargo.oxygen_kg!=cmd.cargo.oxygen_kg||
           sh.departure_tick!=cmd.at_tick || sh.arrival_tick!=cmd.at_tick+cmd.travel_ticks)
            throw std::runtime_error("new shipment custody mismatch");
        if(cmd.carrier_receipt.empty()||cmd.carrier_receipt.size()>4096)
            throw std::invalid_argument("bounded physical handoff ID required");
        Query ins(db,"INSERT INTO sg_regional_shipment_custody (scope,shipment_id,handoff_id,"
                     "origin,destination,depart_tick,arrive_tick,food,water,oxygen,stage) "
                     "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,0)");
        ins.s(1,scope_);ins.n(2,outcome);ins.s(3,cmd.carrier_receipt);
        ins.s(4,sh.origin);ins.s(5,sh.destination);ins.n(6,sh.departure_tick);
        ins.n(7,sh.arrival_tick);ins.n(8,sh.cargo.food_kg);
        ins.n(9,sh.cargo.water_kg);ins.n(10,sh.cargo.oxygen_kg);
        done(ins,"custody handoff collision or insert failure");
    }else if(cmd.op==HostOp::Advance){
        // Only in-flight/reserved manifests; delivered history is not scanned.
        Query q(db,"SELECT shipment_id,handoff_id,origin,destination,depart_tick,arrive_tick,"
                   "food,water,oxygen,stage FROM sg_regional_shipment_custody "
                   "WHERE scope=?1 AND stage<2 AND depart_tick<=?2");
        q.s(1,scope_);q.n(2,after.tick());
        std::vector<std::pair<std::uint64_t,ShipmentStage>> changes;
        for(;;){
            const auto rc=q.step();if(rc==SQLITE_DONE)break;
            if(rc!=SQLITE_ROW)throw std::runtime_error("custody progress query failed");
            auto e=entry(q.get());
            const auto& b=before.shipment(e.shipment_id);
            const auto& a=after.shipment(e.shipment_id);
            match(e,b);match(e,a);
            if(b.stage!=e.stage||static_cast<int>(a.stage)<static_cast<int>(e.stage))
                throw std::runtime_error("custody cannot roll back or diverge from physical state");
            if(a.stage!=e.stage)changes.emplace_back(e.shipment_id,a.stage);
        }
        for(const auto& [id,newstage]:changes){
            Query u(db,"UPDATE sg_regional_shipment_custody SET stage=?3 "
                       "WHERE scope=?1 AND shipment_id=?2 AND stage<?3");
            u.s(1,scope_);u.n(2,id);u.n(3,static_cast<std::uint64_t>(newstage));
            done(u,"custody progress update failed");
            if(sqlite3_changes(db)!=1)throw std::runtime_error("custody progress CAS failed");
        }
    }
}
} // namespace galaxy::regional_events
