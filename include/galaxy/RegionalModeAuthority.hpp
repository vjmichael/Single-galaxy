#pragma once
// ECON-03c7: opt-in persisted, exclusive regional simulation ownership.
// This is an ownership gate, NOT an adapter for live PersistentWorld effects.
#include "galaxy/RegionalCheckpointStore.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace galaxy::regional_events {
enum class SimulationOwner : int { Cold=0, Detailed=1 };
enum class TransitionPhase : int { Stable=0, ToDetailed=1, ToCold=2 };
struct RegionalAuthorityState {
    SimulationOwner owner=SimulationOwner::Cold;
    TransitionPhase phase=TransitionPhase::Stable;
    std::uint64_t generation=0;
    std::uint64_t checkpoint_revision=0;
    std::uint64_t boundary_tick=0;
    std::string handoff_id;
};
class RegionalModeAuthority {
public:
    explicit RegionalModeAuthority(CheckpointStore& store);
    // Initial registration is cold-only; the current checkpoint must exist.
    // Existing registrations survive restart; reopening never resets ownership.
    RegionalAuthorityState enroll();
    RegionalAuthorityState state() const;
    // Persist an intent under BEGIN IMMEDIATE. No simulation ownership changes
    // until commit. The freeze blocks all cold-region commands while pending.
    RegionalAuthorityState prepare(const std::string& handoff_id,
                                   SimulationOwner target,
                                   std::uint64_t expected_generation,
                                   std::uint64_t expected_checkpoint_revision,
                                   std::uint64_t boundary_tick);
    // `durable_stage` executes inside the SAME SQLite transaction as ownership
    // transfer. It must persist the authoritative detailed snapshot (promotion)
    // or reconcile real detailed effects into `candidate` (demotion).
    // It MUST NOT call external services, execute the game, or use another DB.
    // Production host is responsible for proving carrier/NPC/source receipts.
    using DurableStage=std::function<void(sqlite3*,Scheduler&)>;
    RegionalAuthorityState commit(const std::string& handoff_id,
                                  std::uint64_t expected_generation,
                                  std::uint64_t expected_checkpoint_revision,
                                  const DurableStage& durable_stage);
    // Recovery can cancel a prepared intent. Increments generation, ensuring
    // old owner tokens cannot be reused even after an aborted handoff.
    RegionalAuthorityState abort(const std::string& handoff_id,
                                 std::uint64_t expected_generation);
private:
    CheckpointStore& store_;
};
} // namespace galaxy::regional_events
