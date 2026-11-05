#pragma once
// One NPC transportation domain model, independent of vehicle physics.
// Ferry and planetary shuttle remain existing authoritative vehicle adapters.
// No population copy, allocation, SQLite transaction or migration is performed here.
#include "galaxy/TransportIdentity.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::transport {

enum class TransportState : std::uint8_t {
    Boarding=0, InTransit=1, Arrived=2, Interrupted=3
};

// NPC transport is a class of vehicle operation, not a type of colony.
// The same transport can move named occupants between any two valid locations.
class NpcTransport final {
public:
    NpcTransport(std::uint64_t receipt, Mode mode, Endpoint source,
                 Endpoint destination, std::size_t seat_capacity,
                 std::vector<PersonId> passengers, TransportState state)
        : receipt_(receipt), mode_(mode), source_(source),
          destination_(destination), seats_(seat_capacity),
          passengers_(std::move(passengers)), state_(state) {}

    bool valid() const noexcept {
        return receipt_!=0 && source_.id!=0 && destination_.id!=0 &&
               !(source_==destination_) && seats_!=0 &&
               seats_<=10000 && passengers_.size()<=seats_ &&
               canonical_manifest(passengers_);
    }
    bool carries(PersonId person) const noexcept {
        return contains_person(passengers_,person);
    }
    bool arrived(PersonId person) const noexcept {
        return valid() && state_==TransportState::Arrived && carries(person);
    }
    ArrivalEvidence arrival_evidence() const {
        return {mode_, state_==TransportState::Arrived?Phase::Arrived:
                       state_==TransportState::InTransit?Phase::InTransit:
                       state_==TransportState::Interrupted?Phase::Interrupted:
                       Phase::Booked,
                receipt_,source_.id,destination_.id,passengers_};
    }
    std::uint64_t receipt() const noexcept {return receipt_;}
    Mode mode() const noexcept {return mode_;}
    Endpoint source() const noexcept {return source_;}
    Endpoint destination() const noexcept {return destination_;}
    TransportState state() const noexcept {return state_;}
    std::size_t seats() const noexcept {return seats_;}
    const std::vector<PersonId>& passengers() const noexcept {return passengers_;}
private:
    std::uint64_t receipt_;
    Mode mode_;
    Endpoint source_,destination_;
    std::size_t seats_;
    std::vector<PersonId> passengers_;
    TransportState state_;
};

// This checks a typed transport only. The caller must construct it from
// trusted host-owned ferry/shuttle records, not from a UI request.
inline bool completed_transport(const NpcTransport& transport,PersonId npc) {
    return transport.arrived(npc) &&
           completed_arrival(transport.arrival_evidence(),npc,
                             transport.source(),transport.destination());
}

} // namespace galaxy::transport
