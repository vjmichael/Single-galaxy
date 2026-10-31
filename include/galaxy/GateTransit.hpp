#pragma once
// B6.2a: isolated server-authoritative stargate transition kernel.
// Does not mutate the existing single-system Simulation/PersistentWorld; integration
// needs a per-ship system-aware world and a durable transition journal first.
#include "galaxy/Galaxy.hpp"
#include <cstdint>
#include <map>

namespace galaxy {

enum class TransitPhase { InSpace, Jumping, GateCloaked };
enum class JumpOutcome {
    Granted, UnknownShip, Unauthorized, UnknownGate, WrongSystem,
    OutOfRange, GateOffline, TransitionBusy, WeaponLock
};
struct GateEndpoint {
    std::uint64_t id=0;
    std::uint32_t origin=0;
    std::uint32_t destination=0;
    Vector2 position{};
    Vector2 arrival_center{};
};
struct TransitVessel {
    EntityId id=0;
    PlayerId owner=0;
    std::uint32_t system=0;
    Vector2 position{},velocity{};
    TransitPhase phase=TransitPhase::InSpace;
    std::uint64_t pending_gate=0;
    std::uint32_t phase_ticks=0;
    bool weapon_lock=false;
};
class GateTransit {
public:
    explicit GateTransit(const GalaxyMap& map):map_(map){}
    static constexpr std::int64_t kGateRangeMm=2500;
    static constexpr std::uint32_t kTransitTicks=2;
    static constexpr std::uint32_t kCloakTicks=5;
    void add_endpoint(GateEndpoint gate);
    void add_vessel(TransitVessel ship);
    void set_gate_online(std::uint64_t gate,bool online);
    JumpOutcome request_jump(PlayerId issuer,EntityId ship,std::uint64_t gate);
    // Advance once per authoritative tick. No duplicate tick or reversal allowed.
    void advance(std::uint64_t tick);
    // First movement after arrival breaks the arrival cloak (even zero thrust is not movement).
    bool request_movement(PlayerId issuer,EntityId ship,Vector2 new_velocity);
    const TransitVessel& vessel(EntityId ship) const;
    std::uint64_t tick() const noexcept{return tick_;}
    bool protected_from_attack(EntityId ship) const;
private:
    struct GateRecord{GateEndpoint gate; bool online=true;};
    const GalaxyMap& map_;
    std::map<std::uint64_t,GateRecord> gates_;
    std::map<EntityId,TransitVessel> ships_;
    std::uint64_t tick_=0;
};
} // namespace galaxy
