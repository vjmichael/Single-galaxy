#pragma once
// NPC-RESP-01A: authoritative *movement dispatch* only.
// No second flight engine, cargo ledger, combat executor or person ledger.
#include "galaxy/Persistence.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"
#include "galaxy/ColonyDefense.hpp"
#include <algorithm>
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
// A credible nearby foreign contact can justify approaching to hail.
// Actual observation is recomputed from CURRENT named, duty-eligible crew,
// not trusted from a user-supplied contact ID, guessed coordinates or intent.
// This is NO arrest warrant, weapon authority, or boarding permission.
inline Status dispatch_observed_patrol(
    PersistentWorld& world,PlayerId patrol_owner,
    EntityId patrol_ship,std::uint64_t pilot_npc,
    const colony_defense::Contact& observed,std::uint64_t order_id,
    FailPoint fail=FailPoint::None) {
    if(!order_id || !patrol_owner || patrol_owner==Simulation::kServerAuthority ||
       !patrol_ship || !observed.source || !observed.foreign_ship ||
       observed.source!=patrol_ship || observed.foreign_ship==patrol_ship)
        return Status::Invalid;
    const auto* patrol=world.world().find(patrol_ship);
    const auto* target=world.world().find(observed.foreign_ship);
    if(!patrol || !target || patrol->type!=EntityType::Ship ||
       target->type!=EntityType::Ship)return Status::NoPhysicalShip;
    if(patrol->owner_id!=patrol_owner)return Status::WrongOwner;
    if(!target->owner_id || target->owner_id==patrol_owner ||
       observed.foreign_owner!=target->owner_id ||
       observed.world_tick!=world.world().tick())
        return Status::NoAuthority;
    const auto verified=colony_defense::observe_local_contacts(
        world.world(),patrol_ship,observed.observer_npc);
    const auto real=std::find_if(verified.begin(),verified.end(),
        [&](const colony_defense::Contact& c){
          return c.source==observed.source &&
                 c.foreign_ship==observed.foreign_ship &&
                 c.foreign_owner==observed.foreign_owner &&
                 c.observer_npc==observed.observer_npc &&
                 c.world_tick==observed.world_tick &&
                 c.system==observed.system;
        });
    if(real==verified.end())return Status::NoAuthority;
    return world.queue_npc_ship_rendezvous(
        patrol_owner,patrol_ship,pilot_npc,observed.foreign_ship,
        order_id,NpcFlightPurpose::Patrol,fail)?
        Status::Accepted:Status::Refused;
}
inline bool physically_arrived(const Simulation& world,
                               const Request& request) noexcept {
    return ship_interaction::rendezvous_preflight(
        world,request.owner,request.responding_ship,
        request.subject_ship,request.response_id)==
        ship_interaction::Readiness::Ready;
}
} // namespace galaxy::npc_response
