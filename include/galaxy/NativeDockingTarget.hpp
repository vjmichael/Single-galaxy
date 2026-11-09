#pragma once
// FP3D-ENV-01G: select a real station near the original stationary ship.
// Godot never supplies an entity ID or collision range for docking.
#include "galaxy/SpaceMiningContact.hpp"
#include <cstdint>
#include <limits>
#include <optional>
namespace galaxy::fp3d::docking {
inline std::optional<EntityId> nearest_original_station(
    const Simulation& world,const Entity& vessel) {
    if(vessel.type!=EntityType::Ship || vessel.owner_id==0 ||
       vessel.dock_state!=DockState::InSpace ||
       vessel.jump_state!=ShipJumpState::Ready ||
       vessel.velocity.x!=0 || vessel.velocity.y!=0 ||
       vessel.velocity_z_mm_per_tick!=0)return std::nullopt;
    constexpr std::int64_t cap=Simulation::kDockingRangeMm;
    constexpr std::size_t scan_cap=8192;
    std::optional<EntityId> nearest;
    std::int64_t best=std::numeric_limits<std::int64_t>::max();
    std::size_t scanned=0;
    for(const auto id:world.entity_ids_in_system(vessel.system_id)) {
        if(++scanned>scan_cap)break;
        const auto* station=world.find(id);
        if(!station || station->type!=EntityType::Station ||
           !world.same_local_frame(vessel.id,id))continue;
        std::optional<std::int64_t> dx,dy,dz;
        if(vessel.physical_galactic_authoritative &&
           station->physical_galactic_authoritative) {
            dx=space_mining_view::nearby_axis_delta(vessel.physical_galactic.x,
                station->physical_galactic.x,cap);
            dy=space_mining_view::nearby_axis_delta(vessel.physical_galactic.y,
                station->physical_galactic.y,cap);
            dz=space_mining_view::nearby_axis_delta(vessel.physical_galactic.z,
                station->physical_galactic.z,cap);
        } else if(!vessel.physical_galactic_authoritative &&
                  !station->physical_galactic_authoritative) {
            const auto x=station->position.x-vessel.position.x;
            const auto y=station->position.y-vessel.position.y;
            const auto z=station->position_z_mm-vessel.position_z_mm;
            if(x>=-cap&&x<=cap&&y>=-cap&&y<=cap&&z>=-cap&&z<=cap){
                dx=x;dy=y;dz=z;
            }
        }
        if(!dx || !dy || !dz)continue;
        const auto dist=(*dx)*(*dx)+(*dy)*(*dy)+(*dz)*(*dz);
        if(dist>cap*cap || dist>=best)continue;
        nearest=id;best=dist;
    }
    return nearest;
}
} // namespace galaxy::fp3d::docking
