#pragma once
// SPACE-04B: read-only render/interaction projection from the ORIGINAL
// PersistentWorld through the registered immutable galaxy system origin.
// The viewer never owns a second ship position or receives mutation rights.
#include "galaxy/GalacticFloatingFrame.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include <optional>
#include <string>
namespace galaxy::galactic {
struct OriginalShipProjection {
    EntityId ship=0;
    std::uint64_t original_generation=0;
    Point physical{};
    Local3 relative{};
};
inline std::optional<OriginalShipProjection> original_ship_in_floating_frame(
    const std::string& physical_sqlite,const PersistentWorld& world,
    EntityId ship,const FloatingFrame& frame) {
    const auto* e=world.world().find(ship);
    if(!e||e->type!=EntityType::Ship||!world.world().native_3d_physics())
        return std::nullopt;
    const auto physical=original_ship_galactic_position(physical_sqlite,world,ship);
    if(!physical)return std::nullopt; // unregistered star is not invented
    const auto relative=local_from_galactic(frame,*physical);
    if(!relative)return std::nullopt; // outside render proximity, not a wall
    return OriginalShipProjection{ship,world.generation(),*physical,*relative};
}
} // namespace galaxy::galactic
