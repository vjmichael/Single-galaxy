#pragma once
// SHIP-STATUS-04: join a REAL same-tick native HackConfuse command receipt
// to a REAL named-NPC observation inside existing territorial jurisdiction.
// This is evidence of an electronic attack, NOT a legal crime conviction.
#include "galaxy/TerritorialHailing.hpp"
#include <algorithm>
#include <cstdint>

namespace galaxy::colony_disposition {
struct ObservedCyberAttack {
    std::uint64_t command_id=0,world_tick=0,observer_npc=0;
    PlayerId attacker_owner=0,victim_owner=0;
    EntityId colony=0,attacking_ship=0,victim_ship=0;
    bool attempted=false,confusion_applied=false;
    // Always false until an independent law/tribunal process finds an offense.
    bool legal_crime_finding=false,arrest_authorized=false;
    bool damage_authorized=false,seizure_authorized=false;
};
// Host should call during the SAME committed world tick as the original
// action and durably register any resulting case in the future law journal.
// Merely observing ships on a later tick cannot backdate witnessing.
inline ObservedCyberAttack contemporaneous_cyber_evidence(
    const PersistentWorld& world,const TerritoryJournal& journal,
    EntityId colony,std::uint64_t observer_npc,std::uint64_t command_id) {
    ObservedCyberAttack report{};
    if(!colony || !observer_npc || !command_id)return report;
    const auto* home=world.world().find(colony);
    if(!home || home->type!=EntityType::Ship || !home->owner_id ||
       !world.world().passenger_needs_enabled())return report;
    const auto& named=world.world().npc_records();
    const auto guard=named.find(observer_npc);
    if(guard==named.end() || guard->second.home_ship!=colony ||
       guard->second.location!=colony || guard->second.transport_id ||
       !world.world().npc_available_for_duty(observer_npc))return report;
    const auto events=world.recent_history(100);
    const auto found=std::find_if(events.begin(),events.end(),
        [&](const HistoricalEvent& e){
            return e.command_id==command_id &&
                e.tick==world.world().tick() &&
                e.action==Action::HackConfuse &&
                (e.result==Outcome::Applied ||
                 e.result==Outcome::AttackResisted);
        });
    if(found==events.end())return report;
    const auto* attacker=world.world().find(found->actor);
    const auto* victim=world.world().find(found->target);
    if(!attacker || !victim ||
       attacker->type!=EntityType::Ship ||
       victim->type!=EntityType::Ship ||
       !attacker->owner_id || !victim->owner_id ||
       attacker->owner_id==home->owner_id ||
       attacker->owner_id==victim->owner_id ||
       attacker->system_id!=home->system_id ||
       !world.world().same_local_frame(colony,found->actor))
        return report;
    const auto territory=journal.standing(world,colony,found->actor);
    const auto victim_territory=journal.standing(world,colony,found->target);
    if(!territory.in_claimed_region ||
       !victim_territory.in_claimed_region)return report;
    const auto contacts=world.local_colony_watch_contacts(colony,observer_npc);
    const auto observed=std::find_if(contacts.begin(),contacts.end(),
        [&](const colony_defense::Contact& c){
            return c.source==colony &&
                   c.foreign_ship==found->actor &&
                   c.foreign_owner==attacker->owner_id &&
                   c.observer_npc==observer_npc &&
                   c.world_tick==found->tick;
        });
    if(observed==contacts.end())return report;
    report.command_id=command_id;
    report.world_tick=found->tick;
    report.observer_npc=observer_npc;
    report.colony=colony;
    report.attacking_ship=found->actor;
    report.victim_ship=found->target;
    report.attacker_owner=attacker->owner_id;
    report.victim_owner=victim->owner_id;
    report.attempted=true;
    report.confusion_applied=found->result==Outcome::Applied;
    return report;
}
} // namespace galaxy::colony_disposition
