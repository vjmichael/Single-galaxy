#include "galaxy/Simulation.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"

// S6-F5: ORIGINAL Simulation's player/NPC positions and source geography.
// PersistentWorld performs the actual original SQLite atomic writer commit.
namespace galaxy {
bool Simulation::planetary_surface_move(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc_id,
        std::int64_t dx_mm,std::int64_t dz_mm,std::uint64_t receipt) {
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
    if(!vessel || vessel->type!=EntityType::Ship ||
       vessel->owner_id!=owner || vessel->hull<=0 ||
       vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       frame==ship_frames_.end() ||
       frame->second.anchor_body!=std::get<0>(key) ||
       !life_support_.count(ship) || std::get<3>(key)!=1 ||
       facility==planetary_sites_.end() ||
       facility->second.owner!=owner ||
       facility->second.source_ship!=ship ||
       facility->second.spaceport_phase!=3 ||
       !facility->second.surface_location ||
       shuttle==planetary_shuttles_.end() ||
       shuttle->second.owner!=owner ||
       shuttle->second.source_ship!=ship)
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
           p->second.planet_body!=std::get<0>(key) ||
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
    if(position->last_move_receipt && position->last_move_tick>=tick_)
        return false;
    const auto x=position->x_mm+dx_mm;
    const auto z=position->z_mm+dz_mm;
    if(!native_surface_structure_clear(
           x,z,facility->second.housing_phase==2,
           facility->second.utility_phase==2))
        return false;
    const auto observed=scan_planetary_geography(
        owner,ship,std::get<0>(key),std::get<1>(key),std::get<2>(key));
    if(!observed || observed->key.revision!=std::get<3>(key))
        return false;
    const auto env=planetary_physical_environment_.find(key);
    const auto contact=original_ground_contact(*observed,
        env==planetary_physical_environment_.end()?nullptr:&env->second,x,z);
    if(!contact)return false;
    const bool severe_hazard=!contact->safe_to_walk ||
       facility->second.ecology_last_hazard==planetary::PhysicalHazard::Cold ||
       facility->second.ecology_last_hazard==planetary::PhysicalHazard::Heat ||
       facility->second.ecology_last_hazard==planetary::PhysicalHazard::Flood;
    // A newly hazardous surface does not arbitrarily strand the original
    // human/NPC: only retreat towards the existing shuttle hatch is allowed.
    // Actual injury/air/thermal consumption is separate native survival work.
    if(severe_hazard){
        const auto old_d=position->x_mm*position->x_mm+
                         position->z_mm*position->z_mm;
        const auto new_d=x*x+z*z;
        if(new_d>=old_d)return false;
    }
    // Native original-person-to-person contact. Legacy original NPCs without
    // a committed foot-position receipt do not gain fabricated coordinates.
    // Already-overlapping characters may only move farther apart.
    const auto closing=[&](const NativePlanetarySurfacePose& other){
        const auto before_x=position->x_mm-other.x_mm;
        const auto before_z=position->z_mm-other.z_mm;
        const auto after_x=x-other.x_mm,after_z=z-other.z_mm;
        const auto old_d=before_x*before_x+before_z*before_z;
        const auto new_d=after_x*after_x+after_z*after_z;
        return new_d<kActorClearanceMm*kActorClearanceMm && new_d<old_d;
    };
    for(const auto& [id,other]:npcs_){
        if(id==npc_id || other.location!=facility->second.surface_location ||
           !other.surface_pose.last_move_receipt)continue;
        if(closing(other.surface_pose))return false;
    }
    if(npc_id){
        const auto player=player_people_.find(owner);
        if(player!=player_people_.end() &&
           player->second.state==PlayerPersonState::AtPlanetarySurface &&
           player->second.home_ship==ship &&
           player->second.planet_body==std::get<0>(key) &&
           player->second.planet_latitude==std::get<1>(key) &&
           player->second.planet_longitude==std::get<2>(key) &&
           player->second.planet_revision==std::get<3>(key) &&
           closing(player->second.surface_pose))
            return false;
    }
    position->x_mm=x;
    position->y_mm=contact->ground_y_mm;
    position->z_mm=z;
    position->last_move_tick=tick_;
    position->last_move_receipt=receipt;
    surface_move_receipts_.insert(receipt);
    snapshot_version_=std::max(snapshot_version_,132);
    return true;
}
} // namespace galaxy
