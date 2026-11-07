#pragma once
// SPACE-05B: active, host-only multi-cell scheduling above ONE original world.
// Each cell owns a durable worker CAS lease; exactly ONE selected claim can
// commit each generation. Unselected workers are read-only, never shadow
// Simulation owners. The original SQLite BEGIN IMMEDIATE serializes effects.
#include "galaxy/SeamlessCellWorker.hpp"
#include "galaxy/Persistence.hpp"
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
                    // Recovery is only a strictly newer, CAS-issued epoch
                    // replacing a stranded claim on the SAME real cell.
                    if(old.integer(4)!=0 || old.str(1)!=requested ||
                       old.integer(3)>=static_cast<std::int64_t>(worker.epoch))
                        throw std::runtime_error("competing selected global writer");
                    detail::Statement take(db,"UPDATE sg_spatial_tick_dispatch "
                        "SET claim_id=?,worker=?,epoch=?,region_generation=?,"
                        "checkpoint_revision=?,status=0,completed_generation=0 "
                        "WHERE world_generation=? AND scope=? AND epoch<? AND status=0");
                    take.text(1,claim_id);take.text(2,worker.worker);
                    take.number(3,worker.epoch);
                    take.number(4,worker.region_generation);
                    take.number(5,worker.checkpoint_revision);
                    take.number(6,generation);take.text(7,requested);
                    take.number(8,worker.epoch);take.execute();
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
        std::vector<std::future<std::optional<std::pair<std::string,int>>>> pending;
        pending.reserve(leases_.size());
        last_failed_.clear();
        for(const auto& [cell,lease]:leases_){
            pending.push_back(std::async(std::launch::async,
                [world=&immutable,db=db_,cell,lease]()
                    ->std::optional<std::pair<std::string,int>>{
                    if(!worker_may_simulate_cell(db,lease.cell,lease.worker,
                        lease.epoch,lease.region_generation,lease.checkpoint_revision))
                        return std::nullopt; // a revoked worker never receives a tick
                    bool occupied=false;
                    for(const auto& [id,e]:world->entities()){
                        if(e.type!=EntityType::Ship)continue;
                        const auto current=ship_cell(*world,id);
                        if(current && scope(*current)==cell){
                            occupied=true;break;
                        }
                    }
                    if(!occupied)return std::nullopt;
                    const auto detail=desired_detail(*world,lease.cell);
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
        const auto winner=candidates[round_robin_%candidates.size()];
        // Update rotation only after a committed original tick. Rollback
        // retries select the identical worker until an explicit takeover.
        const auto& worker=leases_.at(winner);
        const auto token=claim_original_writer(db_,world_,worker,dispatch_id,true);
        world_.step_as_cell_worker(token,failure);
        ++round_robin_;
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
    std::uint64_t round_robin_=0;
    std::map<std::string,WorkerLease> leases_;
    std::vector<std::string> last_failed_;
};
} // namespace galaxy::spatial
