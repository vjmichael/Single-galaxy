#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <limits>

namespace galaxy {
// DOCK-03A: host-only temporary SHORE DUTY for a named original crew member.
// No new NPC, voyage, cargo or residence is created. The worker remains on
// this real ship's finite CREW ROSTER (crew is a contract/headcount), but their
// authoritative physical location moves between a docked hull and its station.
// A separate native station receipt is committed with the world snapshot.
bool Simulation::npc_available_for_shore_duty(std::uint64_t npc_id,
                                                EntityId station) const {
    const auto it=npcs_.find(npc_id);
    if(it==npcs_.end() || !station)return false;
    const auto& person=it->second;
    if(person.role!=NpcRole::Ship || !person.home_ship ||
       person.location!=station || person.transport_id ||
       person.mentor_id ||
       (person.assigned_site && person.assigned_site!=person.home_ship))
        return false;
    const auto* home=find(person.home_ship);
    if(!home || home->type!=EntityType::Ship ||
       home->dock_state!=DockState::Docked ||
       home->dock_station!=station)return false;
    for(const auto& [key,sh]:planetary_shuttles_){
        (void)key;
        if(sh.pilot_npc==npc_id ||
           std::binary_search(sh.passenger_ids.begin(),sh.passenger_ids.end(),npc_id) ||
           std::binary_search(sh.disembarked_ids.begin(),sh.disembarked_ids.end(),npc_id))
            return false;
    }
    for(const auto& [id,flight]:npc_flight_orders_){
        (void)id;
        if(flight.pilot_npc==npc_id &&
           (flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit))return false;
    }
    const auto health=npc_health_status(npc_id);
    return health==NpcHealthStatus::Healthy ||
           health==NpcHealthStatus::Treated ||
           health==NpcHealthStatus::Recovered;
}

// DOCK-05: a passenger ferry is no longer prohibited from moving solely
// because the ORIGINAL manifest is active. Every person must be aboard the
// same real hull with the exact immutable transport ID. Stranding, rescue,
// unrelated pending passenger movement and over-capacity fail closed.
bool Simulation::original_passenger_ferry_safe_to_fly(EntityId ship) const {
    const auto* vessel=find(ship);
    if(!vessel || vessel->type!=EntityType::Ship ||
       !colony_ship(ship) || !life_support(ship))return false;
    std::set<std::uint64_t> seen;
    std::size_t actual=0;
    for(const auto& [id,npc]:npcs_) {
        (void)id;
        if(npc.location==ship && npc.transport_id)++actual;
    }
    if(actual==0 || actual>16)return false;
    for(const auto& [voyage_id,v]:voyages_) {
        if(v.ferry!=ship || (v.status!=1 && v.status!=3))continue;
        if(v.status!=1 || v.passengers.empty() || v.passengers.size()>16 ||
           v.origin==ship)return false;
        for(const auto id:v.passengers) {
            const auto it=npcs_.find(id);
            if(it==npcs_.end() || it->second.location!=ship ||
               it->second.transport_id!=voyage_id)return false;
            if(!seen.insert(id).second)return false;
        }
    }
    return seen.size()==actual;
}

bool Simulation::station_dock_worker_transfer(
    PlayerId owner,EntityId ship,EntityId station,std::uint64_t npc_id,
    bool disembark) {
    if(!owner || owner==kServerAuthority || !ship || !station || !npc_id ||
       tick_==std::numeric_limits<std::uint64_t>::max())return false;
    const auto* vessel=find(ship);
    const auto* berth=find(station);
    const auto crew=colony_ships_.find(ship);
    const auto it=npcs_.find(npc_id);
    if(!vessel || !berth || berth->type!=EntityType::Station ||
       vessel->type!=EntityType::Ship || vessel->owner_id!=owner ||
       vessel->dock_station!=station || vessel->dock_state!=DockState::Docked ||
       vessel->system_id!=berth->system_id || crew==colony_ships_.end() ||
       (disembark && crew->second.crew<=1) || it==npcs_.end())return false;
    auto& npc=it->second;
    if(npc.home_ship!=ship || npc.role!=NpcRole::Ship ||
       npc.transport_id || npc.mentor_id ||
       (npc.assigned_site!=0 && npc.assigned_site!=ship) ||
       npc.location!=(disembark?ship:station))return false;
    // No leave while another physical ship duty, emergency, or existing
    // scheduled undock is in progress. Return does not need duty eligibility.
    for(const auto& [id,flight]:npc_flight_orders_){
        (void)id;
        if(flight.ship==ship &&
           (flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit))return false;
    }
    for(const auto& [scheduled,commands]:commands_){
        if(scheduled<=tick_)continue;
        for(const auto& command:commands)
            if(command.actor==ship &&
              (command.action==Action::RequestUndock ||
               command.action==Action::RequestJump ||
               command.action==Action::HostColonyDeparture))
                return false;
    }
    for(const auto& [id,dependent]:physical_dependents_){
        (void)id;
        if(dependent.guardian_id==npc_id)return false;
    }
    if(disembark){
        std::uint64_t physically_aboard=0,examined=0;
        for(const auto& [id,person]:npcs_){
            (void)id;
            if(++examined>4096)return false;
            if(person.home_ship==ship && person.location==ship &&
               person.role==NpcRole::Ship && !person.transport_id)
                ++physically_aboard;
        }
        // Shore labor cannot strip a real ship of its last accountable crew.
        if(physically_aboard<=1 || !npc_available_for_duty(npc_id))return false;
    }
    npc.location=disembark?station:ship;
    // Legal residence, home ship, unique NPC identity, experience, crew
    // contract and all physical stores are intentionally preserved.
    return true;
}
} // namespace galaxy
