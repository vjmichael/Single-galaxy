#pragma once
// DISPOSITION-03B: host-facing territorial hail policy and evidence-bound
// aggravation. This is not a network transport, authenticated session,
// sentencing court, weapons authorization, or replacement crime ledger.
#include "galaxy/TerritorialEnforcement.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace galaxy::colony_disposition {

enum class HailChoice : std::uint8_t { Acknowledge=1, Refuse=2 };
enum class HailAction : std::uint8_t {
    None=0, AutomaticHail=1, ManualHailAvailable=2,
    AwaitingVisitorResponse=3, GraceRunning=4, EnforcementPending=5
};
struct PlayerHailPreferences {
    // The player may opt out of *outbound* automatic hails. NPCs can still
    // issue warnings; disabling this does not erase local law or evidence.
    bool auto_hail_on_entry=true;
};
struct HailReadModel {
    HailAction action=HailAction::None;
    EnforcementView enforcement{};
    std::uint64_t notice_id=0;
    bool actual_receipt_proven=false;
};
// This read model is safe to display, but callers MUST filter it using the
// authenticated session owner. The host controls when a new boundary entry
// is detected and may send outbound hail via its real communications adapter.
inline HailReadModel player_territorial_hail(
    const PersistentWorld& world,const TerritoryJournal& journal,
    PlayerId authenticated_owner,EntityId colony,EntityId visiting_ship,
    PlayerHailPreferences preferences={}) {
    HailReadModel result{};
    const auto* ship=world.world().find(visiting_ship);
    if(!authenticated_owner || !ship || ship->owner_id!=authenticated_owner)
        return result;
    result.enforcement=enforcement_view(world,journal,colony,visiting_ship);
    const auto phase=result.enforcement.phase;
    if(phase==EnforcementPhase::Outside || phase==EnforcementPhase::Invited)
        return result;
    result.actual_receipt_proven=result.enforcement.standing.notice_received;
    result.notice_id=result.enforcement.standing.warning_event;
    if(phase==EnforcementPhase::NeedsWarning)
        result.action=preferences.auto_hail_on_entry?
            HailAction::AutomaticHail:HailAction::ManualHailAvailable;
    else if(phase==EnforcementPhase::AwaitingReceipt)
        result.action=HailAction::AwaitingVisitorResponse;
    else if(phase==EnforcementPhase::GracePeriod)
        result.action=HailAction::GraceRunning;
    else result.action=HailAction::EnforcementPending;
    return result;
}
// The trusted host must bind session_owner to its authenticated transport
// session; a client-supplied number is never proof of identity. This method
// does not acknowledge on inbox display or automatic outbound hail.
inline bool player_respond_to_hail(
    const PersistentWorld& world,TerritoryJournal& journal,
    PlayerId session_owner,EntityId colony,EntityId ship,
    std::uint64_t notice_id,std::uint64_t response_id,HailChoice choice,
    TerritoryFailPoint fail=TerritoryFailPoint::None) {
    if(choice!=HailChoice::Acknowledge && choice!=HailChoice::Refuse)
        return false;
    const auto read=player_territorial_hail(
        world,journal,session_owner,colony,ship);
    if(read.action!=HailAction::AwaitingVisitorResponse ||
       read.notice_id!=notice_id || !response_id || response_id==notice_id)
        return false;
    return journal.respond_to_notice(world,session_owner,colony,ship,
        notice_id,response_id,choice==HailChoice::Refuse,fail);
}

// The authoritative crime/custody engine must pass a VERIFIED offense, its
// original event identity and tick, and its lawful baseline sanction units.
// A territorial warning, refusal, trespass or angry NPC is not a crime.
// This is a recommendation to the original sentencing policy, not an effect.
struct VerifiedOffense {
    std::uint64_t offense_id=0;
    std::uint64_t occurred_tick=0;
    PlayerId offender=0;
    EntityId offender_ship=0;
    EntityId jurisdiction_colony=0;
    bool authoritative_crime_finding=false;
};
struct AggravationRecommendation {
    std::uint32_t numerator=1,denominator=1;
    std::uint64_t notice_event=0,visitor_reply_event=0;
    bool apply=false;
};
inline AggravationRecommendation territorial_hail_aggravation(
    const TerritoryJournal& journal,const VerifiedOffense& offense) {
    AggravationRecommendation result{};
    if(!offense.authoritative_crime_finding || !offense.offense_id ||
       !offense.offender || !offense.offender_ship ||
       !offense.jurisdiction_colony)return result;
    // history() validates the complete immutable event chain. Find the
    // latest visitor-origin reply *before* this offense. The reply must
    // refer to a genuine notice for the same owner, ship and jurisdiction.
    const auto events=journal.history();
    for(const auto& reply:events) {
        if(reply.kind!=TerritoryEventKind::NoticeAcknowledged &&
           reply.kind!=TerritoryEventKind::NoticeRefused)continue;
        if(reply.world_tick>=offense.occurred_tick ||
           reply.visitor_owner!=offense.offender ||
           reply.visitor_ship!=offense.offender_ship ||
           reply.colony!=offense.jurisdiction_colony)continue;
        for(const auto& warning:events) {
            if(warning.kind!=TerritoryEventKind::Warning ||
               warning.event_id!=reply.related_event ||
               warning.world_tick>reply.world_tick ||
               warning.colony!=reply.colony ||
               warning.visitor_ship!=reply.visitor_ship ||
               warning.visitor_owner!=reply.visitor_owner)continue;
            if(!result.apply || reply.world_tick>=
                ([&] { for(const auto& e:events)
                    if(e.event_id==result.visitor_reply_event)return e.world_tick;
                    return std::uint64_t{0}; })()) {
                result.apply=true;
                result.notice_event=warning.event_id;
                result.visitor_reply_event=reply.event_id;
                result.numerator=3;
                result.denominator=2;
            }
        }
    }
    return result;
}
// Avoid overflow, rounding surprises and arbitrary stacking. The existing
// legal policy decides whether and to which capped sanction category this
// recommendation applies. It MUST NOT scale damage, arrest authority,
// custody duration without review, or multiply several acknowledgements.
inline std::uint64_t suggested_sanction_units(
    std::uint64_t baseline,const AggravationRecommendation& factor,
    std::uint64_t lawful_cap) {
    if(!factor.apply)return baseline<lawful_cap?baseline:lawful_cap;
    const auto capped=baseline<lawful_cap?baseline:lawful_cap;
    const auto half=capped/2+(capped%2); // round 1.5x up
    if(capped>lawful_cap-half)return lawful_cap;
    return capped+half;
}
} // namespace galaxy::colony_disposition
