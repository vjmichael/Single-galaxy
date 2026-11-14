#pragma once
// SHIP-STATUS-07: graduated colony-security reaction to UNIDENTIFIED
// physical silhouettes. A lookalike can justify a hail or nonviolent patrol,
// NEVER a crime verdict, arrest, weapons discharge or confiscation.
#include "galaxy/CyberIncidentJournal.hpp"
#include "galaxy/NpcResponseDispatch.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::colony_disposition {
enum class CyberSecurityLevel : std::uint8_t {
    NoContact=0,RecordOnly=1,WatchForHail=2,PatrolEligible=3
};
struct CyberSecurityMatch {
    std::uint64_t case_id=0;
    // This is the *currently observed* target track, not a proven culprit.
    EntityId sighted_ship=0;
    CyberSecurityLevel response=CyberSecurityLevel::NoContact;
    std::uint8_t sensor_detail=0;
    bool identified_culprit=false,crime_finding=false;
    bool damage_authorized=false,arrest_authorized=false;
};
inline CyberSecurityLevel security_capability(
    const Simulation& world,EntityId colony) {
    const auto* ship=world.find(colony);
    if(!ship || ship->type!=EntityType::Ship ||
       !world.colony_ship(colony))
        return CyberSecurityLevel::NoContact;
    if(ship_sensors::strong_sensors(*ship))
        return CyberSecurityLevel::PatrolEligible;
    if(ship_sensors::powered_survey_scanner(world,*ship))
        return CyberSecurityLevel::WatchForHail;
    return CyberSecurityLevel::RecordOnly;
}
// Host must pass actual authorized owner, original colony and named observer;
// all contacts and signature matches are recomputed from current world.
inline std::vector<CyberSecurityMatch> security_matches(
    const PersistentWorld& world,const CyberIncidentJournal& cases,
    PlayerId colony_owner,EntityId colony,std::uint64_t observer_npc) {
    std::vector<CyberSecurityMatch> matches;
    const auto* site=world.world().find(colony);
    if(!colony_owner || colony_owner==Simulation::kServerAuthority ||
       !site || site->owner_id!=colony_owner ||
       !world.world().colony_ship(colony))return matches;
    const auto seen=world.local_colony_watch_contacts(colony,observer_npc);
    const auto known=cases.for_colony_owner(world,colony_owner,colony);
    const auto capability=security_capability(world.world(),colony);
    for(const auto& contact:seen){
        for(const auto& recorded:known){
            const auto& old=recorded.evidence;
            if(old.record_version!=2 ||
               old.sensor_detail>contact.sensor_detail ||
               old.world_tick>world.world().tick() ||
               ship_sensors::description_at_detail(
                    world.world(),contact.foreign_ship,old.sensor_detail)!=
                 old.observed_description)
                continue;
            CyberSecurityMatch match{};
            match.case_id=old.case_id;
            match.sighted_ship=contact.foreign_ship;
            match.response=capability;
            match.sensor_detail=contact.sensor_detail;
            matches.push_back(match);
            if(matches.size()>=64)return matches;
        }
    }
    return matches;
}
// An independently approved patrol consumes an ORIGINAL NPC flight slot and
// original physical ship. Even high-security matching has NO force warrant.
// Passing a user-chosen alleged owner, crime finding, or legal warrant is not
// part of this API. The local trusted host must authorize the request.
inline npc_response::Status request_signature_patrol(
    PersistentWorld& world,const CyberIncidentJournal& cases,
    PlayerId colony_owner,EntityId colony,std::uint64_t observer_npc,
    EntityId selected_track,std::uint64_t case_id,
    std::uint64_t flight_order_id,std::uint64_t pilot_npc=0) {
    if(!flight_order_id || !selected_track || !case_id ||
       security_capability(world.world(),colony)!=
          CyberSecurityLevel::PatrolEligible)
        return npc_response::Status::NoAuthority;
    const auto confirmed=security_matches(world,cases,colony_owner,
        colony,observer_npc);
    if(std::none_of(confirmed.begin(),confirmed.end(),
       [&](const CyberSecurityMatch& m){
           return m.case_id==case_id &&
                  m.sighted_ship==selected_track &&
                  m.response==CyberSecurityLevel::PatrolEligible;
       }))return npc_response::Status::NoAuthority;
    const auto observed=world.local_colony_watch_contacts(colony,observer_npc);
    const auto contact=std::find_if(observed.begin(),observed.end(),
        [&](const colony_defense::Contact& c){
            return c.source==colony && c.foreign_ship==selected_track &&
                   c.world_tick==world.world().tick();
        });
    if(contact==observed.end())return npc_response::Status::NoAuthority;
    return npc_response::dispatch_observed_patrol(world,colony_owner,
        colony,pilot_npc,*contact,flight_order_id);
}
} // namespace galaxy::colony_disposition
