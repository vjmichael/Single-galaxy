#pragma once
// NPC-RESP-03A: different NPC societies can be hostile for DIFFERENT reasons.
// Pure strategic read model. It MUST NOT authorize damage, arrest or custody.
#include "galaxy/ColonyConflict.hpp"
#include <cstdint>

namespace galaxy::npc_response {
enum class ColonyCharacter : std::uint8_t {
    Cooperative=0, Territorial=1, Criminal=2, Militant=3
};
enum class VisitorStanding : std::uint8_t {
    Unknown=0, Invited=1, Uninvited=2, Expelled=3
};
enum class ThreatIntent : std::uint8_t {
    Ignore=0, Hail=1, WarnLeave=2, Shadow=3, Intercept=4,
    Threaten=5, PrepareAmbush=6
};
enum class Basis : std::uint8_t {
    None=0, Diplomatic=1, Territorial=2, Predatory=3, Defense=4
};
// The facts here are an evaluation context, NOT a trust token.
// Its real issuer, territorial boundary, invitation and expulsion history
// must be checked against durable gameplay events by a future dispatcher.
struct ThreatContext {
    ColonyCharacter colony=ColonyCharacter::Cooperative;
    VisitorStanding standing=VisitorStanding::Unknown;
    colony_conflict::Attitude relation=colony_conflict::Attitude::Neutral;
    bool in_claimed_region=false;
    bool observed_attack=false; // distinct ORIGINAL physical combat evidence
    bool urgent_shortage=false;
    bool independently_verified=false; // reporting only, never authority
};
struct ThreatAssessment {
    ThreatIntent intent=ThreatIntent::Ignore;
    Basis basis=Basis::None;
    bool legal_crime_finding=false;
    bool can_attack=false;
    bool can_arrest=false;
    bool can_seize_goods=false;
};
// Hostility is an NPC intention rather than a moral/legal verdict.
// Even criminal settlements must possess real weapons and have explicit
// native attack receipts; a nonplayer colony is not exempt from physics.
inline ThreatAssessment assess_threat(const ThreatContext& c) noexcept {
    ThreatAssessment out{};
    if(c.observed_attack){
        out.intent=ThreatIntent::Intercept;
        out.basis=Basis::Defense;
        return out;
    }
    if(c.standing==VisitorStanding::Invited){
        if(c.relation==colony_conflict::Attitude::Hostile){
            out.intent=ThreatIntent::Shadow;
            out.basis=Basis::Diplomatic;
        }
        return out;
    }
    if(c.colony==ColonyCharacter::Criminal){
        if(c.in_claimed_region || c.standing==VisitorStanding::Expelled){
            out.intent=ThreatIntent::PrepareAmbush;
            out.basis=Basis::Predatory;
        }else if(c.urgent_shortage){
            out.intent=ThreatIntent::Threaten;
            out.basis=Basis::Predatory;
        }else{
            out.intent=ThreatIntent::Shadow;
            out.basis=Basis::Predatory;
        }
        return out;
    }
    if(c.colony==ColonyCharacter::Territorial ||
       c.colony==ColonyCharacter::Militant){
        if(!c.in_claimed_region &&
           c.standing!=VisitorStanding::Expelled)return out;
        out.basis=Basis::Territorial;
        if(c.standing==VisitorStanding::Expelled)
            out.intent=ThreatIntent::Intercept;
        else if(c.standing==VisitorStanding::Uninvited)
            out.intent=c.colony==ColonyCharacter::Militant?
                ThreatIntent::Intercept:ThreatIntent::WarnLeave;
        else
            out.intent=ThreatIntent::Hail;
        return out;
    }
    if(c.relation==colony_conflict::Attitude::Hostile){
        out.intent=ThreatIntent::WarnLeave;
        out.basis=Basis::Diplomatic;
    }
    return out;
}
} // namespace galaxy::npc_response
