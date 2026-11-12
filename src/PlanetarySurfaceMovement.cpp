#include "galaxy/Simulation.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"
#include "galaxy/PlanetaryContinuousSurface.hpp"
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace galaxy {
std::optional<std::uint64_t> Simulation::original_walkable_body_radius(
        std::uint64_t body) const {
    const auto system=physical_celestial_systems_.find(system_id_);
    if(!body || system==physical_celestial_systems_.end())return std::nullopt;
    for(const auto& source:system->second){
        if(source.id!=body)continue;
        if(source.kind!=CelestialKind::Planet &&
           source.kind!=CelestialKind::Moon)return std::nullopt;
        const auto radius=source.physical_radius_mm;
        if(radius<planetary::continuous_surface::kMinWalkableBodyRadiusMm ||
           radius>planetary::continuous_surface::kMaxWalkableBodyRadiusMm)
            return std::nullopt;
        return radius;
    }
    return std::nullopt;
}
bool Simulation::near_original_shuttle_hatch(
        const NativePlanetarySurfacePose& pose,
        const PlanetarySiteKey& origin) const {
    if(!pose.last_move_receipt)return true; // never left the original hatch
    if(snapshot_version_<133)
        return planetary::surface_motion::near_shuttle_hatch(
            pose.x_mm,pose.z_mm); // old v132 save compatibility
    const auto center=planetary::continuous_surface::cell_center(
        {std::get<1>(origin),std::get<2>(origin)});
    const auto radius=original_walkable_body_radius(std::get<0>(origin));
    return center && radius &&
        planetary::continuous_surface::within_radius(
            *center,{pose.latitude_pdeg,pose.longitude_pdeg},
            *radius,planetary::surface_motion::kShuttleHatchRadiusMm);
}

// S6-F5-B: one ORIGINAL person/NPC, one ORIGINAL body/spaceport, one writer.
// The new ground position is a deterministic consequence of the approved
// bounded displacement, the fixed original celestial radius, and the SAME
// source-survey vertex triangle split used in Stage6PlanetTiles. No client
// absolute position, collider, height or Godot state is consumed.
bool Simulation::planetary_surface_move(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc_id,
        std::int64_t dx_mm,std::int64_t dz_mm,std::uint64_t receipt) {
    namespace grid=planetary::continuous_surface;
    using namespace planetary::surface_motion;
    if(!owner || owner==kServerAuthority || !ship || !receipt ||
       receipt>=(std::uint64_t{1}<<60) ||
       !within_step(dx_mm,dz_mm,npc_id==0) ||
       surface_move_receipts_.size()>=100000 ||
       surface_move_receipts_.count(receipt) ||
       player_planet_receipts_.count(receipt) ||
       shuttle_flight_request_ids_.count(receipt) ||
       planetary_family_booking_ids_.count(receipt) ||
       submitted_ids_.count(receipt) || voyages_.count(receipt) ||
       pending_celestial_transits_.count(receipt) ||
       completed_celestial_transits_.count(receipt))
        return false;
    const auto* vessel=find(ship);
    const auto frame=ship_frames_.find(ship);
    const auto facility=planetary_sites_.find(key);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto body=std::get<0>(key);
    const auto radius=original_walkable_body_radius(body);
    const auto home=grid::cell_center({std::get<1>(key),std::get<2>(key)});
    if(!vessel || vessel->type!=EntityType::Ship ||
       vessel->owner_id!=owner || vessel->hull<=0 ||
       vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       frame==ship_frames_.end() ||
       frame->second.anchor_body!=body ||
       !life_support_.count(ship) || std::get<3>(key)!=1 ||
       facility==planetary_sites_.end() ||
       facility->second.owner!=owner ||
       facility->second.source_ship!=ship ||
       facility->second.spaceport_phase!=3 ||
       !facility->second.surface_location ||
       shuttle==planetary_shuttles_.end() ||
       shuttle->second.owner!=owner ||
       shuttle->second.source_ship!=ship ||
       !radius || !home)
        return false;
    for(const auto& [request,transit]:pending_celestial_transits_){
        (void)request;
        if(transit.ship==ship)return false;
    }
    NativePlanetarySurfacePose* position=nullptr;
    if(npc_id==0){
        const auto p=player_people_.find(owner);
        if(p==player_people_.end() ||
           p->second.state!=PlayerPersonState::AtPlanetarySurface ||
           p->second.home_ship!=ship ||
           p->second.physical_ship || p->second.physical_station ||
           p->second.planet_body!=body ||
           p->second.planet_latitude!=std::get<1>(key) ||
           p->second.planet_longitude!=std::get<2>(key) ||
           p->second.planet_revision!=std::get<3>(key) ||
           !p->second.planet_descent_receipt ||
           shuttle->second.flight_phase!=2 ||
           shuttle->second.descent_receipt!=p->second.planet_descent_receipt)
            return false;
        position=&p->second.surface_pose;
    }else{
        const auto n=npcs_.find(npc_id);
        if(n==npcs_.end() ||
           n->second.home_ship!=ship ||
           n->second.location!=facility->second.surface_location ||
           n->second.transport_id ||
           (n->second.role!=NpcRole::Ship &&
            n->second.role!=NpcRole::PlanetarySurface))
            return false;
        position=&n->second.surface_pose;
    }
    if(position->last_move_receipt &&
       (position->last_move_tick>=tick_ ||
        position->surface_location!=facility->second.surface_location))
        return false;
    constexpr std::int64_t kOdometerLimit=1000000000000LL;
    // Native pad +X is east, +Z is south, matching the original
    // on-foot viewer's North=-Z and South=+Z controls.
    const auto x=position->x_mm+dx_mm,z=position->z_mm+dz_mm;
    if(x< -kOdometerLimit || x>kOdometerLimit ||
       z< -kOdometerLimit || z>kOdometerLimit)
        return false;

    // Earlier v132 positions are a bounded local tangent-plane odometer.
    // Convert ALL existing original player/NPC poses together at the first
    // successful v133 commit. No half-migrated world can be journaled.
    struct PendingUpgrade {
        NativePlanetarySurfacePose* pose=nullptr;
        grid::GeoPoint place{};
    };
    std::vector<PendingUpgrade> upgrades;
    const auto point_from=[&](const NativePlanetarySurfacePose& pose,
          const PlanetarySiteKey& origin)->std::optional<grid::GeoPoint> {
        if(snapshot_version_>=133 && pose.last_move_receipt)
            return grid::GeoPoint{pose.latitude_pdeg,pose.longitude_pdeg};
        const auto center=grid::cell_center(
            {std::get<1>(origin),std::get<2>(origin)});
        const auto original_radius=original_walkable_body_radius(
            std::get<0>(origin));
        return center&&original_radius?
            grid::native_offset(*center,pose.x_mm,-pose.z_mm,*original_radius):
            std::nullopt;
    };
    if(snapshot_version_<133){
        for(auto& [id,p]:player_people_){
            (void)id;
            if(!p.surface_pose.last_move_receipt)continue;
            const PlanetarySiteKey source{p.planet_body,p.planet_latitude,
                p.planet_longitude,p.planet_revision};
            const auto converted=point_from(p.surface_pose,source);
            if(!converted)return false;
            upgrades.push_back({&p.surface_pose,*converted});
        }
        for(auto& [id,n]:npcs_){
            (void)id;
            if(!n.surface_pose.last_move_receipt)continue;
            const auto site=std::find_if(planetary_sites_.begin(),
                planetary_sites_.end(),[&](const auto& e){
                   return e.second.surface_location==n.surface_pose.surface_location &&
                          e.second.source_ship==n.home_ship &&
                          e.second.surface_location==n.location &&
                          e.second.spaceport_phase==3;
                });
            if(site==planetary_sites_.end())return false;
            const auto converted=point_from(n.surface_pose,site->first);
            if(!converted)return false;
            upgrades.push_back({&n.surface_pose,*converted});
        }
    }
    const auto start=point_from(*position,key);
    if(!start || !grid::canonical(*start))return false;
    const auto end=grid::native_step(*start,dx_mm,-dz_mm,*radius);
    const auto halfway=grid::native_offset(
        *start,dx_mm/2,-dz_mm/2,*radius);
    if(!end||!halfway || !grid::canonical(*end))return false;

    // This callback only reads the ORIGINAL authenticated PGF watershed
    // survey, including neighboring cells. No synthetic geology, elevation,
    // coast, ownership, landfall or landing pad is minted by movement.
    const auto native_source=[&](grid::SourceCell cell)
            ->std::optional<grid::Sample> {
        const auto observation=scan_planetary_geography(owner,ship,body,
                                     cell.latitude,cell.longitude);
        if(!observation || observation->key.body!=body ||
           observation->key.revision!=std::get<3>(key))
            return std::nullopt;
        return grid::Sample{observation->landscape.region.elevation,
                             observation->landscape.region.surface_water};
    };
    const auto old_ground=grid::source_ground(*start,native_source);
    const auto mid_ground=grid::source_ground(*halfway,native_source);
    const auto contact=grid::source_ground(*end,native_source);
    if(!old_ground||!mid_ground||!contact ||
       old_ground->ocean || mid_ground->ocean || contact->ocean)
        return false;
    // Sweep the segment, not just its endpoint. The source triangles are
    // continuous at tile edges and the 72->0 seam; steep or submerged
    // crossing is denied even if the endpoint is otherwise walkable.
    if(std::llabs(mid_ground->height_mm-old_ground->height_mm)>
           grid::kMaxNativeStepRiseMm ||
       std::llabs(contact->height_mm-mid_ground->height_mm)>
           grid::kMaxNativeStepRiseMm)
        return false;

    // Existing physical/ecology hazards remain attached to the original
    // regional source. Never generate weather, injury or inventory from a
    // forecast. Dangerous positions may only retreat toward the real hatch.
    const PlanetarySiteKey region_key{body,contact->cell.latitude,
        contact->cell.longitude,std::get<3>(key)};
    const auto phase=planetary_physical_environment_.find(region_key);
    const auto origin_phase=planetary_physical_environment_.find(key);
    const auto hazard=phase!=planetary_physical_environment_.end()?
        phase->second.hazard:
        (region_key==key && origin_phase!=planetary_physical_environment_.end()?
         origin_phase->second.hazard:planetary::PhysicalHazard::None);
    const bool severe_hazard=hazard==planetary::PhysicalHazard::Cold ||
        hazard==planetary::PhysicalHazard::Heat ||
        hazard==planetary::PhysicalHazard::Flood ||
        (region_key==key &&
         (facility->second.ecology_last_hazard==planetary::PhysicalHazard::Cold ||
          facility->second.ecology_last_hazard==planetary::PhysicalHazard::Heat ||
          facility->second.ecology_last_hazard==planetary::PhysicalHazard::Flood));
    if(severe_hazard &&
       grid::angular_proximity_score(*end,*home)>=
       grid::angular_proximity_score(*start,*home))
        return false;

    // Existing housing/utility equipment pads are still ORIGINAL site
    // footprints, never repeated on other cells or at another planet.
    if(region_key==key &&
       grid::within_radius(*end,*home,*radius,32000) &&
       !native_surface_structure_clear(
           x,z,facility->second.housing_phase==2,
           facility->second.utility_phase==2))
        return false;

    const auto closing=[&](const NativePlanetarySurfacePose& other,
                           const PlanetarySiteKey& other_site){
        const auto at=point_from(other,other_site);
        if(!at)return true; // uncertain other actor: fail closed
        return grid::within_radius(*end,*at,*radius,kActorClearanceMm) &&
               grid::angular_proximity_score(*end,*at)<
               grid::angular_proximity_score(*start,*at);
    };
    for(const auto& [id,other]:npcs_){
        if(id==npc_id ||
           other.location!=facility->second.surface_location ||
           !other.surface_pose.last_move_receipt)continue;
        if(closing(other.surface_pose,key))return false;
    }
    if(npc_id){
        const auto p=player_people_.find(owner);
        if(p!=player_people_.end() &&
           p->second.state==PlayerPersonState::AtPlanetarySurface &&
           p->second.home_ship==ship &&
           p->second.planet_body==body &&
           p->second.planet_latitude==std::get<1>(key) &&
           p->second.planet_longitude==std::get<2>(key) &&
           p->second.planet_revision==std::get<3>(key) &&
           p->second.surface_pose.last_move_receipt &&
           closing(p->second.surface_pose,key))
            return false;
    }
    // All rejection paths above left the original world untouched.
    for(const auto& upgrade:upgrades){
        upgrade.pose->latitude_pdeg=upgrade.place.latitude_pdeg;
        upgrade.pose->longitude_pdeg=upgrade.place.longitude_pdeg;
    }
    position->x_mm=x;
    position->y_mm=contact->height_mm;
    position->z_mm=z;
    position->surface_location=facility->second.surface_location;
    position->last_move_tick=tick_;
    position->last_move_receipt=receipt;
    position->latitude_pdeg=end->latitude_pdeg;
    position->longitude_pdeg=end->longitude_pdeg;
    surface_move_receipts_.insert(receipt);
    snapshot_version_=std::max(snapshot_version_,133);
    return true;
}
} // namespace galaxy
