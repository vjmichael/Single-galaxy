#pragma once
// ECON-03c8a — Conservative, opt-in link to ACTUAL PersistentWorld database.
// Scope: one whole PersistentWorld per bridge, not multi-colony partitioning.
// Only no-effect demotion is implemented. Changed detailed-world inventory or
// state must NEVER be silently reconciled into the cold scheduler.
#include "galaxy/Persistence.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <string>

namespace galaxy::regional_events {
class PersistentWorldBridge {
public:
    PersistentWorldBridge(const std::string& authoritative_sqlite_path,
                          const std::string& scope, PersistentWorld& live);
    // Registers an EMPTY regional simulation, and installs an SQLite trigger
    // preventing real PersistentWorld snapshots while region is cold/pending.
    // Reopening the same scope does not reset ownership.
    RegionalAuthorityState enroll();
    RegionalAuthorityState state() const { return authority_.state(); }
    // Cold->Detailed: stage the actual, validated canonical game snapshot in
    // the SAME transaction as the region ownership flip.
    RegionalAuthorityState promote(const std::string& handoff_id);
    // Detailed->Cold: safe only if the actual persisted game snapshot has not
    // changed at all since promotion. Otherwise refuse, preserving detailed.
    RegionalAuthorityState demote_if_unchanged(const std::string& handoff_id);
    // ECON-03c8b: seal actual persisted detailed mining/refining/basic
    // fabrication effects into a unique, crash-safe physical receipt manifest,
    // without duplicating physical inventory into regional resource fields.
    // Unimplemented ship routes, destruction and unproven transfers are rejected.
    // The cold owner *parks* physical state: it does not yet advance real NPC/
    // factory production. Re-promotion validates and consumes parked manifest.
    RegionalAuthorityState demote_preserving_effects(const std::string& handoff_id);
    // Explicit host recovery after a process crash between prepare and commit.
    // Aborting revokes the pending generation and never changes game inventory.
    RegionalAuthorityState abort_pending(const std::string& handoff_id,
                                          std::uint64_t expected_generation);
    const CheckpointStore& checkpoint() const noexcept {return store_;}
private:
    std::string path_, scope_;
    PersistentWorld& live_;
    CheckpointStore store_;
    RegionalModeAuthority authority_;
};
} // namespace galaxy::regional_events
