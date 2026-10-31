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

// GRSM-02: this is knowledge held or transmitted by individual NPCs, NOT a
// historical fact. Distorted/contrary accounts never modify physical state.
// Firsthand means *local, host-attested opportunity to witness* (not proof that
// a character's report is accurate); hearsay preserves its parent claim.
enum class ClaimFidelity : int { LocalObservation=0, Faithful=1, Embellished=2, Contrary=3 };
struct KnowledgeRecord {
    std::uint64_t id=0;
    std::string incident_id;
    std::uint64_t parent_id=0;  // 0 only for directly-attested observation
    std::uint64_t speaker_npc=0, recipient_npc=0;
    ClaimFidelity fidelity=ClaimFidelity::LocalObservation;
    int valence=0, confidence=0;  // perception, -100..100 and 0..100
    std::uint64_t generation=0, tick=0;
    std::string account;  // bounded player-independent NPC claim text
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
    // Trusted host/test APIs; these are NOT player-submitted commands.
    // Evidence requires a new, locally observable incident on this world tick.
    // No NPC is automatically made omniscient when the historical ledger grows.
    bool record_local_observation(const std::string& incident_id, std::uint64_t npc_id);
    // The speaker is determined by the parent record's recipient. Speaker and
    // listener must currently co-occupy a location and both remain alive.
    bool transmit_claim(std::uint64_t source_record_id, std::uint64_t listener_npc,
                        ClaimFidelity fidelity, int valence, int confidence,
                        const std::string& account);
    std::vector<KnowledgeRecord> recent_knowledge(std::size_t limit = 50) const;


private:
    class Store;
    std::unique_ptr<Store> store_;
    Simulation world_;
    std::int64_t resource_origin_ = 0;
    std::int64_t credit_origin_ = 0;
    std::uint64_t generation_ = 0;
};
} // namespace galaxy
