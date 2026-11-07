#pragma once
// SPACE-04A.2: immutable native galaxy star-system origins on the same
// physical SQLite database as PersistentWorld. This is WORLD GEOGRAPHY,
// not a second ship/cargo ledger, instant travel, EVE boundary or live
// physics engine. Host-approved registration must prove real system IDs
// and match the exact original committed physical world generation/hash.
#include "galaxy/GalacticCoordinates.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/SeamlessSpatialOwnership.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace galaxy::galactic {
namespace registry {
inline void command(sqlite3* db,const char* text) {
    char* error=nullptr;
    if(sqlite3_exec(db,text,nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string reason=error?error:sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("galactic original system registry: "+reason);
    }
}
struct Query {
    sqlite3_stmt* q=nullptr;
    explicit Query(sqlite3* db,const char* source) {
        if(sqlite3_prepare_v2(db,source,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error("galactic registry SQL prepare failed");
    }
    ~Query(){sqlite3_finalize(q);}
    Query(const Query&)=delete;Query& operator=(const Query&)=delete;
    void text(int n,const std::string& v) {
        if(sqlite3_bind_text(q,n,v.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("galactic registry bind");
    }
    void number(int n,std::int64_t v) {
        if(sqlite3_bind_int64(q,n,v)!=SQLITE_OK)
            throw std::runtime_error("galactic registry integer bind");
    }
    bool row() {
        const int code=sqlite3_step(q);
        if(code!=SQLITE_ROW && code!=SQLITE_DONE)
            throw std::runtime_error("galactic registry SQL failed");
        return code==SQLITE_ROW;
    }
    void done(){if(row())throw std::runtime_error("galactic registry extra row");}
    std::string str(int n)const {
        const auto* p=sqlite3_column_text(q,n);
        return p?reinterpret_cast<const char*>(p):"";
    }
    std::int64_t n(int index)const {
        return sqlite3_column_int64(q,index);
    }
};
inline bool snapshot_match(sqlite3* db,const PersistentWorld& native) {
    Query q(db,"SELECT generation,checksum FROM snapshots "
               "ORDER BY generation DESC LIMIT 1");
    return q.row() && q.n(0)>0 &&
        static_cast<std::uint64_t>(q.n(0))==native.generation() &&
        q.str(1)==std::to_string(native.world().state_hash());
}
inline void install(sqlite3* db) {
    registry::command(db,"CREATE TABLE IF NOT EXISTS "
       "sg_galaxy_system_origin("
       "system INTEGER PRIMARY KEY CHECK(system>0),"
       "sector_x INTEGER NOT NULL,mm_x INTEGER NOT NULL,"
       "sector_y INTEGER NOT NULL,mm_y INTEGER NOT NULL,"
       "sector_z INTEGER NOT NULL,mm_z INTEGER NOT NULL,"
       "native_generation INTEGER NOT NULL,world_hash TEXT NOT NULL,"
       "manifest_id TEXT NOT NULL UNIQUE,"
       "CHECK(mm_x>=0 AND mm_x<1000000000000000),"
       "CHECK(mm_y>=0 AND mm_y<1000000000000000),"
       "CHECK(mm_z>=0 AND mm_z<1000000000000000))");
    registry::command(db,"CREATE TRIGGER IF NOT EXISTS "
       "sg_galaxy_origin_no_update BEFORE UPDATE ON "
       "sg_galaxy_system_origin BEGIN "
       "SELECT RAISE(ABORT,'immutable original galactic star origin');END");
    registry::command(db,"CREATE TRIGGER IF NOT EXISTS "
       "sg_galaxy_origin_no_delete BEFORE DELETE ON "
       "sg_galaxy_system_origin BEGIN "
       "SELECT RAISE(ABORT,'immutable original galactic star origin');END");
}
// Registration changes no ship position or physics and therefore must
// NEVER be used as a gameplay movement/warp command. The owning host must
// validate the supplied real survey/galaxy generation provenance externally.
inline bool register_original_system(
    const std::string& native_db,const PersistentWorld& native,
    std::uint32_t system,Point exact_galactic_origin,
    const std::string& world_generation_manifest_id,
    bool trusted_host_authenticated_generation) {
    if(!trusted_host_authenticated_generation || native_db.empty() ||
       !system || !exact_galactic_origin.valid() ||
       !native.world().systems().count(system) ||
       world_generation_manifest_id.empty() ||
       world_generation_manifest_id.size()>128)
        return false;
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(native_db.c_str(),&db,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("cannot open canonical world to register star system");
    }
    bool committed=false;
    try {
        sqlite3_busy_timeout(db,5000);
        registry::command(db,"BEGIN IMMEDIATE");
        if(!snapshot_match(db,native))
            throw std::runtime_error("stale original world generation for star origin");
        install(db);
        Query existing(db,"SELECT sector_x,mm_x,sector_y,mm_y,sector_z,mm_z,"
            "manifest_id FROM sg_galaxy_system_origin WHERE system=?");
        existing.number(1,system);
        bool replay=false;
        if(existing.row()){
            replay=existing.n(0)==exact_galactic_origin.x.sector &&
                   existing.n(1)==exact_galactic_origin.x.millimeters &&
                   existing.n(2)==exact_galactic_origin.y.sector &&
                   existing.n(3)==exact_galactic_origin.y.millimeters &&
                   existing.n(4)==exact_galactic_origin.z.sector &&
                   existing.n(5)==exact_galactic_origin.z.millimeters &&
                   existing.str(6)==world_generation_manifest_id;
            if(!replay)
                throw std::runtime_error("conflicting original galactic system origin");
        }else{
            Query add(db,"INSERT INTO sg_galaxy_system_origin("
                "system,sector_x,mm_x,sector_y,mm_y,sector_z,mm_z,"
                "native_generation,world_hash,manifest_id) "
                "VALUES(?,?,?,?,?,?,?,?,?,?)");
            add.number(1,system);
            add.number(2,exact_galactic_origin.x.sector);
            add.number(3,exact_galactic_origin.x.millimeters);
            add.number(4,exact_galactic_origin.y.sector);
            add.number(5,exact_galactic_origin.y.millimeters);
            add.number(6,exact_galactic_origin.z.sector);
            add.number(7,exact_galactic_origin.z.millimeters);
            add.number(8,static_cast<std::int64_t>(native.generation()));
            add.text(9,std::to_string(native.world().state_hash()));
            add.text(10,world_generation_manifest_id);
            add.done();
        }
        registry::command(db,"COMMIT");committed=true;
        sqlite3_close_v2(db);
        return true;
    }catch(...){
        if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        sqlite3_close_v2(db);throw;
    }
}
// This cannot invent a star's location; if the owning host has not
// registered that real original system, there is NO galactic position.
inline std::optional<Point> original_ship_galactic_position(
    const std::string& native_db,const PersistentWorld& native,EntityId ship){
    if(native_db.empty())return std::nullopt;
    const auto* actual=native.world().find(ship);
    if(!actual || actual->type!=EntityType::Ship)return std::nullopt;
    const auto cosmic=galaxy::spatial::cosmic_ship(native.world(),ship);
    if(!cosmic)return std::nullopt;
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(native_db.c_str(),&db,
        SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        if(db)sqlite3_close_v2(db);
        return std::nullopt;
    }
    try{
        if(!snapshot_match(db,native)){
            sqlite3_close_v2(db);return std::nullopt;
        }
        Query q(db,"SELECT sector_x,mm_x,sector_y,mm_y,sector_z,mm_z "
             "FROM sg_galaxy_system_origin WHERE system=?");
        q.number(1,actual->system_id);
        if(!q.row()){sqlite3_close_v2(db);return std::nullopt;}
        const Point star{{q.n(0),q.n(1)},{q.n(2),q.n(3)},{q.n(4),q.n(5)}};
        const auto coordinate=at_system_origin(star,*cosmic);
        sqlite3_close_v2(db);
        return coordinate;
    }catch(const std::exception&){
        sqlite3_close_v2(db);return std::nullopt;
    }
}
} // namespace galaxy::galactic
