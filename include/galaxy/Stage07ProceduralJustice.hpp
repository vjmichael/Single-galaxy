#pragma once
// Stage 7 POP-10C: noncoercive procedural justice, tied to the ORIGINAL
// immutable nuisance complaint, legitimate territory, appointed agency,
// finite independent named reviewer and original same-SQLite notifications.
// Neither the NPC nor a filing is criminal guilt, seizure, imprisonment,
// damages, an automated police order, or consent on behalf of a respondent.
#include "galaxy/ColonyLawAgency.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::stage7 {
enum class DefensePosition : std::uint8_t { Contest=1, MediationRequested=2 };
enum class ProceduralDisposition : std::uint8_t {
    NoFindingAfterContest=1, VoluntaryMediationOffered=2,
    ClosedWithoutAnswer=3
};
struct JusticeCase {
    std::uint64_t id=0,referral=0,complaint=0,government=0,judge=0;
    std::uint64_t officer=0,original_reviewer=0,opened_generation=0,opened_tick=0;
    PlayerId owner=0,respondent=0;
    EntityId colony=0;
};
struct JusticeAnswer {
    std::uint64_t case_id=0,answered_generation=0,answered_tick=0;
    PlayerId respondent=0;
    DefensePosition position=DefensePosition::Contest;
};
struct JusticeDisposition {
    std::uint64_t id=0,case_id=0,decided_generation=0,decided_tick=0;
    std::uint64_t independent_judge=0;
    ProceduralDisposition result=ProceduralDisposition::ClosedWithoutAnswer;
};
class ProceduralJustice {
    sqlite3* db_=nullptr;
    struct Stmt{
        sqlite3_stmt* s=nullptr;
        Stmt(sqlite3* db,const char* q){
            if(sqlite3_prepare_v2(db,q,-1,&s,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 justice SQL prepare");
        }
        ~Stmt(){sqlite3_finalize(s);}
        void number(int n,std::uint64_t v){
            if(v>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
               sqlite3_bind_int64(s,n,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
                throw std::runtime_error("POP10 justice signed identity overflow");
        }
    };
    void sql(const char* query){
        char* err=nullptr;
        if(sqlite3_exec(db_,query,nullptr,nullptr,&err)!=SQLITE_OK){
            const std::string s=err?err:"POP10 justice SQL failed";
            sqlite3_free(err);throw std::runtime_error(s);
        }
    }
    static std::uint64_t value(sqlite3_stmt* s,int i){
        const auto n=sqlite3_column_int64(s,i);
        if(n<0)throw std::runtime_error("POP10 negative justice evidence");
        return static_cast<std::uint64_t>(n);
    }
    bool world_matched(const PersistentWorld& world){
        Stmt q(db_,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
        return sqlite3_step(q.s)==SQLITE_ROW &&
               value(q.s,0)==world.generation();
    }
    static bool named_independent(const Simulation& sim,EntityId colony,
                                  std::uint64_t id){
        const auto n=sim.npc_records().find(id);
        return id && n!=sim.npc_records().end() &&
            n->second.home_ship==colony && n->second.location==colony &&
            !n->second.transport_id && n->second.role==NpcRole::Ship &&
            !sim.npc_casualties().count(id) &&
            sim.npc_available_for_duty(id);
    }
public:
    explicit ProceduralJustice(const std::string& original_db) {
        try{
            if(sqlite3_open_v2(original_db.c_str(),&db_,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 justice original world unavailable");
            sqlite3_busy_timeout(db_,10000);
            Stmt check(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                           "AND name='snapshots'");
            if(sqlite3_step(check.s)!=SQLITE_ROW)
                throw std::runtime_error("POP10 justice refuses secondary database");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_justice_case("
                "id INTEGER PRIMARY KEY,owner INTEGER NOT NULL,"
                "colony INTEGER NOT NULL,referral INTEGER NOT NULL UNIQUE,"
                "complaint INTEGER NOT NULL,government INTEGER NOT NULL,"
                "judge INTEGER NOT NULL,officer INTEGER NOT NULL,"
                "reviewer INTEGER NOT NULL,respondent INTEGER NOT NULL,"
                "opened_generation INTEGER NOT NULL,opened_tick INTEGER NOT NULL)");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_justice_answer("
                "case_id INTEGER PRIMARY KEY REFERENCES sg_stage7_justice_case(id),"
                "respondent INTEGER NOT NULL,position INTEGER NOT NULL "
                "CHECK(position IN (1,2)),"
                "generation INTEGER NOT NULL,tick INTEGER NOT NULL)");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_justice_disposition("
                "id INTEGER PRIMARY KEY,"
                "case_id INTEGER NOT NULL UNIQUE REFERENCES sg_stage7_justice_case(id),"
                "judge INTEGER NOT NULL,outcome INTEGER NOT NULL "
                "CHECK(outcome IN (1,2,3)),generation INTEGER NOT NULL,"
                "tick INTEGER NOT NULL)");
            for(const char* name:{"sg_stage7_justice_case",
                                 "sg_stage7_justice_answer",
                                 "sg_stage7_justice_disposition"}){
                const auto u=std::string("CREATE TRIGGER IF NOT EXISTS ")+
                    name+"_immutable_update BEFORE UPDATE ON "+name+
                    " BEGIN SELECT RAISE(ABORT,'immutable due process');END";
                const auto d=std::string("CREATE TRIGGER IF NOT EXISTS ")+
                    name+"_immutable_delete BEFORE DELETE ON "+name+
                    " BEGIN SELECT RAISE(ABORT,'immutable due process');END";
                sql(u.c_str());sql(d.c_str());
            }
            ship_notifications::Outbox::ensure_schema(db_);
        }catch(...){if(db_)sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~ProceduralJustice(){if(db_)sqlite3_close(db_);}
    ProceduralJustice(const ProceduralJustice&)=delete;
    ProceduralJustice& operator=(const ProceduralJustice&)=delete;
    std::vector<JusticeCase> cases() {
        Stmt q(db_,"SELECT id,owner,colony,referral,complaint,government,"
                   "judge,officer,reviewer,respondent,opened_generation,"
                   "opened_tick FROM sg_stage7_justice_case ORDER BY id");
        std::vector<JusticeCase> all;
        for(int rc;(rc=sqlite3_step(q.s))!=SQLITE_DONE;){
            if(rc!=SQLITE_ROW)throw std::runtime_error("POP10 bad case index");
            JusticeCase c{};
            c.id=value(q.s,0);c.owner=value(q.s,1);
            c.colony=value(q.s,2);c.referral=value(q.s,3);
            c.complaint=value(q.s,4);c.government=value(q.s,5);
            c.judge=value(q.s,6);c.officer=value(q.s,7);
            c.original_reviewer=value(q.s,8);c.respondent=value(q.s,9);
            c.opened_generation=value(q.s,10);c.opened_tick=value(q.s,11);
            all.push_back(c);
        }
        return all;
    }
    std::vector<JusticeAnswer> answers(){
        Stmt q(db_,"SELECT case_id,respondent,position,generation,tick "
                   "FROM sg_stage7_justice_answer");
        std::vector<JusticeAnswer> out;
        for(int rc;(rc=sqlite3_step(q.s))!=SQLITE_DONE;){
            if(rc!=SQLITE_ROW)throw std::runtime_error("POP10 corrupt response");
            JusticeAnswer a{};a.case_id=value(q.s,0);
            a.respondent=value(q.s,1);
            a.position=static_cast<DefensePosition>(value(q.s,2));
            a.answered_generation=value(q.s,3);
            a.answered_tick=value(q.s,4);out.push_back(a);
        }
        return out;
    }
    std::vector<JusticeDisposition> dispositions(){
        Stmt q(db_,"SELECT id,case_id,judge,outcome,generation,tick "
                   "FROM sg_stage7_justice_disposition");
        std::vector<JusticeDisposition> out;
        for(int rc;(rc=sqlite3_step(q.s))!=SQLITE_DONE;){
            if(rc!=SQLITE_ROW)throw std::runtime_error("POP10 corrupt judgment");
            JusticeDisposition d{};d.id=value(q.s,0);d.case_id=value(q.s,1);
            d.independent_judge=value(q.s,2);
            d.result=static_cast<ProceduralDisposition>(value(q.s,3));
            d.decided_generation=value(q.s,4);d.decided_tick=value(q.s,5);
            out.push_back(d);
        }
        return out;
    }
    bool open(const PersistentWorld& world,
        const colony_disposition::TerritoryJournal& territory,
        const ship_comms::Journal& original_comms,
        const colony_law::Agency& agency,PlayerId authenticated_owner,
        EntityId colony,std::uint64_t referral,std::uint64_t case_id,
        bool separate_owner_approval){
        if(!separate_owner_approval || !case_id || !referral ||
           !world.generation() || !authenticated_owner ||
           authenticated_owner==Simulation::kServerAuthority)return false;
        territory.verify();
        const auto active=agency.active(world,authenticated_owner,colony);
        if(!active.event_id)return false;
        const auto& sim=world.world();
        const auto original=agency.referrals(authenticated_owner,colony);
        const auto found=std::find_if(original.begin(),original.end(),
            [&](const colony_law::Referral& x){
                return x.id==referral && x.officer==active.officer &&
                       x.government_event==active.government_event &&
                       x.owner==authenticated_owner && x.colony==colony;
            });
        if(found==original.end())return false;
        const auto evidence=ship_comms::review::assess(
            world,territory,original_comms,authenticated_owner,colony,colony,
            found->complaint,found->reviewer);
        if(evidence.status!=ship_comms::review::Status::AwaitingLawAgency ||
           !evidence.credible_nuisance_evidence ||
           evidence.reported_caller!=found->reported_caller ||
           evidence.points!=found->points ||
           evidence.reviewer_npc!=found->reviewer)return false;
        std::uint64_t judge=0;
        for(const auto& [id,n]:sim.npc_records()){
            (void)n;
            if(id!=active.leader && id!=active.officer &&
               id!=found->reviewer &&
               named_independent(sim,colony,id)){judge=id;break;}
        }
        if(!judge)return false;
        try{
            sql("BEGIN IMMEDIATE");
            if(!world_matched(world)){sql("ROLLBACK");return false;}
            Stmt insert(db_,"INSERT INTO sg_stage7_justice_case "
                "(id,owner,colony,referral,complaint,government,judge,"
                "officer,reviewer,respondent,opened_generation,opened_tick)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
            const std::uint64_t numbers[]{case_id,authenticated_owner,
                colony,referral,found->complaint,found->government_event,
                judge,found->officer,found->reviewer,found->reported_caller,
                world.generation(),sim.tick()};
            for(int i=0;i<12;++i)insert.number(i+1,numbers[i]);
            if(sqlite3_step(insert.s)!=SQLITE_DONE)
                throw std::runtime_error("POP10 case replay or duplicate");
            // Same original world outbox; the remote respondent only gets a
            // bounded notice, not the reporting colony's private records.
            ship_notifications::Outbox::record_agency(db_,
                found->reported_caller,case_id,colony,found->complaint,
                sim.tick());
            sql("COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
    // The reported owner alone may contest or REQUEST voluntary mediation.
    // No response can be manufactured from its absence, from an AI, or from
    // the complainant's owner privilege.
    bool answer(const PersistentWorld& world,PlayerId authenticated_respondent,
                std::uint64_t case_id,DefensePosition position){
        if(position!=DefensePosition::Contest &&
           position!=DefensePosition::MediationRequested)return false;
        const auto all=cases();
        const auto it=std::find_if(all.begin(),all.end(),
            [&](const JusticeCase& x){return x.id==case_id;});
        const auto decided=dispositions();
        if(it==all.end() || authenticated_respondent!=it->respondent ||
           !world.generation() ||
           world.generation()<it->opened_generation ||
           world.world().tick()<it->opened_tick ||
           std::any_of(decided.begin(),decided.end(),
               [&](const JusticeDisposition& x){return x.case_id==case_id;}))
            return false;
        try{
            sql("BEGIN IMMEDIATE");
            if(!world_matched(world)){sql("ROLLBACK");return false;}
            Stmt insert(db_,"INSERT INTO sg_stage7_justice_answer "
                "(case_id,respondent,position,generation,tick) VALUES(?,?,?,?,?)");
            const std::uint64_t vals[]{case_id,authenticated_respondent,
                static_cast<std::uint64_t>(position),world.generation(),
                world.world().tick()};
            for(int i=0;i<5;++i)insert.number(i+1,vals[i]);
            if(sqlite3_step(insert.s)!=SQLITE_DONE)
                throw std::runtime_error("POP10 duplicate answer");
            sql("COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
    bool decide(const PersistentWorld& world,
        const colony_law::Agency& agency,PlayerId authenticated_owner,
        std::uint64_t case_id,std::uint64_t decision_id,
        bool independent_owner_approval){
        if(!independent_owner_approval || !decision_id || !world.generation())
            return false;
        const auto all=cases();
        const auto it=std::find_if(all.begin(),all.end(),
            [&](const JusticeCase& x){return x.id==case_id;});
        if(it==all.end() || authenticated_owner!=it->owner ||
           world.generation()<it->opened_generation ||
           world.world().tick()<=it->opened_tick)return false;
        const auto appointment=agency.active(
            world,authenticated_owner,it->colony);
        if(!appointment.event_id ||
           appointment.government_event!=it->government ||
           appointment.officer!=it->officer ||
           !named_independent(world.world(),it->colony,it->judge) ||
           it->judge==appointment.leader || it->judge==appointment.officer ||
           it->judge==it->original_reviewer)return false;
        const auto all_responses=answers();
        const auto response=std::find_if(all_responses.begin(),
            all_responses.end(),
            [&](const JusticeAnswer& a){return a.case_id==case_id;});
        if(response==all_responses.end() &&
           world.world().tick()<it->opened_tick+3)return false;
        const auto outcome=response==all_responses.end()?
            ProceduralDisposition::ClosedWithoutAnswer:
            response->position==DefensePosition::MediationRequested?
                ProceduralDisposition::VoluntaryMediationOffered:
                ProceduralDisposition::NoFindingAfterContest;
        try{
            sql("BEGIN IMMEDIATE");
            if(!world_matched(world)){sql("ROLLBACK");return false;}
            Stmt insert(db_,"INSERT INTO sg_stage7_justice_disposition "
                "(id,case_id,judge,outcome,generation,tick) "
                "VALUES(?,?,?,?,?,?)");
            const std::uint64_t vals[]{decision_id,case_id,it->judge,
                static_cast<std::uint64_t>(outcome),
                world.generation(),world.world().tick()};
            for(int i=0;i<6;++i)insert.number(i+1,vals[i]);
            if(sqlite3_step(insert.s)!=SQLITE_DONE)
                throw std::runtime_error("POP10 duplicate disposition");
            sql("COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }
};
} // namespace galaxy::stage7
