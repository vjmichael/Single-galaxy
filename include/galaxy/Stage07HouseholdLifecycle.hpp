#pragma once
// Stage 7 POP-02: an immutable lifecycle view over the ORIGINAL world,
// ART02 family birth/migration events, and original physical child custody.
// This layer must not mint NPCs, population, supplies or autonomous authority.
#include "galaxy/Stage07PopulationHouseholds.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::stage7 {
enum class ChildPhase : std::uint8_t {
    BornWithoutPhysicalRegistration=1, PhysicallyRegistered=2,
    InPhysicalTransit=3, LegallyRelocated=4
};
struct ChildLifecycle {
    std::uint64_t child_id=0,birth_event=0,parent_a=0,parent_b=0;
    std::uint64_t birth_tick=0,registration_command=0,registration_tick=0;
    EntityId natal_home=0,legal_home=0,physical_location=0;
    std::uint64_t current_guardian=0;
    ChildPhase phase=ChildPhase::BornWithoutPhysicalRegistration;
};
struct Lifecycle {
    std::uint64_t world_generation=0,world_tick=0;
    Census original_census{};
    std::map<std::uint64_t,ChildLifecycle> children{};
    std::uint64_t unregistered_births=0,registered_children=0;
    std::uint64_t relocated_children=0,in_transit_children=0;
};
namespace detail {
struct ReadDb {
    sqlite3* db=nullptr;
    explicit ReadDb(const std::string& path) {
        if(path.empty() || sqlite3_open_v2(path.c_str(),&db,
             SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
            if(db)sqlite3_close(db);
            db=nullptr;
            throw std::runtime_error("Stage 7: original world SQLite unreadable");
        }
        sqlite3_busy_timeout(db,1000);
    }
    ~ReadDb(){if(db)sqlite3_close(db);}
    ReadDb(const ReadDb&)=delete;
    ReadDb& operator=(const ReadDb&)=delete;
};
struct Statement {
    sqlite3_stmt* stmt=nullptr;
    explicit Statement(sqlite3* db,const char* query) {
        if(sqlite3_prepare_v2(db,query,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("Stage 7: missing original society evidence");
    }
    ~Statement(){sqlite3_finalize(stmt);}
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
    void bind(int idx,std::uint64_t n) {
        require(n<=static_cast<std::uint64_t>(
                    std::numeric_limits<sqlite3_int64>::max()),
                "Stage 7: original SQLite identity overflow");
        require(sqlite3_bind_int64(stmt,idx,static_cast<sqlite3_int64>(n))==SQLITE_OK,
                "Stage 7: SQLite identity bind failed");
    }
};
} // namespace detail

// Caller supplies ART02 birth-event IDs, never arbitrary child IDs. Distinguish
// a born dependent in the actual society census from an independently
// registered, physically present dependent. A legal move is not a new birth.
// Verify every ART02 birth witness against the exact committed PersistentWorld
// snapshot in the SAME SQLite file; a look-alike family DB cannot pass.
inline Lifecycle audit_household_lifecycle(
        const PersistentWorld& world,const NpcAppearanceStore& family,
        const std::string& original_world_sqlite,
        const std::vector<std::uint64_t>& birth_events) {
    Lifecycle out;
    out.world_generation=world.generation();
    out.world_tick=world.world().tick();
    out.original_census=project(world.world());
    detail::ReadDb connection(original_world_sqlite);
    require(sqlite3_exec(connection.db,"BEGIN",nullptr,nullptr,nullptr)==SQLITE_OK,
            "Stage 7: failed to pin original SQLite snapshot");
    {
        detail::Statement q(connection.db,
            "SELECT state FROM snapshots WHERE generation=?");
        q.bind(1,out.world_generation);
        const auto state=world.world().canonical_state();
        require(sqlite3_step(q.stmt)==SQLITE_ROW &&
                sqlite3_column_bytes(q.stmt,0)==static_cast<int>(state.size()) &&
                std::memcmp(sqlite3_column_blob(q.stmt,0),state.data(),state.size())==0,
                "Stage 7: family source is not the committed original world");
        require(sqlite3_step(q.stmt)==SQLITE_DONE,
                "Stage 7: duplicate original world generation");
    }
    std::set<std::uint64_t> unique_events;
    for(const auto birth_id:birth_events) {
        require(birth_id && unique_events.insert(birth_id).second,
                "Stage 7: missing or repeated immutable birth event");
        const auto birth=family.get_birth(birth_id);
        require(birth.event_id==birth_id && birth.child_id>=(1ULL<<62) &&
                birth.parent_a && birth.parent_b &&
                birth.parent_a!=birth.parent_b && birth.settlement &&
                birth.tick && birth.tick<=out.world_tick,
                "Stage 7: invalid original birth family or timestamp");
        require(!world.world().npc_records().count(birth.child_id) &&
                !world.world().npc_casualties().count(birth.child_id),
                "Stage 7: dependent was cloned as a named worker/casualty");
        for(auto parent:{birth.parent_a,birth.parent_b})
            require(world.world().npc_records().count(parent) ||
                    world.world().npc_casualties().count(parent),
                    "Stage 7: missing original parent identity");
        detail::Statement q(connection.db,
            "SELECT child_id,parent_a,parent_b,home_ship,tick,world_generation "
            "FROM sg_med_society_birth WHERE event_id=?");
        q.bind(1,birth_id);
        require(sqlite3_step(q.stmt)==SQLITE_ROW &&
                sqlite3_column_int64(q.stmt,0)==static_cast<sqlite3_int64>(birth.child_id) &&
                sqlite3_column_int64(q.stmt,1)==static_cast<sqlite3_int64>(birth.parent_a) &&
                sqlite3_column_int64(q.stmt,2)==static_cast<sqlite3_int64>(birth.parent_b) &&
                sqlite3_column_int64(q.stmt,3)==static_cast<sqlite3_int64>(birth.settlement) &&
                sqlite3_column_int64(q.stmt,4)==static_cast<sqlite3_int64>(birth.tick) &&
                sqlite3_column_int64(q.stmt,5)>0 &&
                static_cast<std::uint64_t>(sqlite3_column_int64(q.stmt,5))<=out.world_generation,
                "Stage 7: birth absent from same-world immutable society receipt");
        require(sqlite3_step(q.stmt)==SQLITE_DONE,
                "Stage 7: duplicate original birth witness");
        ChildLifecycle child{};
        child.child_id=birth.child_id;
        child.birth_event=birth_id;
        child.parent_a=birth.parent_a;child.parent_b=birth.parent_b;
        child.birth_tick=birth.tick;child.natal_home=birth.settlement;
        child.legal_home=family.current_residence(birth.child_id);
        require(child.legal_home!=0,"Stage 7: recorded child lacks legal residence");
        const auto physical=world.world().physical_dependents().find(birth.child_id);
        if(physical==world.world().physical_dependents().end()) {
            ++out.unregistered_births;
            child.phase=child.legal_home==birth.settlement ?
                ChildPhase::BornWithoutPhysicalRegistration:ChildPhase::LegallyRelocated;
        } else {
            const auto& p=physical->second;
            const auto new_guardian=family.current_nonparent_guardian(birth.child_id);
            require(p.child_id==birth.child_id &&
                    (p.guardian_id==birth.parent_a ||
                     p.guardian_id==birth.parent_b ||
                     (new_guardian && p.guardian_id==new_guardian)) &&
                    p.registration_command && p.registered_tick>=birth.tick &&
                    p.registered_tick<=out.world_tick && p.location &&
                    p.home_ship==child.legal_home &&
                    world.world().has_command_id(p.registration_command),
                    "Stage 7: physical child violates original birth/legal custody");
            child.registration_command=p.registration_command;
            child.registration_tick=p.registered_tick;
            child.current_guardian=p.guardian_id;
            child.physical_location=p.location;
            ++out.registered_children;
            if(p.location!=p.home_ship) {
                child.phase=ChildPhase::InPhysicalTransit;
                ++out.in_transit_children;
            } else child.phase=child.legal_home!=birth.settlement ?
                ChildPhase::LegallyRelocated:ChildPhase::PhysicallyRegistered;
        }
        if(child.legal_home!=birth.settlement)++out.relocated_children;
        require(out.children.emplace(birth.child_id,child).second,
                "Stage 7: original child claimed by two birth events");
    }
    require(out.registered_children<=out.original_census.registered_dependents &&
            out.children.size()==birth_events.size(),
            "Stage 7: physical/ART02 population reconciliation failed");
    return out;
}
} // namespace galaxy::stage7
