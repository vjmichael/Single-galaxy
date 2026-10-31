#pragma once
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>

namespace galaxy {
// Failures occur inside an SQLite transaction, before durable COMMIT.
enum class FailPoint { None, AfterReceiptWrite, AfterSnapshotWrite };

// GRSM-01: an authoritative outcome of a committed player/host command.
// This is not a reputation, moral judgment, casualty count, or NPC belief.
struct HistoricalEvent {
    std::string event_id;             // Stable event identity: command:<command_id>
    std::uint64_t generation = 0;
    std::uint64_t tick = 0;
    std::uint64_t command_id = 0;
    EntityId actor = 0;
    EntityId target = 0;
    Action action = Action::Mine;
    Outcome result = Outcome::InvalidArgument;
    std::int64_t transferred = 0;
};

// GRSM-01b: immutable, observed physical facts, not rumors or moral judgments.
enum class PhysicalKind : int { CargoMovement=1, JourneyStarted=2, JourneyCompleted=3,
                                ShipDestroyed=4, NpcLost=5 };
struct PhysicalIncident {
    std::string incident_id;
    std::uint64_t generation=0, tick=0, command_id=0;
    PhysicalKind kind=PhysicalKind::CargoMovement;
    std::string source, destination;
    std::int64_t amount=0;
    std::string detail;
};

class PersistentWorld {
public:
    // If the SQLite file is new, persist the supplied initial world.
    // For an existing database, load authoritative saved world and ignore initial state.
    PersistentWorld(const std::string& sqlite_path, Simulation initial_world);
    ~PersistentWorld();
    PersistentWorld(const PersistentWorld&) = delete;
    PersistentWorld& operator=(const PersistentWorld&) = delete;

    // User-invoked salvage: copy source through SQLite backup, replay verified v2
    // operations on the copy, and validate receipts. Never mutate source.
    static void recover_copy(const std::string& source, const std::string& destination);

    bool submit(const Command& command, FailPoint fail = FailPoint::None);
    // Privileged, explicit test/host-only gate administration. Not exposed as
    // a player action. Persists through the normal operation receipt and journal.
    bool submit_testing_gate_change(EntityId forward, EntityId reverse,
                                    bool online, std::uint64_t command_id,
                                    FailPoint fail = FailPoint::None);
    // Host/test-only provisioning. Not exposed to ordinary player input.
    // Staff is an absolute headcount; supplies is an added quantity.
    bool submit_testing_gate_operations(EntityId forward, EntityId reverse,
        Action action, std::int64_t amount, std::uint64_t command_id,
        FailPoint fail = FailPoint::None);
    // Host/test-only consequences; future combat/risk systems supply the
    // validated destructive cause. Never available via player command submission.
    bool submit_testing_destroy_ship(EntityId attacker, EntityId victim,
                                     std::uint64_t command_id, FailPoint fail = FailPoint::None);
    bool submit_testing_crew_loss(EntityId colony_ship, std::int64_t count,
                                  std::uint64_t command_id, FailPoint fail = FailPoint::None);
    void step(FailPoint fail = FailPoint::None);
    void steps(std::uint64_t n);
    const Simulation& world() const noexcept { return world_; }
    std::int64_t resource_origin() const noexcept { return resource_origin_; }
    std::uint64_t generation() const noexcept { return generation_; }
    // Bounded, read-only records; older v4 actions are not retroactively invented.
    std::vector<HistoricalEvent> recent_history(std::size_t limit = 50) const;
    std::vector<PhysicalIncident> recent_physical_incidents(std::size_t limit = 50) const;


private:
    class Store;
    std::unique_ptr<Store> store_;
    Simulation world_;
    std::int64_t resource_origin_ = 0;
    std::int64_t credit_origin_ = 0;
    std::uint64_t generation_ = 0;
};
} // namespace galaxy
