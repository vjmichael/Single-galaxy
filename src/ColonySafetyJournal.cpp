#include "galaxy/ColonySafetyJournal.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <limits>

namespace galaxy::colony_safety::journal {
namespace {
void sql(sqlite3* db,const char* command) {
    char* e=nullptr;
    if(sqlite3_exec(db,command,nullptr,nullptr,&e)!=SQLITE_OK){
        const std::string reason=e?e:"unknown SQLite failure";
        sqlite3_free(e);throw std::runtime_error("colony response journal: "+reason);
    }
}
class Q {
public:
    sqlite3* db;
    sqlite3_stmt* s=nullptr;
    Q(sqlite3* d,const char* query):db(d){
        if(!db||sqlite3_prepare_v2(db,query,-1,&s,nullptr)!=SQLITE_OK)
            throw std::runtime_error("colony response SQL prepare failed");
    }
    ~Q(){sqlite3_finalize(s);}
    Q(const Q&)=delete;Q& operator=(const Q&)=delete;
    void num(int i,std::uint64_t x){
        if(x>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())||
           sqlite3_bind_int64(s,i,static_cast<sqlite3_int64>(x))!=SQLITE_OK)
            throw std::runtime_error("colony response journal integer overflow");
    }
    void str(int i,const std::string& x){
        if(sqlite3_bind_text(s,i,x.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("colony response journal bind text failed");
    }
    bool row(){const int r=sqlite3_step(s);if(r==SQLITE_ROW)return true;
        if(r!=SQLITE_DONE)throw std::runtime_error("colony response journal read failed");
        return false;}
    void done(){if(sqlite3_step(s)!=SQLITE_DONE)
        throw std::runtime_error(std::string("colony response journal write: ")+sqlite3_errmsg(db));}
};
std::uint64_t nn(sqlite3_stmt* s,int i) {
    if(sqlite3_column_type(s,i)!=SQLITE_INTEGER||sqlite3_column_int64(s,i)<0)
        throw std::runtime_error("invalid colony response journal integer");
    return static_cast<std::uint64_t>(sqlite3_column_int64(s,i));
}
std::string txt(sqlite3_stmt* s,int i){
    const auto* p=reinterpret_cast<const char*>(sqlite3_column_text(s,i));
    if(!p)throw std::runtime_error("invalid colony response journal digest");
    return p;
}
std::string hash(const std::string& source){
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char c:source){h^=c;h*=1099511628211ULL;}
    static constexpr char hex[]="0123456789abcdef";
    std::string out(16,'0');
    for(int i=15;i>=0;--i){out[i]=hex[h&15];h>>=4;}
    return out;
}
struct Row {
    EntityId ship=0,site=0;
    std::uint64_t population=0;
    Risk risk=Risk::Safe;
    std::uint64_t food_periods=0,water_periods=0,oxygen_periods=0;
};
using Rows=std::map<EntityId,Row>;
Rows sites(const Simulation& w){
    Rows out;
    for(const auto& [id,ship]:w.colony_ships()){
        (void)ship;
        if(w.life_support(id)){
            auto a=assess(w,id,id);
            out.emplace(id,Row{id,id,a.population,a.risk,a.remaining.food_periods,a.remaining.water_periods,a.remaining.oxygen_periods});
        }
    }
    for(const auto& [id,h]:w.habitats()){
        if(!out.count(id)){
            auto a=assess(w,h.source_ship,id);
            out.emplace(id,Row{h.source_ship,id,a.population,a.risk,a.remaining.food_periods,a.remaining.water_periods,a.remaining.oxygen_periods});
        }
    }
    for(const auto& [id,f]:w.farms()){
        if(!out.count(id)){
            auto a=assess(w,f.source_ship,id);
            out.emplace(id,Row{f.source_ship,id,a.population,a.risk,a.remaining.food_periods,a.remaining.water_periods,a.remaining.oxygen_periods});
        }
    }
    return out;
}
int schema(sqlite3* db){
    Q q(db,"SELECT name FROM sqlite_master WHERE type='table' AND name IN "
           "('sg_colony_response_meta','sg_colony_response_sites','sg_colony_response_events')");
    int n=0;while(q.row())++n;
    if(n && n!=3)throw std::runtime_error("partial colony response journal schema");
    return n;
}
void create(sqlite3* db){
    sql(db,"CREATE TABLE sg_colony_response_meta("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "generation INTEGER NOT NULL CHECK(generation>0),"
        "tick INTEGER NOT NULL CHECK(tick>=0),"
        "world_hash TEXT NOT NULL,"
        "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
    sql(db,"CREATE TABLE sg_colony_response_sites("
        "site INTEGER PRIMARY KEY CHECK(site>0),"
        "ship INTEGER NOT NULL CHECK(ship>0),"
        "population INTEGER NOT NULL CHECK(population>=0),"
        "risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3))");
    sql(db,"CREATE TABLE sg_colony_response_events("
        "id INTEGER PRIMARY KEY CHECK(id>0),"
        "generation INTEGER NOT NULL CHECK(generation>0),"
        "tick INTEGER NOT NULL CHECK(tick>=0),"
        "ship INTEGER NOT NULL CHECK(ship>0),site INTEGER NOT NULL CHECK(site>0),"
        "old_risk INTEGER NOT NULL CHECK(old_risk BETWEEN 0 AND 3),"
        "new_risk INTEGER NOT NULL CHECK(new_risk BETWEEN 0 AND 3),"
        "population INTEGER NOT NULL CHECK(population>=0),"
        "food_periods INTEGER NOT NULL CHECK(food_periods>=0),"
        "water_periods INTEGER NOT NULL CHECK(water_periods>=0),"
        "oxygen_periods INTEGER NOT NULL CHECK(oxygen_periods>=0),"
        "world_hash TEXT NOT NULL,digest TEXT NOT NULL,"
        "UNIQUE(generation,site),CHECK(old_risk!=new_risk))");
    sql(db,"CREATE TRIGGER sg_response_events_immutable_update "
        "BEFORE UPDATE ON sg_colony_response_events BEGIN "
        "SELECT RAISE(ABORT,'immutable response transition');END");
    sql(db,"CREATE TRIGGER sg_response_events_immutable_delete "
        "BEFORE DELETE ON sg_colony_response_events BEGIN "
        "SELECT RAISE(ABORT,'immutable response transition');END");
}
std::string event_body(std::uint64_t id,std::uint64_t generation,std::uint64_t tick,
                       const Row& r,int from,const std::string& world_hash){
    return std::to_string(id)+"|"+std::to_string(generation)+"|"+std::to_string(tick)+
        "|"+std::to_string(r.ship)+"|"+std::to_string(r.site)+"|"+
        std::to_string(from)+"|"+std::to_string(static_cast<int>(r.risk))+"|"+
        std::to_string(r.population)+"|"+std::to_string(r.food_periods)+"|"+
        std::to_string(r.water_periods)+"|"+std::to_string(r.oxygen_periods)+"|"+world_hash;
}
struct Meta {std::uint64_t generation=0,tick=0,count=0;std::string world_hash,tip;};
Meta read_meta(sqlite3* db){
    Q q(db,"SELECT generation,tick,world_hash,event_count,tip "
           "FROM sg_colony_response_meta WHERE singleton=1");
    if(!q.row())throw std::runtime_error("missing colony response journal anchor");
    Meta m{nn(q.s,0),nn(q.s,1),0,txt(q.s,2),txt(q.s,4)};
    m.count=nn(q.s,3);
    if(q.row())throw std::runtime_error("duplicate colony response journal anchor");
    return m;
}
Rows read_sites(sqlite3* db){
    Rows out;
    Q q(db,"SELECT site,ship,population,risk FROM sg_colony_response_sites ORDER BY site");
    while(q.row()){
        const auto site=nn(q.s,0),ship=nn(q.s,1),pop=nn(q.s,2),risk=nn(q.s,3);
        if(!site||!ship||risk>3||!out.emplace(site,Row{ship,site,pop,static_cast<Risk>(risk)}).second)
            throw std::runtime_error("invalid colony response site record");
    }
    return out;
}
void insert_site(sqlite3* db,const Row& r){
    Q q(db,"INSERT INTO sg_colony_response_sites(site,ship,population,risk) VALUES(?,?,?,?)");
    q.num(1,r.site);q.num(2,r.ship);q.num(3,r.population);
    q.num(4,static_cast<int>(r.risk));q.done();
}
void replace_sites(sqlite3* db,const Rows& now){
    sql(db,"DELETE FROM sg_colony_response_sites");
    for(const auto& [id,r]:now){(void)id;insert_site(db,r);}
}
void append_event(sqlite3* db,std::uint64_t generation,std::uint64_t tick,
                  const Row& r,int from,const std::string& world_hash,
                  std::uint64_t& count,std::string& tip){
    if(count==static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
        throw std::runtime_error("colony response event sequence exhausted");
    const auto id=++count;
    tip=hash(tip+"|"+event_body(id,generation,tick,r,from,world_hash));
    Q q(db,"INSERT INTO sg_colony_response_events VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
    q.num(1,id);q.num(2,generation);q.num(3,tick);q.num(4,r.ship);
    q.num(5,r.site);q.num(6,from);q.num(7,static_cast<int>(r.risk));
    q.num(8,r.population);q.num(9,r.food_periods);q.num(10,r.water_periods);
    q.num(11,r.oxygen_periods);q.str(12,world_hash);q.str(13,tip);q.done();
}
} // namespace
void verify(sqlite3* db,const Simulation& current,std::uint64_t generation){
    if(!schema(db))return; // Pre-upgrade worlds have no journal.
    const auto m=read_meta(db);
    if(m.generation!=generation||m.tick!=current.tick()||
       m.world_hash!=std::to_string(current.state_hash()))
        throw std::runtime_error("response journal not bound to current world snapshot");
    const auto actual=sites(current),stored=read_sites(db);
    if(actual.size()!=stored.size())throw std::runtime_error("response journal site count mismatch");
    for(const auto& [site,r]:actual){
        const auto it=stored.find(site);
        if(it==stored.end()||it->second.ship!=r.ship||
           it->second.population!=r.population||it->second.risk!=r.risk)
            throw std::runtime_error("response journal no longer agrees with physical stock");
    }
    Q q(db,"SELECT id,generation,tick,ship,site,old_risk,new_risk,population,"
           "food_periods,water_periods,oxygen_periods,world_hash,digest "
           "FROM sg_colony_response_events ORDER BY id");
    std::uint64_t count=0;
    std::string tip="genesis";
    while(q.row()){
        if(count==static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::runtime_error("response journal count overflow");
        const auto id=nn(q.s,0),gen=nn(q.s,1),tick=nn(q.s,2);
        const auto ship=nn(q.s,3),site=nn(q.s,4),old=nn(q.s,5);
        const auto now=nn(q.s,6),pop=nn(q.s,7);
        const auto food=nn(q.s,8),water=nn(q.s,9),oxygen=nn(q.s,10);
        const auto world_hash=txt(q.s,11),digest=txt(q.s,12);
        if(id!=++count||!gen||gen>generation||tick>current.tick()||
           !ship||!site||old>3||now>3||old==now)
            throw std::runtime_error("invalid response transition history");
        const Row r{ship,site,pop,static_cast<Risk>(now),food,water,oxygen};
        tip=hash(tip+"|"+event_body(id,gen,tick,r,static_cast<int>(old),world_hash));
        if(tip!=digest)throw std::runtime_error("response transition digest mismatch");
    }
    if(count!=m.count||tip!=m.tip)
        throw std::runtime_error("response transition count/tip mismatch");
}
std::uint64_t event_count(sqlite3* db){
    if(!schema(db))return 0;
    return read_meta(db).count;
}
void record_world_commit(sqlite3* db,const Simulation& next,
                         std::uint64_t previous_generation,std::uint64_t new_generation){
    if(!db||new_generation!=previous_generation+1||!new_generation)
        throw std::invalid_argument("invalid world commit response scope");
    const auto now=sites(next);
    if(!schema(db)){
        create(db);
        // Bootstrap existing saves using their previous physical state.
        // This does not pretend older history was observed by this scheduler.
        Rows prior;
        std::uint64_t previous_tick=next.tick();
        std::string previous_hash=std::to_string(next.state_hash());
        if(previous_generation){
            Q q(db,"SELECT state FROM snapshots WHERE generation=?");
            q.num(1,previous_generation);
            if(!q.row())throw std::runtime_error("missing prior world snapshot for response upgrade");
            const auto* p=static_cast<const char*>(sqlite3_column_blob(q.s,0));
            const auto n=sqlite3_column_bytes(q.s,0);
            if(!p||n<=0)throw std::runtime_error("invalid prior physical snapshot");
            const auto previous=Simulation::from_snapshot(std::string(p,static_cast<std::size_t>(n)));
            prior=sites(previous);
            previous_tick=previous.tick();
            previous_hash=std::to_string(previous.state_hash());
        }else prior=now;
        replace_sites(db,prior);
        Q q(db,"INSERT INTO sg_colony_response_meta VALUES(1,?,?,?,?,?)");
        q.num(1,previous_generation?previous_generation:new_generation);
        q.num(2,previous_tick);q.str(3,previous_hash);q.num(4,0);q.str(5,"genesis");
        q.done();
    }else{
        // Verify pre-commit state against the original committed physical snapshot.
        Q q(db,"SELECT state FROM snapshots WHERE generation=?");
        q.num(1,previous_generation);
        if(!q.row())throw std::runtime_error("missing prior snapshot for journal replay");
        const auto* p=static_cast<const char*>(sqlite3_column_blob(q.s,0));
        const auto n=sqlite3_column_bytes(q.s,0);
        if(!p||n<=0)throw std::runtime_error("invalid prior snapshot");
        const auto previous=Simulation::from_snapshot(std::string(p,static_cast<std::size_t>(n)));
        verify(db,previous,previous_generation);
    }
    const auto m=read_meta(db);
    const auto prior=read_sites(db);
    if(next.tick()<m.tick)throw std::runtime_error("response timeline rewound");
    // A world generation may accept a future command without advancing the
    // simulation tick; it still receives a new physical-state anchor.
    auto count=m.count;
    auto tip=m.tip;
    for(const auto& [site,r]:now){
        const auto old=prior.find(site);
        const auto from=old==prior.end()?Risk::Safe:old->second.risk;
        if(old!=prior.end() && old->second.ship!=r.ship)
            throw std::runtime_error("response compartment changed parent ship");
        if(from!=r.risk){
            append_event(db,new_generation,next.tick(),r,static_cast<int>(from),
                         std::to_string(next.state_hash()),count,tip);
        }
    }
    for(const auto& [site,r]:prior){
        if(now.count(site))continue;
        // Never convert a destroyed or disappeared occupied compartment into
        // a fictional 'safe' rescue. The underlying death/transport/custody
        // writer must explicitly certify the removal before this gate opens.
        if(r.population)
            throw std::runtime_error("occupied safety compartment vanished without verified closure adapter");
    }
    replace_sites(db,now);
    Q update(db,"UPDATE sg_colony_response_meta SET generation=?,tick=?,"
                 "world_hash=?,event_count=?,tip=? WHERE singleton=1");
    update.num(1,new_generation);update.num(2,next.tick());
    update.str(3,std::to_string(next.state_hash()));update.num(4,count);
    update.str(5,tip);update.done();
    if(sqlite3_changes(db)!=1)throw std::runtime_error("response anchor update failed");
    // Recalculate all sites and validate the entire append-only digest chain
    // BEFORE the outer world transaction can publish its snapshot.
    verify(db,next,new_generation);
}
} // namespace galaxy::colony_safety::journal
