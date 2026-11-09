#pragma once

#include "galaxy/Simulation.hpp"
#include <optional>

namespace galaxy::station_launch {
// Read-only selector. A berth never creates a ship; it only exposes a
// physically present, owned, docked, intact vessel for host-authorized use.
// Crew/pilot custody checks remain with the command and fleet controllers.
inline std::optional<EntityId> available_ship(
    const Simulation& world, PlayerId owner, EntityId station_id) {
    if(!owner || owner == Simulation::kServerAuthority) return std::nullopt;
    const auto* site = world.find(station_id);
    if(!site || site->type != EntityType::Station) return std::nullopt;
    for(const auto& [id, vessel] : world.entities()) {
        if(vessel.type != EntityType::Ship || vessel.owner_id != owner ||
           vessel.hull <= 0 || vessel.dock_state != DockState::Docked ||
           vessel.dock_station != station_id ||
           vessel.jump_state != ShipJumpState::Ready) continue;
        if(const auto* colony = world.colony_ship(id); colony && colony->crew <= 0)
            continue;
        return id;
    }
    return std::nullopt;
}
} // namespace galaxy::station_launch
