#include "galaxy/Simulation.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"
#include "galaxy/PlanetaryContinuousSurface.hpp"
#include "galaxy/PlanetaryBodyGravity.hpp"
#include "galaxy/PlanetaryVerticalPhysics.hpp"
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
    // A committed v134 airborne person cannot be snapped into terrain
    // solely by a Godot WASD event. Native gravity/ground contact owns Y.
    if(npc_id==0 && snapshot_version_>=134){
        const auto& sv=player_people_.at(owner).surface_survival;
        if(sv.initialized && !sv.grounded &&
           sv.radial_height_mm<contact->height_mm)return false;
    }
    for(const auto& upgrade:upgrades){
        upgrade.pose->latitude_pdeg=upgrade.place.latitude_pdeg;
        upgrade.pose->longitude_pdeg=upgrade.place.longitude_pdeg;
    }
    position->x_mm=x;
    if(npc_id==0 && snapshot_version_>=134 &&
       player_people_.at(owner).surface_survival.initialized){
        auto& sv=player_people_.at(owner).surface_survival;
        if(sv.grounded)sv.radial_height_mm=contact->height_mm;
        position->y_mm=sv.radial_height_mm;
    }else position->y_mm=contact->height_mm;
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

namespace {
// S6-F5-C trusted original-person/site admission. No client coordinates,
// health assessment, gravity, oxygen or force is supplied by the caller.
struct PlayerSurfaceSource {
    PlayerPerson* person=nullptr;
    PlanetarySiteFacility* site=nullptr;
    std::int64_t gravity_mm_s2=0;
    planetary::continuous_surface::Ground ground{};
    planetary::PhysicalHazard hazard=planetary::PhysicalHazard::None;
};
}

// v134: every generated celestial bulk-mass input must match the original
// deterministic physical catalogue. This helper is called after original
// site/ship/player custody checks. No changes on denied requests.
bool Simulation::planetary_surface_vertical_action(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool jump,std::uint64_t receipt) {
    namespace grid=planetary::continuous_surface;
    if(!owner || owner==kServerAuthority || !ship || !receipt ||
       receipt>=(std::uint64_t{1}<<60) ||
       surface_move_receipts_.size()>=100000 ||
       surface_move_receipts_.count(receipt) ||
       player_planet_receipts_.count(receipt) ||
       shuttle_flight_request_ids_.count(receipt) ||
       planetary_family_booking_ids_.count(receipt) ||
       submitted_ids_.count(receipt) || voyages_.count(receipt) ||
       pending_celestial_transits_.count(receipt) ||
       completed_celestial_transits_.count(receipt))
        return false;
    auto p=player_people_.find(owner);
    auto f=planetary_sites_.find(key);
    auto sh=planetary_shuttles_.find(key);
    const auto* vessel=find(ship);
    const auto frame=ship_frames_.find(ship);
    const auto body=std::get<0>(key);
    const auto radius=original_walkable_body_radius(body);
    if(p==player_people_.end() || f==planetary_sites_.end() ||
       sh==planetary_shuttles_.end() || !vessel || !radius ||
       vessel->type!=EntityType::Ship || vessel->owner_id!=owner ||
       vessel->hull<=0 || vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       p->second.state!=PlayerPersonState::AtPlanetarySurface ||
       p->second.home_ship!=ship || p->second.physical_ship ||
       p->second.planet_body!=body ||
       p->second.planet_latitude!=std::get<1>(key) ||
       p->second.planet_longitude!=std::get<2>(key) ||
       p->second.planet_revision!=std::get<3>(key) ||
       !p->second.planet_descent_receipt ||
       sh->second.flight_phase!=2 ||
       sh->second.descent_receipt!=p->second.planet_descent_receipt ||
       f->second.owner!=owner || f->second.source_ship!=ship ||
       !f->second.surface_location || f->second.spaceport_phase!=3)
        return false;
    const auto system=physical_celestial_systems_.find(vessel->system_id);
    if(system==physical_celestial_systems_.end())return false;
    const auto original=std::find_if(system->second.begin(),system->second.end(),
        [body](const PhysicalCelestialBody& b){return b.id==body;});
    if(original==system->second.end() ||
       (original->kind!=CelestialKind::Planet &&
        original->kind!=CelestialKind::Moon))return false;
    // A legacy v133 can only receive mass from the canonical generator,
    // not from the visual tile or arbitrary newly entered numbers.
    const auto expected_mass=original->physical_mass_1e18_kg;
    if(snapshot_version_>=134 && !expected_mass)return false;
    // If this legacy world has not yet been upgraded, perform only the
    // exact original-astronomy source comparison at the final admission gate.
    if(!establish_original_surface_gravity())return false;
    const auto g=planetary::gravity::surface_gravity_mm_s2(
        original->physical_radius_mm,original->physical_mass_1e18_kg);
    if(!g)return false;
    auto& person=p->second;
    auto& sv=person.surface_survival;
    if(sv.injury_points>=100)return false;
    const auto home=grid::cell_center({std::get<1>(key),std::get<2>(key)});
    if(!home)return false;
    const auto pos=person.surface_pose.last_move_receipt?
        grid::GeoPoint{person.surface_pose.latitude_pdeg,
                       person.surface_pose.longitude_pdeg}:*home;
    const auto source=[&](grid::SourceCell cell)->std::optional<grid::Sample> {
        const auto observation=scan_planetary_geography(
            owner,ship,body,cell.latitude,cell.longitude);
        if(!observation || observation->key.body!=body ||
           observation->key.revision!=std::get<3>(key))return std::nullopt;
        return grid::Sample{observation->landscape.region.elevation,
                            observation->landscape.region.surface_water};
    };
    const auto ground=grid::source_ground(pos,source);
    if(!ground || ground->ocean || !grid::canonical(pos))return false;
    if(!sv.initialized){
        sv.radial_height_mm=ground->height_mm;
        sv.radial_speed_mm_s=0;
        sv.grounded=true;
        sv.initialized=true;
    }
    if(jump){
        if(!sv.grounded || sv.radial_height_mm!=ground->height_mm ||
           sv.injury_points>=90)return false;
        sv.radial_speed_mm_s=planetary::vertical::kJumpSpeedMmS;
        sv.grounded=false;
    }
    // A newly initialized hatch position obtains an original one-use receipt
    // and canonical source identity; this is NOT a free player teleport.
    if(!person.surface_pose.last_move_receipt){
        person.surface_pose.surface_location=f->second.surface_location;
        person.surface_pose.latitude_pdeg=pos.latitude_pdeg;
        person.surface_pose.longitude_pdeg=pos.longitude_pdeg;
        person.surface_pose.last_move_tick=tick_;
        person.surface_pose.last_move_receipt=receipt;
        person.surface_pose.x_mm=0;
        person.surface_pose.z_mm=0;
    }
    person.surface_pose.y_mm=sv.radial_height_mm;
    sv.last_vertical_tick=tick_;
    person.last_update_tick=tick_;
    surface_move_receipts_.insert(receipt);
    snapshot_version_=std::max(snapshot_version_,134);
    return true;
}

// No fabricated suit or atmospheric breathability: these are finite whole
// kilograms of ORIGINAL site oxygen carried by the actual original person.
// Each use must be an explicit, same-site host-authorized source transaction.
bool Simulation::planetary_surface_oxygen_action(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool return_to_site,std::uint64_t receipt) {
    if(!owner || owner==kServerAuthority || !ship || !receipt ||
       receipt>=(std::uint64_t{1}<<60) ||
       surface_move_receipts_.size()>=100000 ||
       surface_move_receipts_.count(receipt) ||
       player_planet_receipts_.count(receipt) ||
       shuttle_flight_request_ids_.count(receipt) ||
       planetary_family_booking_ids_.count(receipt) ||
       submitted_ids_.count(receipt) || voyages_.count(receipt) ||
       pending_celestial_transits_.count(receipt) ||
       completed_celestial_transits_.count(receipt))return false;
    const auto it=player_people_.find(owner);
    const auto site=planetary_sites_.find(key);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto* hull=find(ship);
    const auto frame=ship_frames_.find(ship);
    if(it==player_people_.end() || site==planetary_sites_.end() ||
       shuttle==planetary_shuttles_.end() || !hull ||
       hull->type!=EntityType::Ship || hull->owner_id!=owner ||
       hull->hull<=0 || hull->jump_state!=ShipJumpState::Ready ||
       hull->dock_state!=DockState::InSpace ||
       frame==ship_frames_.end() ||
       frame->second.anchor_body!=std::get<0>(key) ||
       it->second.state!=PlayerPersonState::AtPlanetarySurface ||
       it->second.home_ship!=ship || it->second.planet_body!=std::get<0>(key) ||
       it->second.planet_latitude!=std::get<1>(key) ||
       it->second.planet_longitude!=std::get<2>(key) ||
       it->second.planet_revision!=std::get<3>(key) ||
       !it->second.planet_descent_receipt ||
       shuttle->second.flight_phase!=2 ||
       shuttle->second.descent_receipt!=it->second.planet_descent_receipt ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       !near_original_shuttle_hatch(it->second.surface_pose,key) ||
       (it->second.surface_survival.initialized &&
        !it->second.surface_survival.grounded) ||
       (!return_to_site && it->second.surface_survival.injury_points>=100))
        return false;
    auto& p=it->second;
    auto& sv=p.surface_survival;
    const bool partially_used=sv.oxygen_consumption_ticks>0;
    const auto oxygen_return=sv.oxygen_kg-(partially_used?1:0);
    const auto co2_return=sv.exhaled_co2_kg+(partially_used?1:0);
    if(return_to_site){
        if(!sv.oxygen_kg && !sv.exhaled_co2_kg)return false;
        if(partially_used && sv.oxygen_kg<1)return false;
        // A partially used whole-kg allocation is conservatively expended
        // when returned, so cycling CHARGE/RETURN cannot yield free oxygen.
        if(site->second.oxygen_kg>100000000-oxygen_return ||
           site->second.co2_kg>100000000-co2_return)return false;
    }else if(site->second.oxygen_kg<1 ||
             sv.oxygen_kg+sv.exhaled_co2_kg>=4)return false;
    if(!establish_original_surface_gravity())return false;
    if(return_to_site){
        site->second.oxygen_kg+=oxygen_return;
        site->second.co2_kg+=co2_return;
        sv.oxygen_kg=0;
        sv.exhaled_co2_kg=0;
        sv.oxygen_consumption_ticks=0;
    }else{
        --site->second.oxygen_kg;
        ++sv.oxygen_kg;
    }
    surface_move_receipts_.insert(receipt);
    p.last_update_tick=tick_;
    snapshot_version_=std::max(snapshot_version_,134);
    return true;
}

// Called once per ACTUAL Simulation::step (game-time 100 ms), never per
// Godot render frame or client claimed delta. Physics follows the original
// surveyed geometry; medical injury is an original PlayerPerson fact.
void Simulation::process_original_surface_survival() {
    namespace grid=planetary::continuous_surface;
    for(auto& [owner,p]:player_people_){
        if(p.state!=PlayerPersonState::AtPlanetarySurface ||
           !p.surface_survival.initialized)continue;
        auto& sv=p.surface_survival;
        if(sv.last_vertical_tick>=tick_)continue;
        const PlanetarySiteKey key{p.planet_body,p.planet_latitude,
                                  p.planet_longitude,p.planet_revision};
        const auto site=planetary_sites_.find(key);
        const auto* vessel=find(p.home_ship);
        if(site==planetary_sites_.end() || !vessel ||
           vessel->owner_id!=owner || site->second.owner!=owner ||
           site->second.source_ship!=p.home_ship ||
           p.surface_pose.surface_location!=site->second.surface_location)
            continue; // uncertain source -> no invented physics/health
        const auto system=physical_celestial_systems_.find(vessel->system_id);
        if(system==physical_celestial_systems_.end())continue;
        const auto source_body=std::find_if(system->second.begin(),system->second.end(),
            [&p](const PhysicalCelestialBody& b){return b.id==p.planet_body;});
        if(source_body==system->second.end())continue;
        const auto g=planetary::gravity::surface_gravity_mm_s2(
            source_body->physical_radius_mm,
            source_body->physical_mass_1e18_kg);
        if(!g)continue;
        const grid::GeoPoint point{
            p.surface_pose.latitude_pdeg,p.surface_pose.longitude_pdeg};
        const auto source=[&](grid::SourceCell cell)->std::optional<grid::Sample>{
            const auto ground=scan_planetary_geography(owner,p.home_ship,
                p.planet_body,cell.latitude,cell.longitude);
            if(!ground || ground->key.body!=p.planet_body ||
               ground->key.revision!=p.planet_revision)return std::nullopt;
            return grid::Sample{ground->landscape.region.elevation,
                                ground->landscape.region.surface_water};
        };
        const auto ground=grid::source_ground(point,source);
        if(!ground || ground->ocean)continue;
        const PlanetarySiteKey region{p.planet_body,ground->cell.latitude,
            ground->cell.longitude,p.planet_revision};
        const auto physical=planetary_physical_environment_.find(region);
        auto hazard=physical==planetary_physical_environment_.end()?
            planetary::PhysicalHazard::None:physical->second.hazard;
        if(region==key && hazard==planetary::PhysicalHazard::None)
            hazard=site->second.ecology_last_hazard;
        planetary::vertical::Source env{};
        env.ground_height_mm=ground->height_mm;
        env.gravity_mm_s2=*g;
        env.hazard=hazard;
        env.evaluate_oxygen=false; // original whole-kg ledger below, NOT assumed breathable
        const planetary::vertical::State state{
            sv.radial_height_mm,sv.radial_speed_mm_s,0,0,sv.grounded};
        const auto physics=planetary::vertical::advance(state,env,100,false);
        if(!physics)continue;
        sv.radial_height_mm=physics->next.height_mm;
        sv.radial_speed_mm_s=physics->next.radial_speed_mm_s;
        sv.grounded=physics->next.grounded;
        p.surface_pose.y_mm=sv.radial_height_mm;
        sv.last_vertical_tick=tick_;
        if(physics->impact &&
           physics->impact_speed_mm_s>=planetary::vertical::kHardImpactMmS)
            sv.injury_points=std::min(100U,sv.injury_points+
                std::uint32_t((physics->impact_speed_mm_s-
                    planetary::vertical::kHardImpactMmS)/1000+10));
        // No atmosphere survey exists yet, so missing carried oxygen is a
        // source-uncertainty exposure flag, NOT fabricated hypoxia damage.
        if(sv.oxygen_kg>0){
            ++sv.oxygen_consumption_ticks;
            if(sv.oxygen_consumption_ticks>=600){
                sv.oxygen_consumption_ticks=0;
                --sv.oxygen_kg;
                ++sv.exhaled_co2_kg; // whole-kg conservation, retained on original player
            }
        }else if(sv.unverified_air_exposure_ms<1000000000000ULL-100)
            sv.unverified_air_exposure_ms+=100;
        // Injury applies only to ACTUAL committed original environmental
        // hazards and observed impacts. A complete protective suit is NOT
        // inferred from the ship life-support tank or from Godot visuals.
        if(hazard==planetary::PhysicalHazard::Heat ||
           hazard==planetary::PhysicalHazard::Cold ||
           hazard==planetary::PhysicalHazard::Flood){
            if(sv.thermal_exposure_ms<1000000000000ULL-100){
                sv.thermal_exposure_ms+=100;
                if(sv.thermal_exposure_ms%1000==0)
                    sv.injury_points=std::min(100U,sv.injury_points+1);
            }
        }
    }
}

} // namespace galaxy
