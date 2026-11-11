#pragma once
// DOCK-04: original-world passenger bookings, exact physical ferry embark,
// bounded gangways and starvation-resistant cargo/passenger arbitration.
//
// This SQLite file is the SAME original PersistentWorld save, under the
// native station writer lease. There is no second NPC/cargo/ship authority.
// One boarding passes through the actual home-ship berth and the EXISTING
// HostEmbarkPassengers -> voyage -> NPC physical custody journal.
#include "galaxy/DockLogistics.hpp"
#include "galaxy/ColonySafety.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::dock04 {
struct TrafficView {
    std::uint64_t waiting=0,boarded=0,held=0,urgent=0;
    std::uint64_t passenger_gangways=0,congestion=0,passenger_streak=0;
    std::uint64_t demand=0; // only from supplied authoritative cohort count
};
// A pure integer, capped demand projection. This is PRESSURE, never a
// manifested passenger, berth, ship ticket or migration approval.
// O(1) for million-/billion-resident cohorts. No per-person allocation.
inline std::uint64_t projected_demand(
    std::uint64_t verified_residents,std::uint32_t travel_per_million,
    std::uint64_t physical_berth_capacity) {
    if(travel_per_million>1000000 || physical_berth_capacity>128)
        throw std::invalid_argument("unbounded aggregate station demand");
    const std::uint64_t candidate =
        (verified_residents/1000000)*travel_per_million+
        ((verified_residents%1000000)*travel_per_million)/1000000;
    return std::min(candidate,physical_berth_capacity);
}
class Traffic {
public:
    static constexpr std::uint64_t kMaxPending=32;
    static constexpr std::uint64_t kGangways=2;
    static constexpr std::uint64_t kFerrySeats=16;
    explicit Traffic(const std::string& path):file_(path) {
        if(sqlite3_open_v2(path.c_str(),&db_,
            SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const std::string error=db_?sqlite3_errmsg(db_):"dock traffic SQLite open";
            sqlite3_close(db_);db_=nullptr;throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(db_,2000);
        exec("CREATE TABLE IF NOT EXISTS dock04_boarding_v1("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "station INTEGER NOT NULL CHECK(station>0),"
             "owner INTEGER NOT NULL CHECK(owner>0),"
             "npc_id INTEGER NOT NULL CHECK(npc_id>0),"
             "source_ship INTEGER NOT NULL CHECK(source_ship>0),"
             "ferry INTEGER NOT NULL CHECK(ferry>0),"
             "priority INTEGER NOT NULL CHECK(priority BETWEEN 0 AND 2),"
             "phase INTEGER NOT NULL CHECK(phase BETWEEN 0 AND 7),"
             "command_id INTEGER NOT NULL DEFAULT 0,"
             "transport_id INTEGER NOT NULL DEFAULT 0,"
             "worker_id INTEGER NOT NULL DEFAULT 0,"
             "created_tick INTEGER NOT NULL)");
        exec("CREATE INDEX IF NOT EXISTS dock04_station_jobs_v1 "
             "ON dock04_boarding_v1(station,owner,phase,priority,id)");
        exec("CREATE UNIQUE INDEX IF NOT EXISTS dock04_active_person_v1 "
             "ON dock04_boarding_v1(npc_id) WHERE phase NOT IN(6,7)");
        exec("CREATE TABLE IF NOT EXISTS dock04_flow_v1("
             "station INTEGER NOT NULL,owner INTEGER NOT NULL,"
             "passenger_streak INTEGER NOT NULL CHECK(passenger_streak BETWEEN 0 AND 2),"
             "PRIMARY KEY(station,owner))");
        exec("CREATE TABLE IF NOT EXISTS dock04_embark_evidence_v1("
             "command_id INTEGER PRIMARY KEY CHECK(command_id>0),"
             "job_id INTEGER NOT NULL UNIQUE,npc_id INTEGER NOT NULL,"
             "worker_id INTEGER NOT NULL,"
             "ferry INTEGER NOT NULL,transport_id INTEGER NOT NULL UNIQUE)");
    }
    ~Traffic(){sqlite3_close(db_);}
    Traffic(const Traffic&)=delete;
    Traffic& operator=(const Traffic&)=delete;

    // One real docking station admits at most two concurrently usable
    // passenger gangways. These are SERVICE slots, NOT fabricated physical
    // ships. Ferry passenger berths are separately enforced by Simulation.
    std::uint64_t physical_gangways(
        const Simulation& world,EntityId station,PlayerId owner) const{
        std::uint64_t count=0;
        for(const auto& [id,ship]:world.entities()){
            (void)id;
            if(ship.type==EntityType::Ship && ship.owner_id==owner &&
               ship.dock_station==station && ship.dock_state==DockState::Docked &&
               world.colony_ship(ship.id) && ++count>=kGangways)break;
        }
        return count>=2 ? kGangways : 0; // need a real source AND ferry
    }
    bool eligible(const Simulation& world,EntityId station,PlayerId owner,
                  std::uint64_t npc_id,EntityId ferry) const{
        if(!station||!owner||!npc_id||!ferry||active(station,owner)>=kMaxPending ||
           !physical_gangways(world,station,owner))return false;
        const auto it=world.npc_records().find(npc_id);
        if(it==world.npc_records().end() || it->second.role!=NpcRole::Ship ||
           it->second.transport_id || it->second.mentor_id ||
           (it->second.assigned_site &&
            it->second.assigned_site!=it->second.home_ship) ||
           !(it->second.location==station ?
             world.npc_available_for_shore_duty(npc_id,station):
             world.npc_available_for_duty(npc_id)) ||
           workforce02::already_on_named_duty(world,npc_id,world.tick()))
           return false;
        const auto& npc=it->second;
        const auto* source=world.find(npc.home_ship);
        const auto* vessel=world.find(ferry);
        if(!source || !vessel || npc.home_ship==ferry ||
           source->owner_id!=owner || vessel->owner_id!=owner ||
           source->type!=EntityType::Ship || vessel->type!=EntityType::Ship ||
           source->dock_state!=DockState::Docked ||
           vessel->dock_state!=DockState::Docked ||
           source->dock_station!=station || vessel->dock_station!=station ||
           source->system_id!=vessel->system_id ||
           !world.colony_ship(npc.home_ship) || !world.colony_ship(ferry) ||
           (npc.location!=station && npc.location!=npc.home_ship) ||
           world.colony_ship(npc.home_ship)->crew<=1 ||
           world.passengers_aboard(ferry)>=kFerrySeats)return false;
        // An actual passenger is not permitted to leave the final operating
        // crew member aboard the original vessel. DOCK-03 applies the same
        // physical rule before any shore crossing, but refuse early at booking.
        if(npc.location==npc.home_ship) {
            std::uint64_t onboard=0,scanned=0;
            for(const auto& [id,p]:world.npc_records()) {
                (void)id;
                if(++scanned>4096)return false;
                if(p.home_ship==npc.home_ship && p.location==npc.home_ship &&
                   p.role==NpcRole::Ship && !p.transport_id)++onboard;
            }
            // Reserve against every outstanding request for this source:
            // no overbooking a vessel's last real crew at the gangway.
            if(onboard<=1 || source_reserved(npc.home_ship)>=onboard-1)
                return false;
        }
        // Physical deck occupancy + all pending bookings must fit in the
        // ORIGINAL sixteen real ferry berths. No optimistic seat claim.
        const auto booked=booked_seats(ferry);
        if(booked>=kFerrySeats ||
           world.passengers_aboard(ferry)>kFerrySeats-1-booked)return false;
        const auto* stock=world.life_support(ferry);
        const auto actual=world.people_physically_aboard(ferry);
        if(!stock || actual>1000 || booked>1000-actual ||
           actual+booked+1>1000)return false;
        const auto needs=static_cast<std::int64_t>(actual+booked+1)*2;
        if(stock->food_kg<needs || stock->water_kg<needs ||
           stock->oxygen_kg<needs)return false;
        if(pending_for_person(npc_id) || on_active_dock_shift(npc_id,world.tick()))
            return false;
        return true;
    }
    bool enqueue(const Simulation& world,EntityId station,PlayerId owner,
                 std::uint64_t npc,EntityId ferry){
        if(!eligible(world,station,owner,npc,ferry))return false;
        const auto home=world.npc_records().at(npc).home_ship;
        // Emergency precedence is based only on the ORIGINAL live colony
        // safety model. There is no priority input from a client.
        const auto risk=colony_safety::assess(world,home,home).risk;
        const auto priority=
            static_cast<int>(risk)>=static_cast<int>(colony_safety::Risk::Critical)
                ?std::uint64_t{2}:std::uint64_t{0};
        dock::Statement q(db_,"INSERT INTO dock04_boarding_v1 "
            "(station,owner,npc_id,source_ship,ferry,priority,phase,created_tick)"
            " VALUES(?,?,?,?,?,?,0,?)");
        q.bind(1,station);q.bind(2,owner);q.bind(3,npc);q.bind(4,home);
        q.bind(5,ferry);q.bind(6,priority);q.bind(7,world.tick());q.next();
        return sqlite3_changes(db_)==1;
    }
    TrafficView snapshot(const Simulation& world,EntityId station,
                         PlayerId owner) const {
        TrafficView v{};
        dock::Statement q(db_,"SELECT priority,phase,COUNT(*) FROM dock04_boarding_v1 "
                             "WHERE station=? AND owner=? GROUP BY priority,phase");
        q.bind(1,station);q.bind(2,owner);
        while(q.next()==SQLITE_ROW) {
            const auto pr=q.number(0),state=q.number(1),count=q.number(2);
            if(state==6)v.boarded+=count;
            else if(state==7)v.held+=count;
            else {
                v.waiting+=count;
                if(pr==2)v.urgent+=count;
            }
        }
        v.passenger_gangways=physical_gangways(world,station,owner);
        v.passenger_streak=streak(station,owner);
        // Congestion is explicit bounded WAITING WORK per active gangway,
        // never a fabricated person or a per-population pathfinding actor.
        v.congestion=v.passenger_gangways ?
            std::min<std::uint64_t>(100,
                (100*v.waiting)/(kMaxPending*v.passenger_gangways)) : 
            (v.waiting?100:0);
        return v;
    }
    std::uint64_t active(EntityId station,PlayerId owner) const {
        dock::Statement q(db_,"SELECT COUNT(*) FROM dock04_boarding_v1 "
                             "WHERE station=? AND owner=? AND phase NOT IN(6,7)");
        q.bind(1,station);q.bind(2,owner);
        return q.next()==SQLITE_ROW?q.number(0):0;
    }
    std::uint64_t streak(EntityId station,PlayerId owner) const{
        dock::Statement q(db_,"SELECT passenger_streak FROM dock04_flow_v1 "
                             "WHERE station=? AND owner=?");
        q.bind(1,station);q.bind(2,owner);
        return q.next()==SQLITE_ROW?q.number(0):0;
    }
    void freight_completed(EntityId station,PlayerId owner){
        dock::Statement s(db_,"INSERT INTO dock04_flow_v1 "
          "(station,owner,passenger_streak) VALUES(?,?,0) "
          "ON CONFLICT(station,owner) DO UPDATE SET passenger_streak=0");
        s.bind(1,station);s.bind(2,owner);s.next();
    }
    // Reconcile ORIGINAL APPLIED voyage results, not a queue timestamp.
    // Any in-between physical mutation without its receipt is held for
    // owner review: never silently retry an ambiguous passenger movement.
    void reconcile(PersistentWorld& world,EntityId station,PlayerId owner){
        dock::Statement q(db_,"SELECT id,npc_id,source_ship,ferry,phase,"
                             "command_id,transport_id FROM dock04_boarding_v1 "
                             "WHERE station=? AND owner=? AND "
                             "(phase IN(1,3,5) OR (phase=7 AND command_id>0))");
        q.bind(1,station);q.bind(2,owner);
        struct Pending{std::uint64_t id,npc,source,ferry,phase,cmd,transport;};
        std::vector<Pending> jobs;
        while(q.next()==SQLITE_ROW)
            jobs.push_back({q.number(0),q.number(1),q.number(2),q.number(3),
                            q.number(4),q.number(5),q.number(6)});
        for(const auto& j:jobs){
            const auto it=world.world().npc_records().find(j.npc);
            if(it==world.world().npc_records().end()){hold(j.id);continue;}
            if(j.phase==1||j.phase==3){
                const auto next=j.phase==1?station:j.source;
                const auto expected_receipt=move_receipt(j.id,j.phase==1);
                if(it->second.location==next &&
                   move_proven(expected_receipt,station,j.source,j.npc,
                               j.phase==1)){
                    set_phase(j.id,j.phase,j.phase==1?2:4);
                }else hold(j.id);
            }else{
                bool applied=false;
                for(const auto& event:world.world().events())
                    if(event.command_id==j.cmd &&
                       event.result==Outcome::Applied)applied=true;
                if(applied && world.world().voyage_status(j.transport)==1 &&
                   it->second.location==j.ferry &&
                   it->second.transport_id==j.transport) {
                    if(j.phase==7){
                        // Held due to original-world in-flight uncertainty,
                        // subsequently resolved by original APPLIED evidence.
                        dock::Statement restore(db_,"UPDATE dock04_boarding_v1 "
                            "SET phase=5 WHERE id=? AND phase=7 AND command_id=?");
                        restore.bind(1,j.id);restore.bind(2,j.cmd);restore.next();
                    }
                    complete(j.id,j.cmd,j.transport,j.npc,j.ferry,station,owner);
                }else if(j.phase!=7)hold(j.id);
            }
        }
    }
    // One verified boarding is up to three original durable physical steps:
    // home ship -> local check-in -> source hatch -> real ferry voyage.
    // Each crossing is idempotently reserved in SQLite before original STEP.
    bool serve_one(PersistentWorld& world,EntityId station,PlayerId owner,
                   std::uint64_t worker){
        if(!worker || !physical_gangways(world.world(),station,owner))
            return false;
        dock::Ledger original_staff(file_);
        const auto roster=original_staff.service_roster(world.world(),station,owner);
        if(std::find(roster.begin(),roster.end(),worker)==roster.end())
            return false;
        dock::Statement q(db_,"SELECT id,npc_id,source_ship,ferry,phase "
            "FROM dock04_boarding_v1 WHERE station=? AND owner=? "
            "AND phase IN(0,2,4) ORDER BY priority DESC,id ASC LIMIT 1");
        q.bind(1,station);q.bind(2,owner);
        if(q.next()!=SQLITE_ROW)return false;
        const auto id=q.number(0),npc=q.number(1),source=q.number(2),
                   ferry=q.number(3),phase=q.number(4);
        if(id>0x0000FFFFFFFFFFFFULL){hold(id);return false;}
        if(!same_berth(world.world(),source,ferry,station,owner)){
            hold(id);return false;
        }
        auto person=world.world().npc_records().find(npc);
        if(person==world.world().npc_records().end() || person->second.transport_id ||
           person->second.home_ship!=source){hold(id);return false;}
        if(phase==0){
            if(person->second.location==station){
                // A real original shore-duty NPC may already have crossed
                // through the original hatch; don't teleport them to repeat
                // the outward transition solely for queue bookkeeping.
                set_phase(id,0,2);
            }else {
                if(person->second.location!=source){hold(id);return false;}
                set_phase(id,0,1);
                if(!world.station_dock_worker_transfer(
                    owner,source,station,npc,true,move_receipt(id,true))){
                    hold(id);return false;
                }
                set_phase(id,1,2);
            }
        }
        if(phase<=2){
            person=world.world().npc_records().find(npc);
            if(person==world.world().npc_records().end() ||
               person->second.location!=station){hold(id);return false;}
            set_phase(id,2,3);
            if(!world.station_dock_worker_transfer(
                owner,source,station,npc,false,move_receipt(id,false))){
                hold(id);return false;
            }
            set_phase(id,3,4);
        }
        // Revalidate station worker authority after BOTH original-world
        // physical transfers: logical ticks can expire a shift mid-service.
        const auto fresh_roster=original_staff.service_roster(world.world(),station,owner);
        if(std::find(fresh_roster.begin(),fresh_roster.end(),worker)==fresh_roster.end()){
            hold(id);return false;
        }
        const auto& current=world.world();
        person=current.npc_records().find(npc);
        if(person==current.npc_records().end() ||
           person->second.location!=source ||
           !same_berth(current,source,ferry,station,owner) ||
           current.passengers_aboard(ferry)>=kFerrySeats){
            hold(id);return false;
        }
        const auto cmd=command_id(id),voyage=voyage_id(id);
        reserve_board(id,cmd,voyage,worker);
        Command embark{};
        embark.id=cmd;embark.sequence=cmd;
        embark.execute_tick=current.tick()+1;
        embark.issuer=Simulation::kServerAuthority;
        embark.action=Action::HostEmbarkPassengers;
        embark.actor=ferry;embark.target=source;
        embark.transport_id=voyage;embark.passenger_ids={npc};
        if(!world.submit_host_passenger_action(embark)){
            hold(id);return false;
        }
        world.step();
        bool applied=false;
        for(const auto& event:world.world().events())
            if(event.command_id==cmd && event.result==Outcome::Applied)applied=true;
        const auto latest=world.world().npc_records().find(npc);
        if(!applied || world.world().voyage_status(voyage)!=1 ||
           latest==world.world().npc_records().end() ||
           latest->second.location!=ferry ||
           latest->second.transport_id!=voyage){
            hold(id);return false;
        }
        complete(id,cmd,voyage,npc,ferry,station,owner);
        return true;
    }
private:
    std::string file_;
    sqlite3* db_=nullptr;
    static std::uint64_t move_receipt(std::uint64_t job,bool outward) {
        return 0x5200000000000000ULL|(job<<8)|(outward?1ULL:2ULL);
    }
    static std::uint64_t command_id(std::uint64_t job) {
        return 0x5700000000000000ULL|(job<<8)|3ULL;
    }
    static std::uint64_t voyage_id(std::uint64_t job) {
        return 0x5600000000000000ULL|(job<<8)|4ULL;
    }
    void exec(const char* sql) const {
        char* e=nullptr;
        if(sqlite3_exec(db_,sql,nullptr,nullptr,&e)!=SQLITE_OK){
            const std::string reason=e?e:"dock04 SQLite error";
            sqlite3_free(e);throw std::runtime_error(reason);
        }
    }
    bool same_berth(const Simulation& world,EntityId source,EntityId ferry,
                    EntityId station,PlayerId owner) const {
        const auto* home=world.find(source),*boat=world.find(ferry);
        return home&&boat&&source!=ferry&&home->owner_id==owner&&
           boat->owner_id==owner&&home->type==EntityType::Ship&&
           boat->type==EntityType::Ship&&
           home->dock_state==DockState::Docked&&boat->dock_state==DockState::Docked&&
           home->dock_station==station&&boat->dock_station==station&&
           home->system_id==boat->system_id&&world.colony_ship(source)&&
           world.colony_ship(ferry);
    }
    std::uint64_t booked_seats(EntityId ferry) const {
        dock::Statement q(db_,"SELECT COUNT(*) FROM dock04_boarding_v1 "
                             "WHERE ferry=? AND phase NOT IN(6,7)");
        q.bind(1,ferry);return q.next()==SQLITE_ROW?q.number(0):0;
    }
    std::uint64_t source_reserved(EntityId source) const {
        dock::Statement q(db_,"SELECT COUNT(*) FROM dock04_boarding_v1 "
                             "WHERE source_ship=? AND phase NOT IN(6,7)");
        q.bind(1,source);return q.next()==SQLITE_ROW?q.number(0):0;
    }
    bool pending_for_person(std::uint64_t npc)const {
        dock::Statement q(db_,"SELECT 1 FROM dock04_boarding_v1 "
                             "WHERE npc_id=? AND phase NOT IN(6,7) LIMIT 1");
        q.bind(1,npc);return q.next()==SQLITE_ROW;
    }
    bool on_active_dock_shift(std::uint64_t npc,std::uint64_t tick)const {
        dock::Statement q(db_,"SELECT 1 FROM dock_staff_v1 WHERE npc_id=? "
                             "AND assigned_tick<=? AND shift_end_tick>? LIMIT 1");
        q.bind(1,npc);q.bind(2,tick);q.bind(3,tick);
        return q.next()==SQLITE_ROW;
    }
    bool move_proven(std::uint64_t receipt,EntityId station,EntityId source,
                     std::uint64_t npc,bool outward) const {
        dock::Statement table(db_,"SELECT 1 FROM sqlite_master "
             "WHERE type='table' AND name='sg_dock_worker_moves'");
        if(table.next()!=SQLITE_ROW)return false;
        dock::Statement q(db_,"SELECT 1 FROM sg_dock_worker_moves "
                             "WHERE receipt=? AND station=? AND ship=? "
                             "AND npc_id=? AND direction=? LIMIT 1");
        q.bind(1,receipt);q.bind(2,station);q.bind(3,source);
        q.bind(4,npc);q.bind(5,outward?1:2);return q.next()==SQLITE_ROW;
    }
    void set_phase(std::uint64_t id,std::uint64_t before,std::uint64_t after) {
        dock::Statement q(db_,"UPDATE dock04_boarding_v1 SET phase=? "
                             "WHERE id=? AND phase=?");
        q.bind(1,after);q.bind(2,id);q.bind(3,before);q.next();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("dock04 phase changed");
    }
    void hold(std::uint64_t id){
        dock::Statement q(db_,"UPDATE dock04_boarding_v1 SET phase=7 "
                             "WHERE id=? AND phase!=6");
        q.bind(1,id);q.next();
    }
    void reserve_board(std::uint64_t id,std::uint64_t cmd,
                       std::uint64_t voyage,std::uint64_t worker) {
        dock::Statement q(db_,"UPDATE dock04_boarding_v1 SET phase=5,"
                             "command_id=?,transport_id=?,worker_id=? "
                             "WHERE id=? AND phase=4");
        q.bind(1,cmd);q.bind(2,voyage);q.bind(3,worker);q.bind(4,id);q.next();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("boarding reservation changed");
    }
    void complete(std::uint64_t id,std::uint64_t cmd,
                  std::uint64_t voyage,std::uint64_t npc,EntityId ferry,
                  EntityId station,PlayerId owner) {
        exec("BEGIN IMMEDIATE");
        try{
            dock::Statement worker_record(db_,"SELECT worker_id FROM dock04_boarding_v1 "
                                              "WHERE id=? AND phase=5");
            worker_record.bind(1,id);
            if(worker_record.next()!=SQLITE_ROW || !worker_record.number(0))
                throw std::runtime_error("boarding lacks actual available dock worker");
            const auto worker_id=worker_record.number(0);
            dock::Statement q(db_,"INSERT OR IGNORE INTO dock04_embark_evidence_v1 "
                  "(command_id,job_id,npc_id,worker_id,ferry,transport_id) "
                  "VALUES(?,?,?,?,?,?)");
            q.bind(1,cmd);q.bind(2,id);q.bind(3,npc);
            q.bind(4,worker_id);q.bind(5,ferry);q.bind(6,voyage);q.next();
            if(sqlite3_changes(db_)!=1)
                throw std::runtime_error("boarding receipt already exists");
            dock::Statement done(db_,"UPDATE dock04_boarding_v1 SET phase=6 "
                                    "WHERE id=? AND phase=5 AND command_id=? "
                                    "AND transport_id=?");
            done.bind(1,id);done.bind(2,cmd);done.bind(3,voyage);done.next();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("boarding completion mismatch");
            dock::Statement streak(db_,"INSERT INTO dock04_flow_v1 "
                "(station,owner,passenger_streak) VALUES(?,?,1) "
                "ON CONFLICT(station,owner) DO UPDATE SET "
                "passenger_streak=MIN(2,passenger_streak+1)");
            streak.bind(1,station);streak.bind(2,owner);streak.next();
            exec("COMMIT");
        }catch(...){exec("ROLLBACK");throw;}
    }
};
} // namespace galaxy::dock04
