#pragma once
// DISPOSITION-02: signed territorial evidence *narrows* original diplomacy.
// It never confers criminal conviction, arrest or combat authority.
#include "galaxy/TerritoryJournal.hpp"
#include "galaxy/ColonyDispositionCoordinator.hpp"
namespace galaxy::colony_disposition {

struct TerritorialEvaluation {
    Evaluation original{};
    TerritorialStanding territory{};
    npc_response::ThreatAssessment interpreted{};
};
// Original governance and GRSM still decide social posture. Territory policy
// overlays ONLY when current physical position is in a durable local claim.
inline TerritorialEvaluation assess_territorial(
    const PersistentWorld& world,const TerritoryJournal& journal,
    EntityId colony,EntityId foreign,std::uint64_t contact_event) {
    TerritorialEvaluation e{};
    e.original=assess(world,colony,contact_event);
    if(e.original.decision.target_ship!=foreign)
        throw std::invalid_argument("territorial disposition target mismatch");
    e.territory=journal.standing(world,colony,foreign);
    e.interpreted=e.original.threat;
    if(!e.territory.in_claimed_region)return e;
    if(e.territory.invited){
        // Invitation waives territorial trespass, not old diplomatic
        // grievances, real crimes, or the colony's subjective mistrust.
        if(e.interpreted.basis==npc_response::Basis::Territorial){
            e.interpreted.intent=npc_response::ThreatIntent::Ignore;
            e.interpreted.basis=npc_response::Basis::None;
        }
    }else if(e.territory.standing==npc_response::VisitorStanding::Expelled){
        e.interpreted.intent=npc_response::ThreatIntent::Intercept;
        e.interpreted.basis=npc_response::Basis::Territorial;
    }else if(e.territory.trespass_recorded){
        e.interpreted.intent=npc_response::ThreatIntent::Intercept;
        e.interpreted.basis=npc_response::Basis::Territorial;
    }else if(e.territory.warned){
        e.interpreted.intent=npc_response::ThreatIntent::WarnLeave;
        e.interpreted.basis=npc_response::Basis::Territorial;
    }else if(e.interpreted.intent==npc_response::ThreatIntent::Ignore){
        e.interpreted.intent=npc_response::ThreatIntent::Hail;
        e.interpreted.basis=npc_response::Basis::Territorial;
    }
    if(e.interpreted.legal_crime_finding || e.interpreted.can_attack ||
       e.interpreted.can_arrest || e.interpreted.can_seize_goods)
        throw std::logic_error("trespass inference escalated into force");
    return e;
}
// The existing patrol's fresh named-NPC witness is independently checked
// by NPC-RESP-01; the territory journal is evidence for WHY it is ordered.
// No direct combat, confiscation, warrants or prisoner records are generated.
inline npc_response::Status dispatch_territorial_patrol(
    PersistentWorld& world,const TerritoryJournal& journal,
    EntityId colony,EntityId foreign,std::uint64_t observer_npc,
    std::uint64_t flight_order,std::uint64_t pilot_npc=0,
    FailPoint fail=FailPoint::None) {
    const auto s=journal.standing(world,colony,foreign);
    if(!s.in_claimed_region || s.invited ||
       (!s.warned && !s.trespass_recorded))
        return npc_response::Status::NoAuthority;
    const auto* source=world.world().find(colony);
    if(!source || !source->owner_id)return npc_response::Status::WrongOwner;
    const auto contacts=world.local_colony_watch_contacts(colony,observer_npc);
    const auto found=std::find_if(contacts.begin(),contacts.end(),
        [&](const colony_defense::Contact& c){
            return c.source==colony && c.foreign_ship==foreign &&
                   c.world_tick==world.world().tick();
        });
    if(found==contacts.end())return npc_response::Status::NoAuthority;
    return npc_response::dispatch_observed_patrol(
        world,source->owner_id,colony,pilot_npc,*found,flight_order,fail);
}
} // namespace galaxy::colony_disposition
