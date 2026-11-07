#pragma once
// WO5-E: fail-closed global native freight fence under the ORIGINAL
// RegionalModeAuthority BEGIN IMMEDIATE SQLite ownership transaction.
// v59 shipping receipt-prefix witnesses already cover manufactured cargo,
// but original food/water/nutrient IntercolonyFreightLeg custody lives in
// canonical world snapshots. Until native freight has a trusted region
// membership binding, NO REGION can change owners with such a leg unresolved.
// This deliberately prefers a stalled handoff over duplicated/lost goods.
#include "galaxy/WO5FreightEvidence.hpp"
#include "galaxy/SeamlessSpatialOwnership.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace galaxy::logistics05 {
inline void require_native_freight_handoff_safe(sqlite3* same_world_db,
    const std::string& requested_region_scope) {
    if(!same_world_db)
        throw std::invalid_argument("native freight owner handoff missing world DB");
    sqlite3_stmt* q=nullptr;
    const auto statement=[&](const char* sql) {
        if(sqlite3_prepare_v2(same_world_db,sql,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error("regional native freight SQL prepare failed");
    };
    try {
        statement("SELECT 1 FROM sqlite_master WHERE type='table' AND name='snapshots'");
        const bool has_world=sqlite3_step(q)==SQLITE_ROW;
        sqlite3_finalize(q);q=nullptr;
        if(!has_world)return; // regional-only checkpoint database, no physical cargo
        statement("SELECT generation,state,checksum FROM snapshots ORDER BY generation DESC LIMIT 1");
        if(sqlite3_step(q)!=SQLITE_ROW)
            throw std::runtime_error("regional handoff has no original world checkpoint");
        const auto original_generation=sqlite3_column_int64(q,0);
        const auto* raw=sqlite3_column_blob(q,1);
        const auto count=sqlite3_column_bytes(q,1);
        const auto* hash=sqlite3_column_text(q,2);
        if(!raw || count<=0 || count>64*1024*1024 || !hash)
            throw std::runtime_error("regional handoff has invalid world checkpoint");
        const std::string bytes(static_cast<const char*>(raw),
                                static_cast<std::size_t>(count));
        const std::string expected(reinterpret_cast<const char*>(hash));
        sqlite3_finalize(q);q=nullptr;
        const auto world=Simulation::from_snapshot(bytes);
        if(original_generation<=0 ||
           std::to_string(world.state_hash())!=expected)
            throw std::runtime_error("regional handoff physical checkpoint checksum mismatch");
        // A canonical spatial scope is allowed to release an UNRELATED
        // cell, but only when every original ship -> cell binding was made
        // under exactly this world's committed generation and checksum.
        // Arbitrary legacy region names use the old fail-closed global fence.
        const auto requested_cell=spatial::parse_scope(requested_region_scope);
        if(requested_region_scope.compare(0,8,"spatial/")==0 &&
           !requested_cell)
            throw std::runtime_error("spatial region scope is noncanonical");
        if(requested_cell &&
           !spatial::verify_native_index(same_world_db,world,
                   static_cast<std::uint64_t>(original_generation)))
            throw std::runtime_error(
               "spatial region cannot handoff without exact canonical ship bindings");
        for(const auto& [id,leg]:world.intercolony_freight()){
            // A completed leg still needs its own verified ORIGINAL cargo
            // receipt. No independent cold worker can fabricate fulfillment.
            if(leg.stage==IntercolonyFreightStage::Delivered){
                const auto evidence=inspect_original_freight_evidence(
                    world,leg.owner,id);
                if(!evidence || !evidence->actual_arrival_proved ||
                   evidence->original_stage!=IntercolonyFreightStage::Delivered)
                    throw std::runtime_error(
                        "regional handoff has freight without physical delivery proof");
                continue;
            }
            if(!requested_cell)
                throw std::runtime_error(
                    "unbound legacy region blocked by unresolved physical freight");
            // The real owner of source, carrier and destination is one
            // canonical world. Spatial ownership changes ONLY for the cell
            // containing an actual involved original hull. Empty/unrelated
            // cells must not stall because cargo is moving elsewhere.
            for(const auto hull:{leg.source_ship,leg.carrier_ship,
                                 leg.destination_ship}){
                const auto actual_cell=spatial::ship_cell(world,hull);
                if(!actual_cell)
                    throw std::runtime_error(
                        "native freight has no verifiable spatial ship identity");
                if(*actual_cell==*requested_cell)
                    throw std::runtime_error(
                        "involved spatial cell blocked by unresolved physical freight");
            }
        }
    }catch(...){
        if(q)sqlite3_finalize(q);
        throw;
    }
}
} // namespace galaxy::logistics05
