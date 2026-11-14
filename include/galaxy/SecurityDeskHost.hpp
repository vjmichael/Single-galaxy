#pragma once
// SHIP-STATUS-07D: one trusted native, owner-scoped security desk.
// The original PersistentWorld, TerritoryJournal, CyberIncidentJournal,
// NPC watch and flight dispatch remain authoritative. No shadow simulation,
// client-selected principal, fabricated warrant or invisible identity lookup.
#include "galaxy/CyberSecurityResponse.hpp"
#include "galaxy/TerritorialHailing.hpp"
#include "galaxy/FleetSelection.hpp"
#include "galaxy/ShipHailJournal.hpp"
#include "galaxy/ShipCommsReview.hpp"
#include "galaxy/ColonyLawAgency.hpp"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <set>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace galaxy::colony_disposition::security_desk {
inline std::string hex(const std::string& input) {
    static constexpr char digits[]="0123456789abcdef";
    std::string result;
    result.reserve(input.size()*2);
    for(const unsigned char c:input){
        result.push_back(digits[c>>4]);
        result.push_back(digits[c&15]);
    }
    return result;
}
// Obtain the real on-duty observer at the observed colony, never an NPC ID
// from HTTP. In particular, an unpowered/no-witness colony cannot fabricate
// a case or claim that it identified a visiting ship.
inline std::uint64_t witness(const PersistentWorld& world,EntityId colony,
                             EntityId visitor=0) {
    for(const auto& [id,npc]:world.world().npc_records()) {
        if(npc.home_ship!=colony || npc.location!=colony ||
           npc.transport_id || !world.world().npc_available_for_duty(id))
            continue;
        try {
            const auto contacts=world.local_colony_watch_contacts(colony,id);
            if(!visitor || std::any_of(contacts.begin(),contacts.end(),
                [&](const colony_defense::Contact& c){
                    return c.foreign_ship==visitor &&
                           c.world_tick==world.world().tick();
                }))return id;
        }catch(const std::exception&){}
    }
    return 0;
}
inline std::uint64_t case_id(EntityId colony,std::uint64_t original_command) {
    // Stable original-command/jurisdiction key; safe for SQLite signed IDs.
    std::uint64_t value=(original_command*0x9e3779b97f4a7c15ULL) ^
                        (colony*0xc2b2ae3d27d4eb4fULL) ^
                        0x0177a7c0de55e7ULL;
    value&=((std::uint64_t{1}<<62)-1);
    if(!value || value==original_command)value^=0x1badd00dULL;
    return value;
}
template<class Emit>
std::size_t capture_current_tick(const PersistentWorld& world,
    const TerritoryJournal& territory,CyberIncidentJournal& cases,
    PlayerId viewer_owner,Emit&& emit) {
    std::size_t recorded=0;
    for(const auto& event:world.recent_history(100)) {
        if(event.tick!=world.world().tick() ||
           event.action!=Action::HackConfuse ||
           (event.result!=Outcome::Applied &&
            event.result!=Outcome::AttackResisted))
            continue;
        for(const auto& [colony,site]:world.world().entities()) {
            if(site.type!=EntityType::Ship ||
               !world.world().colony_ship(colony))continue;
            const auto guard=witness(world,colony,event.actor);
            if(!guard)continue;
            try {
                if(cases.intake(world,territory,colony,guard,
                    event.command_id,case_id(colony,event.command_id))){
                    ++recorded;
                    // The event is publicly a case number, not an owner ID.
                    if(recorded<=32 && site.owner_id==viewer_owner)
                        emit("SECURITY_CASE_FILED|"+
                            std::to_string(site.owner_id)+"|"+
                            std::to_string(case_id(colony,event.command_id)));
                }
            }catch(const std::exception&) {
                emit("SECURITY_ERROR|CASE_INDEX_UNAVAILABLE");
            }
        }
    }
    return recorded;
}
template<class Emit>
void snapshot(const PersistentWorld& world,
    const TerritoryJournal& territory,const CyberIncidentJournal& cases,
    PlayerId owner,EntityId home,EntityId piloted,Emit&& emit,
    const ship_comms::Journal* communications=nullptr,
    const ship_notifications::Outbox* notifications=nullptr,
    const colony_law::Agency* law_agency=nullptr) {
    const auto* home_ship=world.world().find(home);
    if(!owner || owner==Simulation::kServerAuthority ||
       !home_ship || home_ship->owner_id!=owner ||
       !world.world().colony_ship(home))
        return;
    const auto entries=territory.history();
    const bool claimed=std::any_of(entries.begin(),entries.end(),
        [&](const TerritoryEvent& e){
            return e.colony==home && e.kind==TerritoryEventKind::Claim &&
                   e.owner==owner;
        });
    const auto report=cases.for_colony_owner(world,owner,home);
    emit("SECURITY_STATUS|"+std::to_string(home)+"|"+
         std::to_string(piloted)+"|"+std::to_string(claimed?1:0)+"|"+
         std::to_string(report.size())+"|"+
         std::to_string(static_cast<unsigned>(
              security_capability(world.world(),home))));
    emit("SECURITY_CASES_RESET");
    // Cap wire readout without mutating durable history.
    const std::size_t begin=report.size()>32?report.size()-32:0;
    for(std::size_t i=begin;i<report.size();++i) {
        const auto& r=report[i];
        emit("SECURITY_CASE|"+std::to_string(r.evidence.case_id)+"|"+
            std::to_string(r.evidence.world_tick)+"|"+
            std::to_string(r.disrupted_modules?1:0)+"|"+
            std::to_string(r.evidence.sensor_detail)+"|"+
            hex(r.evidence.observed_description)+"|"+
            std::to_string(r.later_identified_controller)+"|"+
            std::to_string(r.identity_hail_event));
    }
    emit("SECURITY_CONTACTS_RESET");
    const auto guard=witness(world,home);
    if(guard){
        const auto contacts=world.local_colony_watch_contacts(home,guard);
        std::size_t count=0;
        for(const auto& c:contacts) {
            if(++count>32)break;
            emit("SECURITY_CONTACT|"+std::to_string(c.foreign_ship)+"|"+
                std::to_string(c.sensor_detail)+"|"+
                hex(c.observed_description));
        }
    }
    emit("SECURITY_NOTICES_RESET");
    std::size_t notices=0;
    for(const auto& notice:territory.pending_notices(world,owner)) {
        if(++notices>32)break;
        emit("SECURITY_NOTICE|"+std::to_string(notice.colony)+"|"+
            std::to_string(notice.visitor_ship)+"|"+
            std::to_string(notice.event_id));
    }
    emit("SECURITY_DESTINATIONS_RESET");
    const auto* pilot=world.world().find(piloted);
    if(pilot && pilot->owner_id==owner){
        std::size_t destinations=0;
        for(const auto& claim:entries) {
            if(claim.kind!=TerritoryEventKind::Claim ||
               claim.owner==owner || !claim.colony)continue;
            const auto state=territory.standing(world,claim.colony,piloted);
            if(!state.in_claimed_region ||
               !witness(world,claim.colony,piloted))continue;
            if(++destinations>32)break;
            emit("SECURITY_DESTINATION|"+std::to_string(claim.colony));
        }
    }
    if(communications)try{
        emit("SECURITY_CALL_INBOX_RESET");
        for(const auto& incoming:communications->pending_for(
             world,owner,piloted)){
            emit("SECURITY_CALL_INBOX|"+
                std::to_string(incoming.request_id)+"|"+
                std::to_string(incoming.caller_ship)+"|"+
                std::to_string(incoming.caller));
        }
        const auto penalties=communications->penalty_for(
            world,owner,piloted);
        emit("SECURITY_CALL_PENALTY|"+
            std::to_string(penalties.penalty_points)+"|"+
            std::to_string(penalties.cooldown_until)+"|"+
            std::to_string(penalties.law_review_eligible?1:0));
        emit("SECURITY_CALL_COMPLAINTS_RESET");
        std::size_t reported=0;
        std::set<PlayerId> reported_callers;
        // Compact total rows supersede each caller's tenth detailed
        // nuisance receipt while exposing the genuine latest score.
        const auto totals=communications->overflow_history();
        for(auto it=totals.rbegin();it!=totals.rend();++it){
            if(it->recipient_ship!=piloted || ++reported>16)continue;
            reported_callers.insert(it->caller_owner);
            emit("SECURITY_CALL_COMPLAINT|"+
                std::to_string(it->last_event)+"|"+
                std::to_string(it->caller_owner)+"|"+
                std::to_string(it->total_points));
            const auto guard=witness(world,home);
            if(guard){
                const auto review=ship_comms::review::assess(world,
                    territory,*communications,owner,home,piloted,
                    it->last_event,guard);
                emit("SECURITY_CALL_REVIEW|"+
                    std::to_string(it->last_event)+"|"+
                    std::to_string(unsigned(review.status))+"|"+
                    std::to_string(review.reviewer_npc));
            }
        }
        const auto committed=communications->history();
        for(auto it=committed.rbegin();it!=committed.rend();++it){
            const auto& item=*it;
            if(item.kind!=ship_comms::Kind::Abuse ||
               item.recipient_ship!=piloted ||
               !item.law_review_eligible ||
               reported_callers.count(item.caller_owner))continue;
            if(++reported>16)break;
            reported_callers.insert(item.caller_owner);
            emit("SECURITY_CALL_COMPLAINT|"+
                std::to_string(item.event_id)+"|"+
                std::to_string(item.caller_owner)+"|"+
                std::to_string(item.penalty_points));
            const auto guard=witness(world,home);
            if(guard){
                const auto review=ship_comms::review::assess(world,
                    territory,*communications,owner,home,piloted,
                    item.event_id,guard);
                emit("SECURITY_CALL_REVIEW|"+
                    std::to_string(item.event_id)+"|"+
                    std::to_string(unsigned(review.status))+"|"+
                    std::to_string(review.reviewer_npc));
            }
        }
    }catch(const std::exception&){
        // An unavailable evidence index must not take down the physical
        // colony simulation, and must never fabricate an accepted report.
        emit("SECURITY_ERROR|COMMS_UNAVAILABLE");
    }
    if(notifications)try{
        emit("SECURITY_NOTIFICATIONS_RESET");
        for(const auto& n:notifications->pending(owner)){
            emit("SECURITY_NOTIFICATION|"+std::to_string(n.sequence)+"|"+
                std::to_string(n.kind)+"|"+
                std::to_string(n.source_event)+"|"+
                std::to_string(n.peer_ship)+"|"+
                std::to_string(n.peer_owner)+"|"+
                std::to_string(n.related));
        }
    }catch(const std::exception&){
        emit("SECURITY_ERROR|OUTBOX_UNAVAILABLE");
    }
    if(law_agency)try{
        const auto authorized=law_agency->active(world,owner,home);
        emit("SECURITY_AGENCY_STATUS|"+std::to_string(authorized.leader)+"|"+
             std::to_string(authorized.officer)+"|"+
             std::to_string(authorized.event_id?1:0));
        const auto referrals=law_agency->referrals(owner,home);
        const auto start=referrals.size()>16?referrals.size()-16:0;
        emit("SECURITY_AGENCY_REFERRALS_RESET");
        for(std::size_t i=start;i<referrals.size();++i){
            const auto& r=referrals[i];
            emit("SECURITY_AGENCY_REFERRAL|"+std::to_string(r.id)+"|"+
                std::to_string(r.complaint)+"|"+
                std::to_string(r.reported_caller)+"|"+
                std::to_string(r.points)+"|"+
                std::to_string(r.officer)+"|"+
                std::to_string(r.reviewer));
        }
        emit("SECURITY_AGENCY_INVESTIGATIONS_RESET");
        const auto tasks=law_agency->investigations(world,owner,home);
        const auto from=tasks.size()>16?tasks.size()-16:0;
        for(std::size_t i=from;i<tasks.size();++i){
            const auto& task=tasks[i];
            emit("SECURITY_AGENCY_INVESTIGATION|"+
                std::to_string(task.order_id)+"|"+
                std::to_string(task.referral)+"|"+
                std::to_string(task.target_ship)+"|"+
                std::to_string(task.officer)+"|"+
                std::to_string(task.native_flight_present?1:0)+"|"+
                std::to_string(unsigned(task.native_phase)));
        }
    }catch(const std::exception&){
        emit("SECURITY_ERROR|AGENCY_UNAVAILABLE");
    }
    emit("SECURITY_REFRESH_DONE");
}
template<class Emit>
bool execute(const std::vector<std::string>& words,
    PersistentWorld& world,TerritoryJournal& territory,
    CyberIncidentJournal& cases,PlayerId owner,EntityId home,
    EntityId piloted,std::uint64_t& nonce,Emit&& emit,
    ship_comms::Journal* communications=nullptr,
    ship_notifications::Outbox* notifications=nullptr,
    colony_law::Agency* law_agency=nullptr) {
    if(words.empty() || words[0].rfind("SECURITY_",0)!=0)return false;
    const auto* home_ship=world.world().find(home);
    const auto* pilot_ship=world.world().find(piloted);
    if(!owner || owner==Simulation::kServerAuthority ||
       !home_ship || home_ship->owner_id!=owner ||
       !world.world().colony_ship(home) ||
       !pilot_ship || pilot_ship->owner_id!=owner){
        emit("SECURITY_RESULT|DENIED|0");return true;
    }
    auto numeric=[](const std::string& value)->std::uint64_t {
        if(value.empty() || value.size()>19 || value[0]=='0')return 0;
        std::uint64_t result=0;
        for(const char digit:value){
            if(digit<'0'||digit>'9')return 0;
            const auto n=std::uint64_t(digit-'0');
            if(result>(std::numeric_limits<std::uint64_t>::max()-n)/10)
                return 0;
            result=result*10+n;
        }
        return result;
    };
    if(words.size()==1 && words[0]=="SECURITY_REFRESH") {
        snapshot(world,territory,cases,owner,home,piloted,emit,
                 communications,notifications,law_agency);
        return true;
    }
    if(words.size()==2 && words[0]=="SECURITY_NOTIFICATION_ACK"){
        const auto seq=numeric(words[1]);
        bool acknowledged=false;
        if(seq && notifications)try{
            acknowledged=notifications->acknowledge(owner,seq);
        }catch(const std::exception&){
            emit("SECURITY_ERROR|OUTBOX_UNAVAILABLE");
        }
        emit("SECURITY_RESULT|SECURITY_NOTIFICATION_ACK|"+
            std::string(acknowledged?"ACCEPTED|":"DENIED|")+
            std::to_string(acknowledged?seq:0)+"|0");
        if(acknowledged)
            snapshot(world,territory,cases,owner,home,piloted,emit,
                     communications,notifications,law_agency);
        return true;
    }
    bool accepted=false,linked=false;
    std::uint64_t receipt=0;
    const auto action=words[0];
    if(words.size()==2 && action=="SECURITY_CLAIM"){
        const auto radius=numeric(words[1]);
        if(radius>=1 && radius<=5000)
            accepted=territory.claim(world,owner,home,receipt=++nonce,
                                      static_cast<std::int64_t>(radius));
    }else if(words.size()==2 && action=="SECURITY_WARN"){
        const auto visitor=numeric(words[1]);
        const auto guard=witness(world,home,visitor);
        if(guard && visitor)
            accepted=territory.warn(world,owner,home,visitor,guard,
                                     receipt=++nonce,10);
    }else if(words.size()==2 && action=="SECURITY_HAIL"){
        const auto colony=numeric(words[1]);
        const auto guard=witness(world,colony,piloted);
        if(colony && guard) {
            accepted=territory.voluntary_hail(world,owner,colony,piloted,
                                            guard,receipt=++nonce);
            if(accepted)
                linked=cases.link_authenticated_hail(world,territory,
                     colony,guard,piloted,owner,receipt);
        }
    }else if(words.size()==4 && action=="SECURITY_REPLY"){
        const auto colony=numeric(words[1]),notice=numeric(words[2]);
        if(colony && notice && (words[3]=="ACK" || words[3]=="REFUSE")){
            accepted=player_respond_to_hail(world,territory,owner,colony,
                piloted,notice,receipt=++nonce,
                words[3]=="ACK"?HailChoice::Acknowledge:HailChoice::Refuse);
            if(accepted){
                const auto guard=witness(world,colony,piloted);
                if(guard)linked=cases.link_authenticated_hail(world,territory,
                    colony,guard,piloted,owner,receipt);
            }
        }
    }else if(words.size()==2 && action=="SECURITY_CALL" &&
             communications){
        const auto other=numeric(words[1]);
        if(other)try{
            const auto result=communications->call(world,territory,owner,
                piloted,other,receipt=++nonce);
            accepted=result.status==ship_comms::Status::CallSent;
            if(result.status==ship_comms::Status::Penalized ||
               result.status==ship_comms::Status::Suppressed)
                emit("SECURITY_CALL_SANCTION|"+
                    std::to_string(other)+"|"+
                    std::to_string(result.penalty_points)+"|"+
                    std::to_string(result.cooldown_until)+"|"+
                    std::to_string(result.law_review_eligible?1:0));
        }catch(const std::exception&){
            emit("SECURITY_ERROR|COMMS_UNAVAILABLE");
        }
    }else if(words.size()==3 && action=="SECURITY_CALL_REPLY" &&
             communications){
        const auto call=numeric(words[1]);
        if(call && (words[2]=="IDENTIFY" || words[2]=="DECLINE"))try{
            const auto result=communications->reply(world,owner,piloted,
                call,receipt=++nonce,words[2]=="IDENTIFY");
            accepted=result.status==ship_comms::Status::Identified ||
                     result.status==ship_comms::Status::Declined;
        }catch(const std::exception&){
            emit("SECURITY_ERROR|COMMS_UNAVAILABLE");
        }
    }else if(words.size()==1 && action=="SECURITY_AGENCY_ESTABLISH" &&
              law_agency){
        try{
            accepted=law_agency->establish(world,territory,owner,home,
                receipt=++nonce);
        }catch(const std::exception&){
            emit("SECURITY_ERROR|AGENCY_UNAVAILABLE");
        }
    }else if(words.size()==2 && action=="SECURITY_AGENCY_REFER" &&
              law_agency && communications){
        const auto complaint=numeric(words[1]);
        if(complaint)try{
            accepted=law_agency->refer(world,territory,*communications,
                owner,home,piloted,complaint,receipt=++nonce);
        }catch(const std::exception&){
            emit("SECURITY_ERROR|AGENCY_UNAVAILABLE");
        }
    }else if(words.size()==3 && action=="SECURITY_AGENCY_INVESTIGATE" &&
              law_agency){
        const auto referral=numeric(words[1]);
        const auto target=numeric(words[2]);
        if(referral && target)try{
            accepted=law_agency->investigate(world,territory,owner,home,
                referral,target,receipt=++nonce);
        }catch(const std::exception&){
            emit("SECURITY_ERROR|AGENCY_UNAVAILABLE");
        }
    }else if(words.size()==3 && action=="SECURITY_PATROL"){
        const auto case_number=numeric(words[1]);
        const auto target=numeric(words[2]);
        const auto guard=witness(world,home,target);
        if(case_number && target && guard){
            const auto status=request_signature_patrol(world,cases,owner,
                home,guard,target,case_number,receipt=++nonce);
            accepted=status==npc_response::Status::Accepted;
        }
    }
    emit("SECURITY_RESULT|"+action+"|"+
         (accepted?"ACCEPTED":"DENIED")+"|"+
         std::to_string(accepted?receipt:0)+"|"+
         std::to_string(linked?1:0));
    snapshot(world,territory,cases,owner,home,piloted,emit,
             communications,notifications,law_agency);
    return true;
}
} // namespace galaxy::colony_disposition::security_desk
