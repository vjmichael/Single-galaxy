#pragma once
// SPACE-01: derived, durable, 3-D computational CELL INDEX on the SAME
// SQLite transaction as the one and only PersistentWorld physical snapshot.
// No cloned ship/NPC/cargo, no physical borders and no transfer of gameplay
// ownership. Cell scopes identify simulation responsibility, not geography
// walls, sovereign claims or permission to create inventory.
#include "galaxy/CosmicLocalFrame.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace galaxy::spatial {
constexpr std::int64_t kCellEdgeMm=100000000LL; // 100 km, indexes only
struct Cell {
    std::uint32_t system=0;
    std::int64_t x=0,y=0,z=0;
    bool operator==(const Cell& r)const {
        return system==r.system && x==r.x && y==r.y && z==r.z;
    }
    bool operator<(const Cell& r)const {
        if(system!=r.system)return system<r.system;
        if(x!=r.x)return x<r.x;
        if(y!=r.y)return y<r.y;
        return z<r.z;
    }
};
inline std::int64_t floor_cell(std::int64_t mm) {
    // C++ signed division truncates toward zero; grids must use mathematical
    // floor, including -1 mm and INT64_MIN. No abs(INT64_MIN), no overflow.
    const auto q=mm/kCellEdgeMm;
    return mm<0 && mm%kCellEdgeMm? q-1:q;
}
inline Cell of(CosmicPosition p,std::uint32_t system) {
    return {system,floor_cell(p.x_mm),floor_cell(p.y_mm),
            floor_cell(p.z_mm)};
}
inline std::string scope(Cell c) {
    if(!c.system)throw std::invalid_argument("spatial cell system zero");
    return "spatial/v1/s"+std::to_string(c.system)+
      "/x"+std::to_string(c.x)+"/y"+std::to_string(c.y)+
      "/z"+std::to_string(c.z);
}
inline std::optional<Cell> parse_scope(const std::string& text) {
    constexpr const char* prefix="spatial/v1/s";
    if(text.compare(0,std::char_traits<char>::length(prefix),prefix)!=0)
        return std::nullopt;
    try {
        const auto a=text.find("/x",11);
        const auto b=a==std::string::npos?a:text.find("/y",a+2);
        const auto c=b==std::string::npos?b:text.find("/z",b+2);
        if(a==std::string::npos||b==std::string::npos||
           c==std::string::npos)return std::nullopt;
        const auto si=text.substr(12,a-12);
        const auto xs=text.substr(a+2,b-a-2);
        const auto ys=text.substr(b+2,c-b-2);
        const auto zs=text.substr(c+2);
        if(si.empty()||xs.empty()||ys.empty()||zs.empty())
            return std::nullopt;
        const auto system=std::stoull(si);
        if(system==0 || system>std::numeric_limits<std::uint32_t>::max())
            return std::nullopt;
        const Cell result{static_cast<std::uint32_t>(system),
            std::stoll(xs),std::stoll(ys),std::stoll(zs)};
        return scope(result)==text?std::optional<Cell>(result):std::nullopt;
    }catch(const std::exception&){return std::nullopt;}
}
// The original v119 native ship has three persisted physical axes; older
// snapshots resolve z=0. This projects the SAME canonical ship, not a
// secondary position ledger. The old XY clamp applies only to v118 saves.
inline std::optional<CosmicPosition> cosmic_ship(
    const Simulation& world,EntityId ship) {
    const auto* e=world.find(ship);
    if(!e||e->type!=EntityType::Ship)return std::nullopt;
    LocalSpaceFrame frame{e->system_id,0,{}};
    const auto it=world.ship_frames().find(ship);
    if(it!=world.ship_frames().end()){
        if(it->second.system!=e->system_id)return std::nullopt;
        frame.anchor_body_id=it->second.anchor_body;
        frame.origin=it->second.origin;
    }
    return local_to_cosmic(frame,{e->position.x,e->position.y,e->position_z_mm});
}
inline std::optional<Cell> ship_cell(const Simulation& world,EntityId id){
    const auto* e=world.find(id);
    if(!e || e->type!=EntityType::Ship)return std::nullopt;
    const auto p=cosmic_ship(world,id);
    if(!p)return std::nullopt;
    return of(*p,e->system_id);
}
// Pure scheduling suggestion, not an independent cold simulation writer.
enum class Detail : std::uint8_t {Dormant=0,Cold=1,Detailed=2};
inline bool touching(Cell a,Cell b) {
    if(a.system!=b.system)return false;
    const auto near=[](std::int64_t x,std::int64_t y) {
        return x==y ||
           (x<std::numeric_limits<std::int64_t>::max() && x+1==y) ||
           (y<std::numeric_limits<std::int64_t>::max() && y+1==x);
    };
    return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);
}
inline Detail desired_detail(const Simulation& world,Cell cell,
    std::optional<Cell> authenticated_view_cell={}) {
    if(authenticated_view_cell && touching(cell,*authenticated_view_cell))
        return Detail::Detailed;
    bool contains_ship=false;
    for(const auto& [id,e]:world.entities()) {
        if(e.type!=EntityType::Ship)continue;
        const auto current=ship_cell(world,id);
        if(!current || !(*current==cell))continue;
        contains_ship=true;
        for(const auto& [order,leg]:world.intercolony_freight()){
            (void)order;
            if(leg.stage!=IntercolonyFreightStage::Delivered &&
               (leg.carrier_ship==id || leg.source_ship==id ||
                leg.destination_ship==id))return Detail::Detailed;
        }
        for(const auto& [order,flight]:world.npc_flight_orders()){
            (void)order;
            if(flight.ship==id &&
               (flight.phase==NpcFlightPhase::Planned ||
                flight.phase==NpcFlightPhase::InTransit))
                return Detail::Detailed;
        }
    }
    return contains_ship?Detail::Cold:Detail::Dormant;
}
namespace detail {
inline void sql(sqlite3* db,const char* command){
    char* error=nullptr;
    if(sqlite3_exec(db,command,nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string why=error?error:sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("spatial index SQL: "+why);
    }
}
struct Statement {
    sqlite3_stmt* q=nullptr;
    explicit Statement(sqlite3* db,const char* command){
        if(sqlite3_prepare_v2(db,command,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("spatial index prepare: ")+
                                     sqlite3_errmsg(db));
    }
    ~Statement(){sqlite3_finalize(q);}
    Statement(const Statement&)=delete;Statement& operator=(const Statement&)=delete;
    void text(int n,const std::string& v){
        if(sqlite3_bind_text(q,n,v.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("spatial bind text");
    }
    void number(int n,std::uint64_t v){
        if(v>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::runtime_error("spatial generation overflow");
        if(sqlite3_bind_int64(q,n,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
            throw std::runtime_error("spatial bind integer");
    }
    void signed_number(int n,std::int64_t v) {
        if(sqlite3_bind_int64(q,n,v)!=SQLITE_OK)
            throw std::runtime_error("spatial bind signed integer");
    }
    bool row(){
        const auto rc=sqlite3_step(q);
        if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)
            throw std::runtime_error("spatial SQL step");
        return rc==SQLITE_ROW;
    }
    void execute(){if(row())throw std::runtime_error("spatial unexpected row");}
    std::string str(int i)const{
        const auto* ptr=sqlite3_column_text(q,i);
        return ptr?reinterpret_cast<const char*>(ptr):"";
    }
    std::int64_t integer(int i)const{return sqlite3_column_int64(q,i);}
};
inline bool installed(sqlite3* db) {
    Statement q(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                   "AND name='sg_spatial_native_ship'");
    return q.row();
}
inline void install(sqlite3* db) {
    sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_native_ship("
        "ship_id TEXT PRIMARY KEY,player_owner TEXT NOT NULL,"
        "scope TEXT NOT NULL,cosmic_x INTEGER NOT NULL,cosmic_y INTEGER NOT NULL,"
        "cosmic_z INTEGER NOT NULL,world_generation INTEGER NOT NULL,"
        "world_hash TEXT NOT NULL)");
    sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_native_transfer("
        "sequence INTEGER PRIMARY KEY AUTOINCREMENT,ship_id TEXT NOT NULL,"
        "player_owner TEXT NOT NULL,from_scope TEXT NOT NULL,"
        "to_scope TEXT NOT NULL,world_generation INTEGER NOT NULL,"
        "world_hash TEXT NOT NULL,UNIQUE(ship_id,world_generation))");
    sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_native_meta("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "world_generation INTEGER NOT NULL,world_hash TEXT NOT NULL,"
        "ship_count INTEGER NOT NULL CHECK(ship_count>=0))");
    sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_no_transfer_update "
        "BEFORE UPDATE ON sg_spatial_native_transfer BEGIN "
        "SELECT RAISE(ABORT,'immutable spatial custody migration');END");
    sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_no_transfer_delete "
        "BEFORE DELETE ON sg_spatial_native_transfer BEGIN "
        "SELECT RAISE(ABORT,'immutable spatial custody migration');END");
}
} // detail

// MUST run INSIDE the original PersistentWorld BEGIN IMMEDIATE transaction,
// after its original canonical snapshot/operation log is written. This index
// is derived metadata only; the original ship/NPC/cargo never moves into it.
inline void record_native_world_commit(sqlite3* db,const Simulation& world,
    std::uint64_t prior_generation,std::uint64_t next_generation) {
    if(!db || !next_generation || next_generation!=prior_generation+1)
        throw std::runtime_error("spatial index invalid original world commit");
    detail::install(db);
    // When already installed, each new native writer must build exactly on
    // the previous committed spatial tip; no stale source/index resurrection.
    {
        detail::Statement prior(db,"SELECT world_generation FROM "
                                   "sg_spatial_native_meta WHERE singleton=1");
        if(prior.row() &&
           prior.integer(0)!=static_cast<std::int64_t>(prior_generation))
            throw std::runtime_error("spatial index prior world generation diverged");
    }
    const auto hash=std::to_string(world.state_hash());
    std::uint64_t count=0;
    for(const auto& [ship,e]:world.entities()){
        if(e.type!=EntityType::Ship)continue;
        const auto p=cosmic_ship(world,ship);
        if(!p)throw std::runtime_error("spatial index missing original cosmic ship frame");
        const auto cell=scope(of(*p,e.system_id));
        const auto id=std::to_string(ship),owner=std::to_string(e.owner_id);
        detail::Statement old(db,"SELECT player_owner,scope,world_generation "
            "FROM sg_spatial_native_ship WHERE ship_id=?");
        old.text(1,id);
        std::string previous;
        if(old.row()){
            if(old.str(0)!=owner || old.integer(2)!=
                static_cast<std::int64_t>(prior_generation))
                throw std::runtime_error("spatial index stale original ship identity/epoch");
            previous=old.str(1);
        }else if(prior_generation){
            // A newly manufactured ship is legitimate. Existing prior ships
            // are checked by the global prior meta/count and current roster.
            previous="";
        }
        if(previous!=cell){
            detail::Statement move(db,"INSERT INTO sg_spatial_native_transfer "
               "(ship_id,player_owner,from_scope,to_scope,world_generation,world_hash)"
               " VALUES(?,?,?,?,?,?)");
            move.text(1,id);move.text(2,owner);move.text(3,previous);
            move.text(4,cell);move.number(5,next_generation);move.text(6,hash);
            move.execute();
        }
        detail::Statement update(db,"INSERT INTO sg_spatial_native_ship("
            "ship_id,player_owner,scope,cosmic_x,cosmic_y,cosmic_z,"
            "world_generation,world_hash) VALUES(?,?,?,?,?,?,?,?) "
            "ON CONFLICT(ship_id) DO UPDATE SET player_owner=excluded.player_owner,"
            "scope=excluded.scope,cosmic_x=excluded.cosmic_x,"
            "cosmic_y=excluded.cosmic_y,cosmic_z=excluded.cosmic_z,"
            "world_generation=excluded.world_generation,world_hash=excluded.world_hash");
        update.text(1,id);update.text(2,owner);update.text(3,cell);
        update.signed_number(4,p->x_mm);update.signed_number(5,p->y_mm);
        update.signed_number(6,p->z_mm);update.number(7,next_generation);
        update.text(8,hash);update.execute();
        ++count;
    }
    // Remove derived identity for ships that have actually ceased to exist.
    // The original world snapshot, not this index, decides existence.
    detail::Statement purge(db,"DELETE FROM sg_spatial_native_ship "
        "WHERE world_generation<>?");
    purge.number(1,next_generation);purge.execute();
    detail::Statement meta(db,"INSERT INTO sg_spatial_native_meta("
        "singleton,world_generation,world_hash,ship_count) VALUES(1,?,?,?) "
        "ON CONFLICT(singleton) DO UPDATE SET "
        "world_generation=excluded.world_generation,"
        "world_hash=excluded.world_hash,ship_count=excluded.ship_count");
    meta.number(1,next_generation);meta.text(2,hash);meta.number(3,count);
    meta.execute();
}
// Exact original state validation under the SAME SQLite read/write lock.
// Never accept an index merely because a caller claims a cell/scope.
inline bool verify_native_index(sqlite3* db,const Simulation& world,
    std::uint64_t generation) {
    if(!db || !detail::installed(db))return false;
    const auto hash=std::to_string(world.state_hash());
    detail::Statement meta(db,"SELECT world_generation,world_hash,ship_count "
                              "FROM sg_spatial_native_meta WHERE singleton=1");
    if(!meta.row() || meta.integer(0)!=static_cast<std::int64_t>(generation) ||
       meta.str(1)!=hash)return false;
    std::uint64_t found=0;
    for(const auto& [ship,e]:world.entities()){
        if(e.type!=EntityType::Ship)continue;
        const auto p=cosmic_ship(world,ship);
        if(!p)return false;
        detail::Statement q(db,"SELECT player_owner,scope,cosmic_x,cosmic_y,"
          "cosmic_z,world_generation,world_hash FROM sg_spatial_native_ship "
          "WHERE ship_id=?");
        q.text(1,std::to_string(ship));
        if(!q.row() || q.str(0)!=std::to_string(e.owner_id) ||
           q.str(1)!=scope(of(*p,e.system_id)) ||
           q.integer(2)!=p->x_mm || q.integer(3)!=p->y_mm ||
           q.integer(4)!=p->z_mm ||
           q.integer(5)!=static_cast<std::int64_t>(generation) ||
           q.str(6)!=hash)return false;
        // Last immutable migration must terminate in the currently
        // indexed cell. An index-only rewrite cannot fake a legal move.
        detail::Statement latest(db,"SELECT player_owner,to_scope,"
          "world_generation FROM sg_spatial_native_transfer "
          "WHERE ship_id=? ORDER BY sequence DESC LIMIT 1");
        latest.text(1,std::to_string(ship));
        if(!latest.row() ||
           latest.str(0)!=std::to_string(e.owner_id) ||
           latest.str(1)!=scope(of(*p,e.system_id)) ||
           latest.integer(2)<=0 ||
           latest.integer(2)>static_cast<std::int64_t>(generation))
            return false;
        ++found;
    }
    detail::Statement extras(db,"SELECT COUNT(*) FROM sg_spatial_native_ship");
    if(!extras.row())return false;
    return meta.integer(2)==static_cast<std::int64_t>(found) &&
           extras.integer(0)==static_cast<std::int64_t>(found);
}
} // namespace galaxy::spatial
