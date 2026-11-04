#include "galaxy/ColonyConflict.hpp"
#include <stdexcept>
namespace galaxy::colony_conflict {
int relationship_delta(int fidelity,int valence,int confidence){
    // GRSM fidelity IDs: 0=local factual encounter (never attributed a
    // sentiment), 1=faithful retelling, 2=embellished, 3=contrary rumor.
    // These multipliers are credibility, not proof of truth or guilt.
    if(fidelity<1 || fidelity>3 || valence< -100 || valence>100 ||
       confidence<0 || confidence>100)
        throw std::invalid_argument("invalid bounded NPC interpretation");
    const int credibility=fidelity==1?100:fidelity==2?60:40;
    return valence*confidence*credibility/10000;
}
Attitude attitude_for_score(int score){
    if(score < -100 || score > 100)
        throw std::invalid_argument("invalid colony relation score");
    if(score<=-50)return Attitude::Hostile;
    if(score<=-15)return Attitude::Wary;
    if(score>=50)return Attitude::Allied;
    if(score>=15)return Attitude::Friendly;
    return Attitude::Neutral;
}
Decision evaluate_diplomatic(Decision d,int score,std::uint64_t event,
                           DiplomaticPolicy policy){
    if(static_cast<int>(policy)<0 || static_cast<int>(policy)>2)
        throw std::invalid_argument("unrecognized historical colony diplomacy policy");
    d.diplomatic_policy=policy;
    d.relation_score=score;
    d.relation_event_id=event;
    const auto attitude=attitude_for_score(score);
    if(d.source_risk==colony_safety::Risk::Critical ||
       d.source_risk==colony_safety::Risk::Emergency)
        return d; // real habitat shortages take precedence over opinions
    // Leaders can seek talks with an adversary without falsifying negative
    // GRSM history. This is restraint, not a real treaty or absolution.
    if(policy==DiplomaticPolicy::Reconciliatory &&
       (attitude==Attitude::Hostile || attitude==Attitude::Wary) &&
       d.posture==Posture::Observe)return d;
    if(attitude==Attitude::Allied){
        // Admiration suppresses *preparation*, not a binding treaty or
        // permanent disarmament. Actual actions still require separate gates.
        d.motive=Motive::None;
        d.posture=Posture::Observe;
    }else if(attitude==Attitude::Friendly &&
             d.posture==Posture::PrepareRaid){
        d.motive=Motive::Security;
        d.posture=Posture::Warn;
    }else if((attitude==Attitude::Hostile || attitude==Attitude::Wary) &&
             d.posture==Posture::Observe){
        d.motive=Motive::Security;
        d.posture=Posture::Warn;
    }
    return d;
}
Decision evaluate(const Simulation& world,EntityId colony,
                  std::uint64_t leader,Agenda agenda,
                  const colony_defense::Contact& seen,
                  std::uint64_t evidence_id){
    const auto* site=world.find(colony);
    const auto* colonyship=world.colony_ship(colony);
    const auto* supply=world.life_support(colony);
    const auto person=world.npc_records().find(leader);
    if(!evidence_id || !site || !colonyship || !supply ||
       site->type!=EntityType::Ship || !site->owner_id ||
       !leader || person==world.npc_records().end() ||
       person->second.home_ship!=colony ||
       person->second.location!=colony ||
       person->second.transport_id || !world.npc_available_for_duty(leader) ||
       seen.source!=colony || !seen.observer_npc ||
       seen.foreign_ship==colony || seen.world_tick!=world.tick() ||
       seen.system!=site->system_id ||
       static_cast<int>(agenda)<0 ||
       static_cast<int>(agenda)>3)
        throw std::invalid_argument("colony strategy lacks live leader/physical contact");
    // Rechecking the *current* watch prevents a stale or fabricated target,
    // even when a real event was observed in a prior generation.
    const auto visible=colony_defense::observe_local_contacts(
        world,colony,seen.observer_npc);
    bool confirmed=false;
    for(const auto& c:visible){
        if(c.foreign_ship==seen.foreign_ship &&
           c.foreign_owner==seen.foreign_owner &&
           c.distance_squared==seen.distance_squared &&
           c.system==seen.system)confirmed=true;
    }
    if(!confirmed)throw std::invalid_argument("foreign target no longer visible");
    Decision d;
    d.source=colony;
    d.target_ship=seen.foreign_ship;
    d.target_owner=seen.foreign_owner;
    d.leader_id=leader;
    d.contact_event_id=evidence_id;
    d.agenda=agenda;
    d.source_risk=colony_safety::assess(world,colony,colony).risk;
    const bool urgent=static_cast<int>(d.source_risk)>=
        static_cast<int>(colony_safety::Risk::Critical);
    // Desperation can turn a normally cooperative settlement to a REQUEST,
    // while a predatory one may make coercive demands. Neither implies that
    // the foreign ship actually carries the requested stock.
    if(urgent){
        d.motive=Motive::Scarcity;
        d.posture=agenda==Agenda::Predatory?
            Posture::DemandSupplies:Posture::RequestAid;
    }else if(agenda==Agenda::Isolationist){
        // Presence is not criminality. A nearby ship can provoke a warning
        // but never automatically authorize firing.
        if(seen.distance_squared<=static_cast<std::uint64_t>(2000*2000)){
            d.motive=Motive::Security;
            d.posture=Posture::Warn;
        }
    }else if(agenda==Agenda::Expansionist){
        d.motive=Motive::Territory;
        d.posture=Posture::Warn;
    }else if(agenda==Agenda::Predatory){
        d.motive=Motive::Predation;
        // This is a strategic *preparation*, not combat. Crew and stored
        // modules demonstrate a real minimum local capacity; the later real
        // fleet/weapon pipeline must prove arms, ammunition and travel.
        d.posture=colonyship->crew>=4 && colonyship->modules>=2?
            Posture::PrepareRaid:Posture::Warn;
    }
    return d;
}
}
