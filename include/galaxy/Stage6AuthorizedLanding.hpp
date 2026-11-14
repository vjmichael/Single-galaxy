#pragma once
// STAGE6-C: client-visible proof of a real already-committed shuttle
// descent, NOT new authority to land a player or launch a ship.
// Landing, NPC transfer, material and return flight remain exclusively the
// original Simulation/PersistentWorld operations and original SQLite journal.
#include "galaxy/Stage6PlanetSurface.hpp"
#include <optional>
namespace galaxy::stage6 {
struct NativeLandingObservation {
    NativePlanetSurface surveyed{};
    PlanetarySiteKey key{};
    std::uint64_t original_surface_location=0;
    std::uint64_t pilot_npc=0;
    std::uint64_t original_descent_receipt=0;
    std::uint64_t world_generation=0,state_hash=0;
    bool original_shuttle_landed=false;
    bool still_current(const PersistentWorld& persistent) const {
        return surveyed.still_current(persistent) &&
            persistent.generation()==world_generation &&
            persistent.world().state_hash()==state_hash;
    }
};
inline std::optional<NativeLandingObservation> observe_native_landed_shuttle(
    const PersistentWorld& native,PlayerId owner,EntityId source_ship,
    const PlanetarySiteKey& key){
    const auto& world=native.world();
    if(std::get<3>(key)!=1)return std::nullopt;
    const auto surface=observe_planet_surface(native,owner,source_ship,
        std::get<0>(key),std::get<1>(key),std::get<2>(key));
    if(!surface)return std::nullopt;
    const auto site=world.planetary_sites().find(key);
    const auto shuttle=world.planetary_shuttles().find(key);
    if(site==world.planetary_sites().end() ||
       shuttle==world.planetary_shuttles().end())return std::nullopt;
    const auto& place=site->second;
    const auto& flight=shuttle->second;
    // Do NOT treat a request, animation, queued departure, surface terrain,
    // floating-origin overlap or landing prediction as a committed arrival.
    if(place.owner!=owner || flight.owner!=owner ||
       place.source_ship!=source_ship || flight.source_ship!=source_ship ||
       place.region.body!=std::get<0>(key) ||
       place.region.latitude!=std::get<1>(key) ||
       place.region.longitude!=std::get<2>(key) ||
       place.spaceport_phase!=3 || !place.surface_location ||
       flight.flight_phase!=2 || flight.flight_request ||
       flight.arrival_tick || !flight.descent_receipt || !flight.pilot_npc ||
       flight.ascent_receipt==flight.descent_receipt)
        return std::nullopt;
    const auto npc=world.npc_records().find(flight.pilot_npc);
    if(npc==world.npc_records().end() ||
       npc->second.home_ship!=source_ship ||
       npc->second.location!=place.surface_location ||
       npc->second.role!=NpcRole::Ship)
        return std::nullopt;
    NativeLandingObservation result{};
    result.surveyed=*surface;result.key=key;
    result.original_surface_location=place.surface_location;
    result.pilot_npc=flight.pilot_npc;
    result.original_descent_receipt=flight.descent_receipt;
    result.world_generation=native.generation();
    result.state_hash=world.state_hash();
    result.original_shuttle_landed=true;
    return result;
}
// S6-F4: a *real original player-person* surface arrival, separate from
// NPC shuttle landing and from a Godot spectator transform. It is true only
// after explicit human disembarkation under the same journaled descent.
struct NativePlayerSurfaceObservation {
    NativeLandingObservation landing{};
    PlayerId owner=0;
    std::uint64_t player_disembark_receipt=0;
    bool still_current(const PersistentWorld& source) const {
        return landing.still_current(source) &&
            source.world().player_people().count(owner) &&
            source.world().player_people().at(owner).state==
                PlayerPersonState::AtPlanetarySurface &&
            source.world().player_people().at(owner).last_receipt==
                player_disembark_receipt;
    }
};
inline std::optional<NativePlayerSurfaceObservation> observe_native_landed_player(
    const PersistentWorld& native,PlayerId owner,EntityId source_ship,
    const PlanetarySiteKey& key) {
    const auto landed=observe_native_landed_shuttle(
        native,owner,source_ship,key);
    if(!landed)return std::nullopt;
    const auto it=native.world().player_people().find(owner);
    if(it==native.world().player_people().end())return std::nullopt;
    const auto& p=it->second;
    if(p.state!=PlayerPersonState::AtPlanetarySurface ||
       p.owner!=owner || p.home_ship!=source_ship || p.physical_ship ||
       p.physical_station || !p.last_receipt ||
       p.planet_body!=std::get<0>(key) ||
       p.planet_latitude!=std::get<1>(key) ||
       p.planet_longitude!=std::get<2>(key) ||
       p.planet_revision!=std::get<3>(key) ||
       p.planet_descent_receipt!=landed->original_descent_receipt ||
       p.planet_ascent_receipt)return std::nullopt;
    return NativePlayerSurfaceObservation{*landed,owner,p.last_receipt};
}
} // namespace galaxy::stage6
