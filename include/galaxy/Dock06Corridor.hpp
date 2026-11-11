#pragma once
// DOCK-06: verified original NPCs move along finite, native-authorized local
// dock corridors. The authoritative identity and ship/station custody remain
// in Simulation/PersistentWorld; these SQLite rows are bounded sub-station
// physical waypoints, not a second population, ship, job, or inventory.
// Original NativeStationWalkGeometry barriers apply to EVERY movement.
#include "galaxy/DockLogistics.hpp"
#include "galaxy/NativeStationWalkGeometry.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::dock06 {

enum class Lane : std::uint64_t {Worker=1,Boarding=2,Visitor=3};
struct Walker {
    std::uint64_t npc=0,lane=0,waypoint=0;
    std::int64_t x_mm=0,z_mm=0;
};
struct Observation {
    std::uint64_t total=0,waiting=0,at_gangway=0,workers=0,visitors=0;
    std::vector<Walker> visible;
};

class Corridor {
public:
    static constexpr std::size_t kMaxTracked=24,kMaxVisible=4;
    static constexpr std::int64_t kStepMm=250,kSeparationMm=420;
    explicit Corridor(const std::string& save) {
        if(sqlite3_open_v2(save.c_str(),&db_,
             SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const auto message=db_?std::string(sqlite3_errmsg(db_)):
                                  std::string("cannot open original dock world");
            if(db_)sqlite3_close(db_);
            db_=nullptr;
            throw std::runtime_error(message);
        }
        sqlite3_busy_timeout(db_,2000);
        exec("CREATE TABLE IF NOT EXISTS dock06_walkers("
            "station INTEGER NOT NULL CHECK(station>0),"
            "owner INTEGER NOT NULL CHECK(owner>0),"
            "npc_id INTEGER NOT NULL CHECK(npc_id>0),"
            "lane INTEGER NOT NULL CHECK(lane BETWEEN 1 AND 3),"
            "waypoint INTEGER NOT NULL CHECK(waypoint BETWEEN 0 AND 5),"
            "x_bias INTEGER NOT NULL CHECK(x_bias BETWEEN 0 AND 16000),"
            "z_bias INTEGER NOT NULL CHECK(z_bias BETWEEN 0 AND 16000),"
            "observed_generation INTEGER NOT NULL CHECK(observed_generation>0),"
            "pulse INTEGER NOT NULL DEFAULT 0 CHECK(pulse BETWEEN 0 AND 1000000000),"
            "PRIMARY KEY(station,npc_id))");
        exec("CREATE INDEX IF NOT EXISTS dock06_by_station "
             "ON dock06_walkers(station,owner,lane,npc_id)");
    }
    ~Corridor(){if(db_)sqlite3_close(db_);}
    Corridor(const Corridor&)=delete;
    Corridor& operator=(const Corridor&)=delete;

    // Synchronization is strictly SOURCE-ATTESTED. No new NPC identity.
    // Inputs are actual dock shifts, real boarding queue and original
    // completed incoming voyage receipts, never aggregate cohort numbers.
    void synchronize(const Simulation& sim,EntityId station,
                     PlayerId owner,std::uint64_t generation){
        const auto* place=sim.find(station);
        if(!station||!owner||!generation||!place ||
           place->type!=EntityType::Station)return;
        std::map<std::uint64_t,Lane> active;
        {
            dock::Statement q(db_,"SELECT npc_id FROM dock_staff_v1 "
                "WHERE station=? AND owner=? AND assigned_tick<=? "
                "AND shift_end_tick>? ORDER BY npc_id LIMIT 24");
            q.bind(1,station);q.bind(2,owner);
            q.bind(3,sim.tick());q.bind(4,sim.tick());
            while(q.next()==SQLITE_ROW){
                const auto npc=q.number(0);
                if(sim.npc_available_for_shore_duty(npc,station))
                    active.emplace(npc,Lane::Worker);
            }
        }
        {
            dock::Statement q(db_,"SELECT npc_id FROM dock04_boarding_v1 "
                "WHERE station=? AND owner=? AND phase IN(0,2,4) "
                "ORDER BY priority DESC,id LIMIT 64");
            q.bind(1,station);q.bind(2,owner);
            while(q.next()==SQLITE_ROW && active.size()<kMaxTracked){
                const auto npc=q.number(0);
                const auto it=sim.npc_records().find(npc);
                if(it!=sim.npc_records().end() &&
                   it->second.role==NpcRole::Ship &&
                   it->second.location==station &&
                   !it->second.transport_id &&
                   !active.count(npc))
                    active.emplace(npc,Lane::Boarding);
            }
        }
        // A physically arrived VISITOR must already have a committed
        // destination ferry voyage AND its own original ship->station
        // receipt. Only the latest direction may be walked.
        if(table("sg_dock05_visitor_moves")){
            dock::Statement q(db_,"SELECT npc_id,receiving_ship,voyage,direction "
                "FROM sg_dock05_visitor_moves WHERE station=? "
                "ORDER BY generation DESC LIMIT 96");
            q.bind(1,station);
            std::set<std::uint64_t> examined;
            while(q.next()==SQLITE_ROW && active.size()<kMaxTracked){
                const auto npc=q.number(0),receiver=q.number(1),
                           voyage=q.number(2),direction=q.number(3);
                if(!examined.insert(npc).second || direction!=1 ||
                   active.count(npc))continue;
                const auto it=sim.npc_records().find(npc);
                const auto* ship=sim.find(receiver);
                if(ship && ship->owner_id==owner &&
                   it!=sim.npc_records().end() &&
                   it->second.location==station &&
                   sim.original_completed_ferry_guest(npc,receiver,voyage))
                    active.emplace(npc,Lane::Visitor);
            }
        }
        exec("BEGIN IMMEDIATE");
        try {
            for(const auto& [id,lane]:active){
                const auto start=route(lane,id)[0];
                dock::Statement insert(db_,"INSERT OR IGNORE INTO dock06_walkers "
                    "(station,owner,npc_id,lane,waypoint,x_bias,z_bias,"
                    "observed_generation) VALUES(?,?,?,?,0,?,?,?)");
                insert.bind(1,station);insert.bind(2,owner);insert.bind(3,id);
                insert.bind(4,static_cast<std::uint64_t>(lane));
                insert.bind(5,static_cast<std::uint64_t>(start.first+8000));
                insert.bind(6,static_cast<std::uint64_t>(start.second+8000));
                insert.bind(7,generation);insert.next();
                dock::Statement update(db_,"UPDATE dock06_walkers SET "
                    "observed_generation=? WHERE station=? AND npc_id=? "
                    "AND owner=? AND lane=?");
                update.bind(1,generation);update.bind(2,station);
                update.bind(3,id);update.bind(4,owner);
                update.bind(5,static_cast<std::uint64_t>(lane));update.next();
            }
            // If a person boards, changes custody, or their shift expires,
            // delete ONLY their local-path projection. Original identity and
            // physical station/ferry custody are never changed here.
            dock::Statement stale(db_,"SELECT npc_id,lane FROM dock06_walkers "
                "WHERE station=? AND owner=?");
            stale.bind(1,station);stale.bind(2,owner);
            std::vector<std::uint64_t> expired;
            while(stale.next()==SQLITE_ROW){
                const auto npc=stale.number(0),kind=stale.number(1);
                const auto it=active.find(npc);
                if(it==active.end() ||
                   static_cast<std::uint64_t>(it->second)!=kind)
                    expired.push_back(npc);
            }
            for(const auto npc:expired){
                dock::Statement erase(db_,"DELETE FROM dock06_walkers "
                    "WHERE station=? AND owner=? AND npc_id=?");
                erase.bind(1,station);erase.bind(2,owner);
                erase.bind(3,npc);erase.next();
            }
            exec("COMMIT");
        }catch(...){exec("ROLLBACK");throw;}
    }

    // Single human/native scheduler pulse; NOT a detached frame-loop writer.
    // Each step is at most 0.25 m along original, tested 3D floor geometry.
    // No visual move bypasses a geometry sample or source NPC presence.
    bool advance(const Simulation& sim,EntityId station,PlayerId owner){
        const auto positions=all(station,owner);
        bool moved=false;
        for(const auto& walker:positions){
            if(!still_source(sim,station,owner,walker))continue;
            if(walker.waypoint>=5)continue;
            const auto nodes=route(static_cast<Lane>(walker.lane),walker.npc);
            const auto target=nodes[walker.waypoint+1];
            auto xx=walker.x_mm,zz=walker.z_mm;
            const auto dx=target.first-xx,dz=target.second-zz;
            // Axis-aligned, native obstacle-screened movement is deliberately
            // predictable for the owner and avoids diagonal corner cutting.
            if(dx){
                xx+=std::clamp(dx,-kStepMm,kStepMm);
            }else if(dz){
                zz+=std::clamp(dz,-kStepMm,kStepMm);
            }
            if(!segment_clear(walker.x_mm,walker.z_mm,xx,zz))
                continue;
            bool crowd_collision=false;
            for(const auto& other:positions){
                if(other.npc==walker.npc)continue;
                const auto diff_x=xx-other.x_mm,diff_z=zz-other.z_mm;
                const auto old_x=walker.x_mm-other.x_mm,
                           old_z=walker.z_mm-other.z_mm;
                const auto new_sep=diff_x*diff_x+diff_z*diff_z;
                const auto old_sep=old_x*old_x+old_z*old_z;
                if(new_sep<kSeparationMm*kSeparationMm &&
                   new_sep<old_sep){crowd_collision=true;break;}
            }
            if(crowd_collision)continue;
            const auto reached=xx==target.first && zz==target.second;
            const auto next_index=walker.waypoint+(reached?1:0);
            dock::Statement step(db_,"UPDATE dock06_walkers SET "
                "x_bias=?,z_bias=?,waypoint=?,pulse=pulse+1 "
                "WHERE station=? AND owner=? AND npc_id=? AND lane=? "
                "AND waypoint=? AND x_bias=? AND z_bias=? AND pulse<1000000000");
            step.bind(1,static_cast<std::uint64_t>(xx+8000));
            step.bind(2,static_cast<std::uint64_t>(zz+8000));
            step.bind(3,next_index);step.bind(4,station);
            step.bind(5,owner);step.bind(6,walker.npc);
            step.bind(7,walker.lane);step.bind(8,walker.waypoint);
            step.bind(9,static_cast<std::uint64_t>(walker.x_mm+8000));
            step.bind(10,static_cast<std::uint64_t>(walker.z_mm+8000));
            // Signed station millimeters are stored with a +8000 offset;
            // all route checks and Godot positions decode to native units.
            if(step.next()==SQLITE_DONE && sqlite3_changes(db_)==1)
                moved=true;
        }
        return moved;
    }
    bool boarding_ready(const Simulation& sim,EntityId station,PlayerId owner,
                        std::uint64_t npc) const {
        dock::Statement q(db_,"SELECT lane,waypoint,x_bias,z_bias FROM dock06_walkers "
                              "WHERE station=? AND owner=? AND npc_id=?");
        q.bind(1,station);q.bind(2,owner);q.bind(3,npc);
        if(q.next()!=SQLITE_ROW || q.number(0)!=2 || q.number(1)!=5)
            return false;
        const auto nodes=route(Lane::Boarding,npc);
        const auto at=nodes[5];
        return q.number(2)==static_cast<std::uint64_t>(at.first+8000) &&
               q.number(3)==static_cast<std::uint64_t>(at.second+8000) &&
               sim.npc_records().count(npc) &&
               sim.npc_records().at(npc).location==station;
    }
    bool worker_ready(const Simulation& sim,EntityId station,PlayerId owner,
                      std::uint64_t npc) const {
        dock::Statement q(db_,"SELECT lane,waypoint,x_bias,z_bias "
            "FROM dock06_walkers WHERE station=? AND owner=? AND npc_id=?");
        q.bind(1,station);q.bind(2,owner);q.bind(3,npc);
        if(q.next()!=SQLITE_ROW || q.number(0)!=1 || q.number(1)!=5 ||
           !sim.npc_available_for_shore_duty(npc,station))return false;
        const auto at=route(Lane::Worker,npc)[5];
        return q.number(2)==static_cast<std::uint64_t>(at.first+8000) &&
               q.number(3)==static_cast<std::uint64_t>(at.second+8000);
    }
    Observation observe(const Simulation& sim,EntityId station,PlayerId owner)const{
        Observation o{};
        const auto current=all(station,owner);
        for(const auto& p:current){
            if(!still_source(sim,station,owner,p))continue;
            ++o.total;
            if(p.lane==1)++o.workers;
            else if(p.lane==2){
                if(p.waypoint==5)++o.at_gangway;
                else ++o.waiting;
            }else if(p.lane==3)++o.visitors;
            if(o.visible.size()<kMaxVisible)o.visible.push_back(p);
        }
        return o;
    }
    static bool all_route_points_clear() {
        for(const auto lane:{Lane::Worker,Lane::Boarding,Lane::Visitor}){
            for(std::uint64_t id=1;id<=3;++id){
                const auto pts=route(lane,id);
                for(std::size_t n=0;n<pts.size();++n){
                    if(!station_walk::clear(pts[n].first,pts[n].second))
                        return false;
                    if(n>0 && !segment_clear(pts[n-1].first,pts[n-1].second,
                                              pts[n].first,pts[n].second))
                        return false;
                }
            }
        }
        return true;
    }
private:
    sqlite3* db_=nullptr;
    static std::array<std::pair<std::int64_t,std::int64_t>,6>
    route(Lane lane,std::uint64_t npc){
        const auto offset=static_cast<std::int64_t>(npc%3)*540-540;
        if(lane==Lane::Worker)return {{
          {-3450,2300},{-3620,1350},{-3620,250},{-3620,-1200},
          {-3620,-2450},{-3550,-3700}}};
        if(lane==Lane::Visitor)return {{
          {900+offset,-3400},{900+offset,-2000},{900+offset,-700},
          {1100+offset,550},{1550+offset,1650},{1700+offset,3000}}};
        return {{
          {1700+offset,3000},{1550+offset,1650},{1100+offset,550},
          {900+offset,-700},{900+offset,-2000},{900+offset,-3400}}};
    }
    static bool segment_clear(std::int64_t x,std::int64_t z,
                              std::int64_t tx,std::int64_t tz){
        const auto dx=tx-x,dz=tz-z;
        const auto distance=std::max(std::abs(dx),std::abs(dz));
        const auto steps=std::max<std::int64_t>(1,(distance+49)/50);
        for(std::int64_t i=0;i<=steps;++i){
            if(!station_walk::clear(x+dx*i/steps,z+dz*i/steps))
                return false;
        }
        return true;
    }
    void exec(const char* sql)const{
        char* error=nullptr;
        if(sqlite3_exec(db_,sql,nullptr,nullptr,&error)!=SQLITE_OK){
            const std::string reason=error?error:sqlite3_errmsg(db_);
            sqlite3_free(error);throw std::runtime_error(reason);
        }
    }
    bool table(const char* name)const{
        sqlite3_stmt* raw=nullptr;
        const char* sql="SELECT 1 FROM sqlite_master WHERE type='table' AND name=?";
        if(sqlite3_prepare_v2(db_,sql,-1,&raw,nullptr)!=SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        sqlite3_bind_text(raw,1,name,-1,SQLITE_TRANSIENT);
        const bool found=sqlite3_step(raw)==SQLITE_ROW;
        sqlite3_finalize(raw);
        return found;
    }
    std::vector<Walker> all(EntityId station,PlayerId owner)const{
        std::vector<Walker> rows;
        dock::Statement q(db_,"SELECT npc_id,lane,waypoint,x_bias,z_bias "
                              "FROM dock06_walkers WHERE station=? AND owner=? "
                              "ORDER BY lane,npc_id LIMIT 25");
        q.bind(1,station);q.bind(2,owner);
        while(q.next()==SQLITE_ROW){
            if(rows.size()>=kMaxTracked)
                throw std::runtime_error("unbounded native dock corridor actors");
            const auto npc=q.number(0),lane=q.number(1),index=q.number(2);
            const auto x=static_cast<std::int64_t>(q.number(3))-8000,
                       z=static_cast<std::int64_t>(q.number(4))-8000;
            if(!npc||lane<1||lane>3||index>5||
               !station_walk::clear(x,z))
                throw std::runtime_error("invalid native corridor checkpoint");
            rows.push_back({npc,lane,index,x,z});
        }
        return rows;
    }
    static bool still_source(const Simulation& sim,EntityId station,
                             PlayerId owner,const Walker& p){
        const auto n=sim.npc_records().find(p.npc);
        if(n==sim.npc_records().end() || n->second.location!=station ||
           n->second.transport_id || n->second.role!=NpcRole::Ship)
            return false;
        const auto* home=sim.find(n->second.home_ship);
        if(!home || home->owner_id!=owner)return false;
        if(p.lane==1)
            return sim.npc_available_for_shore_duty(p.npc,station);
        return true;
    }
};
} // namespace galaxy::dock06
