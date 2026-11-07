#pragma once
// SPACE-05B: active, host-only multi-cell scheduling above ONE original world.
// Each cell owns a durable worker CAS lease; exactly ONE selected claim can
// commit each generation. Unselected workers are read-only, never shadow
// Simulation owners. The original SQLite BEGIN IMMEDIATE serializes effects.
#include "galaxy/SeamlessCellWorker.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/NativeCellProcessRunner.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::spatial {

inline NativeCellWorkerTick claim_original_writer(
    const std::string& native_sqlite,const PersistentWorld& native,
    const WorkerLease& worker,const std::string& claim_id,
    bool authenticated_host){
    if(!authenticated_host || native_sqlite.empty() || claim_id.empty() ||
       claim_id.size()>128 || worker.worker.empty() ||
       !worker.epoch || !worker.region_generation ||
       !worker.checkpoint_revision ||
       native.generation()>=static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        throw std::invalid_argument("host must authorize exact worker dispatch");
    const auto requested=scope(worker.cell);
    bool occupied=false;
    for(const auto& [id,e]:native.world().entities()){
        if(e.type!=EntityType::Ship)continue;
        const auto current=ship_cell(native.world(),id);
        if(!current)
            throw std::runtime_error("original ship cell unresolved");
        if(scope(*current)==requested)occupied=true;
    }
    if(!occupied)throw std::runtime_error("cannot dispatch vacant physical cell");
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(native_sqlite.c_str(),&db,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("original worker journal unavailable");
    }
    bool committed=false;
    try{
        sqlite3_busy_timeout(db,5000);
        detail::sql(db,"BEGIN IMMEDIATE");
        {
            detail::Statement physical(db,"SELECT generation,checksum FROM snapshots "
                "ORDER BY generation DESC LIMIT 1");
            if(!physical.row() || physical.integer(0)!=
               static_cast<std::int64_t>(native.generation()) ||
               physical.str(1)!=std::to_string(native.world().state_hash()) ||
               !verify_native_index(db,native.world(),native.generation()))
                throw std::runtime_error("stale or corrupt native writer snapshot");
            detail::Statement lease(db,"SELECT a.worker,a.epoch,a.region_generation,"
                "a.checkpoint_revision,r.generation,r.checkpoint_revision,r.phase "
                "FROM sg_spatial_worker_lease a "
                "JOIN sg_regional_mode_authority r ON r.scope=a.scope "
                "WHERE a.scope=?");
            lease.text(1,requested);
            if(!lease.row() || lease.str(0)!=worker.worker ||
               lease.integer(1)!=static_cast<std::int64_t>(worker.epoch) ||
               lease.integer(2)!=static_cast<std::int64_t>(worker.region_generation) ||
               lease.integer(3)!=static_cast<std::int64_t>(worker.checkpoint_revision) ||
               lease.integer(4)!=static_cast<std::int64_t>(worker.region_generation) ||
               lease.integer(5)!=static_cast<std::int64_t>(worker.checkpoint_revision) ||
               lease.integer(6)!=0)
                throw std::runtime_error("unleased or stale original cell worker");
            detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_tick_dispatch("
                "world_generation INTEGER PRIMARY KEY,claim_id TEXT NOT NULL UNIQUE,"
                "scope TEXT NOT NULL,worker TEXT NOT NULL,epoch INTEGER NOT NULL,"
                "region_generation INTEGER NOT NULL,checkpoint_revision INTEGER NOT NULL,"
                "status INTEGER NOT NULL CHECK(status IN (0,1)),"
                "completed_generation INTEGER NOT NULL DEFAULT 0)");
            detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_dispatch_attempt("
                "world_generation INTEGER NOT NULL,claim_id TEXT NOT NULL,"
                "scope TEXT NOT NULL,worker TEXT NOT NULL,epoch INTEGER NOT NULL,"
                "PRIMARY KEY(world_generation,claim_id))");
            detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_dispatch_attempt_immutable_update "
                "BEFORE UPDATE ON sg_spatial_dispatch_attempt BEGIN "
                "SELECT RAISE(ABORT,'immutable dispatch attempt');END");
            detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_dispatch_attempt_immutable_delete "
                "BEFORE DELETE ON sg_spatial_dispatch_attempt BEGIN "
                "SELECT RAISE(ABORT,'immutable dispatch attempt');END");
            const auto generation=native.generation();
            detail::Statement old(db,"SELECT claim_id,scope,worker,epoch,status "
                "FROM sg_spatial_tick_dispatch WHERE world_generation=?");
            old.number(1,generation);
            bool insert=true;
            if(old.row()){
                if(old.str(0)==claim_id && old.str(1)==requested &&
                   old.str(2)==worker.worker &&
                   old.integer(3)==static_cast<std::int64_t>(worker.epoch) &&
                   old.integer(4)==0){
                    insert=false; // exact idempotent retry after a failed commit
                }else{
                    // A stranded global claim may be moved to another
                    // occupied cell ONLY after the previous cell's lease has
                    // been revoked by the host's durable worker-epoch CAS.
                    // A merely silent/slow worker with a live epoch CANNOT
                    // be preempted by another untrusted process.
                    const auto former_scope=old.str(1);
                    const auto former_worker=old.str(2);
                    const auto former_epoch=old.integer(3);
                    const auto former_claim=old.str(0);
                    if(old.integer(4)!=0 || former_epoch<=0 ||
                       former_scope.empty() || former_worker.empty())
                        throw std::runtime_error("competing selected global writer");
                    detail::Statement previous(db,"SELECT worker,epoch "
                        "FROM sg_spatial_worker_lease WHERE scope=?");
                    previous.text(1,former_scope);
                    if(!previous.row() ||
                       (previous.str(0)==former_worker &&
                        previous.integer(1)==former_epoch))
                        throw std::runtime_error("previous global writer still leased");
                    if(former_scope==requested &&
                       former_epoch>=static_cast<std::int64_t>(worker.epoch))
                        throw std::runtime_error("same-cell takeover epoch stale");
                    detail::Statement take(db,"UPDATE sg_spatial_tick_dispatch "
                        "SET claim_id=?,scope=?,worker=?,epoch=?,region_generation=?,"
                        "checkpoint_revision=?,status=0,completed_generation=0 "
                        "WHERE world_generation=? AND claim_id=? AND scope=? "
                        "AND worker=? AND epoch=? AND status=0");
                    take.text(1,claim_id);take.text(2,requested);
                    take.text(3,worker.worker);take.number(4,worker.epoch);
                    take.number(5,worker.region_generation);
                    take.number(6,worker.checkpoint_revision);
                    take.number(7,generation);take.text(8,former_claim);
                    take.text(9,former_scope);take.text(10,former_worker);
                    take.number(11,static_cast<std::uint64_t>(former_epoch));
                    take.execute();
                    if(sqlite3_changes(db)!=1)
                        throw std::runtime_error("writer takeover lost CAS race");
                }
            }else{
                detail::Statement add(db,"INSERT INTO sg_spatial_tick_dispatch("
                    "world_generation,claim_id,scope,worker,epoch,region_generation,"
                    "checkpoint_revision,status) VALUES(?,?,?,?,?,?,?,0)");
                add.number(1,generation);add.text(2,claim_id);
                add.text(3,requested);add.text(4,worker.worker);
                add.number(5,worker.epoch);
                add.number(6,worker.region_generation);
                add.number(7,worker.checkpoint_revision);
                add.execute();
            }
            if(insert){
                detail::Statement audit(db,"INSERT INTO sg_spatial_dispatch_attempt "
                    "(world_generation,claim_id,scope,worker,epoch) VALUES(?,?,?,?,?)");
                audit.number(1,generation);audit.text(2,claim_id);
                audit.text(3,requested);audit.text(4,worker.worker);
                audit.number(5,worker.epoch);audit.execute();
            }
        }
        detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
        return NativeCellWorkerTick{requested,worker.worker,worker.epoch,
            worker.region_generation,worker.checkpoint_revision,claim_id};
    }catch(...){
        if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        sqlite3_close_v2(db);throw;
    }
}

// An invalid player/worker command must not strand its selected global
// dispatch. The original host may revoke a STILL-PENDING claim under the
// same SQLite writer lock. Every revocation is immutable/auditable and a
// later stale worker commit must then fail admission.
inline bool release_uncommitted_writer_claim(
    const std::string& path,const PersistentWorld& native,
    const NativeCellWorkerTick& token,bool trusted_host){
    if(!trusted_host || path.empty() || token.dispatch_id.empty() ||
       token.canonical_cell_scope.empty())
        throw std::invalid_argument("original host claim release required");
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(path.c_str(),&db,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("original dispatch release SQLite unavailable");
    }
    bool committed=false;
    try{
        sqlite3_busy_timeout(db,5000);
        detail::sql(db,"BEGIN IMMEDIATE");
        detail::Statement latest(db,"SELECT generation,checksum FROM snapshots "
            "ORDER BY generation DESC LIMIT 1");
        if(!latest.row() ||
           latest.integer(0)!=static_cast<std::int64_t>(native.generation()) ||
           latest.str(1)!=std::to_string(native.world().state_hash()) ||
           !verify_native_index(db,native.world(),native.generation()))
            throw std::runtime_error("world changed before dispatch release");
        detail::Statement selected(db,"SELECT scope,worker,epoch,status "
            "FROM sg_spatial_tick_dispatch WHERE world_generation=? AND claim_id=?");
        selected.number(1,native.generation());
        selected.text(2,token.dispatch_id);
        if(!selected.row()){
            detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
            return false;
        }
        if(selected.str(0)!=token.canonical_cell_scope ||
           selected.str(1)!=token.trusted_worker ||
           selected.integer(2)!=static_cast<std::int64_t>(token.worker_epoch) ||
           selected.integer(3)!=0)
            throw std::runtime_error("cannot release different or committed dispatch");
        detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_dispatch_release("
            "claim_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL,"
            "scope TEXT NOT NULL,worker TEXT NOT NULL,epoch INTEGER NOT NULL,"
            "original_hash TEXT NOT NULL)");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_dispatch_release_no_update "
            "BEFORE UPDATE ON sg_spatial_dispatch_release BEGIN "
            "SELECT RAISE(ABORT,'immutable native dispatch cancellation');END");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_dispatch_release_no_delete "
            "BEFORE DELETE ON sg_spatial_dispatch_release BEGIN "
            "SELECT RAISE(ABORT,'immutable native dispatch cancellation');END");
        detail::Statement audit(db,"INSERT INTO sg_spatial_dispatch_release "
            "(claim_id,world_generation,scope,worker,epoch,original_hash) "
            "VALUES(?,?,?,?,?,?)");
        audit.text(1,token.dispatch_id);
        audit.number(2,native.generation());
        audit.text(3,token.canonical_cell_scope);
        audit.text(4,token.trusted_worker);
        audit.number(5,token.worker_epoch);
        audit.text(6,std::to_string(native.world().state_hash()));
        audit.execute();
        detail::Statement clear(db,"DELETE FROM sg_spatial_tick_dispatch "
            "WHERE world_generation=? AND claim_id=? AND status=0");
        clear.number(1,native.generation());clear.text(2,token.dispatch_id);
        clear.execute();
        if(sqlite3_changes(db)!=1)
            throw std::runtime_error("writer dispatch release race");
        detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
        return true;
    }catch(...){
        if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        sqlite3_close_v2(db);throw;
    }
}

// No unbounded CPU/process or task queue; the host selects one of at most
// four admitted cell workers for each ORIGINAL tick. Detailed work takes
// priority; cold cells still retain all original NPC needs/cargo every tick.
// A crashed worker is replaced by transfer_cell_worker's epoch CAS and
// claim_original_writer's durable takeover, not a replayed world tick.
class NativeMultiCellSupervisor {
public:
    explicit NativeMultiCellSupervisor(std::string original_sqlite,
        PersistentWorld& original,std::size_t max_workers=4):
        db_(std::move(original_sqlite)),world_(original),limit_(max_workers){
        if(db_.empty() || limit_==0 || limit_>4)
            throw std::invalid_argument("bounded original supervisor capacity");
    }
    // Production mode: each admitted cell executes in a constrained native
    // child process. Unlike in-process async eligibility calculations, this
    // exercises a real OS process lifecycle and a deadline/kill policy.
    // No SQLite writer path or mutable world is passed to children.
    void use_native_process_workers(std::string executable){
        if(executable.empty() || executable.size()>4096)
            throw std::invalid_argument("worker executable path required");
        process_executable_=std::move(executable);
    }
    void register_worker(const WorkerLease& lease){
        const auto name=scope(lease.cell);
        if(leases_.size()>=limit_ && !leases_.count(name))
            throw std::runtime_error("native supervisor worker budget exceeded");
        if(!worker_may_simulate_cell(db_,lease.cell,lease.worker,lease.epoch,
                                    lease.region_generation,lease.checkpoint_revision))
            throw std::runtime_error("worker lease not authorized in original SQLite");
        leases_[name]=lease;
    }
    WorkerLease recover_worker(const Cell& cell,const std::string& replacement,
        const std::string& unique_handoff_id,bool authenticated_host){
        const auto name=scope(cell);
        const auto found=leases_.find(name);
        if(found==leases_.end())
            throw std::runtime_error("cannot recover unknown cell lease");
        const auto& old=found->second;
        const auto successor=transfer_cell_worker(db_,cell,replacement,
            old.epoch,old.region_generation,old.checkpoint_revision,
            unique_handoff_id,authenticated_host);
        found->second=successor;
        return successor;
    }
    // Player commands use the exact same global writer arbitration as a
    // physical tick. A rejected/no-op command releases its uncommitted
    // claim with immutable audit, rather than deadlocking subsequent ticks.
    bool submit_original_command(const Command& command,
        const std::string& claim_id,FailPoint failure=FailPoint::None){
        if(claim_id.empty() || !command.actor)
            throw std::invalid_argument("original command dispatch required");
        const auto* actor=world_.world().find(command.actor);
        const auto cell=ship_cell(world_.world(),command.actor);
        if(!actor || actor->type!=EntityType::Ship || !cell)
            throw std::runtime_error("command needs current physical ship");
        const auto key=scope(*cell);
        const auto leased=leases_.find(key);
        if(leased==leases_.end() ||
           !worker_may_simulate_cell(db_,leased->second.cell,
                leased->second.worker,leased->second.epoch,
                leased->second.region_generation,
                leased->second.checkpoint_revision))
            throw std::runtime_error("original command cell not currently leased");
        if(!process_executable_.empty()){
            std::size_t ships=0;
            for(const auto& [id,e]:world_.world().entities()){
                if(e.type!=EntityType::Ship)continue;
                const auto resident=ship_cell(world_.world(),id);
                if(resident && scope(*resident)==key)++ships;
            }
            const auto grade=desired_detail(world_.world(),*cell);
            const CellProcessTask request{key,leased->second.worker,
                world_.generation(),world_.world().state_hash(),ships,
                static_cast<std::uint8_t>(grade)};
            const auto result=run_cell_process(process_executable_,request);
            if(!result.passed)
                throw std::runtime_error("native OS command worker unavailable");
        }
        const auto token=claim_original_writer(
            db_,world_,leased->second,claim_id,true);
        try{
            const bool accepted=world_.submit_as_cell_worker(command,token,failure);
            if(!accepted &&
               !release_uncommitted_writer_claim(db_,world_,token,true))
                throw std::runtime_error("missing rejected-command claim release");
            return accepted;
        }catch(...){
            try{
                (void)release_uncommitted_writer_claim(db_,world_,token,true);
            }catch(const std::exception&){} // preserve original error
            throw;
        }
    }
    struct Committed {
        std::string scope,worker,dispatch_id;
        std::uint64_t world_generation=0,simulation_tick=0;
    };
    Committed run_next_tick(const std::string& dispatch_id,
        FailPoint failure=FailPoint::None){
        if(dispatch_id.empty())throw std::invalid_argument("dispatch ID required");
        if(leases_.empty())throw std::runtime_error("no native cell workers");
        // These are REAL bounded concurrent read-only worker tasks. No task
        // receives a mutable Simulation or SQLite game-writer handle. The
        // supervisor waits for every result before electing the ONE durable
        // original-world writer; no worker may independently advance a tick.
        const Simulation& immutable=world_.world();
        const auto native_generation=world_.generation();
        const auto native_hash=immutable.state_hash();
        std::vector<std::future<std::optional<std::pair<std::string,int>>>> pending;
        pending.reserve(leases_.size());
        last_failed_.clear();
        for(const auto& [cell,lease]:leases_){
            pending.push_back(std::async(std::launch::async,
                [world=&immutable,db=db_,cell,lease,
                 binary=process_executable_,native_generation,native_hash]()
                    ->std::optional<std::pair<std::string,int>>{
                    if(!worker_may_simulate_cell(db,lease.cell,lease.worker,
                        lease.epoch,lease.region_generation,lease.checkpoint_revision))
                        return std::nullopt; // a revoked worker never receives a tick
                    std::size_t physical_ships=0;
                    for(const auto& [id,e]:world->entities()){
                        if(e.type!=EntityType::Ship)continue;
                        const auto current=ship_cell(*world,id);
                        if(current && scope(*current)==cell)
                            ++physical_ships;
                    }
                    if(!physical_ships)return std::nullopt;
                    const auto detail=desired_detail(*world,lease.cell);
                    if(!binary.empty()){
                        const CellProcessTask request{cell,lease.worker,
                            native_generation,native_hash,physical_ships,
                            static_cast<std::uint8_t>(detail)};
                        const auto result=run_cell_process(binary,request);
                        if(!result.passed)
                            throw std::runtime_error(
                                "OS cell process denied: "+result.reason);
                    }
                    return std::make_pair(cell,static_cast<int>(detail));
                }));
        }
        std::vector<std::pair<std::string,int>> eligible;
        for(std::size_t i=0;i<pending.size();++i){
            try{
                auto report=pending[i].get();
                if(report)eligible.push_back(*report);
            }catch(const std::exception&){
                auto at=leases_.begin();
                std::advance(at,static_cast<std::ptrdiff_t>(i));
                last_failed_.push_back(at->first);
            }
        }
        if(eligible.empty())
            throw std::runtime_error("no healthy occupied leased native cells");
        const auto max_detail=std::max_element(eligible.begin(),eligible.end(),
            [](const auto& a,const auto& b){return a.second<b.second;})->second;
        std::vector<std::string> candidates;
        for(const auto& e:eligible)
            if(e.second==max_detail)candidates.push_back(e.first);
        // The original durable tick is the only rotation clock. Restarting
        // the supervisor cannot change the selected worker for an uncommitted
        // generation or lose a cell's turn after command-only generations.
        const auto winner=candidates[
            static_cast<std::size_t>(world_.world().tick()%candidates.size())];
        const auto& worker=leases_.at(winner);
        const auto token=claim_original_writer(db_,world_,worker,dispatch_id,true);
        world_.step_as_cell_worker(token,failure);
        return {winner,worker.worker,dispatch_id,world_.generation(),world_.world().tick()};
    }
    std::size_t worker_count() const noexcept {return leases_.size();}
    const std::vector<std::string>& last_failed_cells() const noexcept {
        return last_failed_;
    }
private:
    std::string db_;
    PersistentWorld& world_;
    std::size_t limit_;
    std::map<std::string,WorkerLease> leases_;
    std::vector<std::string> last_failed_;
    std::string process_executable_;
};
} // namespace galaxy::spatial
