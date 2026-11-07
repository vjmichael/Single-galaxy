#pragma once
// WO5-E: READ-ONLY native freight recovery audit binding three original
// authorities: the SAME SQLite file's world snapshot, scoped regional
// generation/revision, and physical shipment/pilot/arrival receipts.
// No mutation, new freight schema, custody transfer or blanket permission.
// Region mode may change after observation: the actual host effect MUST
// independently recheck its own original SQLite transaction admission.
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalShipmentReconciliation.hpp"
#include "galaxy/WO5FreightEvidence.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::logistics05 {
struct NativeRegionFreightAudit {
    std::string scope;
    PlayerId owner=0;
    regional_events::SimulationOwner mode=
        regional_events::SimulationOwner::Cold;
    regional_events::TransitionPhase phase=
        regional_events::TransitionPhase::Stable;
    std::uint64_t owner_generation=0,checkpoint_revision=0;
    std::uint64_t original_world_generation=0,world_tick=0,state_hash=0;
    std::vector<OriginalFreightEvidence> original_shipments;
    std::uint64_t awaiting=0,loaded=0,delivered=0;
    bool may_consider_detailed_only_work=false;
    bool has_unresolved_original_freight=false;
};

// Return nullopt for stale or unbound runtime, bad scope/DB pairing,
// damaged cargo or missing original receipts. Historical manufactured
// shipment prefix is verified by existing immutable v59 SQL witnesses.
inline std::optional<NativeRegionFreightAudit>
observe_native_regional_recovery(
    const std::string& world_sqlite_path,
    const PersistentWorld& physical,
    const regional_events::CheckpointStore& checkpoint,
    const regional_events::RegionalModeAuthority& authority,
    PlayerId owner,
    std::uint64_t expected_owner_generation,
    std::uint64_t expected_checkpoint_revision) {
    using namespace regional_events;
    if(world_sqlite_path.empty() || checkpoint.scope().empty() ||
       !owner || owner==Simulation::kServerAuthority ||
       !expected_owner_generation || !expected_checkpoint_revision)
        return std::nullopt;
    const auto before=authority.state();
    const auto cp=checkpoint.load();
    if(before.generation!=expected_owner_generation ||
       before.checkpoint_revision!=expected_checkpoint_revision ||
       !cp || cp->revision!=before.checkpoint_revision ||
       cp->scheduler.tick()!=before.boundary_tick)
        return std::nullopt;
    // Bind data returned by two *caller-supplied handles* to the exact same
    // durable database. A caller could otherwise point a Cold epoch at an
    // unrelated physically rich world, defeating stale-owner fencing.
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(world_sqlite_path.c_str(),&db,
            SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        if(db)sqlite3_close(db);
        return std::nullopt;
    }
    bool same_database=false;
    try {
        sqlite3_stmt* q=nullptr;
        if(sqlite3_prepare_v2(db,
                "SELECT owner,phase,generation,checkpoint_revision,boundary_tick "
                "FROM sg_regional_mode_authority WHERE scope=?",-1,
                &q,nullptr)!=SQLITE_OK)
            throw std::runtime_error("region audit no owner table");
        sqlite3_bind_text(q,1,checkpoint.scope().c_str(),-1,SQLITE_TRANSIENT);
        const bool matched=sqlite3_step(q)==SQLITE_ROW &&
            sqlite3_column_int(q,0)==static_cast<int>(before.owner) &&
            sqlite3_column_int(q,1)==static_cast<int>(before.phase) &&
            sqlite3_column_int64(q,2)==static_cast<sqlite3_int64>(before.generation) &&
            sqlite3_column_int64(q,3)==static_cast<sqlite3_int64>(before.checkpoint_revision) &&
            sqlite3_column_int64(q,4)==static_cast<sqlite3_int64>(before.boundary_tick);
        sqlite3_finalize(q);q=nullptr;
        if(!matched)throw std::runtime_error("region audit wrong checkpoint database");
        if(sqlite3_prepare_v2(db,
            "SELECT generation,checksum FROM snapshots ORDER BY generation DESC LIMIT 1",
            -1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error("region audit no physical snapshots");
        if(sqlite3_step(q)!=SQLITE_ROW)
            throw std::runtime_error("region audit missing canonical state");
        const auto generation=sqlite3_column_int64(q,0);
        const auto* hash=sqlite3_column_text(q,1);
        const std::string sha=hash?reinterpret_cast<const char*>(hash):"";
        sqlite3_finalize(q);q=nullptr;
        same_database=generation>0 &&
            static_cast<std::uint64_t>(generation)==physical.generation() &&
            sha==std::to_string(physical.world().state_hash());
    }catch(...){
        sqlite3_close(db);return std::nullopt;
    }
    sqlite3_close(db);
    if(!same_database)return std::nullopt;
    // Runs original v59 immutable receipt-prefix verification. This does
    // not imply third-party shipping consent; it checks historical facts.
    try{shipping::verify_regional_shipment_boundaries(world_sqlite_path);}
    catch(const std::exception&){return std::nullopt;}
    NativeRegionFreightAudit audit{};
    audit.scope=checkpoint.scope();audit.owner=owner;
    audit.owner_generation=before.generation;
    audit.checkpoint_revision=before.checkpoint_revision;
    audit.mode=before.owner;audit.phase=before.phase;
    audit.original_world_generation=physical.generation();
    audit.world_tick=physical.world().tick();
    audit.state_hash=physical.world().state_hash();
    for(const auto& [id,f]:physical.world().intercolony_freight()){
        if(f.owner!=owner)continue;
        if(audit.original_shipments.size()>=10000)return std::nullopt;
        const auto evidence=inspect_original_freight_evidence(
            physical.world(),owner,id);
        if(!evidence)return std::nullopt;
        audit.original_shipments.push_back(*evidence);
        if(evidence->original_stage==IntercolonyFreightStage::AwaitingPickup)
            ++audit.awaiting;
        else if(evidence->original_stage==IntercolonyFreightStage::Loaded)
            ++audit.loaded;
        else if(evidence->original_stage==IntercolonyFreightStage::Delivered &&
                evidence->actual_arrival_proved)++audit.delivered;
        else return std::nullopt;
    }
    audit.has_unresolved_original_freight=audit.awaiting||audit.loaded;
    // The call is only an audit; independent host write authorization is
    // still mandatory, including atomic region-generation CAS at mutation.
    const auto after=authority.state();
    if(after.owner!=before.owner || after.phase!=before.phase ||
       after.generation!=before.generation ||
       after.checkpoint_revision!=before.checkpoint_revision ||
       physical.world().state_hash()!=audit.state_hash ||
       physical.generation()!=audit.original_world_generation)
        return std::nullopt;
    audit.may_consider_detailed_only_work=
        after.owner==SimulationOwner::Detailed &&
        after.phase==TransitionPhase::Stable;
    return audit;
}
} // namespace galaxy::logistics05
