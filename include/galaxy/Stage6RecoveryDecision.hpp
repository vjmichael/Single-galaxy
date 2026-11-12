#pragma once
// S6-F6: pure, source-authoritative recovery role selection.
// No new simulation, wall-clock catch-up, SQLite writer or Godot authority.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <optional>
#include <tuple>

namespace galaxy::stage6::recovery {
enum class Role : std::uint8_t {
    Invalid=0, Station=1, Ship=2, Shuttle=3, Surface=4
};
struct Decision {
    Role role=Role::Invalid;
    PlayerPersonState person_state=PlayerPersonState::AboardOwnShip;
    EntityId ship=0, surface=0;
    std::uint64_t body=0;
    std::uint16_t latitude=0,longitude=0;
    std::uint8_t shuttle_phase=0;
};
inline std::optional<Decision> inspect(const Simulation& world,PlayerId owner) {
    const auto it=world.player_people().find(owner);
    if(it==world.player_people().end())return std::nullopt;
    const auto& p=it->second;
    if(p.owner!=owner)return std::nullopt;
    Decision result{};
    result.person_state=p.state;
    // The station role may legitimately have no assigned home_ship, because
    // the real person has not yet boarded its first named original vessel.
    if(p.state==PlayerPersonState::AtStation) {
        if(p.physical_station==0 || p.physical_ship!=0 ||
           p.planet_body!=0 || p.planet_revision!=0)
            return std::nullopt;
        const auto* station=world.find(p.physical_station);
        if(!station || station->type!=EntityType::Station)
            return std::nullopt;
        result.role=Role::Station;
        return result;
    }
    if(!p.home_ship)return std::nullopt;
    const auto* ship=world.find(p.home_ship);
    if(!ship || ship->owner_id!=owner || ship->type!=EntityType::Ship)
        return std::nullopt;
    result.ship=p.home_ship;
    if(p.state==PlayerPersonState::AboardOwnShip) {
        if(p.physical_ship!=p.home_ship || p.physical_station!=0 ||
           p.planet_body!=0 || p.planet_revision!=0)
            return std::nullopt;
        result.role=Role::Ship;
        return result;
    }
    // Custody in a real planetary shuttle/surface, never inferred from
    // landscape visuals, Godot camera, a site in the map or local clocks.
    if(p.state!=PlayerPersonState::InPlanetaryShuttle &&
       p.state!=PlayerPersonState::PlanetaryTransit &&
       p.state!=PlayerPersonState::AtPlanetarySurface)
        return std::nullopt;
    if(p.physical_ship!=0 || p.physical_station!=0 ||
       !p.planet_body || p.planet_revision!=1 ||
       p.planet_latitude>=36 || p.planet_longitude>=72)
        return std::nullopt;
    const PlanetarySiteKey key{p.planet_body,p.planet_latitude,
                                p.planet_longitude,p.planet_revision};
    const auto sh=world.planetary_shuttles().find(key);
    const auto site=world.planetary_sites().find(key);
    if(sh==world.planetary_shuttles().end() ||
       site==world.planetary_sites().end())
        return std::nullopt;
    if(sh->second.owner!=owner || sh->second.source_ship!=p.home_ship ||
       !sh->second.pilot_npc || sh->second.flight_phase>3 ||
       site->second.owner!=owner || site->second.source_ship!=p.home_ship ||
       site->second.spaceport_phase!=3 || !site->second.surface_location)
        return std::nullopt;
    const auto phase=sh->second.flight_phase;
    if(p.state==PlayerPersonState::PlanetaryTransit) {
        if((phase!=1 && phase!=3) || !p.planet_flight_receipt ||
           p.planet_flight_receipt!=sh->second.flight_request)
            return std::nullopt;
    } else if(p.state==PlayerPersonState::InPlanetaryShuttle) {
        if(phase!=0 && phase!=2)return std::nullopt;
        // Once landed, only the actual completed descent can prove
        // permission to disembark. Before a first descent no proof exists.
        if(phase==2 && (!p.planet_descent_receipt ||
                        p.planet_descent_receipt!=sh->second.descent_receipt))
            return std::nullopt;
        if(phase==0 && p.planet_ascent_receipt &&
           p.planet_ascent_receipt!=sh->second.ascent_receipt)
            return std::nullopt;
    } else {
        if(phase!=2 || !p.planet_descent_receipt ||
           p.planet_descent_receipt!=sh->second.descent_receipt ||
           (p.surface_pose.surface_location!=0 &&
            p.surface_pose.surface_location!=site->second.surface_location))
            return std::nullopt;
    }
    result.role=p.state==PlayerPersonState::AtPlanetarySurface?
        Role::Surface:Role::Shuttle;
    result.body=p.planet_body;
    result.latitude=p.planet_latitude;
    result.longitude=p.planet_longitude;
    result.surface=site->second.surface_location;
    result.shuttle_phase=phase;
    return result;
}
} // namespace galaxy::stage6::recovery
