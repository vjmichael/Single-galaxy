#pragma once
// ECON-03c4 — durable opt-in checkpoint for the independent regional scheduler.
// Does NOT automatically replace active-world Simulation/PersistentWorld ticks.
#include "galaxy/RegionalEventScheduler.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <sqlite3.h>

namespace galaxy::regional_events {
struct VersionedCheckpoint {
    Scheduler scheduler;
    std::uint64_t revision=0;
};
struct CommandReceipt {
    std::uint64_t revision=0;
    std::uint64_t outcome=0;
    bool replayed=false;
};
using SqliteMutationEffect=std::function<void(sqlite3*,const Scheduler&,const Scheduler&,std::uint64_t)>;
class RegionalModeAuthority;
class CheckpointStore {
    friend class RegionalModeAuthority;
public:
    // `scope` partitions independent regions in the same SQLite database.
    CheckpointStore(const std::string& sqlite_file, const std::string& scope);
    ~CheckpointStore();
    CheckpointStore(const CheckpointStore&)=delete;
    CheckpointStore& operator=(const CheckpointStore&)=delete;
    std::optional<VersionedCheckpoint> load() const;
    const std::string& scope() const noexcept {return scope_;}
    // Atomic optimistic update. Must pass the revision obtained from load(),
    // or zero only if no checkpoint exists. Includes pending notices and escrow.
    // Caller should only replace its in-memory committed state after success.
    std::uint64_t save(const Scheduler& scheduler, std::uint64_t expected_revision);
    // DEMOG-05q6b: host-only borrowed world transaction, never commits itself.
    // Strict cold-owner generation fence, same physical SQLite and CAS revision.
    // Updates multiple named colony population projections in one checkpoint.
    std::uint64_t commit_casualties_in_world_transaction(
        sqlite3* active_world_db, std::uint64_t expected_revision,
        std::uint64_t expected_owner_generation, std::uint64_t tick,
        const std::map<std::string,std::uint64_t>& reductions,
        const std::map<std::string,std::uint64_t>& initial_populations,
        const std::string& command_id) const;
    // DEMOG-05q6c: journaled ART02 dependent birth, positive population
    // delta only, using same real scheduler and cold owner fence.
    std::uint64_t commit_birth_in_world_transaction(
        sqlite3* active_world_db, std::uint64_t expected_revision,
        std::uint64_t expected_owner_generation, std::uint64_t tick,
        const std::string& colony, std::uint64_t initial_population,
        const std::string& birth_event_id) const;
    // DEMOG-05q6d: one real, nonfatal NPC changes home only after physical
    // disembarkation. Paired regional deltas in one cold-owner fenced write.
    std::uint64_t commit_migration_in_world_transaction(
        sqlite3* active_world_db,std::uint64_t expected_revision,
        std::uint64_t expected_owner_generation,std::uint64_t tick,
        const std::string& origin,const std::string& destination,
        std::uint64_t origin_population,std::uint64_t destination_population,
        const std::string& command_id,std::uint64_t person_count=1) const;
    // Host-only primitive: apply a purely in-memory operation and write the
    // region checkpoint AND immutable command receipt in the SAME SQLite txn.
    // Caller must have already authenticated/authorized request via host policy.
    // Mutator must NEVER perform external effects or write other databases.
    CommandReceipt commit_command(const std::string& command_id,
                                  const std::string& actor,
                                  const std::string& canonical_request,
                                  std::uint64_t expected_revision,
                                  const std::function<std::uint64_t(Scheduler&)>& mutate,
                                  const SqliteMutationEffect& atomic_effect={},
                                  bool custody_enforced=false,
                                  std::uint64_t expected_owner_generation=0,
                                  int required_owner_mode=0);
private:
    sqlite3* db_=nullptr;
    std::string scope_;
    std::string encode(const Scheduler& scheduler) const;
    Scheduler decode(const std::string& bytes) const;
};
} // namespace galaxy::regional_events
