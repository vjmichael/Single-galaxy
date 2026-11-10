#pragma once
// STAGE6-A: read-only join of one genuine original celestial body and one
// previously authorized procedural geographic cell. This is a render
// description, NOT a second planet registry, landing action, inventory,
// terraforming command, world-space teleport or persistence writer.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetaryGeography.hpp"
#include <cstdint>
#include <optional>

namespace galaxy::stage6 {

struct NativePlanetSurface {
    PlayerId observer=0;
    EntityId observed_from_ship=0;
    std::uint64_t original_body_id=0;
    std::uint64_t world_tick=0,world_generation=0,state_hash=0;
    PhysicalCelestialBody original_body{};
    planetary::GeographicSite surveyed_cell{};
    // Physical position is the original high-precision galactic body
    // anchor. No lossy floating-origin coordinate is used as an identity.
    bool still_current(const PersistentWorld& native) const {
        const auto& world=native.world();
        return native.generation()==world_generation &&
               world.tick()==world_tick && world.state_hash()==state_hash;
    }
};

inline std::optional<NativePlanetSurface> observe_planet_surface(
    const PersistentWorld& native,PlayerId owner,EntityId from_ship,
    std::uint64_t body_id,std::uint16_t latitude,
    std::uint16_t longitude) {
    const auto& world=native.world();
    if(!owner || owner==Simulation::kServerAuthority ||
       !from_ship || !body_id || latitude>=planetary::kWatershedRows ||
       longitude>=planetary::kWatershedColumns)
        return std::nullopt;
    const auto* player_ship=world.find(from_ship);
    if(!player_ship || player_ship->type!=EntityType::Ship ||
       player_ship->owner_id!=owner)
        return std::nullopt;
    // These TWO existing authoritative admission checks must independently
    // agree on a real original body. A fabricated tile ID or foreign owner
    // cannot be used to draw a navigable/landable planet.
    const auto anchor=world.celestial_navigation_anchor(
        owner,from_ship,body_id);
    const auto site=world.scan_planetary_geography(
        owner,from_ship,body_id,latitude,longitude);
    if(!anchor || !site ||
       (anchor->kind!=CelestialKind::Planet &&
        anchor->kind!=CelestialKind::Moon) ||
       anchor->id!=body_id || anchor->system!=world.system_id() ||
       !anchor->physical_radius_mm ||
       site->key.body!=body_id || !site->key.revision ||
       site->key.latitude!=latitude || site->key.longitude!=longitude)
        return std::nullopt;
    // The original world position, region hydrology and any existing site
    // inventory remain separate original authorities. No new terrain
    // changes a resource quantity or advances an original game tick.
    NativePlanetSurface result{};
    result.observer=owner;result.observed_from_ship=from_ship;
    result.original_body_id=body_id;
    result.world_generation=native.generation();
    result.world_tick=world.tick();result.state_hash=world.state_hash();
    result.original_body=*anchor;
    result.surveyed_cell=*site;
    return result;
}
} // namespace galaxy::stage6
