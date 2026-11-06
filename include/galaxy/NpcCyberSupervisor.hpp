#pragma once
// SHIP-STATUS-05: host-supervised finite NPC cyber tactics.
// No detached loop, extra combat engine, client trust, implied warrants,
// spawned attackers, or invented NPC crew. One caller per committed world tick.
#include "galaxy/NpcCyberTactics.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace galaxy::npc_cyber {
struct Pulse {
    std::uint64_t tick=0;
    std::uint32_t candidates=0,observations=0,queued=0,refused=0;
    EntityId last_attacking_ship=0,last_target_ship=0;
    std::uint64_t last_command=0;
};
inline constexpr std::uint64_t kSupervisorCadenceTicks=5;
inline constexpr std::uint32_t kMaxCyberOrdersPerPulse=2;
inline constexpr std::uint32_t kMaxObservationsPerPulse=32;
// Reserve [2^49,2^50) for supervisor-only journal/command IDs.
// 128 IDs per committed tick, 4 per physical target attempt, and a
// distinct namespace from the native interactive host command counter.
inline constexpr std::uint64_t kSupervisorIdBase=(std::uint64_t{1}<<49);
inline constexpr std::uint64_t kSupervisorIdsPerTick=128;

// The HOST must call at most once per committed native tick.
// IDs derive from the persistent tick and bounded original ship-order slot,
// never from a volatile host counter that resets on process restart.
inline Pulse supervise(PersistentWorld& world,PlayerId player_owner) {
    Pulse result{};
    result.tick=world.world().tick();
    if(!result.tick || result.tick%kSupervisorCadenceTicks ||
       !player_owner || player_owner==Simulation::kServerAuthority ||
       result.tick>=(kSupervisorIdBase/kSupervisorIdsPerTick))
        return result;
    const auto& simulation=world.world();
    std::uint64_t observed_slot=0;
    for(const auto& [ship_id,ship]:simulation.entities()) {
        if(result.queued>=kMaxCyberOrdersPerPulse ||
           observed_slot>=kMaxObservationsPerPulse)break;
        if(ship.type!=EntityType::Ship || !ship.owner_id ||
           ship.owner_id==player_owner || !simulation.colony_ship(ship_id) ||
           !ship.fitting.intrusion_emitter() ||
           ship.dock_state!=DockState::InSpace ||
           ship.jump_state!=ShipJumpState::Ready ||
           ship.last_hack_tick &&
             result.tick<ship.last_hack_tick+Simulation::kHackCooldownTicks)
            continue;
        const auto slot=ship.fitting.hardpoints[0]==3?0u:1u;
        if(!simulation.hardpoint_effective_on(ship_id,
              static_cast<std::uint8_t>(slot)))continue;
        // A real coalition/political policy must exist. No auto-generated
        // evil faction merely because a foreign ship is nearby.
        colony_conflict::GovernanceEpoch governance{};
        try{governance=world.colony_governance(ship_id);}
        catch(const std::exception&){continue;}
        if(governance.agenda!=colony_conflict::Agenda::Predatory ||
           !governance.leader_id)continue;
        bool already_pending=false;
        for(const auto& [id,order]:simulation.npc_cyber_operators()){
            (void)id;
            if(order.attacking_ship==ship_id &&
               order.admitted_tick==result.tick){
                already_pending=true;break;
            }
        }
        if(already_pending)continue;
        ++result.candidates;
        std::uint64_t operator_npc=0;
        for(const auto& [npc_id,npc]:simulation.npc_records()){
            if(npc.home_ship==ship_id && npc.location==ship_id &&
               !n.transport_id && n.role==NpcRole::Ship &&
               simulation.npc_available_for_duty(npc_id)){
                operator_npc=npc_id;break;
            }
        }
        if(!operator_npc)continue;
        // Original nearby physical sensor with a named, present crew member.
        // Sensor may see a ship without providing a combat permit.
        std::vector<colony_defense::Contact> contacts;
        try{contacts=world.local_colony_watch_contacts(ship_id,operator_npc);}
        catch(const std::exception&){continue;}
        for(const auto& contact:contacts){
            const auto* target=simulation.find(contact.foreign_ship);
            if(!target || target->training_drone ||
               target->type!=EntityType::Ship ||
               target->owner_id==ship.owner_id ||
               !simulation.same_local_frame(ship_id,contact.foreign_ship))
                continue;
            if(observed_slot>=kMaxObservationsPerPulse)return result;
            const auto prefix=kSupervisorIdBase+
                result.tick*kSupervisorIdsPerTick+4*observed_slot++;
            const auto witness=prefix+1,decision=prefix+2,attack=prefix+3;
            try{
                // The native witness and actual governing choice precede the
                // original queued HackConfuse. If any refuses, no attack.
                world.record_colony_watch_contact(
                    witness,ship_id,operator_npc,contact.foreign_ship);
                ++result.observations;
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
                // Host monitoring is best effort. A refused offense is not
                // promoted into a positive native combat outcome.
                ++result.refused;
            }
        }
    }
    return result;
}
} // namespace galaxy::npc_cyber
