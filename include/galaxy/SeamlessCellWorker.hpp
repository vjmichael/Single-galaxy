#pragma once
// SPACE-03: host-only, per-cell computational worker lease. The original
// RegionalModeAuthority remains the Cold/Detailed epoch authority, and the
// single PersistentWorld remains the ONLY physics/NPC/cargo writer.
// This CAS lease cannot authorize a ship transfer or fabricate inventory.
#include "galaxy/SeamlessSpatialOwnership.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace galaxy::spatial {
struct WorkerLease {
    Cell cell{};
    std::string worker;
    std::string transfer_id;
    std::uint64_t epoch=0;
    std::uint64_t region_generation=0,checkpoint_revision=0;
    std::uint64_t observed_original_generation=0;
    bool replayed=false;
};
// Only trusted host holds a writable original world DB filename and can
// assert host approval. Every transfer is serialized with native world and
// region authority via the SAME physical SQLite BEGIN IMMEDIATE lock.
inline WorkerLease transfer_cell_worker(
    const std::string& original_sqlite,
    const Cell& cell,const std::string& next_worker,
    std::uint64_t expected_worker_epoch,
    std::uint64_t expected_region_generation,
    std::uint64_t expected_checkpoint_revision,
    const std::string& durable_transfer_id,
    bool trusted_host_authenticated_approval) {
    if(!trusted_host_authenticated_approval || original_sqlite.empty() ||
       next_worker.empty() || next_worker.size()>128 ||
       durable_transfer_id.empty() || durable_transfer_id.size()>128 ||
       !expected_region_generation || !expected_checkpoint_revision ||
       expected_worker_epoch>=static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        throw std::invalid_argument("spatial worker requires explicit host/epoch");
    const auto scoped=scope(cell);
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(original_sqlite.c_str(),&db,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("spatial worker original SQLite unavailable");
    }
    bool committed=false;
    try {
        sqlite3_busy_timeout(db,5000);
        detail::sql(db,"BEGIN IMMEDIATE");
        // No separate worker ownership if original source/region provenance
        // is not yet bootstrapped. A fictional unoccupied grid is not a
        // source of player goods.
        detail::Statement state(db,"SELECT generation,checkpoint_revision,phase "
            "FROM sg_regional_mode_authority WHERE scope=?");
        state.text(1,scoped);
        if(!state.row() ||
           state.integer(0)!=static_cast<std::int64_t>(expected_region_generation) ||
           state.integer(1)!=static_cast<std::int64_t>(expected_checkpoint_revision) ||
           state.integer(2)!=0)
            throw std::runtime_error("spatial worker region authority stale or pending");
        detail::Statement snap(db,"SELECT generation,state,checksum FROM snapshots "
                                  "ORDER BY generation DESC LIMIT 1");
        if(!snap.row())throw std::runtime_error("missing original physical snapshot");
        const auto generation=snap.integer(0);
        const auto* raw=sqlite3_column_blob(snap.q,1);
        const auto length=sqlite3_column_bytes(snap.q,1);
        const auto saved=snap.str(2);
        if(generation<=0||!raw||length<=0||length>16*1024*1024)
            throw std::runtime_error("corrupt original spatial snapshot");
        const auto world=Simulation::from_snapshot(
            std::string(static_cast<const char*>(raw),
                        static_cast<std::size_t>(length)));
        if(saved!=std::to_string(world.state_hash()) ||
           !verify_native_index(db,world,static_cast<std::uint64_t>(generation)))
            throw std::runtime_error("spatial worker without verified original cell index");
        detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_worker_lease("
            "scope TEXT PRIMARY KEY,worker TEXT NOT NULL,epoch INTEGER NOT NULL,"
            "region_generation INTEGER NOT NULL,checkpoint_revision INTEGER NOT NULL,"
            "world_generation INTEGER NOT NULL,transfer_id TEXT NOT NULL)");
        detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_worker_transfer("
            "scope TEXT NOT NULL,transfer_id TEXT NOT NULL,"
            "from_worker TEXT NOT NULL,to_worker TEXT NOT NULL,"
            "from_epoch INTEGER NOT NULL,to_epoch INTEGER NOT NULL,"
            "region_generation INTEGER NOT NULL,world_generation INTEGER NOT NULL,"
            "PRIMARY KEY(scope,transfer_id),UNIQUE(scope,to_epoch))");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_worker_log_no_update "
            "BEFORE UPDATE ON sg_spatial_worker_transfer BEGIN "
            "SELECT RAISE(ABORT,'immutable spatial worker handoff');END");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_worker_log_no_delete "
            "BEFORE DELETE ON sg_spatial_worker_transfer BEGIN "
            "SELECT RAISE(ABORT,'immutable spatial worker handoff');END");
        detail::Statement existing(db,"SELECT worker,epoch,transfer_id,"
            "region_generation,checkpoint_revision "
            "FROM sg_spatial_worker_lease WHERE scope=?");
        existing.text(1,scoped);
        const bool present=existing.row();
        const std::string prior=present?existing.str(0):std::string{};
        const auto epoch=present?existing.integer(1):0;
        const auto previous_id=present?existing.str(2):std::string{};
        const auto previous_region=present?existing.integer(3):0;
        const auto previous_revision=present?existing.integer(4):0;
        WorkerLease result{};
        result.cell=cell;result.worker=next_worker;
        result.transfer_id=durable_transfer_id;
        result.region_generation=expected_region_generation;
        result.checkpoint_revision=expected_checkpoint_revision;
        result.observed_original_generation=static_cast<std::uint64_t>(generation);
        // Exact same transfer replay is allowed but must not re-issue any
        // physical effect, increment a worker epoch or create a new job.
        if(present && previous_id==durable_transfer_id &&
           prior==next_worker && epoch>0 &&
           previous_region==static_cast<std::int64_t>(expected_region_generation) &&
           previous_revision==static_cast<std::int64_t>(expected_checkpoint_revision) &&
           static_cast<std::uint64_t>(epoch)==expected_worker_epoch+1){
            result.epoch=static_cast<std::uint64_t>(epoch);
            result.replayed=true;
        }else {
            if(epoch<0 ||
               static_cast<std::uint64_t>(epoch)!=expected_worker_epoch)
                throw std::runtime_error("stale spatial worker generation");
            detail::Statement insert(db,"INSERT INTO sg_spatial_worker_transfer("
               "scope,transfer_id,from_worker,to_worker,from_epoch,to_epoch,"
               "region_generation,world_generation) VALUES(?,?,?,?,?,?,?,?)");
            insert.text(1,scoped);insert.text(2,durable_transfer_id);
            insert.text(3,prior);insert.text(4,next_worker);
            insert.number(5,expected_worker_epoch);
            insert.number(6,expected_worker_epoch+1);
            insert.number(7,expected_region_generation);
            insert.number(8,static_cast<std::uint64_t>(generation));
            insert.execute();
            detail::Statement update(db,"INSERT INTO sg_spatial_worker_lease("
               "scope,worker,epoch,region_generation,checkpoint_revision,"
               "world_generation,transfer_id) VALUES(?,?,?,?,?,?,?) "
               "ON CONFLICT(scope) DO UPDATE SET "
               "worker=excluded.worker,epoch=excluded.epoch,"
               "region_generation=excluded.region_generation,"
               "checkpoint_revision=excluded.checkpoint_revision,"
               "world_generation=excluded.world_generation,"
               "transfer_id=excluded.transfer_id");
            update.text(1,scoped);update.text(2,next_worker);
            update.number(3,expected_worker_epoch+1);
            update.number(4,expected_region_generation);
            update.number(5,expected_checkpoint_revision);
            update.number(6,static_cast<std::uint64_t>(generation));
            update.text(7,durable_transfer_id);update.execute();
            result.epoch=expected_worker_epoch+1;
        }
        detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
        return result;
    }catch(...){
        if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        sqlite3_close_v2(db);throw;
    }
}
// Admission is ONLY an opt-in worker CPU authorization, NOT permission to
// command a ship or move cargo. Every native gameplay command still goes
// through the original PersistentWorld authorization and journal.
inline bool worker_may_simulate_cell(
    const std::string& original_sqlite,
    const Cell& cell,const std::string& trusted_worker,
    std::uint64_t expected_worker_epoch,
    std::uint64_t expected_region_generation,
    std::uint64_t expected_checkpoint_revision) {
    if(original_sqlite.empty() || trusted_worker.empty() ||
       !expected_worker_epoch || !expected_region_generation ||
       !expected_checkpoint_revision)return false;
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(original_sqlite.c_str(),&db,
        SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);return false;
    }
    try{
        const auto scoped=scope(cell);
        detail::Statement q(db,"SELECT a.worker,a.epoch,a.region_generation,"
          "a.checkpoint_revision,r.generation,r.checkpoint_revision,r.phase "
          "FROM sg_spatial_worker_lease a JOIN sg_regional_mode_authority r "
          "ON r.scope=a.scope WHERE a.scope=?");
        q.text(1,scoped);
        const bool valid=q.row() && q.str(0)==trusted_worker &&
          q.integer(1)==static_cast<sqlite3_int64>(expected_worker_epoch) &&
          q.integer(2)==static_cast<sqlite3_int64>(expected_region_generation) &&
          q.integer(3)==static_cast<sqlite3_int64>(expected_checkpoint_revision) &&
          q.integer(4)==static_cast<sqlite3_int64>(expected_region_generation) &&
          q.integer(5)==static_cast<sqlite3_int64>(expected_checkpoint_revision) &&
          q.integer(6)==0;
        sqlite3_close_v2(db);return valid;
    }catch(const std::exception&){sqlite3_close_v2(db);return false;}
}
} // namespace galaxy::spatial
