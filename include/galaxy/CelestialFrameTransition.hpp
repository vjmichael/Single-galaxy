#pragma once
// PLAY-13C.2 — preflight ONLY for a host-authorized completed interplanetary transit.
// A plan does not grant travel, does not mutate Simulation, and is not a receipt.
// The caller must validate durable transit provenance and commit the world atomically.
#include "galaxy/CosmicLocalFrame.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace galaxy {
struct CelestialFrameTransition {
    EntityId ship_id=0;
    PlayerId owner=0;
    std::uint32_t system=0;
    std::uint64_t destination_body=0;
    LocalSpaceFrame destination_frame{};
    Vector2 arrival_position{};
    Vector2 arrival_velocity{};
    // A nonzero request ID is mandatory; its presence is NOT completion proof.
    std::uint64_t transit_request_id=0;
};
enum class FrameTransitionRejection {
    None, MissingTransit, InvalidActor, Unauthorized, WrongSystem,
    NotInSpace, Busy, HasActiveProbe, ActiveMining, InvalidBody,
    UnsafeApproach, Unrepresentable
};
struct FrameTransitionDecision {
    FrameTransitionRejection rejection=FrameTransitionRejection::InvalidActor;
    std::optional<CelestialFrameTransition> plan{};
};

// Host supplies a request ID that must exist in the durable transit journal.
// This pure geometry planner cannot establish departure or completion. The
// PersistentWorld journal alone authorizes and commits a real arrival.
inline FrameTransitionDecision plan_celestial_frame_arrival(
    const Simulation& world,PlayerId owner,EntityId ship_id,
    std::uint64_t body_id,std::uint64_t transit_request_id) {
    if(!verified_transit_request_id)
        return {FrameTransitionRejection::MissingTransit,{}};
    const auto* ship=world.find(ship_id);
    if(!ship || ship->type!=EntityType::Ship)
        return {FrameTransitionRejection::InvalidActor,{}};
    if(!owner || owner==Simulation::kServerAuthority || ship->owner_id!=owner)
        return {FrameTransitionRejection::Unauthorized,{}};
    if(ship->dock_state!=DockState::InSpace)
        return {FrameTransitionRejection::NotInSpace,{}};
    if(ship->jump_state!=ShipJumpState::Ready || ship->pending_gate ||
       ship->jump_ticks || ship->transition_ticks)
        return {FrameTransitionRejection::Busy,{}};
    if(world.active_mining().count(ship_id))
        return {FrameTransitionRejection::ActiveMining,{}};
    for(const auto& entry:world.survey_probes()) {
        if(entry.second.carrier==ship_id)
            return {FrameTransitionRejection::HasActiveProbe,{}};
    }
    if(!body_id || std::uint32_t(body_id>>32)!=ship->system_id)
        return {FrameTransitionRejection::WrongSystem,{}};
    const auto systems=world.physical_celestial_systems().find(ship->system_id);
    if(systems==world.physical_celestial_systems().end())
        return {FrameTransitionRejection::InvalidBody,{}};
    const auto& bodies=systems->second;
    const auto it=std::find_if(bodies.begin(),bodies.end(),
        [body_id](const PhysicalCelestialBody& b){return b.id==body_id;});
    if(it==bodies.end() || it->system!=ship->system_id)
        return {FrameTransitionRejection::InvalidBody,{}};
    // Arrival is at zero in a newly anchored short-range local frame.
    // Frame origin is 100 km beyond the catalogue body's physical radius.
    // No gravity or collision physics are implied by this separation.
    constexpr std::int64_t stand_off_mm=100000000LL;
    if(it->physical_radius_mm>
       std::uint64_t(std::numeric_limits<std::int64_t>::max()-stand_off_mm))
        return {FrameTransitionRejection::Unrepresentable,{}};
    const auto offset=static_cast<std::int64_t>(it->physical_radius_mm)+stand_off_mm;
    const auto origin_x=checked_cosmic_add(it->position.x_mm,offset);
    if(!origin_x)
        return {FrameTransitionRejection::Unrepresentable,{}};
    LocalSpaceFrame frame{ship->system_id,it->id,
        {*origin_x,it->position.y_mm,it->position.z_mm}};
    if(!local_to_cosmic(frame,{0,0,0}))
        return {FrameTransitionRejection::Unrepresentable,{}};
    // Target location is computed from catalogue geometry, never browser input.
    CelestialFrameTransition plan;
    plan.ship_id=ship_id;
    plan.owner=owner;
    plan.system=ship->system_id;
    plan.destination_body=body_id;
    plan.destination_frame=frame;
    plan.arrival_position={0,0};
    plan.arrival_velocity={0,0};
    plan.transit_request_id=verified_transit_request_id;
    return {FrameTransitionRejection::None,plan};
}
} // namespace galaxy
