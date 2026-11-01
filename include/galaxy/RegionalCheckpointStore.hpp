#pragma once
// ECON-03c4 — durable opt-in checkpoint for the independent regional scheduler.
// Does NOT automatically replace active-world Simulation/PersistentWorld ticks.
#include "galaxy/RegionalEventScheduler.hpp"
#include <cstdint>
#include <functional>
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
                                  std::uint64_t expected_owner_generation=0);
private:
    sqlite3* db_=nullptr;
    std::string scope_;
    std::string encode(const Scheduler& scheduler) const;
    Scheduler decode(const std::string& bytes) const;
};
} // namespace galaxy::regional_events
