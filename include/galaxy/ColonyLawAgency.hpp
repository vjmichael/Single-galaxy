#pragma once
// SHIP-STATUS-07J: a formally appointed, finite-staff colony law agency.
// Separate government appointment, original territorial jurisdiction,
// independent reviewer, verified immutable complaint and agency case intake.
// Referral is NOT guilt, a warrant, arrest, pursuit, gunfire or seizure.
#include "galaxy/ShipCommsReview.hpp"
#include "galaxy/ShipNotificationOutbox.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::colony_law {
struct Appointment {
    std::uint64_t event_id=0,government_event=0,leader=0,officer=0;
    std::uint64_t world_tick=0;
    PlayerId owner=0;
    EntityId colony=0;
};
struct Referral {
    std::uint64_t id=0,complaint=0,reviewer=0,officer=0;
    std::uint64_t government_event=0,points=0,world_tick=0;
    PlayerId owner=0,reported_caller=0;
    EntityId colony=0;
};
class Agency {
    sqlite3* db_=nullptr;
    struct Stmt {
        sqlite3_stmt* p=nullptr;
        Stmt(sqlite3* db,const char* q){
            if(sqlite3_prepare_v2(db,q,-1,&p,nullptr)!=SQLITE_OK)
                throw std::runtime_error("law agency SQL prepare failed");
        }
        ~Stmt(){sqlite3_finalize(p);}
        Stmt(const Stmt&)=delete;
        Stmt& operator=(const Stmt&)=delete;
    };
    static void sql(sqlite3* db,const char* q){
        char* error=nullptr;
        if(sqlite3_exec(db,q,nullptr,nullptr,&error)!=SQLITE_OK){
            const std::string reason=error?error:"law agency SQL failed";
            sqlite3_free(error);throw std::runtime_error(reason);
        }
    }
    static sqlite3_int64 num(std::uint64_t value){
        if(value>std::uint64_t(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("agency signed identity overflow");
        return static_cast<sqlite3_int64>(value);
    }
    static std::uint64_t field(sqlite3_stmt* s,int i){
        const auto v=sqlite3_column_int64(s,i);
        if(v<0)throw std::runtime_error("negative agency evidence field");
        return static_cast<std::uint64_t>(v);
    }
    static void bind(sqlite3_stmt* s,const std::uint64_t* values,int n){
        for(int i=0;i<n;++i)
            if(sqlite3_bind_int64(s,i+1,num(values[i]))!=SQLITE_OK)
                throw std::runtime_error("law agency bind failed");
    }
    static bool duty(const Simulation& sim,EntityId colony,
                     std::uint64_t person){
        if(!person)return false;
        const auto& npcs=sim.npc_records();
        const auto it=npcs.find(person);
        return it!=npcs.end() &&
            it->second.home_ship==colony && it->second.location==colony &&
            !it->second.transport_id &&
            it->second.role==NpcRole::Ship &&
            sim.npc_available_for_duty(person);
    }
    static bool jurisdiction(
        const colony_disposition::TerritoryJournal& land,
        PlayerId owner,EntityId colony){
        land.verify();
        const auto history=land.history();
        return std::any_of(history.begin(),history.end(),
            [&](const colony_disposition::TerritoryEvent& e){
                return e.kind==colony_disposition::TerritoryEventKind::Claim &&
                    e.owner==owner && e.colony==colony;
            });
    }
    static bool owns(const PersistentWorld& world,PlayerId owner,
                     EntityId colony){
        const auto* ship=world.world().find(colony);
        return owner && owner!=Simulation::kServerAuthority &&
            ship && ship->type==EntityType::Ship &&
            ship->owner_id==owner && world.world().colony_ship(colony);
    }
public:
    explicit Agency(const std::string& original_world){
        try{
            if(sqlite3_open_v2(original_world.c_str(),&db_,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
                throw std::runtime_error("agency must use original SQLite world");
            sqlite3_busy_timeout(db_,10000);
            Stmt q(db_,"SELECT 1 FROM sqlite_master WHERE "
                "type='table' AND name='snapshots'");
            if(sqlite3_step(q.p)!=SQLITE_ROW)
                throw std::runtime_error("agency refuses a fake second world");
            sql(db_,"CREATE TABLE IF NOT EXISTS sg_colony_law07j ("
                "event INTEGER PRIMARY KEY,owner INTEGER NOT NULL,"
                "colony INTEGER NOT NULL,government INTEGER NOT NULL,"
                "leader INTEGER NOT NULL,officer INTEGER NOT NULL,"
                "tick INTEGER NOT NULL,"
                "UNIQUE(owner,colony,government))");
            sql(db_,"CREATE TABLE IF NOT EXISTS sg_law_referrals07j ("
                "event INTEGER PRIMARY KEY,owner INTEGER NOT NULL,"
                "colony INTEGER NOT NULL,government INTEGER NOT NULL,"
                "complaint INTEGER NOT NULL,reviewer INTEGER NOT NULL,"
                "officer INTEGER NOT NULL,reported_caller INTEGER NOT NULL,"
                "points INTEGER NOT NULL,tick INTEGER NOT NULL,"
                "UNIQUE(owner,colony,complaint))");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07j_no_appointment_update "
                "BEFORE UPDATE ON sg_colony_law07j "
                "BEGIN SELECT RAISE(ABORT,'immutable agency appointment'); END");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07j_no_appointment_delete "
                "BEFORE DELETE ON sg_colony_law07j "
                "BEGIN SELECT RAISE(ABORT,'immutable agency appointment'); END");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07j_no_referral_update "
                "BEFORE UPDATE ON sg_law_referrals07j "
                "BEGIN SELECT RAISE(ABORT,'immutable legal referral'); END");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07j_no_referral_delete "
                "BEFORE DELETE ON sg_law_referrals07j "
                "BEGIN SELECT RAISE(ABORT,'immutable legal referral'); END");
            ship_notifications::Outbox::ensure_schema(db_);
        }catch(...){if(db_)sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~Agency(){if(db_)sqlite3_close(db_);}
    Agency(const Agency&)=delete;
    Agency& operator=(const Agency&)=delete;
    std::vector<Appointment> appointments() const{
        Stmt q(db_,"SELECT event,owner,colony,government,leader,officer,tick "
            "FROM sg_colony_law07j ORDER BY event");
        std::vector<Appointment> records;
        while(true){
            const auto step=sqlite3_step(q.p);
            if(step==SQLITE_DONE)break;
            if(step!=SQLITE_ROW)throw std::runtime_error("agency index unreadable");
            Appointment e{};
            e.event_id=field(q.p,0);e.owner=field(q.p,1);
            e.colony=field(q.p,2);e.government_event=field(q.p,3);
            e.leader=field(q.p,4);e.officer=field(q.p,5);
            e.world_tick=field(q.p,6);records.push_back(e);
        }
        return records;
    }
    std::vector<Referral> referrals(PlayerId owner,EntityId colony) const{
        Stmt q(db_,"SELECT event,owner,colony,government,complaint,reviewer,"
            "officer,reported_caller,points,tick FROM sg_law_referrals07j "
            "WHERE owner=? AND colony=? ORDER BY event");
        const std::uint64_t ids[]{owner,colony};bind(q.p,ids,2);
        std::vector<Referral> records;
        while(true){
            const auto step=sqlite3_step(q.p);
            if(step==SQLITE_DONE)break;
            if(step!=SQLITE_ROW)throw std::runtime_error("agency referral unreadable");
            Referral r{};
            r.id=field(q.p,0);r.owner=field(q.p,1);
            r.colony=field(q.p,2);r.government_event=field(q.p,3);
            r.complaint=field(q.p,4);r.reviewer=field(q.p,5);
            r.officer=field(q.p,6);r.reported_caller=field(q.p,7);
            r.points=field(q.p,8);r.world_tick=field(q.p,9);
            records.push_back(r);
        }
        return records;
    }
    Appointment active(const PersistentWorld& world,PlayerId owner,
                       EntityId colony) const{
        if(!owns(world,owner,colony))return {};
        colony_conflict::GovernanceEpoch gov{};
        try{gov=world.colony_governance(colony);}
        catch(const std::exception&){return {};}
        if(!gov.event_id || !gov.leader_id ||
           !duty(world.world(),colony,gov.leader_id))return {};
        const auto records=appointments();
        auto it=std::find_if(records.rbegin(),records.rend(),
            [&](const Appointment& p){
                return p.owner==owner && p.colony==colony &&
                    p.government_event==gov.event_id &&
                    p.leader==gov.leader_id;
            });
        if(it==records.rend() || !duty(world.world(),colony,it->officer) ||
           it->officer==gov.leader_id)return {};
        return *it;
    }
    bool establish(const PersistentWorld& world,
        const colony_disposition::TerritoryJournal& territory,
        PlayerId owner,EntityId colony,std::uint64_t receipt){
        if(!receipt || !owns(world,owner,colony) ||
           !jurisdiction(territory,owner,colony))return false;
        colony_conflict::GovernanceEpoch gov{};
        try{gov=world.colony_governance(colony);}
        catch(const std::exception&){return false;}
        if(!gov.event_id || !duty(world.world(),colony,gov.leader_id) ||
           active(world,owner,colony).event_id)return false;
        std::uint64_t officer=0;
        for(const auto& [id,npc]:world.world().npc_records())
            if(id!=gov.leader_id && duty(world.world(),colony,id)){
                officer=id;break;
            }
        if(!officer)return false; // actual finite named civilian staffing
        Stmt s(db_,"INSERT INTO sg_colony_law07j "
            "(event,owner,colony,government,leader,officer,tick) "
            "VALUES(?,?,?,?,?,?,?)");
        const std::uint64_t values[]{
            receipt,owner,colony,gov.event_id,gov.leader_id,officer,
            world.world().tick()};
        try{
            sql(db_,"BEGIN IMMEDIATE");
            bind(s.p,values,7);
            if(sqlite3_step(s.p)!=SQLITE_DONE)
                throw std::runtime_error("duplicate or failed government appointment");
            sql(db_,"COMMIT");return true;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            return false;
        }
    }
    bool refer(const PersistentWorld& world,
        const colony_disposition::TerritoryJournal& territory,
        const ship_comms::Journal& comms,PlayerId owner,
        EntityId colony,EntityId victim,std::uint64_t complaint,
        std::uint64_t receipt){
        if(!receipt || !complaint || !owns(world,owner,colony) ||
           !jurisdiction(territory,owner,colony))return false;
        const auto appointed=active(world,owner,colony);
        if(!appointed.event_id || !duty(world.world(),colony,appointed.officer))
            return false;
        // The agency officer must not personally review the evidence or
        // also be the appointing leader. An INDEPENDENT live named reviewer
        // must examine the original signed evidence and current jurisdiction.
        ship_comms::review::Assessment verified{};
        for(const auto& [id,npc]:world.world().npc_records()){
            if(id==appointed.leader || id==appointed.officer ||
               !duty(world.world(),colony,id))continue;
            const auto candidate=ship_comms::review::assess(world,territory,
                comms,owner,colony,victim,complaint,id);
            if(candidate.status==ship_comms::review::Status::AwaitingLawAgency &&
               candidate.credible_nuisance_evidence){
                verified=candidate;break;
            }
        }
        if(!verified.reviewer_npc || !verified.reported_caller ||
           verified.points<ship_comms::kReportThreshold)return false;
        try{
            sql(db_,"BEGIN IMMEDIATE");
            Stmt s(db_,"INSERT INTO sg_law_referrals07j "
                "(event,owner,colony,government,complaint,reviewer,officer,"
                "reported_caller,points,tick) VALUES(?,?,?,?,?,?,?,?,?,?)");
            const std::uint64_t values[]{
                receipt,owner,colony,appointed.government_event,complaint,
                verified.reviewer_npc,appointed.officer,
                verified.reported_caller,verified.points,world.world().tick()};
            bind(s.p,values,10);
            if(sqlite3_step(s.p)!=SQLITE_DONE)
                throw std::runtime_error("duplicate agency referral");
            // Referral notification is atomic with independent agency intake.
            ship_notifications::Outbox::record_agency(db_,owner,receipt,
                colony,complaint,world.world().tick());
            sql(db_,"COMMIT");return true;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            return false;
        }
    }
    std::uint64_t max_event() const{
        std::uint64_t maximum=0;
        for(const auto& r:appointments())maximum=std::max(maximum,r.event_id);
        Stmt s(db_,"SELECT MAX(event) FROM sg_law_referrals07j");
        if(sqlite3_step(s.p)==SQLITE_ROW &&
           sqlite3_column_type(s.p,0)!=SQLITE_NULL)
            maximum=std::max(maximum,field(s.p,0));
        return maximum;
    }
};
} // namespace galaxy::colony_law
