#pragma once
// SHIP-STATUS-06: cyber tactics share the original NPC flight and physical
// combat world. A threat assessment is NOT a warrant, damage or boarding right.
// All contacts, political decisions, patrols and attacks use durable original
// world receipts. This code never teleports vessels or creates crew/equipment.
#include "galaxy/NpcCyberTactics.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

namespace galaxy::npc_cyber {
struct Pulse {
    std::uint64_t tick=0;
    std::uint32_t candidates=0,observations=0,queued=0,approaches=0,refused=0;
    EntityId last_attacking_ship=0,last_target_ship=0;
    std::uint64_t last_command=0;
    EntityId last_approaching_ship=0,last_approach_target=0;
    std::uint64_t last_flight=0;
};
inline constexpr std::uint64_t kSupervisorCadenceTicks=5;
inline constexpr std::uint32_t kMaxCyberOrdersPerPulse=2;
inline constexpr std::uint32_t kMaxApproachesPerPulse=2;
inline constexpr std::uint32_t kMaxObservationsPerPulse=32;
// Keep the original cyber commands in [2^49,2^50) and tactical flight IDs
// in [2^39,2^40). Original flight IDs must be <2^40. No volatile host nonce.
inline constexpr std::uint64_t kSupervisorIdBase=(std::uint64_t{1}<<49);
inline constexpr std::uint64_t kSupervisorIdsPerTick=128;
inline constexpr std::uint64_t kTacticalFlightIdBase=(std::uint64_t{1}<<39);
inline constexpr std::uint64_t kTacticalFlightsPerTick=32;
// A watched ship farther than this should be approached by the ORIGINAL
// native patrol autopilot rather than cyber-attacked from a static standoff.
// Watch is independently limited to five km by ColonyDefense.
inline constexpr std::uint64_t kCyberManeuverDistanceSquared=
    std::uint64_t{3000}*3000;

inline bool active_npc_flight(const Simulation& sim,EntityId ship) {
    for(const auto& [id,flight]:sim.npc_flight_orders()){
        (void)id;
        if(flight.ship==ship &&
           (flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit))
            return true;
    }
    return false;
}

// Host calls at most once per committed world tick. Persistent contact and
// command IDs also reject duplicate admission after a crash/restart.
inline Pulse supervise(PersistentWorld& world,PlayerId player_owner) {
    Pulse result{};
    result.tick=world.world().tick();
    if(!result.tick || result.tick%kSupervisorCadenceTicks ||
       !player_owner || player_owner==Simulation::kServerAuthority ||
       result.tick>=(kTacticalFlightIdBase/kTacticalFlightsPerTick) ||
       result.tick>=(kSupervisorIdBase/kSupervisorIdsPerTick))
        return result;

    // PersistentWorld swaps the internal Simulation on every committed write.
    // Never retain iterators/references/pointers to entities across an
    // observation, diplomatic-intent or attack admission commit.
    std::vector<EntityId> ship_ids;
    for(const auto& [id,e]:world.world().entities())
        if(e.type==EntityType::Ship)ship_ids.push_back(id);
    std::uint64_t observed_slot=0;
    for(const auto ship_id:ship_ids) {
        if(result.queued+result.approaches>=
             kMaxCyberOrdersPerPulse ||
           observed_slot>=kMaxObservationsPerPulse)break;
        const auto& sim=world.world();
        const auto* ship=sim.find(ship_id);
        if(!ship || !ship->owner_id || ship->owner_id==player_owner ||
           !sim.colony_ship(ship_id) ||
           !ship->fitting.intrusion_emitter() ||
           ship->dock_state!=DockState::InSpace ||
           ship->jump_state!=ShipJumpState::Ready ||
           (ship->last_hack_tick &&
             result.tick<ship->last_hack_tick+Simulation::kHackCooldownTicks) ||
           active_npc_flight(sim,ship_id))
            continue;
        const auto slot=ship->fitting.hardpoints[0]==3?0u:1u;
        if(!sim.hardpoint_effective_on(
            ship_id,static_cast<std::uint8_t>(slot)))continue;
        colony_conflict::GovernanceEpoch governance{};
        try{governance=world.colony_governance(ship_id);}
        catch(const std::exception&){continue;}
        if(governance.agenda!=colony_conflict::Agenda::Predatory ||
           !governance.leader_id)continue;
        bool already_pending=false;
        for(const auto& [id,order]:sim.npc_cyber_operators()){
            (void)id;
            if(order.attacking_ship==ship_id &&
               order.admitted_tick==result.tick){
                already_pending=true;break;
            }
        }
        if(already_pending)continue;
        ++result.candidates;
        std::uint64_t operator_npc=0;
        for(const auto& [npc_id,npc]:sim.npc_records()){
            if(npc.home_ship==ship_id && npc.location==ship_id &&
               !npc.transport_id && npc.role==NpcRole::Ship &&
               sim.npc_available_for_duty(npc_id)){
                operator_npc=npc_id;break;
            }
        }
        if(!operator_npc)continue;

        std::vector<colony_defense::Contact> contacts;
        try{contacts=world.local_colony_watch_contacts(ship_id,operator_npc);}
        catch(const std::exception&){continue;}
        for(const auto& contact:contacts){
            if(result.queued+result.approaches>=kMaxCyberOrdersPerPulse ||
               observed_slot>=kMaxObservationsPerPulse)break;
            const auto* target=world.world().find(contact.foreign_ship);
            const auto* current=world.world().find(ship_id);
            if(!current || !target || target->training_drone ||
               target->type!=EntityType::Ship ||
               target->owner_id==current->owner_id ||
               !world.world().same_local_frame(ship_id,contact.foreign_ship))
                continue;

            const auto index=observed_slot++;
            const auto prefix=kSupervisorIdBase+
                result.tick*kSupervisorIdsPerTick+4*index;
            const auto witness=prefix+1,decision=prefix+2,attack=prefix+3;
            const auto flight=kTacticalFlightIdBase+
                result.tick*kTacticalFlightsPerTick+index;
            try{
                world.record_colony_watch_contact(
                    witness,ship_id,operator_npc,contact.foreign_ship);
                ++result.observations;
                // Original governance decides WHETHER an offensive operation
                // is appropriate. Neither a sighting nor a generic patrol
                // observation may silently authorize a hostile pursuit.
                const auto policy=colony_disposition::assess(
                    world,ship_id,witness);
                if(policy.decision.posture!=
                   colony_conflict::Posture::PrepareRaid){
                    ++result.refused;continue;
                }
                if(contact.distance_squared>kCyberManeuverDistanceSquared){
                    // Coordinate with FLIGHT-01/03 and NPC-RESP-01. This
                    // records the original diplomatic decision and queues
                    // an actual persistent Patrol flight with a named pilot.
                    // Hacking must wait until that movement has completed
                    // and another fresh original contact is observed.
                    const auto response=
                        colony_disposition::dispatch_witnessed_response(
                            world,ship_id,witness,decision,flight,
                            operator_npc,operator_npc);
                    if(response.status==npc_response::Status::Accepted){
                        ++result.approaches;
                        result.last_approaching_ship=ship_id;
                        result.last_approach_target=contact.foreign_ship;
                        result.last_flight=flight;
                        break;
                    }
                    ++result.refused;
                    continue;
                }
                // In engagement range: the SAME SHIP-STATUS-04 command
                // executor, observer and durable cooldown/resistance logic.
                const auto dispatched=dispatch_governed_hack(
                    world,ship_id,witness,decision,operator_npc,
                    operator_npc,attack);
                if(dispatched.status==DispatchStatus::Queued){
                    ++result.queued;
                    result.last_attacking_ship=ship_id;
                    result.last_target_ship=contact.foreign_ship;
                    result.last_command=attack;
                    break;
                }
                ++result.refused;
            }catch(const std::exception&){
                // No positive native outcome is invented on rejection.
                ++result.refused;
            }
        }
    }
    return result;
}
} // namespace galaxy::npc_cyber
