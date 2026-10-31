#pragma once

#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy {

// B3 local presentation contract, not the historical EVE protocol.
// A stream epoch changes on host restart; all sequence numbers are scoped to it.
enum class FrameKind : std::uint8_t { Snapshot = 1, Delta = 2 };
enum class ChangeKind : std::uint8_t { Upsert = 1, Remove = 2, MiningUpsert = 3,
    MiningRemove = 4, CommandResult = 5, Activity = 6, DockUpsert = 7,
    DockRemove = 8, HangarUpsert = 9, HangarRemove = 10, WalletUpsert = 11, WalletRemove = 12,
    MarketUpsert = 13, MarketRemove = 14, JumpUpsert = 15, JumpRemove = 16 };
struct DockTelemetry {
    DockState state = DockState::InSpace;
    EntityId station = 0;
    std::uint32_t remaining_ticks = 0;
};
struct JumpTelemetry { ShipJumpState state = ShipJumpState::Ready; EntityId gate = 0; std::uint32_t remaining_ticks = 0; };
struct Change {
    ChangeKind kind = ChangeKind::Upsert;
    Entity entity{};         // Upsert
    EntityId id = 0;        // Remove, MiningUpsert, MiningRemove
    AutoMining mining{};    // MiningUpsert
    Event command{};        // CommandResult
    ActivityEvent activity{}; // Activity
    DockTelemetry dock{}; // DockUpsert, indexed by ship ID (id)
    JumpTelemetry jump{}; // JumpUpsert, indexed by ship ID (id)
    StationHangar hangar{}; // HangarUpsert, indexed by station+owner
    EntityId station_id = 0; // hangar station
    PlayerId owner_id = 0; // hangar/wallet owner
    std::int64_t credits = 0;
    StationMarket market{};
};
struct ViewFrame {
    FrameKind kind = FrameKind::Snapshot;
    std::uint64_t stream_id = 0;
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::uint32_t system_id = 0;
    std::uint64_t projection_hash = 0; // transport consistency only, not cryptographic authentication
    std::vector<Change> changes;
};
struct ClientProjection {
    std::uint64_t tick = 0;
    std::uint32_t system_id = 0;
    std::map<EntityId, Entity> entities;
    std::map<EntityId, AutoMining> mining;
    std::map<EntityId, DockTelemetry> docks;
    std::map<EntityId, JumpTelemetry> jumps;
    std::map<std::pair<EntityId, PlayerId>, StationHangar> hangars;
    std::map<PlayerId, std::int64_t> wallets;
    std::map<EntityId, StationMarket> markets;
};
std::uint64_t projection_hash(const ClientProjection& projection);

// Observe only committed simulation steps (PersistentWorld::step must have returned).
// No network, no authorization or real-time scheduling is implemented by this adapter.
class ServerViewBridge {
public:
    ServerViewBridge(const Simulation& world, std::uint64_t stream_id, PlayerId viewer_owner = 0);
    ViewFrame snapshot() const;
    ViewFrame after_committed_step(const Simulation& world);
    std::uint64_t sequence() const noexcept { return sequence_; }
private:
    ClientProjection previous_;
    PlayerId viewer_owner_ = 0;
    std::uint64_t stream_id_ = 0;
    std::uint64_t sequence_ = 0;
    std::size_t command_cursor_ = 0;
    std::size_t activity_cursor_ = 0;
};

enum class ApplyResult { Applied, DuplicateIgnored, NeedsSnapshot };
class ClientView {
public:
    ApplyResult apply(const ViewFrame& frame);
    const ClientProjection& world() const noexcept { return current_; }
    const std::vector<Change>& recent_notifications() const noexcept { return notifications_; }
    std::uint64_t stream_id() const noexcept { return stream_id_; }
    std::uint64_t sequence() const noexcept { return sequence_; }
    bool initialized() const noexcept { return initialized_; }
private:
    ClientProjection current_;
    std::vector<Change> notifications_;
    std::uint64_t stream_id_ = 0;
    std::uint64_t sequence_ = 0;
    bool initialized_ = false;
};

// Versioned plain-text test transport with strict numeric parsing and bounded frames.
// Can later be replaced with Protobuf without changing simulation rules.
std::string encode_frame(const ViewFrame& frame);
ViewFrame decode_frame(const std::string& bytes);
} // namespace galaxy
