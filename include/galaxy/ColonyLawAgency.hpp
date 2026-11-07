#pragma once
// SHIP-STATUS-07J: a formally appointed, finite-staff colony law agency.
// Separate government appointment, original territorial jurisdiction,
// independent reviewer, verified immutable complaint and agency case intake.
// Referral is NOT guilt, a warrant, arrest, pursuit, gunfire or seizure.
#include "galaxy/ShipCommsReview.hpp"
#include "galaxy/ShipNotificationOutbox.hpp"
#include "galaxy/NpcResponseDispatch.hpp"
#include "galaxy/WorkCapacity.hpp"
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
// 07L: one durable work-intent record linked to the ORIGINAL NPC flight
// ledger. There is no second movement executor and NO assumed completion.
struct Investigation {
    std::uint64_t order_id=0,referral=0,government_event=0;
    std::uint64_t officer=0,observer=0,requested_tick=0;
    PlayerId owner=0;
    EntityId colony=0,target_ship=0;
    bool native_flight_present=false;
    NpcFlightPhase native_phase=NpcFlightPhase::Cancelled;
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
            {
                Stmt q(db_,"SELECT 1 FROM sqlite_master WHERE "
                    "type='table' AND name='snapshots'");
                if(sqlite3_step(q.p)!=SQLITE_ROW)
                    throw std::runtime_error("agency refuses a fake second world");
            }
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
            sql(db_,"CREATE TABLE IF NOT EXISTS sg_law_investigations07l ("
                "event INTEGER PRIMARY KEY,owner INTEGER NOT NULL,"
                "colony INTEGER NOT NULL,referral INTEGER NOT NULL,"
                "government INTEGER NOT NULL,officer INTEGER NOT NULL,"
                "observer INTEGER NOT NULL,target INTEGER NOT NULL,"
                "tick INTEGER NOT NULL,UNIQUE(owner,colony,referral))");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07l_no_update "
                "BEFORE UPDATE ON sg_law_investigations07l "
                "BEGIN SELECT RAISE(ABORT,'immutable investigation intent'); END");
            sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_law07l_no_delete "
                "BEFORE DELETE ON sg_law_investigations07l "
                "BEGIN SELECT RAISE(ABORT,'investigation intent cannot be deleted'); END");
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
    // Read state only from the already committed ORIGINAL NPC flight order.
    // An intent without that flight is not accepted, and on restart never
    // masquerades as dispatch, arrival, a warrant or an investigation result.
    std::vector<Investigation> investigations(const PersistentWorld& world,
        PlayerId owner,EntityId colony) const {
        std::vector<Investigation> result;
        if(!owns(world,owner,colony))return result;
        Stmt q(db_,"SELECT event,owner,colony,referral,government,officer,"
            "observer,target,tick FROM sg_law_investigations07l "
            "WHERE owner=? AND colony=? ORDER BY event");
        const std::uint64_t ids[]{owner,colony};bind(q.p,ids,2);
        while(true){
            const int rc=sqlite3_step(q.p);
            if(rc==SQLITE_DONE)break;
            if(rc!=SQLITE_ROW)throw std::runtime_error(
                "law investigation index unreadable");
            Investigation i{};
            i.order_id=field(q.p,0);i.owner=field(q.p,1);
            i.colony=field(q.p,2);i.referral=field(q.p,3);
            i.government_event=field(q.p,4);i.officer=field(q.p,5);
            i.observer=field(q.p,6);i.target_ship=field(q.p,7);
            i.requested_tick=field(q.p,8);
            const auto f=world.world().npc_flight_orders().find(i.order_id);
            if(f!=world.world().npc_flight_orders().end()){
                const auto& flight=f->second;
                if(flight.owner==i.owner && flight.ship==i.colony &&
                   flight.pilot_npc==i.officer &&
                   flight.station==i.target_ship &&
                   flight.mode==NpcFlightMode::LocalRendezvous &&
                   flight.purpose==NpcFlightPurpose::Patrol){
                    i.native_flight_present=true;
                    i.native_phase=flight.phase;
                }
            }
            result.push_back(i);
        }
        return result;
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
    // A player-directed, nonviolent investigation uses EXISTING native
    // workforce availability, qualified on-board staff, real live contact
    // and the *existing* NPC response/FLIGHT-03 Patrol rendezvous executor.
    // Its immutable intent is journaled BEFORE dispatch; if the native world
    // transaction fails or the process crashes, a later read does not claim
    // the flight happened. A retry uses this same reserved original ID,
    // after current observation, government and staffing are reverified.
    bool investigate(PersistentWorld& world,
        const colony_disposition::TerritoryJournal& territory,
        PlayerId owner,EntityId colony,std::uint64_t referral_id,
        EntityId target,std::uint64_t requested_order){
        if(!requested_order || requested_order>=(std::uint64_t{1}<<40) ||
           !referral_id || !target || target==colony ||
           !owns(world,owner,colony) ||
           !jurisdiction(territory,owner,colony))return false;
        const auto appointed=active(world,owner,colony);
        if(!appointed.event_id)return false;
        const auto all_referrals=referrals(owner,colony);
        const auto source=std::find_if(all_referrals.begin(),
            all_referrals.end(),[&](const Referral& r){
                return r.id==referral_id &&
                    r.government_event==appointed.government_event &&
                    r.officer==appointed.officer &&
                    r.reported_caller && r.points>=ship_comms::kReportThreshold;
            });
        if(source==all_referrals.end())return false;
        const auto* observed_target=world.world().find(target);
        if(!observed_target ||
           observed_target->type!=EntityType::Ship ||
           observed_target->owner_id!=source->reported_caller ||
           !world.world().same_local_frame(colony,target))return false;
        // Existing WORK-01B.2 reservation index, actual native medical/
        // passenger duty, and the original NPC flight pilot reservation.
        // This neither creates a generic assignment nor shadows work data.
        work::NativeCapacityContext capacity{};
        capacity.yards=&world.world().shipyard_constructions();
        capacity.builds=&world.world().freighter_build_orders();
        capacity.native_duty_available=[&](std::uint64_t id){
            return world.world().npc_available_for_duty(id);
        };
        const auto busy=work::index_native_capacity(capacity);
        const auto free_worker=[&](std::uint64_t id){
            if(!duty(world.world(),colony,id) ||
               busy.working_npcs.count(id))return false;
            for(const auto& [flight_id,f]:world.world().npc_flight_orders()){
                (void)flight_id;
                if(f.pilot_npc==id &&
                   f.phase!=NpcFlightPhase::Cancelled &&
                   f.phase!=NpcFlightPhase::Arrived)return false;
            }
            return true;
        };
        if(!free_worker(appointed.officer))return false;
        colony_defense::Contact contact{};
        bool seen=false;
        for(const auto& [id,n]:world.world().npc_records()){
            (void)n;
            if(id==appointed.leader || id==appointed.officer ||
               !free_worker(id))continue;
            const auto contacts=world.local_colony_watch_contacts(colony,id);
            for(const auto& c:contacts)
                if(c.source==colony && c.foreign_ship==target &&
                   c.foreign_owner==source->reported_caller &&
                   c.world_tick==world.world().tick()){
                    contact=c;seen=true;break;
                }
            if(seen)break;
        }
        if(!seen)return false;
        const auto prior=investigations(world,owner,colony);
        const auto old=std::find_if(prior.begin(),prior.end(),
            [&](const Investigation& i){return i.referral==referral_id;});
        std::uint64_t order_id=requested_order;
        if(old!=prior.end()){
            if(old->native_flight_present || old->target_ship!=target ||
               old->officer!=appointed.officer ||
               old->observer!=contact.observer_npc ||
               old->government_event!=appointed.government_event)
                return false;
            order_id=old->order_id; // retry one already durable intent
            if(world.world().npc_flight_orders().count(order_id))
                return false; // conflicting original native flight ID
        }else{
            if(world.world().npc_flight_orders().count(order_id))return false;
            try{
                sql(db_,"BEGIN IMMEDIATE");
                Stmt q(db_,"INSERT INTO sg_law_investigations07l "
                    "(event,owner,colony,referral,government,officer,"
                    "observer,target,tick) VALUES(?,?,?,?,?,?,?,?,?)");
                const std::uint64_t values[]{
                    order_id,owner,colony,referral_id,
                    appointed.government_event,appointed.officer,
                    contact.observer_npc,target,world.world().tick()};
                bind(q.p,values,9);
                if(sqlite3_step(q.p)!=SQLITE_DONE)
                    throw std::runtime_error("duplicate investigation intent");
                sql(db_,"COMMIT");
            }catch(...){
                sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
                return false;
            }
        }
        // Cross-faction law coercion remains forbidden. This path only
        // approaches an observed ship under native Patrol navigation; it
        // confers no search, stop, boarding, seizure, arrest or attack right.
        const auto response=npc_response::dispatch_observed_patrol(
            world,owner,colony,appointed.officer,contact,order_id);
        if(response!=npc_response::Status::Accepted)return false;
        const auto f=world.world().npc_flight_orders().find(order_id);
        return f!=world.world().npc_flight_orders().end() &&
            f->second.owner==owner && f->second.ship==colony &&
            f->second.pilot_npc==appointed.officer &&
            f->second.station==target &&
            f->second.mode==NpcFlightMode::LocalRendezvous &&
            f->second.purpose==NpcFlightPurpose::Patrol;
    }
    std::uint64_t max_event() const{
        std::uint64_t maximum=0;
        for(const auto& r:appointments())maximum=std::max(maximum,r.event_id);
        Stmt s(db_,"SELECT MAX(event) FROM sg_law_referrals07j");
        if(sqlite3_step(s.p)==SQLITE_ROW &&
           sqlite3_column_type(s.p,0)!=SQLITE_NULL)
            maximum=std::max(maximum,field(s.p,0));
        Stmt work(db_,"SELECT MAX(event) FROM sg_law_investigations07l");
        if(sqlite3_step(work.p)==SQLITE_ROW &&
           sqlite3_column_type(work.p,0)!=SQLITE_NULL)
            maximum=std::max(maximum,field(work.p,0));
        return maximum;
    }
};
} // namespace galaxy::colony_law
