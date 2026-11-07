#pragma once
// FLIGHT-04: read-only rendezvous preflight. This is NOT an interaction grant.
// Native rescue, freight, mining and transfer executors retain all resource,
// custody, consent, capacity, and ownership authority.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace galaxy::ship_interaction {
enum class Readiness : std::uint8_t {
    Ready, InvalidRequest, WrongOwner, InvalidPhysicalTarget,
    WrongFrame, InTransition, TooFar, RelativeMotion,
    MissingRendezvous, MissingPhysicalReceipt
};
// Checked without squaring coordinate differences or overflowing integer math.
inline bool within_axis(std::int64_t a,std::int64_t b,
                        std::int64_t bound) noexcept {
    if(bound<0)return false;
    // Sign-bit bias maps signed order to unsigned order, portable on MSVC.
    constexpr std::uint64_t sign=std::uint64_t{1}<<63;
    const auto x=static_cast<std::uint64_t>(a)^sign;
    const auto y=static_cast<std::uint64_t>(b)^sign;
    const auto distance=x>=y?x-y:y-x;
    return distance<=static_cast<std::uint64_t>(bound);
}
// Only checks a spatial envelope; actual interaction executors must impose
// their own gameplay-specific ranges and passenger/cargo permissions.
inline Readiness spatial_preflight(const Simulation& world,PlayerId owner,
    EntityId actor,EntityId target,std::int64_t max_axis_gap_mm=2500,
    std::int64_t max_relative_speed_mm_per_tick=250) noexcept {
    if(!owner || owner==Simulation::kServerAuthority || !actor ||
       !target || actor==target || max_axis_gap_mm<0 ||
       max_relative_speed_mm_per_tick<0)
        return Readiness::InvalidRequest;
    const auto* a=world.find(actor),*b=world.find(target);
    if(!a || !b || a->type!=EntityType::Ship ||
       b->type!=EntityType::Ship)
        return Readiness::InvalidPhysicalTarget;
    if(a->owner_id!=owner)return Readiness::WrongOwner;
    if(!world.same_local_frame(actor,target))
        return Readiness::WrongFrame;
    if(a->dock_state!=DockState::InSpace ||
       b->dock_state!=DockState::InSpace ||
       a->jump_state!=ShipJumpState::Ready ||
       b->jump_state!=ShipJumpState::Ready)
        return Readiness::InTransition;
    if(!within_axis(a->position.x,b->position.x,max_axis_gap_mm) ||
       !within_axis(a->position.y,b->position.y,max_axis_gap_mm) ||
       (world.native_3d_physics() &&
        !within_axis(a->position_z_mm,b->position_z_mm,max_axis_gap_mm)))
        return Readiness::TooFar;
    if(!within_axis(a->velocity.x,b->velocity.x,max_relative_speed_mm_per_tick) ||
       !within_axis(a->velocity.y,b->velocity.y,max_relative_speed_mm_per_tick) ||
       (world.native_3d_physics() &&
        !within_axis(a->velocity_z_mm_per_tick,b->velocity_z_mm_per_tick,
                     max_relative_speed_mm_per_tick)))
        return Readiness::RelativeMotion;
    return Readiness::Ready;
}
// A physical approach alone is not clearance to use another ship. The native
// original activity receipt is mandatory even when an order says Arrived.
// A stale receipt is insufficient unless current geometry remains safe.
inline Readiness rendezvous_preflight(const Simulation& world,PlayerId owner,
    EntityId actor,EntityId target,std::uint64_t flight_order_id) noexcept {
    const auto physical=spatial_preflight(world,owner,actor,target);
    if(physical!=Readiness::Ready)return physical;
    const auto it=world.npc_flight_orders().find(flight_order_id);
    if(!flight_order_id || it==world.npc_flight_orders().end() ||
       it->second.owner!=owner || it->second.ship!=actor ||
       it->second.station!=target ||
       it->second.mode!=NpcFlightMode::LocalRendezvous ||
       it->second.phase!=NpcFlightPhase::Arrived)
        return Readiness::MissingRendezvous;
    const auto& f=it->second;
    if(!f.started_tick || f.resolved_tick<f.started_tick ||
       f.resolved_tick>world.tick() || !f.last_local_command)
        return Readiness::MissingPhysicalReceipt;
    for(const auto& event:world.activity_events())
        if(event.kind==ActivityKind::NpcRendezvous &&
           event.actor==actor && event.target==target &&
           event.start_command_id==f.last_local_command &&
           event.tick==f.resolved_tick &&
           event.result==Outcome::Applied && event.transferred==0)
            return Readiness::Ready;
    return Readiness::MissingPhysicalReceipt;
}
} // namespace galaxy::ship_interaction
