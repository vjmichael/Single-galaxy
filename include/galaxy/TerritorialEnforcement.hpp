#pragma once
// DISPOSITION-03: one read-model and coordinator over DISPOSITION-02 receipts.
// No second flight, legal verdict, combat authority, custody or mailbox.
#include "galaxy/ColonyTerritoryResponse.hpp"
#include <algorithm>
#include <cstdint>

namespace galaxy::colony_disposition {

enum class EnforcementPhase : std::uint8_t {
    Outside=0,Invited=1,NeedsWarning=2,AwaitingReceipt=3,
    GracePeriod=4,MayRecordTrespass=5,TrespassRecorded=6,
    Expelled=7
};
struct EnforcementView {
    EnforcementPhase phase=EnforcementPhase::Outside;
    TerritorialStanding standing{};
    std::uint64_t ticks_remaining=0;
    bool allows_damage=false,allows_arrest=false,allows_seizure=false;
};

inline EnforcementView enforcement_view(const PersistentWorld& world,
    const TerritoryJournal& journal,EntityId colony,EntityId ship) {
    EnforcementView view{};
    view.standing=journal.standing(world,colony,ship);
    const auto& s=view.standing;
    if(!s.in_claimed_region)return view;
    if(s.invited){view.phase=EnforcementPhase::Invited;return view;}
    if(s.standing==npc_response::VisitorStanding::Expelled){
        view.phase=EnforcementPhase::Expelled;return view;
    }
    if(s.trespass_recorded){
        view.phase=EnforcementPhase::TrespassRecorded;return view;
    }
    if(!s.warned){view.phase=EnforcementPhase::NeedsWarning;return view;}
    if(!s.notice_received){
        view.phase=EnforcementPhase::AwaitingReceipt;return view;
    }
    if(world.world().tick()<s.grace_until){
        view.phase=EnforcementPhase::GracePeriod;
        view.ticks_remaining=s.grace_until-world.world().tick();
        return view;
    }
    view.phase=EnforcementPhase::MayRecordTrespass;
    return view;
}

struct EnforcementProgress {
    EnforcementPhase initial=EnforcementPhase::Outside;
    EnforcementPhase final=EnforcementPhase::Outside;
    bool warning_recorded=false,trespass_recorded=false;
    npc_response::Status patrol_status=npc_response::Status::Refused;
};
// Called by the host on its normal bounded NPC contact-work cadence.
// Every attempt uses the original real colony owner, physical named NPC
// observation and original flight registry. The host must own unique IDs;
// refused/repeated calls can never silently rewrite existing evidence.
inline EnforcementProgress supervise_territorial_contact(
    PersistentWorld& world,TerritoryJournal& journal,
    EntityId colony,EntityId foreign,std::uint64_t observer_npc,
    std::uint64_t warning_id,std::uint64_t trespass_id,
    std::uint64_t patrol_flight_id,std::uint64_t grace_ticks,
    std::uint64_t pilot_npc=0, TerritoryFailPoint fail=TerritoryFailPoint::None) {
    EnforcementProgress result{};
    const auto start=enforcement_view(world,journal,colony,foreign);
    result.initial=start.phase;
    result.final=start.phase;
    if(start.phase==EnforcementPhase::Outside ||
       start.phase==EnforcementPhase::Invited ||
       start.phase==EnforcementPhase::Expelled ||
       start.phase==EnforcementPhase::TrespassRecorded)return result;
    const auto* owner=world.world().find(colony);
    const auto* target=world.world().find(foreign);
    if(!owner || !target || !owner->owner_id || !target->owner_id ||
       owner->owner_id==target->owner_id)return result;
    // Require a fresh original named-NPC watch before any new step.
    const auto contacts=world.local_colony_watch_contacts(colony,observer_npc);
    const auto observed=std::find_if(contacts.begin(),contacts.end(),
        [&](const colony_defense::Contact& c){
            return c.source==colony && c.foreign_ship==foreign &&
                   c.foreign_owner==target->owner_id &&
                   c.observer_npc==observer_npc &&
                   c.world_tick==world.world().tick();
        });
    if(observed==contacts.end())return result;
    if(start.phase==EnforcementPhase::NeedsWarning) {
        result.warning_recorded=journal.warn(world,owner->owner_id,
            colony,foreign,observer_npc,warning_id,grace_ticks,fail);
    } else if(start.phase==EnforcementPhase::MayRecordTrespass) {
        result.trespass_recorded=journal.record_trespass(
            world,owner->owner_id,colony,foreign,observer_npc,
            trespass_id,fail);
    }
    result.final=enforcement_view(world,journal,colony,foreign).phase;
    // A warning can justify a *nonviolent* physical hail; a trespass
    // can justify intercepting. Both use the SAME existing flight engine.
    if((result.warning_recorded || result.trespass_recorded) &&
       patrol_flight_id)
        result.patrol_status=dispatch_territorial_patrol(
            world,journal,colony,foreign,observer_npc,
            patrol_flight_id,pilot_npc);
    return result;
}
} // namespace galaxy::colony_disposition
