#pragma once
// S6-H2: the original PersistentWorld has ALREADY committed the ship motion.
// This host-only action reconciles OS worker responsibility for its immutable
// SQLite migration receipt. It cannot move a ship, NPC, cargo or a world tick.
// Old lease retirement + destination lease CAS + immutable journal are ONE
// SQLite BEGIN IMMEDIATE transaction. A crash or injected failure rolls back
// everything. The original ship/index are the exclusive movement authority.
#include "galaxy/SeamlessCellWorker.hpp"
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace galaxy::spatial {
struct ShipCellHandoff {
    EntityId ship=0;
    std::string from_scope,to_scope;
    WorkerLease destination{};
    std::uint64_t physical_migration_generation=0;
    bool source_retired=false,replayed=false;
};
inline ShipCellHandoff reconcile_committed_ship_handoff(
    const std::string& original_sqlite,const PersistentWorld& native,
    EntityId ship,const WorkerLease& source,const Cell& destination,
    const std::string& destination_worker,std::uint64_t destination_prior_epoch,
    std::uint64_t destination_region_generation,
    std::uint64_t destination_checkpoint_revision,
    const std::string& durable_handoff_id,bool authenticated_host,
    FailPoint failure=FailPoint::None) {
    const auto from=scope(source.cell),to=scope(destination);
    if(!authenticated_host || original_sqlite.empty() || !ship ||
       from==to || source.worker.empty() || !source.epoch ||
       !source.region_generation || !source.checkpoint_revision ||
       destination_worker.empty() || destination_worker.size()>128 ||
       durable_handoff_id.empty() || durable_handoff_id.size()>128 ||
       !destination_region_generation || !destination_checkpoint_revision ||
       destination_prior_epoch>=static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        throw std::invalid_argument("original H2 handoff needs finite host authorization");
    const auto& original=native.world();
    const auto* entity=original.find(ship);
    if(!original.native_galactic_physics() || !entity ||
       entity->type!=EntityType::Ship ||
       !entity->physical_galactic_authoritative ||
       !original.systems().count(destination.system))
        throw std::runtime_error("H2 requires an original authoritative galactic ship");
    const auto current=ship_cell(original,ship);
    if(!current || !(*current==destination))
        throw std::runtime_error("H2 destination is not committed original physical position");
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(original_sqlite.c_str(),&db,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("H2 original SQLite unavailable");
    }
    bool committed=false;
    try {
        sqlite3_busy_timeout(db,5000);
        detail::sql(db,"BEGIN IMMEDIATE");
        detail::Statement snapshot(db,"SELECT generation,checksum FROM snapshots "
            "ORDER BY generation DESC LIMIT 1");
        if(!snapshot.row() || snapshot.integer(0)!=
           static_cast<sqlite3_int64>(native.generation()) ||
           snapshot.str(1)!=std::to_string(original.state_hash()) ||
           !verify_native_index(db,original,native.generation()))
            throw std::runtime_error("H2 stale original snapshot or spatial index");
        // The last immutable source migration, not a caller-proposed position,
        // authorizes the handoff. No earlier crossing may be claimed again.
        detail::Statement movement(db,"SELECT sequence,player_owner,from_scope,"
            "to_scope,world_generation,world_hash FROM sg_spatial_native_transfer "
            "WHERE ship_id=? ORDER BY sequence DESC LIMIT 1");
        movement.text(1,std::to_string(ship));
        if(!movement.row() || movement.integer(0)<=0 ||
           movement.str(1)!=std::to_string(entity->owner_id) ||
           movement.str(2)!=from || movement.str(3)!=to ||
           movement.integer(4)<=0 ||
           movement.integer(4)>static_cast<sqlite3_int64>(native.generation()) ||
           movement.str(5).empty())
            throw std::runtime_error("H2 no matching immutable original ship movement");
        const auto sequence=static_cast<std::uint64_t>(movement.integer(0));
        const auto movement_generation=static_cast<std::uint64_t>(movement.integer(4));
        // If SQLite retained the historical snapshot, its hash must match
        // the immutable migration's original physical state at that tick.
        detail::Statement historical(db,"SELECT checksum FROM snapshots WHERE generation=?");
        historical.number(1,movement_generation);
        if(historical.row() && historical.str(0)!=movement.str(5))
            throw std::runtime_error("H2 movement hash conflicts with native history");
        detail::sql(db,"CREATE TABLE IF NOT EXISTS sg_spatial_ship_handoff("
            "ship_id TEXT NOT NULL,movement_sequence INTEGER NOT NULL,"
            "handoff_id TEXT NOT NULL UNIQUE,from_scope TEXT NOT NULL,"
            "to_scope TEXT NOT NULL,source_worker TEXT NOT NULL,"
            "source_epoch INTEGER NOT NULL,destination_worker TEXT NOT NULL,"
            "destination_epoch INTEGER NOT NULL,source_retired INTEGER NOT NULL "
            "CHECK(source_retired IN (0,1)),movement_generation INTEGER NOT NULL,"
            "world_generation INTEGER NOT NULL,world_hash TEXT NOT NULL,"
            "PRIMARY KEY(ship_id,movement_sequence))");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_handoff_no_update "
            "BEFORE UPDATE ON sg_spatial_ship_handoff BEGIN "
            "SELECT RAISE(ABORT,'immutable original ship worker handoff');END");
        detail::sql(db,"CREATE TRIGGER IF NOT EXISTS sg_spatial_handoff_no_delete "
            "BEFORE DELETE ON sg_spatial_ship_handoff BEGIN "
            "SELECT RAISE(ABORT,'immutable original ship worker handoff');END");
        detail::Statement prior(db,"SELECT handoff_id,from_scope,to_scope,"
            "source_worker,source_epoch,destination_worker,destination_epoch,"
            "source_retired,movement_generation FROM sg_spatial_ship_handoff "
            "WHERE ship_id=? AND movement_sequence=?");
        prior.text(1,std::to_string(ship));prior.number(2,sequence);
        ShipCellHandoff answer{};
        answer.ship=ship;answer.from_scope=from;answer.to_scope=to;
        answer.physical_migration_generation=movement_generation;
        answer.destination.cell=destination;
        answer.destination.worker=destination_worker;
        answer.destination.transfer_id=durable_handoff_id;
        answer.destination.region_generation=destination_region_generation;
        answer.destination.checkpoint_revision=destination_checkpoint_revision;
        answer.destination.observed_original_generation=native.generation();
        if(prior.row()){
            if(prior.str(0)!=durable_handoff_id || prior.str(1)!=from ||
               prior.str(2)!=to || prior.str(3)!=source.worker ||
               prior.integer(4)!=static_cast<sqlite3_int64>(source.epoch) ||
               prior.str(5)!=destination_worker ||
               prior.integer(6)!=static_cast<sqlite3_int64>(destination_prior_epoch+1) ||
               prior.integer(8)!=static_cast<sqlite3_int64>(movement_generation))
                throw std::runtime_error("H2 replay conflicts with original handoff");
            answer.source_retired=prior.integer(7)!=0;
            answer.destination.epoch=destination_prior_epoch+1;
            answer.destination.replayed=true;answer.replayed=true;
            // A replay is accepted only while the same original worker
            // destination lease remains authoritative and the source has not
            // been resurrected. No stale token receives authority.
            detail::Statement dst(db,"SELECT worker,epoch,region_generation,"
                "checkpoint_revision FROM sg_spatial_worker_lease WHERE scope=?");
            dst.text(1,to);
            if(!dst.row() || dst.str(0)!=destination_worker ||
               dst.integer(1)!=static_cast<sqlite3_int64>(answer.destination.epoch) ||
               dst.integer(2)!=static_cast<sqlite3_int64>(destination_region_generation) ||
               dst.integer(3)!=static_cast<sqlite3_int64>(destination_checkpoint_revision))
                throw std::runtime_error("H2 replay destination lease superseded");
            if(answer.source_retired){
                detail::Statement src(db,"SELECT 1 FROM sg_spatial_worker_lease WHERE scope=?");
                src.text(1,from);
                if(src.row())throw std::runtime_error("H2 retired source reappeared");
            }
            detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
            return answer;
        }
        detail::Statement src(db,"SELECT worker,epoch,region_generation,"
           "checkpoint_revision FROM sg_spatial_worker_lease WHERE scope=?");
        src.text(1,from);
        if(!src.row() || src.str(0)!=source.worker ||
           src.integer(1)!=static_cast<sqlite3_int64>(source.epoch) ||
           src.integer(2)!=static_cast<sqlite3_int64>(source.region_generation) ||
           src.integer(3)!=static_cast<sqlite3_int64>(source.checkpoint_revision))
            throw std::runtime_error("H2 source worker epoch stale or missing");
        detail::Statement region(db,"SELECT generation,checkpoint_revision,phase "
            "FROM sg_regional_mode_authority WHERE scope=?");
        region.text(1,to);
        if(!region.row() ||
           region.integer(0)!=static_cast<sqlite3_int64>(destination_region_generation) ||
           region.integer(1)!=static_cast<sqlite3_int64>(destination_checkpoint_revision) ||
           region.integer(2)!=0)
            throw std::runtime_error("H2 destination region authority not enrolled");
        detail::Statement previous(db,"SELECT worker,epoch FROM sg_spatial_worker_lease "
            "WHERE scope=?");
        previous.text(1,to);
        const bool dest_exists=previous.row();
        const auto former_worker=dest_exists?previous.str(0):std::string{};
        const auto former_epoch=dest_exists?previous.integer(1):0;
        if(former_epoch<0 || static_cast<std::uint64_t>(former_epoch)!=destination_prior_epoch)
            throw std::runtime_error("H2 destination lease lost CAS epoch");
        bool still_occupied=false;
        for(const auto& [id,item]:original.entities()){
            if(item.type!=EntityType::Ship)continue;
            const auto c=ship_cell(original,id);
            if(!c)throw std::runtime_error("H2 unresolved original ship cell");
            if(scope(*c)==from)still_occupied=true;
        }
        answer.source_retired=!still_occupied;
        if(answer.source_retired){
            detail::Statement retire(db,"DELETE FROM sg_spatial_worker_lease "
                "WHERE scope=? AND worker=? AND epoch=?");
            retire.text(1,from);retire.text(2,source.worker);
            retire.number(3,source.epoch);retire.execute();
            if(sqlite3_changes(db)!=1)
                throw std::runtime_error("H2 source retirement lost CAS");
        }
        // This injection proves atomic rollback of both worker mutations.
        if(failure!=FailPoint::None)
            throw std::runtime_error("H2 injected crash after source retirement");
        detail::Statement log(db,"INSERT INTO sg_spatial_worker_transfer("
            "scope,transfer_id,from_worker,to_worker,from_epoch,to_epoch,"
            "region_generation,world_generation) VALUES(?,?,?,?,?,?,?,?)");
        log.text(1,to);log.text(2,durable_handoff_id);
        log.text(3,former_worker);log.text(4,destination_worker);
        log.number(5,destination_prior_epoch);
        log.number(6,destination_prior_epoch+1);
        log.number(7,destination_region_generation);
        log.number(8,native.generation());log.execute();
        detail::Statement install(db,"INSERT INTO sg_spatial_worker_lease("
            "scope,worker,epoch,region_generation,checkpoint_revision,"
            "world_generation,transfer_id) VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(scope) DO UPDATE SET worker=excluded.worker,"
            "epoch=excluded.epoch,region_generation=excluded.region_generation,"
            "checkpoint_revision=excluded.checkpoint_revision,"
            "world_generation=excluded.world_generation,"
            "transfer_id=excluded.transfer_id");
        install.text(1,to);install.text(2,destination_worker);
        install.number(3,destination_prior_epoch+1);
        install.number(4,destination_region_generation);
        install.number(5,destination_checkpoint_revision);
        install.number(6,native.generation());install.text(7,durable_handoff_id);
        install.execute();
        detail::Statement audit(db,"INSERT INTO sg_spatial_ship_handoff("
            "ship_id,movement_sequence,handoff_id,from_scope,to_scope,"
            "source_worker,source_epoch,destination_worker,destination_epoch,"
            "source_retired,movement_generation,world_generation,world_hash) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
        audit.text(1,std::to_string(ship));audit.number(2,sequence);
        audit.text(3,durable_handoff_id);audit.text(4,from);audit.text(5,to);
        audit.text(6,source.worker);audit.number(7,source.epoch);
        audit.text(8,destination_worker);audit.number(9,destination_prior_epoch+1);
        audit.number(10,answer.source_retired?1:0);
        audit.number(11,movement_generation);audit.number(12,native.generation());
        audit.text(13,std::to_string(original.state_hash()));audit.execute();
        answer.destination.epoch=destination_prior_epoch+1;
        detail::sql(db,"COMMIT");committed=true;sqlite3_close_v2(db);
        return answer;
    }catch(...){
        if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        sqlite3_close_v2(db);throw;
    }
}
} // namespace galaxy::spatial
