#pragma once
// STATUS-LAW-01: grounded preliminary nuisance case review. This is NOT
// judicial guilt and NOT a permit to seize, arrest, detain, attack or produce
// police ships. The original game does not yet have registered police agencies.
#include "galaxy/ShipHailJournal.hpp"
#include "galaxy/TerritoryJournal.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::ship_comms::review {
enum class Status : std::uint8_t {
    MissingEvidence=0,WrongOwner=1,NoClaim=2,NoDutyReviewer=3,
    AwaitingLawAgency=4
};
struct Assessment {
    Status status=Status::MissingEvidence;
    std::uint64_t complaint_event=0,points=0,reviewer_npc=0;
    PlayerId reported_caller=0,receiving_owner=0;
    EntityId receiving_ship=0,review_colony=0;
    bool credible_nuisance_evidence=false;
    bool formal_law_agency_present=false;
    bool criminal_guilt=false;
    bool arrest_authorized=false;
    bool armed_intercept_authorized=false;
    bool property_seizure_authorized=false;
    bool police_dispatch_authorized=false;
};
// Proof must be an existing immutable detailed abuse receipt or a legitimate
// compact aggregate last_event whose seed is independently verified against
// the initial ten original incident receipts. Client-supplied "crime IDs",
// owner's assertion or naked user/ship IDs are not proof.
inline Assessment assess(const PersistentWorld& world,
    const colony_disposition::TerritoryJournal& territory,
    const ship_comms::Journal& journal,PlayerId authenticated_owner,
    EntityId review_colony,EntityId victim_ship,
    std::uint64_t original_complaint,std::uint64_t named_reviewer) {
    Assessment a{};
    if(!authenticated_owner ||
       authenticated_owner==Simulation::kServerAuthority ||
       !review_colony || !victim_ship || !original_complaint ||
       !named_reviewer)return a;
    const auto* colony=world.world().find(review_colony);
    const auto* victim=world.world().find(victim_ship);
    if(!colony || !victim ||
       colony->type!=EntityType::Ship ||
       victim->type!=EntityType::Ship ||
       !world.world().colony_ship(review_colony) ||
       colony->owner_id!=authenticated_owner ||
       victim->owner_id!=authenticated_owner){
        a.status=Status::WrongOwner;return a;
    }
    a.receiving_owner=authenticated_owner;
    a.review_colony=review_colony;
    a.receiving_ship=victim_ship;
    a.complaint_event=original_complaint;
    journal.verify();
    bool found=false;
    for(const auto& entry:journal.history()){
        if(entry.event_id!=original_complaint ||
           entry.kind!=ship_comms::Kind::Abuse ||
           entry.recipient_ship!=victim_ship ||
           !entry.law_review_eligible)continue;
        a.reported_caller=entry.caller_owner;
        a.points=entry.penalty_points;
        found=true;break;
    }
    if(!found)for(const auto& compact:journal.overflow_history()){
        if(compact.last_event!=original_complaint ||
           compact.recipient_ship!=victim_ship ||
           compact.total_points<3)continue;
        const auto old=journal.history();
        const auto seed=std::find_if(old.begin(),old.end(),
            [&](const ship_comms::Receipt& e){
                return e.event_id==compact.seed_event &&
                       e.kind==ship_comms::Kind::Abuse &&
                       e.recipient_ship==victim_ship;
            });
        if(seed==old.end())continue;
        a.reported_caller=compact.caller_owner;
        a.points=compact.total_points;
        found=true;break;
    }
    if(!found)return a;
    a.credible_nuisance_evidence=true;
    const auto claims=territory.history();
    const bool claim_exists=std::any_of(claims.begin(),claims.end(),
        [&](const colony_disposition::TerritoryEvent& event){
            return event.kind==
                       colony_disposition::TerritoryEventKind::Claim &&
                   event.owner==authenticated_owner &&
                   event.colony==review_colony;
        });
    if(!claim_exists){
        a.status=Status::NoClaim;return a;
    }
    territory.verify();
    const auto& npcs=world.world().npc_records();
    const auto n=npcs.find(named_reviewer);
    if(n==npcs.end() || n->second.home_ship!=review_colony ||
       n->second.location!=review_colony ||
       n->second.transport_id ||
       n->second.role!=NpcRole::Ship ||
       !world.world().npc_available_for_duty(named_reviewer)){
        a.status=Status::NoDutyReviewer;return a;
    }
    a.reviewer_npc=named_reviewer;
    // The physical game has no authoritative registered law-agency
    // capability ledger yet; a claimed jurisdiction and guard do not create
    // police or a court. This is a triaged EVIDENCE REFERRAL only.
    a.status=Status::AwaitingLawAgency;
    return a;
}
} // namespace galaxy::ship_comms::review
