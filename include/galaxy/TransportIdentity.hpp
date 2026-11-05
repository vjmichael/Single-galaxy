#pragma once
// Shared identity and arrival proof for all physical NPC transport.
// A transport adapter supplies the actual authoritative vehicle state;
// migration must not forge a ferry to represent a planetary shuttle.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::transport {
using PersonId=std::uint64_t;
using LocationId=std::uint64_t;
enum class Mode : std::uint8_t { Ferry=1, PlanetaryShuttle=2 };
enum class Phase : std::uint8_t { Booked=0, InTransit=1, Arrived=2, Interrupted=3 };

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
// The vehicle adapter must verify the evidence was sourced from committed
// flight/voyage state. This helper intentionally cannot attest consent,
// immigration policy, dependent custody, or capacity.
inline bool completed_arrival(const ArrivalEvidence& a,PersonId npc,
                              LocationId origin,LocationId destination) {
    return a.transport_id!=0 && a.phase==Phase::Arrived &&
           a.origin==origin && a.destination==destination &&
           origin!=0 && destination!=0 && origin!=destination &&
           canonical_manifest(a.manifest) &&
           contains_person(a.manifest,npc);
}
// A landed shuttle visitor has an actual surface custody ledger.
// Unlike a completed ferry voyage, its flight_request may already be cleared;
// use the durable descent receipt rather than inventing a ferry voyage.
inline bool completed_shuttle_visit(std::uint64_t descent_receipt,
                                    bool landed_or_disembarked,
                                    const std::vector<PersonId>& visitors,
                                    PersonId npc) {
    return descent_receipt!=0 && landed_or_disembarked &&
           canonical_manifest(visitors) && contains_person(visitors,npc);
}
} // namespace galaxy::transport
