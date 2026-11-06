#pragma once
// STATUS-COMMS-01 / STATUS-NPC-01: one original-world supervised NPC response
// to real received direct hails. No owner forged by HTTP, no second simulation
// and no automated reply for the local human owner. Future multiplayer host
// must supply the actual authenticated human-owner set/ NPC-controller registry
// rather than assuming all foreign owners are NPCs.
#include "galaxy/ShipHailJournal.hpp"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

namespace galaxy::npc_hail {
enum class Policy : std::uint8_t {Silent=0,Identify=1,Decline=2};
struct Pulse {
    std::uint64_t tick=0;
    std::uint32_t inspected=0,answered=0,identified=0,declined=0,skipped=0;
    std::uint64_t last_request=0,last_receipt=0;
};
inline constexpr std::uint32_t kMaxIncomingInspectedPerTick=32;
inline constexpr std::uint32_t kMaxRepliesPerTick=4;

// Governance is a *real durable policy*, not something derived from a
// contact's apparent silhouette or a user's guessed foreign owner.
inline Policy response_policy(
    const colony_conflict::GovernanceEpoch& epoch) noexcept {
    if(!epoch.event_id || !epoch.leader_id)return Policy::Silent;
    switch(epoch.agenda){
    case colony_conflict::Agenda::Cooperative: return Policy::Identify;
    case colony_conflict::Agenda::Isolationist: return Policy::Decline;
    case colony_conflict::Agenda::Expansionist: return Policy::Decline;
    case colony_conflict::Agenda::Predatory: return Policy::Silent;
    }
    return Policy::Silent;
}
inline bool actual_on_duty_operator(const Simulation& sim,
    EntityId ship,std::uint64_t leader) {
    if(!leader)return false;
    const auto& records=sim.npc_records();
    const auto it=records.find(leader);
    return it!=records.end() && it->second.home_ship==ship &&
        it->second.location==ship && !it->second.transport_id &&
        it->second.role==NpcRole::Ship &&
        sim.npc_available_for_duty(leader);
}

// The local desktop relay currently authenticates a *single* human owner,
// not a fleet of real player accounts. Only during that known one-owner
// mode may a foreign colony with its own original named NPC leader answer
// automatically. No NPC crew or identity is generated for the purpose.
inline Pulse supervise(PersistentWorld& world,ship_comms::Journal& comms,
    PlayerId sole_human_owner,std::uint64_t& next_receipt) {
    Pulse out{};
    out.tick=world.world().tick();
    if(!sole_human_owner ||
       sole_human_owner==Simulation::kServerAuthority)
        return out;
    std::vector<EntityId> actual_colonies;
    for(const auto& [id,entity]:world.world().entities())
        if(entity.type==EntityType::Ship &&
           entity.owner_id && entity.owner_id!=sole_human_owner &&
           world.world().colony_ship(id))
            actual_colonies.push_back(id);
    for(const auto colony:actual_colonies){
        if(out.inspected>=kMaxIncomingInspectedPerTick ||
           out.answered>=kMaxRepliesPerTick)break;
        const auto* original=world.world().find(colony);
        if(!original || original->type!=EntityType::Ship ||
           !original->owner_id)continue;
        const auto owner=original->owner_id;
        colony_conflict::GovernanceEpoch governance{};
        try{governance=world.colony_governance(colony);}
        catch(const std::exception&){++out.skipped;continue;}
        const auto policy=response_policy(governance);
        if(policy==Policy::Silent ||
           !actual_on_duty_operator(world.world(),colony,
                                    governance.leader_id)){
            ++out.skipped;continue;
        }
        const auto requests=comms.pending_for(world,owner,colony);
        for(const auto& call:requests){
            if(out.inspected++>=kMaxIncomingInspectedPerTick ||
               out.answered>=kMaxRepliesPerTick)break;
            if(!world.world().find(call.caller_ship) ||
               !world.world().same_local_frame(
                   colony,call.caller_ship) ||
               next_receipt>=std::uint64_t(
                   std::numeric_limits<sqlite3_int64>::max()-1024)){
                ++out.skipped;continue;
            }
            // Journal::reply separately verifies this exact original Call,
            // actual ship controller and pending time window under SQLite.
            const auto result=comms.reply(world,owner,colony,
                call.request_id,++next_receipt,
                policy==Policy::Identify);
            if(result.status==ship_comms::Status::Identified ||
               result.status==ship_comms::Status::Declined){
                ++out.answered;
                if(policy==Policy::Identify)++out.identified;
                else ++out.declined;
                out.last_request=call.request_id;
                out.last_receipt=result.event_id;
            }else ++out.skipped;
        }
    }
    return out;
}
} // namespace galaxy::npc_hail
