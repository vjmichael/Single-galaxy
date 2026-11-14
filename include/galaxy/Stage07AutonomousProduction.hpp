#pragma once
// Stage 7 POP-10A: owner grants a finite original-workshop production budget.
// A trusted host may poll this scheduler without reapproving each tick, but
// the ONLY work execution is the original WO2-J ApproveWorkshopJob command,
// original named supervisor, original material/energy and original SQLite.
// No independent factory, worker, stock, skill, batch or success counter.
#include "galaxy/Stage07IndependentCommerce.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace galaxy::stage7 {
struct ProductionGrant {
    std::uint64_t id=0,issued_generation=0,expires_tick=0;
    PlayerId owner=0;
    EntityId home=0,site=0;
    std::uint64_t supervisor=0,appointment=0;
    WorkshopTrade trade=WorkshopTrade::Cut;
    std::uint32_t batches=0;
};
enum class ProductionState : std::uint8_t {
    Refused, WaitingForActualStock, IntentReserved, OriginalApplied,
    OriginalDenied
};
struct ProductionReceipt {
    ProductionState state=ProductionState::Refused;
    std::uint64_t grant_id=0,original_command=0;
};
class AutonomousProduction {
    sqlite3* db_=nullptr;
    struct Statement {
        sqlite3_stmt* s=nullptr;
        Statement(sqlite3* d,const char* query) {
            if(sqlite3_prepare_v2(d,query,-1,&s,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 production SQL prepare refused");
        }
        ~Statement(){sqlite3_finalize(s);}
        Statement(const Statement&)=delete;
        Statement& operator=(const Statement&)=delete;
        void n(int position,std::uint64_t value) {
            if(value>static_cast<std::uint64_t>(
               std::numeric_limits<sqlite3_int64>::max()) ||
               sqlite3_bind_int64(s,position,static_cast<sqlite3_int64>(
                   value))!=SQLITE_OK)
                throw std::runtime_error("POP10 production numeric bind rejected");
        }
    };
    void sql(const char* command) {
        char* detail=nullptr;
        if(sqlite3_exec(db_,command,nullptr,nullptr,&detail)!=SQLITE_OK) {
            std::string message=detail?detail:"POP10 SQL";
            sqlite3_free(detail);
            throw std::runtime_error(message);
        }
    }
    static std::uint64_t field(sqlite3_stmt* s,int position) {
        const auto value=sqlite3_column_int64(s,position);
        if(value<0)throw std::runtime_error("POP10 negative identity");
        return static_cast<std::uint64_t>(value);
    }
    bool bound_generation(const PersistentWorld& world) {
        Statement q(db_,"SELECT MAX(generation) FROM snapshots");
        return sqlite3_step(q.s)==SQLITE_ROW &&
            field(q.s,0)==world.generation();
    }
    std::optional<ProductionGrant> read(std::uint64_t id) {
        Statement q(db_,"SELECT id,issued_generation,expires_tick,owner,"
            "home,site,supervisor,appointment,trade,batches FROM "
            "sg_stage7_production_grant WHERE id=?");
        q.n(1,id);
        if(sqlite3_step(q.s)!=SQLITE_ROW)return std::nullopt;
        ProductionGrant p{};
        p.id=field(q.s,0);p.issued_generation=field(q.s,1);
        p.expires_tick=field(q.s,2);p.owner=field(q.s,3);
        p.home=field(q.s,4);p.site=field(q.s,5);
        p.supervisor=field(q.s,6);p.appointment=field(q.s,7);
        const auto trade=field(q.s,8);
        if(trade<1 || trade>2)throw std::runtime_error("POP10 invalid original trade");
        p.trade=static_cast<WorkshopTrade>(trade);
        const auto batches=field(q.s,9);
        if(batches==0 || batches>5)throw std::runtime_error("POP10 invalid grant cap");
        p.batches=static_cast<std::uint32_t>(batches);
        return p;
    }
    std::uint64_t intent(std::uint64_t grant) {
        Statement q(db_,"SELECT command FROM sg_stage7_production_intent "
                        "WHERE grant_id=?");
        q.n(1,grant);
        return sqlite3_step(q.s)==SQLITE_ROW?field(q.s,0):0;
    }
    static bool original_applied(const PersistentWorld& world,
                                 const ProductionGrant& g,std::uint64_t cmd) {
        const auto* shop=world.world().workshop(g.site);
        if(!shop || !world.world().has_command_id(cmd) ||
           shop->approved_jobs[g.trade==WorkshopTrade::Cut?0:1].command_id!=cmd)
            return false;
        unsigned n=0;
        for(const auto& event:world.world().events())
            if(event.command_id==cmd){
                if(event.result!=Outcome::Applied)return false;
                ++n;
            }
        return n==1;
    }
public:
    explicit AutonomousProduction(const std::string& original_sqlite) {
        try{
            if(sqlite3_open_v2(original_sqlite.c_str(),&db_,
               SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
                throw std::runtime_error("POP10 requires original SQLite");
            sqlite3_busy_timeout(db_,10000);
            Statement s(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                             "AND name='snapshots'");
            if(sqlite3_step(s.s)!=SQLITE_ROW)
                throw std::runtime_error("POP10 refuses a secondary world");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_production_grant("
                "id INTEGER PRIMARY KEY,issued_generation INTEGER NOT NULL,"
                "expires_tick INTEGER NOT NULL,owner INTEGER NOT NULL,"
                "home INTEGER NOT NULL,site INTEGER NOT NULL,"
                "supervisor INTEGER NOT NULL,appointment INTEGER NOT NULL,"
                "trade INTEGER NOT NULL CHECK(trade IN (1,2)),"
                "batches INTEGER NOT NULL CHECK(batches BETWEEN 1 AND 5))");
            sql("CREATE TABLE IF NOT EXISTS sg_stage7_production_intent("
                "grant_id INTEGER PRIMARY KEY REFERENCES "
                "sg_stage7_production_grant(id),command INTEGER NOT NULL UNIQUE)");
            for(const auto* table:{"sg_stage7_production_grant",
                                  "sg_stage7_production_intent"}){
                const auto update=std::string("CREATE TRIGGER IF NOT EXISTS ")+
                    table+"_freeze_u BEFORE UPDATE ON "+table+
                    " BEGIN SELECT RAISE(ABORT,'immutable production event'); END";
                const auto removal=std::string("CREATE TRIGGER IF NOT EXISTS ")+
                    table+"_freeze_d BEFORE DELETE ON "+table+
                    " BEGIN SELECT RAISE(ABORT,'immutable production event'); END";
                sql(update.c_str());sql(removal.c_str());
            }
        }catch(...){if(db_)sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~AutonomousProduction(){if(db_)sqlite3_close(db_);}
    AutonomousProduction(const AutonomousProduction&)=delete;
    AutonomousProduction& operator=(const AutonomousProduction&)=delete;

    // One authenticated owner approves an explicit site, existing certified
    // named supervisor and bounded <=5 batch cap. This is a durable standing
    // grant, not a permission for arbitrary player command execution.
    bool issue(const PersistentWorld& world,PlayerId authenticated_owner,
               EntityId home,EntityId site,WorkshopTrade trade,
               std::uint32_t batches,std::uint64_t end_tick,
               std::uint64_t grant_id,bool independent_owner_approval) {
        if(!independent_owner_approval || !world.generation() ||
           !authenticated_owner ||
           authenticated_owner==Simulation::kServerAuthority ||
           (trade!=WorkshopTrade::Cut && trade!=WorkshopTrade::Shape) ||
           !grant_id || batches==0 || batches>5 ||
           end_tick<=world.world().tick() ||
           end_tick-world.world().tick()>10000)
            return false;
        const auto* ship=world.world().find(home);
        const auto* shop=world.world().workshop(site);
        const auto* utility=world.world().utility(site);
        if(!ship || ship->owner_id!=authenticated_owner ||
           !shop || shop->source_ship!=home ||
           !utility || !utility->powered || utility->condition<=0 ||
           (trade==WorkshopTrade::Shape && !shop->finisher_installed) ||
           !shop->supervisor_npc_id || !shop->supervisor_appointment_id ||
           shop->supervisor_trade!=trade ||
           !world.world().workshop_trade_certified(
               shop->supervisor_npc_id,trade))
            return false;
        const auto& person=world.world().npc_records();
        const auto supervisor=person.find(shop->supervisor_npc_id);
        if(supervisor==person.end() || supervisor->second.home_ship!=home ||
           supervisor->second.location!=site || supervisor->second.transport_id ||
           world.world().npc_casualties().count(supervisor->first))
            return false;
        try {
            sql("BEGIN IMMEDIATE");
            if(!bound_generation(world)){sql("ROLLBACK");return false;}
            Statement w(db_,"INSERT INTO sg_stage7_production_grant "
                "(id,issued_generation,expires_tick,owner,home,site,supervisor,"
                "appointment,trade,batches) VALUES(?,?,?,?,?,?,?,?,?,?)");
            const std::uint64_t values[]{grant_id,world.generation(),end_tick,
                authenticated_owner,home,site,shop->supervisor_npc_id,
                shop->supervisor_appointment_id,
                static_cast<std::uint64_t>(trade),batches};
            for(int i=0;i<10;++i)w.n(i+1,values[i]);
            if(sqlite3_step(w.s)!=SQLITE_DONE)throw std::runtime_error(
                "POP10 original budget already assigned");
            sql("COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    }

    // Can run on a periodic trusted host tick with no new owner prompt.
    // The immutable grant reserves exactly ONE native job; a crash can retry
    // only its original command ID and never charge a second job to that grant.
    ProductionReceipt poll(PersistentWorld& world,std::uint64_t grant_id,
                           std::uint64_t native_command,
                           FailPoint fail=FailPoint::None) {
        ProductionReceipt out{};out.grant_id=grant_id;
        const auto g=read(grant_id);
        if(!g || !native_command ||
           native_command>static_cast<std::uint64_t>(
               std::numeric_limits<sqlite3_int64>::max()))
            return out;
        const auto old=intent(grant_id);
        if(old && old!=native_command)return out;
        out.original_command=native_command;
        if(old && original_applied(world,*g,native_command)){
            out.state=ProductionState::OriginalApplied;
            return out;
        }
        const auto* ship=world.world().find(g->home);
        const auto* shop=world.world().workshop(g->site);
        const auto* utility=world.world().utility(g->site);
        if(!ship || ship->owner_id!=g->owner ||
           !shop || shop->source_ship!=g->home ||
           shop->supervisor_npc_id!=g->supervisor ||
           shop->supervisor_appointment_id!=g->appointment ||
           shop->supervisor_trade!=g->trade ||
           !world.world().workshop_trade_certified(g->supervisor,g->trade) ||
           !world.generation() ||
           world.generation()<g->issued_generation ||
           world.world().tick()>g->expires_tick)
            return out;
        if(world.world().has_command_id(native_command)){
            out.state=ProductionState::IntentReserved; // no fake Applied
            return out;
        }
        if(!utility || !utility->powered || utility->condition<=0){
            out.state=ProductionState::WaitingForActualStock;return out;
        }
        const auto stock=g->trade==WorkshopTrade::Cut?
            shop->aluminum_kg:shop->panels_kg;
        if(stock<static_cast<std::int64_t>(g->batches)*20 ||
           utility->stored_energy<1){
            out.state=ProductionState::WaitingForActualStock;return out;
        }
        const auto native=workforce02::approved_workshop_job(
            world.world(),g->owner,g->home,g->site,g->trade,
            g->batches,native_command,true);
        if(!native)return out;
        if(!old){
            try {
                sql("BEGIN IMMEDIATE");
                if(!bound_generation(world) || intent(grant_id)){
                    sql("ROLLBACK");return out;
                }
                Statement s(db_,"INSERT INTO sg_stage7_production_intent "
                    "(grant_id,command) VALUES(?,?)");
                s.n(1,grant_id);s.n(2,native_command);
                if(sqlite3_step(s.s)!=SQLITE_DONE)
                    throw std::runtime_error("POP10 grant double booked");
                sql("COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);return out;}
        }
        out.state=ProductionState::IntentReserved;
        if(!world.submit(*native))return out;
        world.step(fail); // ORIGINAL durable receipt, energy and job admission
        out.state=original_applied(world,*g,native_command)?
            ProductionState::OriginalApplied:ProductionState::OriginalDenied;
        return out;
    }
};
} // namespace galaxy::stage7
