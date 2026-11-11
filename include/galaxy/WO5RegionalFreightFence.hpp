#pragma once
// WO5-E: fail-closed global native freight fence under the ORIGINAL
// RegionalModeAuthority BEGIN IMMEDIATE SQLite ownership transaction.
// v59 shipping receipt-prefix witnesses already cover manufactured cargo,
// but original food/water/nutrient IntercolonyFreightLeg custody lives in
// canonical world snapshots. Until native freight has a trusted region
// membership binding, NO REGION can change owners with such a leg unresolved.
// This deliberately prefers a stalled handoff over duplicated/lost goods.
#include "galaxy/WO5FreightEvidence.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace galaxy::logistics05 {
inline void require_native_freight_handoff_safe(sqlite3* same_world_db) {
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
        statement("SELECT state,checksum FROM snapshots ORDER BY generation DESC LIMIT 1");
        if(sqlite3_step(q)!=SQLITE_ROW)
            throw std::runtime_error("regional handoff has no original world checkpoint");
        const auto* raw=sqlite3_column_blob(q,0);
        const auto count=sqlite3_column_bytes(q,0);
        const auto* hash=sqlite3_column_text(q,1);
        if(!raw || count<=0 || count>64*1024*1024 || !hash)
            throw std::runtime_error("regional handoff has invalid world checkpoint");
        const std::string bytes(static_cast<const char*>(raw),
                                static_cast<std::size_t>(count));
        const std::string expected(reinterpret_cast<const char*>(hash));
        sqlite3_finalize(q);q=nullptr;
        const auto world=Simulation::from_snapshot(bytes);
        if(std::to_string(world.state_hash())!=expected)
            throw std::runtime_error("regional handoff physical checkpoint checksum mismatch");
        for(const auto& [id,leg]:world.intercolony_freight()){
            // The original world remains the SOLE carrier/consumption owner.
            // Never transfer a cold simulation while that cargo is aboard,
            // an actual pickup is pending or an NPC still needs to deliver.
            if(leg.stage!=IntercolonyFreightStage::Delivered)
                throw std::runtime_error(
                    "regional handoff blocked by unresolved original physical freight");
            const auto evidence=inspect_original_freight_evidence(world,leg.owner,id);
            if(!evidence || !evidence->actual_arrival_proved ||
               evidence->original_stage!=IntercolonyFreightStage::Delivered)
                throw std::runtime_error(
                    "regional handoff has freight without physical delivery proof");
        }
    }catch(...){
        if(q)sqlite3_finalize(q);
        throw;
    }
}
} // namespace galaxy::logistics05
