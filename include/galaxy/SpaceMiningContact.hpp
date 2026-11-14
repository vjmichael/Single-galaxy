#pragma once
// PLAY-STATION-04A: read-only original-world local 3D asteroid projection.
// A rock contact is NOT permission to mine. Unfitted hulls MUST remain unable
// to harvest. Godot receives relative coordinates, never a world authority.
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace galaxy::space_mining_view {
constexpr std::int64_t kContactRangeMm=30000;
constexpr std::size_t kMaxSystemEntitiesScanned=8192;
constexpr std::uint8_t kMiningToolHardpoint=4; // v126 manufactured fitted drill, never free
struct Contact {
  EntityId rock=0;
  std::int64_t ore_tonnes=0;
  std::int64_t x_mm=0,y_mm=0,z_mm=0, radius_mm=0;
  std::uint8_t taxonomy=3; // 3 = unclassified, never invent ore composition
  bool mining_tool_online=false;
  std::int64_t cargo_tonnes=0,cargo_capacity_tonnes=0;
  bool scan_truncated=false;
};

inline std::optional<std::int64_t> nearby_axis_delta(
    galactic::Axis observer,galactic::Axis contact,std::int64_t cap) noexcept {
  if(!observer.valid()||!contact.valid())return std::nullopt;
  auto delta=contact.millimeters-observer.millimeters; // bounded by 1e15
  if(contact.sector!=observer.sector){
    const auto plus=galactic::checked_add(observer.sector,1);
    const auto minus=galactic::checked_add(observer.sector,-1);
    if(plus && contact.sector==*plus)delta+=galactic::kSectorEdgeMm;
    else if(minus && contact.sector==*minus)delta-=galactic::kSectorEdgeMm;
    else return std::nullopt;
  }
  if(delta < -cap || delta>cap)return std::nullopt;
  return delta;
}
inline bool tool_online(const Simulation& world,const Entity& ship) {
  for(std::uint8_t slot=0;slot<2;++slot)
    if(ship.fitting.hardpoints[slot]==kMiningToolHardpoint &&
       world.hardpoint_effective_on(ship.id,slot))return true;
  return false;
}
inline std::optional<Contact> nearest_original_asteroid(
    const Simulation& world,PlayerId owner,EntityId ship_id) {
  const auto* ship=world.find(ship_id);
  if(!owner||!ship||ship->type!=EntityType::Ship||ship->owner_id!=owner||
     ship->dock_state!=DockState::InSpace)
    return std::nullopt;
  const auto candidates=world.entity_ids_in_system(ship->system_id);
  Contact best{};
  std::int64_t nearest=std::numeric_limits<std::int64_t>::max();
  std::size_t scanned=0;
  bool truncated=false;
  for(const auto id:candidates) {
    if(++scanned>kMaxSystemEntitiesScanned){truncated=true;break;}
    const auto* rock=world.find(id);
    if(!rock || rock->type!=EntityType::Asteroid || rock->ore<=0 ||
       !world.same_local_frame(ship_id,id))continue;
    std::optional<std::int64_t> dx,dy,dz;
    if(ship->physical_galactic_authoritative && rock->physical_galactic_authoritative) {
       dx=nearby_axis_delta(ship->physical_galactic.x,rock->physical_galactic.x,kContactRangeMm);
       dy=nearby_axis_delta(ship->physical_galactic.y,rock->physical_galactic.y,kContactRangeMm);
       dz=nearby_axis_delta(ship->physical_galactic.z,rock->physical_galactic.z,kContactRangeMm);
    } else if(!ship->physical_galactic_authoritative && !rock->physical_galactic_authoritative) {
       const auto x=rock->position.x-ship->position.x;
       const auto y=rock->position.y-ship->position.y;
       const auto z=rock->position_z_mm-ship->position_z_mm;
       if(std::abs(x)<=kContactRangeMm && std::abs(y)<=kContactRangeMm &&
          std::abs(z)<=kContactRangeMm) {dx=x;dy=y;dz=z;}
    }
    if(!dx || !dy || !dz)continue;
    const std::int64_t distance=(*dx)*(*dx)+(*dy)*(*dy)+(*dz)*(*dz);
    if(distance>kContactRangeMm*kContactRangeMm || distance>=nearest)continue;
    nearest=distance;
    const auto* geology=world.geology(id);
    const std::uint8_t taxonomy=geology?static_cast<std::uint8_t>(geology->taxonomy):std::uint8_t{3};
    const auto radius=geology?std::min<std::int64_t>(geology->radius_m,10000)*1000:500;
    best={id,rock->ore,*dx,*dy,*dz,radius,taxonomy,
          tool_online(world,*ship),ship->cargo,ship->cargo_capacity,truncated};
  }
  if(!best.rock)return std::nullopt;
  best.scan_truncated=truncated;
  return best;
}
} // namespace galaxy::space_mining_view
