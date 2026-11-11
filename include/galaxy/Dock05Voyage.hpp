#pragma once
// DOCK-05: original ferry journey -> original flight -> original destination
// receiving ship -> original visitor station custody. All world changes are
// issued to PersistentWorld; these same-file SQLite rows are intent/receipts,
// NEVER independent people, movement, residence or inventory.
#include "galaxy/Dock04Traffic.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace galaxy::dock05 {

struct Status {
    std::uint64_t active=0,delivered=0,held=0,passengers_travelling=0;
    std::uint64_t visitors_arrived=0,reviewable_route=0;
};
struct Destination {
    EntityId station=0,receiving_ship=0;
};
class Routes {
public:
    explicit Routes(const std::string& file):file_(file){
        if(sqlite3_open_v2(file.c_str(),&db_,
            SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const std::string why=db_?sqlite3_errmsg(db_):"SQLite open";
            sqlite3_close(db_);db_=nullptr;throw std::runtime_error(why);
        }
        sqlite3_busy_timeout(db_,2000);
        exec("CREATE TABLE IF NOT EXISTS dock05_route_v1("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "owner INTEGER NOT NULL CHECK(owner>0),"
             "origin INTEGER NOT NULL CHECK(origin>0),"
             "destination INTEGER NOT NULL CHECK(destination>0),"
             "ferry INTEGER NOT NULL CHECK(ferry>0),"
             "receiver INTEGER NOT NULL CHECK(receiver>0),"
             "mode INTEGER NOT NULL CHECK(mode IN(1,2)),"
             "destination_body INTEGER NOT NULL,"
             "stage INTEGER NOT NULL DEFAULT 0 CHECK(stage BETWEEN 0 AND 9),"
             "posted_tick INTEGER NOT NULL,source_generation INTEGER NOT NULL)");
        exec("CREATE INDEX IF NOT EXISTS dock05_route_station "
             "ON dock05_route_v1(origin,owner,stage,id)");
        exec("CREATE UNIQUE INDEX IF NOT EXISTS dock05_ferry_active "
             "ON dock05_route_v1(ferry) WHERE stage NOT IN(6,8)");
        exec("CREATE TABLE IF NOT EXISTS dock05_route_people_v1("
             "route INTEGER NOT NULL,npc INTEGER NOT NULL,home INTEGER NOT NULL,"
             "voyage INTEGER NOT NULL,ordinal INTEGER NOT NULL "
             "CHECK(ordinal BETWEEN 1 AND 16),"
             "stage INTEGER NOT NULL DEFAULT 0 "
             "CHECK(stage BETWEEN 0 AND 5),"
             "PRIMARY KEY(route,npc),UNIQUE(voyage))");
        exec("CREATE INDEX IF NOT EXISTS dock05_people_state "
             "ON dock05_route_people_v1(route,stage,npc)");
        exec("CREATE TABLE IF NOT EXISTS dock05_terminal_audit_v1("
             "route INTEGER PRIMARY KEY,owner INTEGER NOT NULL,"
             "origin INTEGER NOT NULL,destination INTEGER NOT NULL,"
             "ferry INTEGER NOT NULL,receiver INTEGER NOT NULL,"
             "people INTEGER NOT NULL,completed_tick INTEGER NOT NULL,"
             "outcome INTEGER NOT NULL)");
    }
    ~Routes(){sqlite3_close(db_);}
    Routes(const Routes&)=delete;
    Routes& operator=(const Routes&)=delete;

    std::vector<Destination> destinations(
        const Simulation& sim,EntityId origin,PlayerId owner) const{
        std::vector<Destination> out;
        if(!origin||!owner)return out;
        const auto* here=sim.find(origin);
        if(!here || here->type!=EntityType::Station)return out;
        for(const auto& [id,ship]:sim.entities()){
            if(out.size()>=4)break;
            if(ship.type!=EntityType::Ship || ship.owner_id!=owner ||
               ship.dock_state!=DockState::Docked ||
               !ship.dock_station || ship.dock_station==origin ||
               !sim.colony_ship(id) || !sim.life_support(id))continue;
            const auto* there=sim.find(ship.dock_station);
            if(!there || there->type!=EntityType::Station ||
               there->system_id!=here->system_id)continue;
            if(!std::any_of(out.begin(),out.end(),[&](const Destination& d){
                return d.station==there->id && d.receiving_ship==id;
            }))out.push_back({there->id,id});
        }
        return out;
    }
    Status status(EntityId origin,PlayerId owner) const {
        Status v{};
        dock::Statement q(db_,"SELECT stage,COUNT(*) FROM dock05_route_v1 "
                              "WHERE origin=? AND owner=? GROUP BY stage");
        q.bind(1,origin);q.bind(2,owner);
        while(q.next()==SQLITE_ROW){
            const auto stage=q.number(0),n=q.number(1);
            if(stage==6)v.delivered+=n;
            else if(stage==7)v.held+=n;
            else if(stage!=8)v.active+=n;
        }
        dock::Statement p(db_,"SELECT COUNT(*),COALESCE(SUM(CASE WHEN "
            "p.stage=4 THEN 1 ELSE 0 END),0) FROM dock05_route_people_v1 p "
            "JOIN dock05_route_v1 r ON r.id=p.route "
            "WHERE r.origin=? AND r.owner=? AND r.stage!=8");
        p.bind(1,origin);p.bind(2,owner);
        if(p.next()==SQLITE_ROW){
            const auto all=p.number(0),arrived=p.number(1);
            v.visitors_arrived=arrived;
            v.passengers_travelling=all>=arrived?all-arrived:0;
        }
        dock::Statement a(db_,"SELECT COUNT(*) FROM dock05_terminal_audit_v1 "
                              "WHERE origin=? AND owner=? AND outcome=6");
        a.bind(1,origin);a.bind(2,owner);
        if(a.next()==SQLITE_ROW)v.delivered+=a.number(0);
        dock::Statement arrived(db_,"SELECT COALESCE(SUM(people),0) "
            "FROM dock05_terminal_audit_v1 "
            "WHERE origin=? AND owner=? AND outcome=6");
        arrived.bind(1,origin);arrived.bind(2,owner);
        if(arrived.next()==SQLITE_ROW)
            v.visitors_arrived+=arrived.number(0);
        dock::Statement review(db_,"SELECT COALESCE(MAX(id),0) FROM "
            "dock05_route_v1 WHERE origin=? AND owner=? AND stage IN(0,7,9)");
        review.bind(1,origin);review.bind(2,owner);
        if(review.next()==SQLITE_ROW)v.reviewable_route=review.number(0);
        return v;
    }
    EntityId first_boarded_ferry(const Simulation& sim,EntityId station,
                                 PlayerId owner) const {
        dock::Statement q(db_,"SELECT DISTINCT ferry FROM dock04_boarding_v1 "
            "WHERE station=? AND owner=? AND phase=6 ORDER BY ferry LIMIT 64");
        q.bind(1,station);q.bind(2,owner);
        while(q.next()==SQLITE_ROW) {
            const auto ferry=q.number(0);
            const auto* boat=sim.find(ferry);
            if(boat && boat->type==EntityType::Ship &&
               boat->owner_id==owner &&
               boat->dock_state==DockState::Docked &&
               boat->dock_station==station &&
               sim.original_passenger_ferry_safe_to_fly(ferry) &&
               !has_active_ferry(ferry))
                return ferry;
        }
        return 0;
    }
    std::uint64_t book(const PersistentWorld& world,EntityId origin,
        PlayerId owner,EntityId ferry,EntityId destination,
        EntityId receiver){
        const auto& sim=world.world();
        const auto* boat=sim.find(ferry),*port=sim.find(origin),
                  *far=sim.find(destination),*home=sim.find(receiver);
        if(!origin||!owner||!ferry||!destination||!receiver ||
           origin==destination || ferry==receiver ||
           !boat||!port||!far||!home ||
           port->type!=EntityType::Station ||
           far->type!=EntityType::Station ||
           boat->type!=EntityType::Ship ||
           home->type!=EntityType::Ship ||
           boat->owner_id!=owner || home->owner_id!=owner ||
           boat->dock_state!=DockState::Docked ||
           boat->dock_station!=origin ||
           home->dock_state!=DockState::Docked ||
           home->dock_station!=destination ||
           !sim.colony_ship(ferry)||!sim.colony_ship(receiver) ||
           !sim.life_support(ferry)||!sim.life_support(receiver) ||
           !sim.original_passenger_ferry_safe_to_fly(ferry) ||
           port->system_id!=far->system_id)return 0;
        // The existing galactic transit is currently same-system only.
        // Different frames require the REAL planetary/celestial destination
        // anchor and an installed original scanner; otherwise refuse.
        const bool local=sim.same_local_frame(ferry,destination);
        const auto body=sim.local_entity_anchors().find(destination);
        const std::uint64_t anchor=
            body==sim.local_entity_anchors().end()?0:body->second;
        if(!local && (!anchor || !boat->fitting.exploration_scanner()))
            return 0;
        if(has_active_ferry(ferry))return 0;
        struct Ticket {
            std::uint64_t npc=0,home=0,voyage=0;
        };
        std::vector<Ticket> tickets;
        dock::Statement q(db_,"SELECT npc_id,source_ship,transport_id "
            "FROM dock04_boarding_v1 WHERE station=? AND owner=? "
            "AND ferry=? AND phase=6 ORDER BY npc_id LIMIT 17");
        q.bind(1,origin);q.bind(2,owner);q.bind(3,ferry);
        while(q.next()==SQLITE_ROW){
            if(tickets.size()>=16)return 0;
            const auto npc=q.number(0),src=q.number(1),voyage=q.number(2);
            const auto n=sim.npc_records().find(npc);
            if(!voyage || n==sim.npc_records().end() ||
               n->second.location!=ferry ||
               n->second.transport_id!=voyage ||
               n->second.home_ship!=src || sim.voyage_status(voyage)!=1 ||
               !sim.find(src) || sim.find(src)->owner_id!=owner)
                continue; // an earlier delivered ticket does not reboard
            tickets.push_back({npc,src,voyage});
        }
        if(tickets.empty() || tickets.size()!=sim.passengers_aboard(ferry))
            return 0;
        const auto* supplies=sim.life_support(receiver);
        if(!supplies)return 0;
        const auto actual=sim.people_physically_aboard(receiver);
        if(actual>1000 || tickets.size()>1000-actual)return 0;
        const auto ration=static_cast<std::int64_t>(actual+tickets.size())*2;
        if(supplies->food_kg<ration || supplies->water_kg<ration ||
           supplies->oxygen_kg<ration)return 0;
        exec("BEGIN IMMEDIATE");
        try{
            dock::Statement ins(db_,"INSERT INTO dock05_route_v1 "
                "(owner,origin,destination,ferry,receiver,mode,"
                "destination_body,posted_tick,source_generation) "
                "VALUES(?,?,?,?,?,?,?,?,?)");
            ins.bind(1,owner);ins.bind(2,origin);ins.bind(3,destination);
            ins.bind(4,ferry);ins.bind(5,receiver);ins.bind(6,local?1:2);
            ins.bind(7,anchor);ins.bind(8,sim.tick());
            ins.bind(9,world.generation());ins.next();
            const auto route=static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
            if(!route || route>0x00000000001FFFFFULL)
                throw std::runtime_error("bounded ferry route identity exhausted");
            std::uint64_t ordinal=0;
            for(const auto& t:tickets){
                dock::Statement person(db_,"INSERT INTO dock05_route_people_v1 "
                    "(route,npc,home,voyage,ordinal) VALUES(?,?,?,?,?)");
                person.bind(1,route);person.bind(2,t.npc);
                person.bind(3,t.home);person.bind(4,t.voyage);
                person.bind(5,++ordinal);person.next();
            }
            exec("COMMIT");return route;
        }catch(...){exec("ROLLBACK");throw;}
    }
    // Existing physical navigation order progresses ONLY via original
    // Simulation::step, not by DB edits. An aborted native publish keeps the
    // intent uncertain; never launch a second flight on restart.
    bool advance(PersistentWorld& world,EntityId origin,PlayerId owner){
        reconcile(world,origin,owner);
        const auto pending=first_active(origin,owner);
        if(!pending.id)return false;
        const auto j=pending;
        if(j.stage==7||j.stage==8)return false;
        if(j.stage==9){
            const auto expected=j.mode==2 && !celestial_complete(world.world(),j)?
                celestial_flight(j.id):local_flight(j.id);
            const auto it=world.world().npc_flight_orders().find(expected);
            if(it==world.world().npc_flight_orders().end()){
                set_route(j.id,9,7);return false;
            }
            set_route(j.id,9,expected==celestial_flight(j.id)?1:3);
            return true;
        }
        if(j.stage==0){
            // Reserve intent BEFORE issuing original protected flight.
            // On failure, owner review is required before any retry.
            set_route(j.id,0,9);
            bool posted=false;
            if(j.mode==1)
                posted=world.queue_npc_station_docking(owner,j.ferry,0,
                    j.destination,local_flight(j.id),
                    NpcFlightPurpose::Passenger);
            else
                posted=world.queue_npc_flight(owner,j.ferry,0,j.body,
                    celestial_flight(j.id),transit_id(j.id),
                    NpcFlightPurpose::Passenger);
            if(!posted){set_route(j.id,9,7);return false;}
            set_route(j.id,9,j.mode==1?3:1);
            return true;
        }
        if(j.stage==1){
            const auto it=world.world().npc_flight_orders().find(celestial_flight(j.id));
            if(it==world.world().npc_flight_orders().end() ||
               it->second.phase==NpcFlightPhase::Cancelled){
                set_route(j.id,1,7);return false;
            }
            if(celestial_complete(world.world(),j)){
                set_route(j.id,1,2);return true;
            }
            world.step();return true;
        }
        if(j.stage==2){
            if(!world.world().same_local_frame(j.ferry,j.destination)){
                set_route(j.id,2,7);return false;
            }
            set_route(j.id,2,9);
            if(!world.queue_npc_station_docking(owner,j.ferry,0,
                    j.destination,local_flight(j.id),
                    NpcFlightPurpose::Passenger)){
                set_route(j.id,9,7);return false;
            }
            set_route(j.id,9,3);return true;
        }
        if(j.stage==3){
            const auto it=world.world().npc_flight_orders().find(local_flight(j.id));
            if(it==world.world().npc_flight_orders().end() ||
               it->second.phase==NpcFlightPhase::Cancelled){
                set_route(j.id,3,7);return false;
            }
            const auto* ferry=world.world().find(j.ferry);
            if(it->second.phase==NpcFlightPhase::Arrived &&
               ferry && ferry->dock_state==DockState::Docked &&
               ferry->dock_station==j.destination){
                set_route(j.id,3,4);return true;
            }
            world.step();return true;
        }
        if(j.stage==4){
            // One actual visitor at most per scheduler operation; a
            // 16-person manifest consumes 16 separate source receipts.
            return unload_one(world,j);
        }
        return false;
    }
    // Exact original native command or original station movement may be
    // recovered; missing/ambiguous evidence becomes a held request only.
    void reconcile(PersistentWorld& world,EntityId origin,PlayerId owner){
        dock::Statement q(db_,"SELECT p.route,p.npc,p.home,p.voyage,p.stage,"
               "r.receiver,r.destination,p.ordinal FROM dock05_route_people_v1 p "
               "JOIN dock05_route_v1 r ON r.id=p.route "
               "WHERE r.origin=? AND r.owner=? AND p.stage IN(1,3)");
        q.bind(1,origin);q.bind(2,owner);
        struct Pending {std::uint64_t route,npc,home,voyage,stage,receiver,station,ordinal;};
        std::vector<Pending> list;
        while(q.next()==SQLITE_ROW)
            list.push_back({q.number(0),q.number(1),q.number(2),q.number(3),
                            q.number(4),q.number(5),q.number(6),q.number(7)});
        for(const auto& p:list){
            if(p.stage==1){
                if(world.world().completed_voyage_matches(
                    p.voyage,route_ferry(p.route),p.home,p.receiver,{p.npc})){
                    set_person(p.route,p.npc,1,2);
                }else{
                    set_person(p.route,p.npc,1,5);
                    hold_route(p.route);
                }
            }else{
                const auto it=world.world().npc_records().find(p.npc);
                if(it!=world.world().npc_records().end() &&
                   it->second.location==p.station &&
                   visitor_proven(visitor_receipt(p.route,p.ordinal),
                                  p.npc,p.voyage,p.receiver,p.station)){
                    set_person(p.route,p.npc,3,4);
                }else{
                    set_person(p.route,p.npc,3,5);
                    hold_route(p.route);
                }
            }
        }
    }
    // Retain recent finished journeys in the active routing table. Move
    // older completed rows into a compact immutable summary ONLY after
    // independent voyage and dock-walk receipts have been verified.
    // Original world travel/visitor receipts are never deleted.
    std::uint64_t archive_delivered(const Simulation& sim,EntityId origin,
                                    PlayerId owner,std::uint64_t retain=64){
        if(!origin||!owner||retain<8||retain>128)return 0;
        dock::Statement q(db_,"SELECT id,receiver,ferry,destination "
            "FROM dock05_route_v1 WHERE origin=? AND owner=? AND stage=6 "
            "ORDER BY id DESC LIMIT -1 OFFSET ?");
        q.bind(1,origin);q.bind(2,owner);q.bind(3,retain);
        struct Terminal{std::uint64_t id,receiver,ferry,destination,count;};
        std::vector<Terminal> ready;
        while(q.next()==SQLITE_ROW)
            ready.push_back({q.number(0),q.number(1),q.number(2),q.number(3),0});
        for(auto& route:ready){
            dock::Statement p(db_,"SELECT npc,voyage,ordinal FROM "
                "dock05_route_people_v1 WHERE route=? AND stage=4");
            p.bind(1,route.id);
            while(p.next()==SQLITE_ROW){
                const auto npc=p.number(0),voyage=p.number(1),
                           ordinal=p.number(2);
                if(!sim.original_completed_ferry_guest(npc,route.receiver,voyage) ||
                   !visitor_proven(visitor_receipt(route.id,ordinal),
                       npc,voyage,route.receiver,route.destination))
                    throw std::runtime_error("delivered route lacks original arrival proof");
                ++route.count;
            }
            dock::Statement total(db_,"SELECT COUNT(*) FROM dock05_route_people_v1 "
                                   "WHERE route=?");
            total.bind(1,route.id);
            if(total.next()!=SQLITE_ROW || route.count==0 ||
               route.count!=total.number(0))
                throw std::runtime_error("route archive missing real passenger");
        }
        if(ready.empty())return 0;
        exec("BEGIN IMMEDIATE");
        try{
            for(const auto& route:ready){
                dock::Statement audit(db_,"INSERT INTO dock05_terminal_audit_v1 "
                    "(route,owner,origin,destination,ferry,receiver,people,"
                    "completed_tick,outcome) VALUES(?,?,?,?,?,?,?,?,6)");
                audit.bind(1,route.id);audit.bind(2,owner);audit.bind(3,origin);
                audit.bind(4,route.destination);audit.bind(5,route.ferry);
                audit.bind(6,route.receiver);audit.bind(7,route.count);
                audit.bind(8,sim.tick());audit.next();
                dock::Statement remove_people(db_,"DELETE FROM "
                    "dock05_route_people_v1 WHERE route=?");
                remove_people.bind(1,route.id);remove_people.next();
                dock::Statement remove_route(db_,"DELETE FROM dock05_route_v1 "
                    "WHERE id=? AND stage=6 AND origin=? AND owner=?");
                remove_route.bind(1,route.id);remove_route.bind(2,origin);
                remove_route.bind(3,owner);remove_route.next();
                if(sqlite3_changes(db_)!=1)
                    throw std::runtime_error("delivered route archive raced original journey");
            }
            exec("COMMIT");return ready.size();
        }catch(...){exec("ROLLBACK");throw;}
    }
    // Review held jobs without replay; the original physical state always
    // wins. A reviewer can safely abandon an unstarted route, but cannot
    // erase passengers or an ongoing original native flight.
    bool cancel_unstarted(PersistentWorld& world,EntityId origin,
                          PlayerId owner,std::uint64_t route){
        const auto j=load_route(route);
        if(j.id!=route || j.owner!=owner || j.origin!=origin ||
           (j.stage!=0 && j.stage!=7 && j.stage!=9))return false;
        if(world.world().npc_flight_orders().count(local_flight(route)) ||
           world.world().npc_flight_orders().count(celestial_flight(route)))
            return false;
        set_route(route,j.stage,8);
        return true;
    }
private:
    struct Route {
        std::uint64_t id=0,owner=0,origin=0,destination=0,ferry=0,
           receiver=0,mode=0,body=0,stage=0;
    };
    std::string file_;
    sqlite3* db_=nullptr;
    static std::uint64_t local_flight(std::uint64_t route){
        return 0xD050000000ULL | (route<<3)|1ULL;
    }
    static std::uint64_t celestial_flight(std::uint64_t route){
        return 0xD050000000ULL | (route<<3)|2ULL;
    }
    static std::uint64_t transit_id(std::uint64_t route){
        return 0xD050000000ULL | (route<<3)|3ULL;
    }
    static std::uint64_t disembark_command(std::uint64_t route,
                                            std::uint64_t ordinal){
        return 0x5800000000000000ULL|(route<<8)|(ordinal*2+1);
    }
    static std::uint64_t visitor_receipt(std::uint64_t route,
                                         std::uint64_t ordinal){
        return 0x5900000000000000ULL|(route<<8)|(ordinal*2+2);
    }
    void exec(const char* sql)const{
        char* e=nullptr;
        if(sqlite3_exec(db_,sql,nullptr,nullptr,&e)!=SQLITE_OK){
            const std::string why=e?e:sqlite3_errmsg(db_);
            sqlite3_free(e);throw std::runtime_error(why);
        }
    }
    bool has_active_ferry(EntityId ferry)const{
        dock::Statement q(db_,"SELECT 1 FROM dock05_route_v1 "
            "WHERE ferry=? AND stage NOT IN(6,8) LIMIT 1");
        q.bind(1,ferry);return q.next()==SQLITE_ROW;
    }
    Route load_route(std::uint64_t route)const{
        dock::Statement q(db_,"SELECT id,owner,origin,destination,ferry,"
                    "receiver,mode,destination_body,stage FROM dock05_route_v1 "
                    "WHERE id=?");
        q.bind(1,route);
        if(q.next()!=SQLITE_ROW)return {};
        return {q.number(0),q.number(1),q.number(2),q.number(3),q.number(4),
                q.number(5),q.number(6),q.number(7),q.number(8)};
    }
    Route first_active(EntityId origin,PlayerId owner)const{
        dock::Statement q(db_,"SELECT id FROM dock05_route_v1 "
           "WHERE origin=? AND owner=? AND stage NOT IN(6,7,8) ORDER BY id LIMIT 1");
        q.bind(1,origin);q.bind(2,owner);
        return q.next()==SQLITE_ROW?load_route(q.number(0)):Route{};
    }
    EntityId route_ferry(std::uint64_t route)const{
        return load_route(route).ferry;
    }
    void set_route(std::uint64_t route,std::uint64_t previous,
                   std::uint64_t next) {
        dock::Statement q(db_,"UPDATE dock05_route_v1 SET stage=? "
           "WHERE id=? AND stage=?");
        q.bind(1,next);q.bind(2,route);q.bind(3,previous);q.next();
        if(sqlite3_changes(db_)!=1)
            throw std::runtime_error("route native state was concurrently changed");
    }
    void set_person(std::uint64_t route,std::uint64_t npc,
                    std::uint64_t previous,std::uint64_t next){
        dock::Statement q(db_,"UPDATE dock05_route_people_v1 SET stage=? "
            "WHERE route=? AND npc=? AND stage=?");
        q.bind(1,next);q.bind(2,route);q.bind(3,npc);
        q.bind(4,previous);q.next();
        if(sqlite3_changes(db_)!=1)
            throw std::runtime_error("passenger state was concurrently changed");
    }
    void hold_route(std::uint64_t route) {
        dock::Statement q(db_,"UPDATE dock05_route_v1 SET stage=7 "
            "WHERE id=? AND stage NOT IN(6,7,8)");
        q.bind(1,route);q.next();
    }
    bool celestial_complete(const Simulation& sim,const Route& j)const{
        const auto flight=sim.npc_flight_orders().find(celestial_flight(j.id));
        const auto arrived=sim.completed_celestial_transits().find(transit_id(j.id));
        return flight!=sim.npc_flight_orders().end() &&
               flight->second.phase==NpcFlightPhase::Arrived &&
               arrived!=sim.completed_celestial_transits().end() &&
               arrived->second.ship==j.ferry &&
               arrived->second.destination_body==j.body;
    }
    bool visitor_proven(std::uint64_t receipt,std::uint64_t npc,
                        std::uint64_t voyage,EntityId ship,EntityId station)const{
        dock::Statement table(db_,"SELECT 1 FROM sqlite_master "
                     "WHERE type='table' AND name='sg_dock05_visitor_moves'");
        if(table.next()!=SQLITE_ROW)return false;
        dock::Statement q(db_,"SELECT 1 FROM sg_dock05_visitor_moves "
           "WHERE receipt=? AND npc_id=? AND voyage=? AND receiving_ship=? "
           "AND station=? AND direction=1 LIMIT 1");
        q.bind(1,receipt);q.bind(2,npc);q.bind(3,voyage);
        q.bind(4,ship);q.bind(5,station);
        return q.next()==SQLITE_ROW;
    }
    bool unload_one(PersistentWorld& world,const Route& j){
        const auto& sim=world.world();
        const auto* ferry=sim.find(j.ferry),*receiver=sim.find(j.receiver);
        if(!ferry||!receiver || ferry->owner_id!=j.owner ||
           receiver->owner_id!=j.owner ||
           ferry->dock_state!=DockState::Docked ||
           receiver->dock_state!=DockState::Docked ||
           ferry->dock_station!=j.destination ||
           receiver->dock_station!=j.destination ||
           !sim.life_support(j.receiver)){
            hold_route(j.id);return false;
        }
        dock::Statement p(db_,"SELECT npc,home,voyage,stage,ordinal "
                              "FROM dock05_route_people_v1 "
                              "WHERE route=? AND stage IN(0,2) ORDER BY npc LIMIT 1");
        p.bind(1,j.id);
        if(p.next()!=SQLITE_ROW){
            dock::Statement last(db_,"SELECT COUNT(*) FROM dock05_route_people_v1 "
                                      "WHERE route=? AND stage!=4");
            last.bind(1,j.id);
            if(last.next()==SQLITE_ROW && last.number(0)==0){
                set_route(j.id,4,6);return true;
            }
            hold_route(j.id);return false;
        }
        const auto npc=p.number(0),home=p.number(1),voyage=p.number(2),
                   state=p.number(3),ordinal=p.number(4);
        if(state==0){
            const auto it=sim.npc_records().find(npc);
            if(it==sim.npc_records().end() ||
               it->second.location!=j.ferry ||
               it->second.transport_id!=voyage ||
               sim.voyage_status(voyage)!=1 ||
               !sim.original_passenger_ferry_safe_to_fly(j.ferry)) {
                hold_route(j.id);return false;
            }
            const auto cmd=disembark_command(j.id,ordinal);
            set_person(j.id,npc,0,1);
            Command c{};
            c.id=cmd;c.sequence=cmd;c.execute_tick=sim.tick()+1;
            c.issuer=Simulation::kServerAuthority;
            c.action=Action::HostDisembarkPassengers;
            c.actor=j.ferry;c.target=j.receiver;
            c.transport_id=voyage;c.passenger_ids={npc};
            if(!world.submit_host_passenger_action(c)){
                set_person(j.id,npc,1,5);hold_route(j.id);return false;
            }
            world.step();
            if(!world.world().completed_voyage_matches(
                voyage,j.ferry,home,j.receiver,{npc})){
                set_person(j.id,npc,1,5);hold_route(j.id);return false;
            }
            set_person(j.id,npc,1,2);return true;
        }
        if(state==2){
            if(!sim.original_completed_ferry_guest(npc,j.receiver,voyage)) {
                hold_route(j.id);return false;
            }
            const auto receipt=visitor_receipt(j.id,ordinal);
            set_person(j.id,npc,2,3);
            if(!world.station_visitor_transfer(j.owner,j.receiver,
                    j.destination,npc,voyage,true,receipt)){
                set_person(j.id,npc,3,5);hold_route(j.id);return false;
            }
            set_person(j.id,npc,3,4);
            return true;
        }
        return false;
    }
};
} // namespace galaxy::dock05
