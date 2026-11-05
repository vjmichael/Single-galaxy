#pragma once
// Shared named-NPC movement contract. Historical "ferry" is now a vehicle
// adapter, not a restriction on which NPC locations can communicate.
// A caller must derive evidence from committed authoritative vehicle records.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::transport {
using PersonId=std::uint64_t;
using LocationId=std::uint64_t;

// A location can be a colony ship, a persistent station/colony, or an exact
// planetary surface custody address. Never infer its kind from the number.
enum class LocationKind : std::uint8_t {
    ColonyShip=1, SpaceColony=2, PlanetarySite=3, OtherNpcLocation=4
};
struct Endpoint {
    LocationKind kind=LocationKind::OtherNpcLocation;
    LocationId id=0;
    friend constexpr bool operator==(Endpoint a,Endpoint b) noexcept {
        return a.kind==b.kind && a.id==b.id;
    }
    friend constexpr bool operator!=(Endpoint a,Endpoint b) noexcept {
        return !(a==b);
    }
};
enum class Mode : std::uint8_t {
    Ferry=1, PlanetaryShuttle=2, SurfaceVehicle=3, OtherVehicle=4
};
enum class Phase : std::uint8_t { Booked=0, InTransit=1, Arrived=2, Interrupted=3 };

// Legacy ferry receipts remain valid. Versioning the location *kind* in a
// future unified journal must be additive and not rewrite old snapshots.
struct ArrivalEvidence {
    Mode mode=Mode::Ferry;
    Phase phase=Phase::Booked;
    std::uint64_t transport_id=0;
    LocationId origin=0;
    LocationId destination=0;
    std::vector<PersonId> manifest;
};
inline bool canonical_manifest(const std::vector<PersonId>& ids) {
    return !ids.empty() && ids.front()!=0 &&
           std::is_sorted(ids.begin(),ids.end()) &&
           std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
inline bool contains_person(const std::vector<PersonId>& ids,PersonId id) {
    return id!=0 && std::binary_search(ids.begin(),ids.end(),id);
}
// This is the one mode-independent identity/arrival validator.
// Neither vehicle type nor location kind decides immigration eligibility.
// The authority must still authenticate the source receipt and the physical
// custody against canonical world state before calling it.
inline bool completed_arrival(const ArrivalEvidence& a,PersonId npc,
                              LocationId origin,LocationId destination) {
    return a.transport_id!=0 && a.phase==Phase::Arrived &&
           a.origin==origin && a.destination==destination &&
           origin!=0 && destination!=0 && origin!=destination &&
           canonical_manifest(a.manifest) &&
           contains_person(a.manifest,npc);
}
inline bool completed_arrival(const ArrivalEvidence& a,PersonId npc,
                              Endpoint origin,Endpoint destination) {
    return origin.id!=0 && destination.id!=0 && !(origin==destination) &&
           completed_arrival(a,npc,origin.id,destination.id);
}
} // namespace galaxy::transport
