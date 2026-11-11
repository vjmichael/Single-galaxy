#pragma once
// GROUP3-B: bounded optical original-world asteroid observation. NOT an
// authority to mine, teleport, collect, land or fabricate source material.
// A passive silhouette reveals geometry only; geological composition and
// exact remaining ore require an existing powered exploration scanner.
// All source identity, frame, 3D position and finite quantities are read
// from the original Simulation, never generated from a Godot camera.
#include "galaxy/SpaceMiningContact.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::mining_survey {
inline constexpr std::int64_t kOpticalRangeMm=8500000; // <= High drawing clip
inline constexpr std::size_t kMaxSurveyed=16;
inline constexpr std::size_t kScanBudget=8192;

struct Source {
    EntityId rock=0;
    std::int64_t x_mm=0,y_mm=0,z_mm=0,radius_mm=0;
    // -1 means the passive observer does NOT know exact original inventory.
    std::int64_t ore_tonnes=-1;
    std::uint8_t taxonomy=3; // unknown until powered scanner resolves it
    bool geological_detail=false;
    std::uint64_t tick=0;
    bool scan_truncated=false;
};

inline std::vector<Source> nearby_original_asteroids(
        const Simulation& world,PlayerId owner,EntityId observer_id,
        std::size_t max_results=kMaxSurveyed) {
    std::vector<Source> rows;
    const auto* ship=world.find(observer_id);
    if(!owner||!ship||ship->type!=EntityType::Ship||
       ship->owner_id!=owner||ship->dock_state!=DockState::InSpace||
       max_results==0||max_results>kMaxSurveyed)return rows;
    const bool active_scanner=ship_sensors::powered_survey_scanner(world,*ship);
    const auto ids=world.entity_ids_in_system(ship->system_id);
    std::size_t scanned=0;
    bool truncated=false;
    for(const auto id:ids){
        if(++scanned>kScanBudget){truncated=true;break;}
        const auto* rock=world.find(id);
        if(!rock||rock->type!=EntityType::Asteroid||rock->ore<=0||
           !world.same_local_frame(observer_id,id))continue;
        std::optional<std::int64_t> dx,dy,dz;
        if(ship->physical_galactic_authoritative &&
           rock->physical_galactic_authoritative){
            dx=space_mining_view::nearby_axis_delta(
                ship->physical_galactic.x,rock->physical_galactic.x,kOpticalRangeMm);
            dy=space_mining_view::nearby_axis_delta(
                ship->physical_galactic.y,rock->physical_galactic.y,kOpticalRangeMm);
            dz=space_mining_view::nearby_axis_delta(
                ship->physical_galactic.z,rock->physical_galactic.z,kOpticalRangeMm);
        }else if(!ship->physical_galactic_authoritative &&
                  !rock->physical_galactic_authoritative){
            const auto x=rock->position.x-ship->position.x;
            const auto y=rock->position.y-ship->position.y;
            const auto z=rock->position_z_mm-ship->position_z_mm;
            if(x>=-kOpticalRangeMm && x<=kOpticalRangeMm &&
               y>=-kOpticalRangeMm && y<=kOpticalRangeMm &&
               z>=-kOpticalRangeMm && z<=kOpticalRangeMm){dx=x;dy=y;dz=z;}
        }
        if(!dx||!dy||!dz)continue;
        const auto length_sq=(*dx)*(*dx)+(*dy)*(*dy)+(*dz)*(*dz);
        if(length_sq>kOpticalRangeMm*kOpticalRangeMm)continue;
        const auto* geology=world.geology(id);
        const auto radius=geology?
            std::min<std::int64_t>(10000,geology->radius_m)*1000:500;
        rows.push_back({id,*dx,*dy,*dz,radius,
            active_scanner?rock->ore:-1,
            active_scanner&&geology?
                static_cast<std::uint8_t>(geology->taxonomy):std::uint8_t{3},
            active_scanner,world.tick(),false});
    }
    std::sort(rows.begin(),rows.end(),[](const Source& a,const Source& b){
        const auto da=a.x_mm*a.x_mm+a.y_mm*a.y_mm+a.z_mm*a.z_mm;
        const auto db=b.x_mm*b.x_mm+b.y_mm*b.y_mm+b.z_mm*b.z_mm;
        return da==db?a.rock<b.rock:da<db;
    });
    if(rows.size()>max_results){rows.resize(max_results);truncated=true;}
    for(auto& row:rows)row.scan_truncated=truncated;
    return rows;
}
} // namespace galaxy::mining_survey
