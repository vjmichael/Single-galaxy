#pragma once
// NPC-RESP-01A: authoritative *movement dispatch* only.
// No second flight engine, cargo ledger, combat executor or person ledger.
#include "galaxy/Persistence.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"
#include <cstdint>

namespace galaxy::npc_response {
enum class Kind : std::uint8_t {
    OwnerDistress=1, OwnerEscort=2, LawfulIntercept=3,
    PlayerRecovery=4, Enforcement=5
};
enum class Status : std::uint8_t {
    Accepted, Invalid, WrongOwner, NoPhysicalShip, NoAuthority, Refused
};
struct Request {
    std::uint64_t response_id=0;
    PlayerId owner=0;           // authenticated owner of responding NPC ship
    EntityId responding_ship=0;
    EntityId subject_ship=0;     // real physical player-controlled hull
    std::uint64_t pilot_npc=0;  // zero selects existing qualified crew
    Kind kind=Kind::OwnerDistress;
};
// The central response dispatcher does NOT invent warrants, crime evidence
// or a free damage/arrest right. LawfulIntercept and Enforcement are withheld
// until an incident/evidence and faction-law authority ledger exists.
// Even OwnerDistress does not automatically approve boarding or repairs.
inline Status dispatch(PersistentWorld& world,const Request& r,
                       FailPoint fail=FailPoint::None){
    if(!r.response_id || !r.owner ||
       r.owner==Simulation::kServerAuthority ||
       !r.responding_ship || !r.subject_ship ||
       r.responding_ship==r.subject_ship)
        return Status::Invalid;
    const auto* responding=world.world().find(r.responding_ship);
    const auto* subject=world.world().find(r.subject_ship);
    if(!responding || !subject ||
       responding->type!=EntityType::Ship ||
       subject->type!=EntityType::Ship)
        return Status::NoPhysicalShip;
    if(responding->owner_id!=r.owner)return Status::WrongOwner;
    // Initial physically grounded cases: player's own qualified response
    // crews. Cross-faction warrants and distress-call authentication belong
    // to the later incident and faction authority gate, not client input.
    if(r.kind!=Kind::OwnerDistress && r.kind!=Kind::OwnerEscort)
        return Status::NoAuthority;
    if(subject->owner_id!=r.owner)return Status::NoAuthority;
    if(!world.world().same_local_frame(r.responding_ship,r.subject_ship))
        return Status::Refused;
    const auto purpose=r.kind==Kind::OwnerDistress?
        NpcFlightPurpose::Rescue:NpcFlightPurpose::Patrol;
    return world.queue_npc_ship_rendezvous(
        r.owner,r.responding_ship,r.pilot_npc,r.subject_ship,
        r.response_id,purpose,fail) ? Status::Accepted:Status::Refused;
}
inline bool physically_arrived(const Simulation& world,
                               const Request& request) noexcept {
    return ship_interaction::rendezvous_preflight(
        world,request.owner,request.responding_ship,
        request.subject_ship,request.response_id)==
        ship_interaction::Readiness::Ready;
}
} // namespace galaxy::npc_response
