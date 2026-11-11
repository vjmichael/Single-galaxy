#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <limits>

namespace galaxy {
// DOCK-03A: host-only temporary SHORE DUTY for a named original crew member.
// No new NPC, voyage, cargo or residence is created. The worker remains on
// this real ship's finite CREW ROSTER (crew is a contract/headcount), but their
// authoritative physical location moves between a docked hull and its station.
// A separate native station receipt is committed with the world snapshot.
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
