#pragma once
// DEMOG-05q6e5b8i: locally committed, immutable receipt prefix witnesses.
// This module never moves goods; the physical world's journal owns inventory.
#include <cstdint>
#include <string>
struct sqlite3;
namespace galaxy::shipping {
// Must be invoked INSIDE the already-open regional ownership SQL transaction.
// No network calls, external database or commits are permitted.
void seal_regional_shipment_boundary(sqlite3* world_db, const std::string& scope,
    const std::string& handoff_id, std::uint64_t from_generation,
    std::uint64_t checkpoint_revision, int target_mode);
// Called on world reopen through the existing shipping integrity verifier.
// Replays the immutable historical shipping receipt prefix at each cutover.
void verify_regional_shipment_boundaries(const std::string& database_path);
}
