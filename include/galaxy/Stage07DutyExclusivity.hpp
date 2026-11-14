#pragma once
// Stage 7 POP-05: a single *original SQLite* decision boundary between
// DOCK-03 finite shore shifts and the native workshop/medical workforce.
// A projection or model suggestion cannot grant two conflicting duties.
// Called under PersistentWorld's existing BEGIN IMMEDIATE; not a new ledger.
#include "galaxy/Simulation.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace galaxy::stage7 {
namespace duty_detail {
struct SqlStatement {
    sqlite3_stmt* stmt=nullptr;
    explicit SqlStatement(sqlite3* db,const char* sql) {
        if(!db || sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("Stage 7: original duty SQLite prepare failed");
    }
    ~SqlStatement(){sqlite3_finalize(stmt);}
    SqlStatement(const SqlStatement&)=delete;
    SqlStatement& operator=(const SqlStatement&)=delete;
    void bind(int index,std::uint64_t n) {
        if(n>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(stmt,index,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
            throw std::runtime_error("Stage 7: invalid duty SQLite identity");
    }
    int next() {
        const int rc=sqlite3_step(stmt);
        if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)
            throw std::runtime_error("Stage 7: original duty SQLite read failed");
        return rc;
    }
};
} // namespace duty_detail

// DockLedger is handed a Simulation snapshot by the caller. A caller cannot
// reserve labor using a stale or constructed snapshot while the original
// world was concurrently assigned elsewhere. Must run after BEGIN IMMEDIATE,
// before the dock table INSERT, on the same original SQLite file.
inline bool original_snapshot_matches(sqlite3* db,const Simulation& sim) {
    if(!db)return false;
    duty_detail::SqlStatement q(db,
        "SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1");
    if(q.next()!=SQLITE_ROW)return false;
    const auto canonical=sim.canonical_state();
    const auto* stored=sqlite3_column_blob(q.stmt,0);
    const auto length=sqlite3_column_bytes(q.stmt,0);
    const bool equal=stored && length>0 &&
        static_cast<std::size_t>(length)==canonical.size() &&
        std::memcmp(stored,canonical.data(),canonical.size())==0;
    if(q.next()!=SQLITE_DONE)
        throw std::runtime_error("Stage 7: duplicate latest world snapshot");
    return equal;
}

// Exactly the original named-NPC fields used by these native actions.
// Medical host commands identify their medic in target, with amount being
// training or finite shift duration (NEVER the medic's identity).
inline std::uint64_t appointed_original_worker(const Command& command) {
    switch(command.action){
        case Action::AssignWorkshopOperator:
        case Action::AssignWorkshopFinisher:
        case Action::StartWorkshopShift:
        case Action::AppointWorkshopSupervisor:
            return command.amount>0 ?
                static_cast<std::uint64_t>(command.amount):0;
        case Action::HostScheduleMedicShift:
        case Action::HostTrainMedic:
            return command.target;
        default:return 0;
    }
}

// Active dock shift overlaps either an immediate native appointment or a
// future scheduled workshop shift. Long-duration dock reservations are
// measured from their original assigned_tick / shift_end_tick, not "now".
inline bool original_dock_shift_overlaps(sqlite3* db,
        std::uint64_t worker,std::uint64_t start,std::uint64_t end) {
    if(!db || !worker || !start || end<=start)
        throw std::invalid_argument("Stage 7: invalid original duty interval");
    duty_detail::SqlStatement table(db,
        "SELECT 1 FROM sqlite_master WHERE type='table' "
        "AND name='dock_staff_v1' LIMIT 1");
    if(table.next()==SQLITE_DONE)return false;
    duty_detail::SqlStatement duty(db,
        "SELECT 1 FROM dock_staff_v1 WHERE npc_id=? AND "
        "assigned_tick<? AND shift_end_tick>? LIMIT 1");
    duty.bind(1,worker);duty.bind(2,end);duty.bind(3,start);
    return duty.next()==SQLITE_ROW;
}

inline void enforce_original_dock_exclusivity(sqlite3* db,
        const Command& command) {
    const auto worker=appointed_original_worker(command);
    if(!worker)return;
    if(!command.execute_tick ||
       command.execute_tick==std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Stage 7: duty time invalid");
    const auto start=command.execute_tick;
    auto end=start+1;
    if(command.action==Action::StartWorkshopShift &&
       command.thrust_y>0 &&
       static_cast<std::uint64_t>(command.thrust_y)<
           std::numeric_limits<std::uint64_t>::max()-start)
        end=start+static_cast<std::uint64_t>(command.thrust_y);
    if(original_dock_shift_overlaps(db,worker,start,end))
        throw std::runtime_error(
            "Stage 7: worker already holds independently booked dock duty");
}
} // namespace galaxy::stage7
