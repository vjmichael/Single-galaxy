#include "galaxy/HostBlueprintRegistry.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy::industry {
namespace {
constexpr std::int64_t kHostIdBase=500000000000000000LL;
// Serializes lifecycle calls across registry instances inside ONE host process.
// This does not turn raw SQLite files or multiple uncoordinated hosts into an
// authenticated multiplayer service; a production host must own a single world.
std::mutex gHostConstructionLifecycle;
class Statement {
public:
    Statement(sqlite3* db,const char* sql):db_(db) {
        if(sqlite3_prepare_v2(db,sql,-1,&stmt_,nullptr)!=SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement(){sqlite3_finalize(stmt_);}
    Statement(const Statement&)=delete;Statement& operator=(const Statement&)=delete;
    void number(int col,std::int64_t n){if(sqlite3_bind_int64(stmt_,col,n)!=SQLITE_OK)throw std::runtime_error("SQLite integer bind failed");}
    void string(int col,const std::string& v){if(sqlite3_bind_text(stmt_,col,v.data(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("SQLite text bind failed");}
    bool row(){const int rc=sqlite3_step(stmt_);if(rc==SQLITE_ROW)return true;if(rc==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db_));}
    void done(){if(row())throw std::runtime_error("unexpected SQLite row");}
    std::int64_t number(int col)const{return sqlite3_column_int64(stmt_,col);}
    std::string string(int col)const{const auto* v=sqlite3_column_text(stmt_,col);return v?reinterpret_cast<const char*>(v):"";}
private:sqlite3* db_=nullptr;sqlite3_stmt* stmt_=nullptr;
};
void exec(sqlite3* db,const char* sql){char* err=nullptr;const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);if(rc!=SQLITE_OK){std::string msg=err?err:"SQLite error";sqlite3_free(err);throw std::runtime_error(msg);}}
std::int64_t signed_id(std::uint64_t v){if(!v || v>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))throw std::invalid_argument("invalid host plan identity");return static_cast<std::int64_t>(v);}
// Stable corruption-detection checksum, NOT a cryptographic signature. The
// trusted host and OS/database access controls remain the security boundary.
std::string digest(std::int64_t id,PlayerId owner,EntityId ship,EntityId site,const std::string& content){
    std::ostringstream data;data<<"sg-host-plan-v1|"<<id<<'|'<<owner<<'|'<<ship<<'|'<<site<<'|'<<content.size()<<'|'<<content;
    std::uint64_t h=14695981039346656037ULL;for(unsigned char c:data.str()){h^=c;h*=1099511628211ULL;}
    std::ostringstream hex;hex<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return hex.str();
}
struct Record {
    std::int64_t id=0;PlayerId owner=0;EntityId ship=0,site=0;
    std::string manifest;bool cancelled=false;
};
}
class HostBlueprintRegistry::Db {
public:
    explicit Db(const std::string& path){
        if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK){
            std::string error=db_?sqlite3_errmsg(db_):"SQLite open failed";sqlite3_close(db_);db_=nullptr;throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(db_,5000);
        exec(db_,"PRAGMA foreign_keys=ON");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_host_plans("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, owner INTEGER NOT NULL,ship INTEGER NOT NULL,site INTEGER NOT NULL,"
              "manifest TEXT NOT NULL,digest TEXT NOT NULL,cancelled INTEGER NOT NULL DEFAULT 0 CHECK(cancelled IN (0,1)))");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_host_plan_events("
              "seq INTEGER PRIMARY KEY AUTOINCREMENT, plan_id INTEGER NOT NULL REFERENCES sg_host_plans(id),"
              "kind TEXT NOT NULL CHECK(kind IN ('register','cancel')), tick INTEGER NOT NULL, owner INTEGER NOT NULL)");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_host_command_seq("
              "id INTEGER PRIMARY KEY CHECK(id=1), last_value INTEGER NOT NULL)");
        exec(db_,"INSERT OR IGNORE INTO sg_host_command_seq(id,last_value) VALUES(1,0)");
    }
    ~Db(){sqlite3_close(db_);}
    sqlite3* get()const{return db_;}
    void begin(){exec(db_,"BEGIN IMMEDIATE");}
    void commit(){exec(db_,"COMMIT");}
    void rollback()noexcept{sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
    Record get(std::int64_t id)const{
        Statement q(db_,"SELECT owner,ship,site,manifest,digest,cancelled FROM sg_host_plans WHERE id=?");q.number(1,id);
        if(!q.row())throw std::invalid_argument("construction plan does not exist");
        const std::int64_t owner=q.number(0),ship=q.number(1),site=q.number(2),flag=q.number(5);
        if(owner<=0||ship<=0||site<=0||(flag!=0&&flag!=1))throw std::runtime_error("corrupt construction plan identity/status");
        Record p{id,static_cast<PlayerId>(owner),static_cast<EntityId>(ship),static_cast<EntityId>(site),q.string(3),flag==1};
        if(q.string(4)!=digest(id,p.owner,p.ship,p.site,p.manifest))throw std::runtime_error("construction manifest integrity mismatch");
        auto parsed=BlueprintConstruction::from_manifest(p.manifest);
        if(parsed.cancelled() || parsed.manifest()!=p.manifest || parsed.jobs().empty() ||
           id>(std::numeric_limits<std::int64_t>::max()-100000)/20000 ||
           parsed.jobs().front().command.id!=static_cast<std::uint64_t>(id)*20000+100001 ||
           parsed.jobs().front().command.issuer!=p.owner ||
           parsed.jobs().front().command.actor!=p.ship ||
           parsed.jobs().front().command.target!=p.site)
            throw std::runtime_error("immutable plan identity/authority mismatch");
        Statement events(db_,"SELECT kind,owner FROM sg_host_plan_events WHERE plan_id=? ORDER BY seq");events.number(1,id);
        int seen=0;bool cancelled=false;
        while(events.row()){
            auto kind=events.string(0);if(events.number(1)!=owner)throw std::runtime_error("foreign plan control event");
            if((seen==0&&kind!="register")||(seen==1&&kind!="cancel")||seen>=2)
                throw std::runtime_error("invalid plan control event chain");
            cancelled=kind=="cancel";++seen;
        }
        if(seen==0 || cancelled!=p.cancelled)throw std::runtime_error("plan cancellation/audit mismatch");
        return p;
    }
    std::int64_t next_command_id(){
        begin();try{
            Statement q(db_,"SELECT last_value FROM sg_host_command_seq WHERE id=1");if(!q.row())throw std::runtime_error("host ID counter missing");
            const auto n=q.number(0);if(n<0 || n>=std::numeric_limits<std::int64_t>::max()-kHostIdBase-1)throw std::overflow_error("host ID range exhausted");
            Statement u(db_,"UPDATE sg_host_command_seq SET last_value=? WHERE id=1");u.number(1,n+1);u.done();
            commit();return kHostIdBase+n+1;
        }catch(...){rollback();throw;}
    }
private:sqlite3* db_=nullptr;
};
HostBlueprintRegistry::HostBlueprintRegistry(const std::string& path,PersistentWorld& w)
    :db_(std::make_unique<Db>(path)),world_(w){}
HostBlueprintRegistry::~HostBlueprintRegistry()=default;
std::int64_t HostBlueprintRegistry::register_layout(const std::string& text,
    PlayerId owner,EntityId ship,EntityId site){
    std::lock_guard<std::mutex> host_guard(gHostConstructionLifecycle);
    signed_id(owner);signed_id(ship);signed_id(site);
    if(text.empty()||text.size()>1024*1024)throw std::invalid_argument("layout exceeds host import limit");
    const auto bp=FactoryLayout::from_blueprint(text);
    if(bp.canonical_blueprint()!=text)throw std::invalid_argument("noncanonical editor layout");
    // Avoid creating a plan for a foreign/dead outpost, or conflicting with
    // equipment already installed. No import authorizes a construction action.
    const auto& sites=world_.world().built_outposts();auto it=sites.find(site);
    const auto* actor=world_.world().find(ship);
    if(it==sites.end() || it->second.source_colony_ship!=ship || !actor || actor->owner_id!=owner || !world_.world().colony_ship(ship))
        throw std::invalid_argument("authenticated player does not own target colony outpost");
    db_->begin();try{
        // Validate every existing record, including cancelled plans. A forged
        // cancelled flag must never bypass the one-active-plan guard.
        Statement existing(db_->get(),"SELECT id FROM sg_host_plans WHERE site=? ORDER BY id");
        existing.number(1,signed_id(site));
        while(existing.row()){
            const auto old=db_->get(existing.number(0));
            auto prior=BlueprintConstruction::from_manifest(old.manifest);
            const auto progress=prior.inspect(world_.world());
            if(!old.cancelled && !progress.conflicting && !progress.finished())
                throw std::invalid_argument("outpost already has a pending host-authorized construction plan");
        }
        Statement insert(db_->get(),"INSERT INTO sg_host_plans(owner,ship,site,manifest,digest,cancelled) VALUES(?,?,?,'','',0)");
        insert.number(1,signed_id(owner));insert.number(2,signed_id(ship));insert.number(3,signed_id(site));insert.done();
        const auto id=sqlite3_last_insert_rowid(db_->get());
        if(id<1 || id>(std::numeric_limits<std::int64_t>::max()-100000)/20000)throw std::overflow_error("host plan sequence overflow");
        const auto base=static_cast<std::uint64_t>(id)*20000+100000;
        BlueprintConstruction candidate(bp,site,ship,owner,base);
        const auto progress=candidate.inspect(world_.world());
        if(progress.conflicting)throw std::invalid_argument("blueprint conflicts with world: "+progress.reason);
        const auto manifest=candidate.manifest();
        Statement update(db_->get(),"UPDATE sg_host_plans SET manifest=?,digest=? WHERE id=?");
        update.string(1,manifest);update.string(2,digest(id,owner,ship,site,manifest));update.number(3,id);update.done();
        Statement ev(db_->get(),"INSERT INTO sg_host_plan_events(plan_id,kind,tick,owner) VALUES(?,'register',?,?)");
        ev.number(1,id);ev.number(2,signed_id(world_.world().tick()+1));ev.number(3,signed_id(owner));ev.done();
        db_->commit();return id;
    }catch(...){db_->rollback();throw;}
}
HostedPlanStatus HostBlueprintRegistry::status(std::int64_t id,PlayerId owner)const{
    std::lock_guard<std::mutex> host_guard(gHostConstructionLifecycle);
    if(id<=0)throw std::invalid_argument("invalid plan ID");
    const auto p=db_->get(id);
    if(p.owner!=owner)throw std::invalid_argument("plan owner authorization denied");
    auto plan=BlueprintConstruction::from_manifest(p.manifest);
    auto progress=plan.inspect(world_.world());progress.cancelled=p.cancelled;
    return HostedPlanStatus{id,p.owner,p.ship,p.site,progress};
}
std::vector<HostedPlanStatus> HostBlueprintRegistry::list(PlayerId owner)const{
    std::lock_guard<std::mutex> host_guard(gHostConstructionLifecycle);
    signed_id(owner);
    Statement query(db_->get(),"SELECT id FROM sg_host_plans WHERE owner=? ORDER BY id");
    query.number(1,signed_id(owner));
    std::vector<HostedPlanStatus> result;
    while(query.row()){
        const auto p=db_->get(query.number(0));
        if(p.owner!=owner)throw std::runtime_error("plan owner isolation violation");
        auto bp=BlueprintConstruction::from_manifest(p.manifest);
        auto progress=bp.inspect(world_.world());progress.cancelled=p.cancelled;
        result.push_back(HostedPlanStatus{p.id,p.owner,p.ship,p.site,progress});
    }
    return result;
}
bool HostBlueprintRegistry::cancel(std::int64_t id,PlayerId owner){
    std::lock_guard<std::mutex> host_guard(gHostConstructionLifecycle);
    db_->begin();try{
        const auto p=db_->get(id);if(p.owner!=owner)throw std::invalid_argument("plan owner authorization denied");
        if(p.cancelled){db_->commit();return false;}
        Statement u(db_->get(),"UPDATE sg_host_plans SET cancelled=1 WHERE id=? AND cancelled=0");u.number(1,id);u.done();
        if(sqlite3_changes(db_->get())!=1)throw std::runtime_error("plan cancellation conflict");
        Statement e(db_->get(),"INSERT INTO sg_host_plan_events(plan_id,kind,tick,owner) VALUES(?,'cancel',?,?)");
        e.number(1,id);e.number(2,signed_id(world_.world().tick()+1));e.number(3,signed_id(owner));e.done();
        db_->commit();return true;
    }catch(...){db_->rollback();throw;}
}
Event HostBlueprintRegistry::execute_one(std::int64_t id,PlayerId owner){
    std::lock_guard<std::mutex> host_guard(gHostConstructionLifecycle);
    const auto p=db_->get(id);
    if(p.owner!=owner)throw std::invalid_argument("plan owner authorization denied");
    if(p.cancelled)throw std::runtime_error("host has cancelled this construction plan");
    auto plan=BlueprintConstruction::from_manifest(p.manifest);
    const auto progress=plan.inspect(world_.world());
    if(progress.conflicting || progress.finished())throw std::runtime_error("plan complete or conflicting: "+progress.reason);
    // Sequence allocation commits BEFORE world action. A crash may leave a gap,
    // never a reusable construction command ID. Authoritative world receipts
    // define completion and recovery, not the counter or editor document.
    const auto fresh=static_cast<std::uint64_t>(db_->next_command_id());
    return plan.execute_next(world_,owner,p.ship,fresh);
}
} // namespace galaxy::industry
