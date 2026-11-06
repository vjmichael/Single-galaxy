#pragma once
// SHIP-STATUS-07E: authoritative player/NPC ship-to-ship hailing.
// The recipient controls disclosure. Once it has identified itself to the
// calling account (or expressly declined contact), repeat unsolicited calls
// become immutable, escalating communications-abuse evidence.
//
// This journal never asserts criminal guilt, creates a police faction,
// grants force/arrest authority, or modifies the original Simulation.
#include "galaxy/TerritoryJournal.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::ship_comms {

enum class Kind : std::uint8_t {Call=1, Identify=2, Decline=3, Abuse=4};
enum class Status : std::uint8_t {
    Denied=0, CallSent=1, Identified=2, Declined=3,
    Penalized=4, Suppressed=5
};
// At the native 20–25 ms tick, roughly 48–60 seconds to respond.
inline constexpr std::uint64_t kReplyWindowTicks=2400;
inline constexpr std::uint64_t kRefusalQuietTicks=1200;
inline constexpr std::uint64_t kReportThreshold=3;
inline constexpr std::uint64_t kMaximumRecords=10000;

struct Receipt {
    std::uint64_t sequence=0,event_id=0,world_tick=0,world_generation=0;
    PlayerId caller_owner=0,recipient_controller=0;
    EntityId caller_ship=0,recipient_ship=0;
    Kind kind=Kind::Call;
    std::uint64_t related=0,penalty_points=0,cooldown_until=0;
    bool law_review_eligible=false;
    std::uint8_t proof_kind=0; // 0=not needed, 1=direct reply, 2=territorial
    std::string prior_digest,digest;
};
struct Result {
    Status status=Status::Denied;
    std::uint64_t event_id=0,penalty_points=0,cooldown_until=0;
    bool law_review_eligible=false;
};
struct OverflowPenalty {
    PlayerId caller_owner=0;
    EntityId recipient_ship=0;
    // The first ten detailed abuse receipts remain immutable. Further
    // committed violations accrue here using one compact row per pair.
    std::uint64_t total_points=0,seed_event=0,last_event=0;
    std::uint64_t last_tick=0,last_generation=0,cooldown_until=0;
};
struct Incoming {
    std::uint64_t request_id=0,request_tick=0;
    PlayerId caller=0; // authenticated initiating account, not a sensor guess
    EntityId caller_ship=0,recipient_ship=0;
};

class Journal {
    sqlite3* db_=nullptr;
    static void run(sqlite3* db,const char* q){
        char* error=nullptr;
        if(sqlite3_exec(db,q,nullptr,nullptr,&error)!=SQLITE_OK){
            const std::string message=error?error:"SQLite failure";
            sqlite3_free(error);throw std::runtime_error(message);
        }
    }
    struct Statement {
        sqlite3_stmt* stmt=nullptr;
        sqlite3* db=nullptr;
        Statement(sqlite3* database,const char* sql):db(database){
            if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
                throw std::runtime_error("ship hail SQL prepare failed");
        }
        ~Statement(){sqlite3_finalize(stmt);}
        Statement(const Statement&)=delete;
        Statement& operator=(const Statement&)=delete;
    };
    static std::uint64_t field(sqlite3_stmt* s,int c){
        const auto n=sqlite3_column_int64(s,c);
        if(n<0)throw std::runtime_error("ship hail negative identity");
        return static_cast<std::uint64_t>(n);
    }
    static std::string text_field(sqlite3_stmt* s,int c){
        const auto p=sqlite3_column_text(s,c);
        if(!p)throw std::runtime_error("missing ship hail checksum");
        return reinterpret_cast<const char*>(p);
    }
    static std::string digest_for(const Receipt& r){
        std::ostringstream s;
        s<<r.prior_digest<<'|'<<r.sequence<<'|'<<r.event_id<<'|'
         <<int(r.kind)<<'|'<<r.caller_owner<<'|'<<r.caller_ship<<'|'
         <<r.recipient_ship<<'|'<<r.recipient_controller<<'|'
         <<r.world_tick<<'|'<<r.world_generation<<'|'<<r.related<<'|'
         <<r.penalty_points<<'|'<<r.cooldown_until<<'|'
         <<int(r.law_review_eligible)<<'|'<<int(r.proof_kind);
        std::uint64_t value=14695981039346656037ULL;
        for(const unsigned char c:s.str()){
            value^=c;value*=1099511628211ULL;
        }
        std::ostringstream out;
        out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;
        return out.str();
    }
    bool seen(const galaxy::PersistentWorld& world,
              EntityId source,EntityId target) const {
        const auto* a=world.world().find(source);
        const auto* b=world.world().find(target);
        if(!a || !b || a->type!=EntityType::Ship ||
           b->type!=EntityType::Ship || source==target ||
           !world.world().same_local_frame(source,target))
            return false;
        const auto range=ship_sensors::detection_range_mm(
            world.world(),*a,*b);
        const auto dx=double(a->position.x)-b->position.x;
        const auto dy=double(a->position.y)-b->position.y;
        return std::hypot(dx,dy)<=double(range);
    }
    // sqlite3_save uses signed 64-bit fields. Caller supplies host-generated
    // event identities, never client-selected IDs or principal IDs.
    static void bind_num(sqlite3_stmt* s,int i,std::uint64_t value){
        if(value>std::uint64_t(std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(s,i,sqlite3_int64(value))!=SQLITE_OK)
            throw std::runtime_error("ship hail ID exceeds SQLite");
    }
    void commit(Receipt value){
        run(db_,"BEGIN IMMEDIATE");
        try{
            verify();
            const auto prior=history();
            if(!value.event_id || prior.size()>=kMaximumRecords)
                throw std::runtime_error("ship hail journal full or missing ID");
            if(std::any_of(prior.begin(),prior.end(),
                [&](const Receipt& e){return e.event_id==value.event_id;}))
                throw std::runtime_error("duplicate authoritative hail event");
            value.sequence=prior.size()+1;
            value.prior_digest=prior.empty()?"genesis":prior.back().digest;
            if(!prior.empty() &&
               (value.world_tick<prior.back().world_tick ||
                value.world_generation<prior.back().world_generation))
                throw std::runtime_error("stale ship hail commit");
            value.digest=digest_for(value);
            Statement s(db_,
                "INSERT INTO sg_ship_hail07e "
                "(sequence,event_id,kind,caller_owner,caller_ship,"
                "recipient_ship,recipient_controller,world_tick,"
                "world_generation,related,penalty,cooldown,review,proof,"
                "prior_digest,digest) VALUES "
                "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            const std::uint64_t cols[]{
                value.sequence,value.event_id,std::uint64_t(value.kind),
                value.caller_owner,value.caller_ship,value.recipient_ship,
                value.recipient_controller,value.world_tick,
                value.world_generation,value.related,value.penalty_points,
                value.cooldown_until,value.law_review_eligible?1ULL:0ULL,
                value.proof_kind};
            for(int i=0;i<14;++i)bind_num(s.stmt,i+1,cols[i]);
            if(sqlite3_bind_text(s.stmt,15,value.prior_digest.c_str(),-1,
                  SQLITE_TRANSIENT)!=SQLITE_OK ||
               sqlite3_bind_text(s.stmt,16,value.digest.c_str(),-1,
                  SQLITE_TRANSIENT)!=SQLITE_OK ||
               sqlite3_step(s.stmt)!=SQLITE_DONE)
                throw std::runtime_error("ship hail append failed");
            run(db_,"COMMIT");
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            throw;
        }
        verify();
    }
public:
    explicit Journal(const std::string& original_db){
        try{
            if(sqlite3_open_v2(original_db.c_str(),&db_,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,
                nullptr)!=SQLITE_OK)
                throw std::runtime_error("ship hail needs existing world SQLite");
            sqlite3_busy_timeout(db_,10000);
            // Never create a counterfeit second galaxy save.
            {
                Statement schema(db_,
                    "SELECT 1 FROM sqlite_master WHERE "
                    "type='table' AND name='snapshots'");
                if(sqlite3_step(schema.stmt)!=SQLITE_ROW)
                    throw std::runtime_error("ship hail needs original snapshots");
            }
            run(db_,"CREATE TABLE IF NOT EXISTS sg_ship_hail07e ("
                "sequence INTEGER PRIMARY KEY,event_id INTEGER UNIQUE NOT NULL,"
                "kind INTEGER NOT NULL,caller_owner INTEGER NOT NULL,"
                "caller_ship INTEGER NOT NULL,recipient_ship INTEGER NOT NULL,"
                "recipient_controller INTEGER NOT NULL,"
                "world_tick INTEGER NOT NULL,world_generation INTEGER NOT NULL,"
                "related INTEGER NOT NULL,penalty INTEGER NOT NULL,"
                "cooldown INTEGER NOT NULL,review INTEGER NOT NULL,"
                "proof INTEGER NOT NULL,prior_digest TEXT NOT NULL,"
                "digest TEXT NOT NULL)");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_ship_hail07e_totals ("
                "caller_owner INTEGER NOT NULL,recipient_ship INTEGER NOT NULL,"
                "points INTEGER NOT NULL,seed_event INTEGER NOT NULL,"
                "last_event INTEGER UNIQUE NOT NULL,last_tick INTEGER NOT NULL,"
                "last_generation INTEGER NOT NULL,cooldown INTEGER NOT NULL,"
                "PRIMARY KEY(caller_owner,recipient_ship),"
                "CHECK(points>=11))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_hail07e_immutable_update "
                "BEFORE UPDATE ON sg_ship_hail07e "
                "BEGIN SELECT RAISE(ABORT,'immutable ship hail'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_hail07e_immutable_delete "
                "BEFORE DELETE ON sg_ship_hail07e "
                "BEGIN SELECT RAISE(ABORT,'immutable ship hail'); END");
            verify();
        }catch(...){
            if(db_)sqlite3_close(db_);
            throw;
        }
    }
    ~Journal(){if(db_)sqlite3_close(db_);}
    Journal(const Journal&)=delete;
    Journal& operator=(const Journal&)=delete;

    std::vector<Receipt> history() const {
        Statement s(db_,"SELECT sequence,event_id,kind,caller_owner,"
            "caller_ship,recipient_ship,recipient_controller,world_tick,"
            "world_generation,related,penalty,cooldown,review,proof,"
            "prior_digest,digest FROM sg_ship_hail07e ORDER BY sequence");
        std::vector<Receipt> result;
        int rc=SQLITE_OK;
        while((rc=sqlite3_step(s.stmt))==SQLITE_ROW){
            Receipt r{};
            r.sequence=field(s.stmt,0);r.event_id=field(s.stmt,1);
            r.kind=static_cast<Kind>(field(s.stmt,2));
            r.caller_owner=field(s.stmt,3);
            r.caller_ship=field(s.stmt,4);
            r.recipient_ship=field(s.stmt,5);
            r.recipient_controller=field(s.stmt,6);
            r.world_tick=field(s.stmt,7);r.world_generation=field(s.stmt,8);
            r.related=field(s.stmt,9);r.penalty_points=field(s.stmt,10);
            r.cooldown_until=field(s.stmt,11);
            r.law_review_eligible=field(s.stmt,12)!=0;
            r.proof_kind=static_cast<std::uint8_t>(field(s.stmt,13));
            r.prior_digest=text_field(s.stmt,14);
            r.digest=text_field(s.stmt,15);
            result.push_back(std::move(r));
            if(result.size()>kMaximumRecords)
                throw std::runtime_error("ship hail record limit exceeded");
        }
        if(rc!=SQLITE_DONE)
            throw std::runtime_error("ship hail read failed");
        return result;
    }
    std::vector<OverflowPenalty> overflow_history() const {
        Statement s(db_,
            "SELECT caller_owner,recipient_ship,points,seed_event,"
            "last_event,last_tick,last_generation,cooldown "
            "FROM sg_ship_hail07e_totals ORDER BY caller_owner,recipient_ship");
        std::vector<OverflowPenalty> out;
        int rc=SQLITE_OK;
        while((rc=sqlite3_step(s.stmt))==SQLITE_ROW){
            OverflowPenalty p{};
            p.caller_owner=field(s.stmt,0);
            p.recipient_ship=field(s.stmt,1);
            p.total_points=field(s.stmt,2);
            p.seed_event=field(s.stmt,3);
            p.last_event=field(s.stmt,4);
            p.last_tick=field(s.stmt,5);
            p.last_generation=field(s.stmt,6);
            p.cooldown_until=field(s.stmt,7);
            out.push_back(p);
            if(out.size()>kMaximumRecords)
                throw std::runtime_error("ship hail aggregate row limit");
        }
        if(rc!=SQLITE_DONE)
            throw std::runtime_error("ship hail aggregate read failure");
        return out;
    }
    void verify() const {
        std::uint64_t sequence=0,tick=0,generation=0;
        std::string previous="genesis";
        std::map<std::uint64_t,Receipt> preceding;
        for(const auto& e:history()){
            if(e.sequence!=++sequence || !e.event_id || !e.caller_owner ||
               e.caller_owner==Simulation::kServerAuthority ||
               !e.caller_ship || !e.recipient_ship ||
               e.caller_ship==e.recipient_ship ||
               int(e.kind)<1 || int(e.kind)>4 ||
               e.world_tick<tick || e.world_generation<generation ||
               e.prior_digest!=previous || e.digest!=digest_for(e) ||
               (e.kind==Kind::Call &&
                 (e.related || e.penalty_points ||
                  e.cooldown_until || e.law_review_eligible ||
                  e.recipient_controller || e.proof_kind)) ||
               ((e.kind==Kind::Identify || e.kind==Kind::Decline) &&
                 (!e.related || e.penalty_points || e.cooldown_until ||
                  e.law_review_eligible || e.proof_kind ||
                  (e.kind==Kind::Identify && !e.recipient_controller) ||
                  (e.kind==Kind::Decline && e.recipient_controller))) ||
               (e.kind==Kind::Abuse &&
                 (!e.related || !e.penalty_points ||
                  e.cooldown_until<=e.world_tick ||
                  e.proof_kind<1 || e.proof_kind>2 ||
                  (e.penalty_points>=kReportThreshold)!=
                      e.law_review_eligible ||
                  e.recipient_controller)) ||
               !preceding.emplace(e.event_id,e).second)
                throw std::runtime_error(
                    "corrupt or rewritten immutable ship hail history");
            if(e.kind==Kind::Identify || e.kind==Kind::Decline){
                const auto it=preceding.find(e.related);
                if(it==preceding.end() || it->second.kind!=Kind::Call ||
                   it->second.caller_owner!=e.caller_owner ||
                   it->second.caller_ship!=e.caller_ship ||
                   it->second.recipient_ship!=e.recipient_ship ||
                   e.world_tick<it->second.world_tick ||
                   e.world_tick-it->second.world_tick>kReplyWindowTicks)
                    throw std::runtime_error("ship hail reply lacks original call");
                for(const auto& [id,prior]:preceding){
                    (void)id;
                    if(prior.event_id!=e.event_id &&
                       (prior.kind==Kind::Identify || prior.kind==Kind::Decline) &&
                       prior.related==e.related)
                        throw std::runtime_error("duplicate ship hail answer");
                }
            }else if(e.kind==Kind::Abuse && e.proof_kind==1){
                const auto it=preceding.find(e.related);
                if(it==preceding.end() ||
                   it->second.kind!=Kind::Identify ||
                   it->second.caller_owner!=e.caller_owner ||
                   it->second.recipient_ship!=e.recipient_ship)
                    throw std::runtime_error("ship hail abuse lacks prior identity");
            }else if(e.kind==Kind::Abuse && e.proof_kind==2){
                // The original separate immutable territorial journal must
                // contain a matching signed/linked actual identity receipt.
                Statement proof(db_,
                  "SELECT 1 FROM sg_disposition02 WHERE event_id=? "
                  "AND colony=? AND owner=? AND visitor_ship=? "
                  "AND kind IN (7,8,9)");
                bind_num(proof.stmt,1,e.related);
                bind_num(proof.stmt,2,e.caller_ship);
                bind_num(proof.stmt,3,e.caller_owner);
                bind_num(proof.stmt,4,e.recipient_ship);
                if(sqlite3_step(proof.stmt)!=SQLITE_ROW)
                    throw std::runtime_error(
                        "ship hail abuse lacks real territorial identity receipt");
            }
            previous=e.digest;tick=e.world_tick;
            generation=e.world_generation;
        }
        const auto events=history();
        for(const auto& p:overflow_history()){
            const auto seed=std::find_if(events.begin(),events.end(),
                [&](const Receipt& e){
                    return e.event_id==p.seed_event &&
                           e.kind==Kind::Abuse &&
                           e.caller_owner==p.caller_owner &&
                           e.recipient_ship==p.recipient_ship &&
                           e.penalty_points==10;
                });
            if(seed==events.end() || !p.caller_owner ||
               !p.recipient_ship || !p.last_event ||
               p.total_points<11 ||
               p.total_points>std::uint64_t(
                   std::numeric_limits<sqlite3_int64>::max()) ||
               p.last_tick<seed->world_tick ||
               p.last_generation<seed->world_generation ||
               p.cooldown_until<=p.last_tick ||
               std::any_of(events.begin(),events.end(),
                 [&](const Receipt& e){return e.event_id==p.last_event;}))
                throw std::runtime_error(
                    "ship hail compact penalty cannot reconcile to ten original receipts");
        }
    }
    // After ten immutable detailed receipts, increment this same original
    // SQLite game's one-row-per-caller/recipient materialized penalty state.
    // Never create another user-visible hail or rewrite the original ten.
    // Any failure rolls back and the trusted host denies the outbound call.
    void compact_strike(const PersistentWorld& world,PlayerId caller,
        EntityId recipient,std::uint64_t event_id,
        std::uint64_t seed_id,std::uint64_t next_points,
        std::uint64_t cooldown_until){
        if(!caller || !recipient || !event_id || !seed_id ||
           next_points<11 ||
           next_points>std::uint64_t(
             std::numeric_limits<sqlite3_int64>::max()) ||
           cooldown_until<=world.world().tick() ||
           cooldown_until>std::uint64_t(
             std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid compact nuisance event");
        run(db_,"BEGIN IMMEDIATE");
        try{
            verify();
            const auto rows=overflow_history();
            const auto prior=std::find_if(rows.begin(),rows.end(),
                [&](const OverflowPenalty& p){
                    return p.caller_owner==caller &&
                           p.recipient_ship==recipient;
                });
            const auto events=history();
            const auto seed=std::find_if(events.begin(),events.end(),
                [&](const Receipt& r){
                    return r.event_id==seed_id &&
                           r.caller_owner==caller &&
                           r.recipient_ship==recipient &&
                           r.kind==Kind::Abuse && r.penalty_points==10;
                });
            if(seed==events.end() ||
               world.world().tick()<seed->world_tick ||
               world.generation()<seed->world_generation ||
               std::any_of(events.begin(),events.end(),
                 [&](const Receipt& e){return e.event_id==event_id;}))
                throw std::runtime_error("compact penalty seed not proven");
            if(prior==rows.end()){
                if(next_points!=11 || rows.size()>=kMaximumRecords)
                    throw std::runtime_error("compact penalty quota or sequence");
                Statement q(db_,"INSERT INTO sg_ship_hail07e_totals "
                    "(caller_owner,recipient_ship,points,seed_event,last_event,"
                    "last_tick,last_generation,cooldown) "
                    "VALUES (?,?,?,?,?,?,?,?)");
                const std::uint64_t v[]{caller,recipient,next_points,seed_id,
                    event_id,world.world().tick(),world.generation(),
                    cooldown_until};
                for(int i=0;i<8;++i)bind_num(q.stmt,i+1,v[i]);
                if(sqlite3_step(q.stmt)!=SQLITE_DONE)
                    throw std::runtime_error("compact penalty insert failed");
            }else{
                if(prior->seed_event!=seed_id ||
                   prior->total_points+1!=next_points ||
                   world.world().tick()<=prior->last_tick ||
                   world.generation()<prior->last_generation)
                    throw std::runtime_error("duplicate compact penalty tick");
                Statement q(db_,"UPDATE sg_ship_hail07e_totals SET "
                    "points=?,last_event=?,last_tick=?,last_generation=?,"
                    "cooldown=? WHERE caller_owner=? AND recipient_ship=? "
                    "AND points=? AND last_tick<?");
                const std::uint64_t v[]{next_points,event_id,
                    world.world().tick(),world.generation(),cooldown_until,
                    caller,recipient,prior->total_points,world.world().tick()};
                for(int i=0;i<9;++i)bind_num(q.stmt,i+1,v[i]);
                if(sqlite3_step(q.stmt)!=SQLITE_DONE ||
                   sqlite3_changes(db_)!=1)
                    throw std::runtime_error("compact penalty CAS failed");
            }
            run(db_,"COMMIT");
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            throw;
        }
        verify();
    }
    Result call(const PersistentWorld& world,
                const colony_disposition::TerritoryJournal& territory,
                PlayerId principal,EntityId source,EntityId target,
                std::uint64_t event_id) {
        Result outcome{};
        const auto* caller=world.world().find(source);
        const auto* recipient=world.world().find(target);
        if(!event_id || !principal || principal==Simulation::kServerAuthority ||
           !caller || !recipient || caller->owner_id!=principal ||
           !recipient->owner_id || recipient->owner_id==principal ||
           !seen(world,source,target))return outcome;
        verify();
        const auto old=history();
        const bool journal_full=old.size()>=kMaximumRecords;
        std::uint64_t proof=0,prior_score=0,cooldown_until=0;
        std::uint64_t seed_id=0;
        std::uint64_t declined_until=0;
        std::uint8_t proof_kind=0;
        bool already_scored_this_tick=false;
        std::map<std::uint64_t,std::uint64_t> pending;
        for(const auto& e:old){
            if(e.caller_owner!=principal || e.recipient_ship!=target)
                continue;
            if(e.kind==Kind::Identify &&
               e.recipient_controller==recipient->owner_id){
                proof=e.event_id;proof_kind=1;
            }else if(e.kind==Kind::Call){
                pending.emplace(e.event_id,e.world_tick);
            }else if(e.kind==Kind::Decline){
                pending.erase(e.related);
                declined_until=std::max(
                    declined_until,e.world_tick+kRefusalQuietTicks);
            }else if(e.kind==Kind::Abuse){
                prior_score=std::max(prior_score,e.penalty_points);
                cooldown_until=std::max(cooldown_until,e.cooldown_until);
                if(e.penalty_points==10)seed_id=e.event_id;
                if(e.world_tick==world.world().tick())
                    already_scored_this_tick=true;
            }
            if(e.kind==Kind::Identify)pending.erase(e.related);
        }
        for(const auto& compact:overflow_history()){
            if(compact.caller_owner!=principal ||
               compact.recipient_ship!=target)continue;
            prior_score=compact.total_points;
            seed_id=compact.seed_event;
            cooldown_until=std::max(cooldown_until,compact.cooldown_until);
            if(compact.last_tick==world.world().tick())
                already_scored_this_tick=true;
        }
        // A voluntarily identified visiting ship is already known to the
        // exact claimed colony account. Verify the original immutable chain
        // before accepting a historical identity receipt as a penalty basis.
        territory.verify();
        for(const auto& t:territory.history()){
            if(t.colony==source && t.owner==principal &&
               t.visitor_ship==target &&
               t.visitor_owner==recipient->owner_id &&
               (t.kind==colony_disposition::TerritoryEventKind::VoluntaryHail ||
                t.kind==colony_disposition::TerritoryEventKind::NoticeAcknowledged ||
                t.kind==colony_disposition::TerritoryEventKind::NoticeRefused)){
                proof=t.event_id;proof_kind=2;
            }
        }
        if(proof){
            const auto account=penalty_for(world,principal,source);
            outcome.penalty_points=account.penalty_points;
            outcome.cooldown_until=account.cooldown_until;
            outcome.law_review_eligible=account.law_review_eligible;
            if(already_scored_this_tick ||
               prior_score>=std::uint64_t(
                  std::numeric_limits<sqlite3_int64>::max())){
                outcome.status=Status::Suppressed;return outcome;
            }
            // Every new *tick* can accrue more points, regardless of the
            // ten-row detailed evidence cap. The actual call stays blocked.
            const auto points=prior_score+1;
            const auto timeout=points==1?200ULL:points==2?800ULL:
                points==3?2400ULL:7200ULL;
            if(world.world().tick()>
               std::uint64_t(std::numeric_limits<sqlite3_int64>::max())
                   -timeout){
                outcome.status=Status::Suppressed;return outcome;
            }
            const auto expires=world.world().tick()+timeout;
            if(points<=10){
                if(journal_full){
                    outcome.status=Status::Suppressed;return outcome;
                }
                Receipt r{};
                r.event_id=event_id;r.kind=Kind::Abuse;
                r.world_tick=world.world().tick();
                r.world_generation=world.generation();
                r.caller_owner=principal;r.caller_ship=source;
                r.recipient_ship=target;r.related=proof;
                r.proof_kind=proof_kind;
                r.penalty_points=points;
                r.cooldown_until=expires;
                r.law_review_eligible=points>=kReportThreshold;
                commit(r);
            }else{
                // A bounded aggregate row is updated transactionally once
                // per new tick. Ten original detailed receipts stay immutable.
                compact_strike(world,principal,target,event_id,
                               seed_id,points,expires);
            }
            const auto updated=penalty_for(world,principal,source);
            outcome.status=Status::Penalized;
            outcome.event_id=event_id;
            outcome.penalty_points=updated.penalty_points;
            outcome.cooldown_until=updated.cooldown_until;
            outcome.law_review_eligible=updated.law_review_eligible;
            return outcome;
        }
        // A nuisance strike mutes the verified caller's outbound hails to
        // ALL ships for its actual cooldown duration, not just this target.
        // Genuine inbound notice replies and distress workflows are separate.
        const auto global=penalty_for(world,principal,source);
        bool outstanding=false;
        for(const auto& [id,tick]:pending){
            (void)id;
            if(world.world().tick()>=tick &&
               world.world().tick()-tick<=kReplyWindowTicks)
                outstanding=true;
        }
        if(outstanding || declined_until>world.world().tick() ||
           global.cooldown_until>world.world().tick()){
            outcome.status=Status::Suppressed;
            outcome.cooldown_until=std::max(
                declined_until,global.cooldown_until);
            outcome.penalty_points=global.penalty_points;
            outcome.law_review_eligible=global.law_review_eligible;
            return outcome;
        }
        if(journal_full){
            // Full detailed ledger cannot produce another new call. The
            // already-identified pairs still accrue compact penalties.
            outcome.status=Status::Suppressed;
            return outcome;
        }
        Receipt r{};
        r.event_id=event_id;r.kind=Kind::Call;
        r.world_tick=world.world().tick();
        r.world_generation=world.generation();
        r.caller_owner=principal;
        r.caller_ship=source;r.recipient_ship=target;
        commit(r);
        outcome.status=Status::CallSent;
        outcome.event_id=event_id;
        return outcome;
    }
    Result reply(const PersistentWorld& world,PlayerId principal,
                 EntityId recipient_ship,std::uint64_t call_event,
                 std::uint64_t response_event,bool identify) {
        Result result{};
        const auto* target=world.world().find(recipient_ship);
        if(!principal || principal==Simulation::kServerAuthority ||
           !target || target->owner_id!=principal ||
           !call_event || !response_event ||
           call_event==response_event)return result;
        verify();
        const auto old=history();
        if(old.size()>=kMaximumRecords)return result;
        const auto found=std::find_if(old.begin(),old.end(),
            [&](const Receipt& r){
                return r.event_id==call_event && r.kind==Kind::Call &&
                       r.recipient_ship==recipient_ship;
            });
        if(found==old.end() ||
           world.world().tick()<found->world_tick ||
           world.world().tick()-found->world_tick>kReplyWindowTicks ||
           !world.world().same_local_frame(
                recipient_ship,found->caller_ship))
            return result;
        for(const auto& e:old)
            if((e.kind==Kind::Identify || e.kind==Kind::Decline) &&
               e.related==call_event)return result;
        Receipt r{};
        r.event_id=response_event;
        r.kind=identify?Kind::Identify:Kind::Decline;
        r.caller_owner=found->caller_owner;
        r.caller_ship=found->caller_ship;
        r.recipient_ship=recipient_ship;
        r.recipient_controller=identify?principal:0;
        r.related=call_event;
        r.world_tick=world.world().tick();
        r.world_generation=world.generation();
        commit(r);
        result.status=identify?Status::Identified:Status::Declined;
        result.event_id=response_event;
        return result;
    }
    std::vector<Incoming> pending_for(const PersistentWorld& world,
                     PlayerId principal,EntityId recipient_ship) const {
        std::vector<Incoming> inbox;
        const auto* recipient=world.world().find(recipient_ship);
        if(!principal || !recipient || recipient->owner_id!=principal)
            return inbox;
        const auto old=history();
        for(const auto& r:old){
            if(r.kind!=Kind::Call || r.recipient_ship!=recipient_ship ||
               world.world().tick()<r.world_tick ||
               world.world().tick()-r.world_tick>kReplyWindowTicks ||
               !world.world().same_local_frame(
                    recipient_ship,r.caller_ship))continue;
            const bool answered=std::any_of(old.begin(),old.end(),
                [&](const Receipt& other){
                    return (other.kind==Kind::Identify ||
                            other.kind==Kind::Decline) &&
                           other.related==r.event_id;
                });
            if(!answered)inbox.push_back(
                {r.event_id,r.world_tick,r.caller_owner,
                 r.caller_ship,recipient_ship});
            if(inbox.size()>=32)break;
        }
        return inbox;
    }
    Result penalty_for(const PersistentWorld& world,PlayerId principal,
                       EntityId source) const {
        Result result{};
        const auto* ship=world.world().find(source);
        if(!principal || !ship || ship->owner_id!=principal)return result;
        std::map<EntityId,std::uint64_t> by_recipient;
        for(const auto& e:history()){
            if(e.kind==Kind::Abuse && e.caller_owner==principal){
                auto& count=by_recipient[e.recipient_ship];
                count=std::max(count,e.penalty_points);
                result.cooldown_until=std::max(
                    result.cooldown_until,e.cooldown_until);
                result.law_review_eligible=
                    result.law_review_eligible||e.law_review_eligible;
            }
        }
        for(const auto& p:overflow_history()){
            if(p.caller_owner!=principal)continue;
            auto& count=by_recipient[p.recipient_ship];
            count=std::max(count,p.total_points);
            result.cooldown_until=std::max(
                result.cooldown_until,p.cooldown_until);
            result.law_review_eligible=true;
        }
        // Owner reputation is the SUM over genuinely verified targets,
        // not just the highest per-target strike sequence. The fixed-length
        // detailed evidence journal is not a cap on cumulative penalties.
        const auto limit=std::uint64_t(
            std::numeric_limits<sqlite3_int64>::max());
        for(const auto& [recipient,points]:by_recipient){
            (void)recipient;
            result.penalty_points=points>limit-result.penalty_points?
                limit:result.penalty_points+points;
        }
        return result;
    }
};
} // namespace galaxy::ship_comms
