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
} // namespace galaxy::stage6
