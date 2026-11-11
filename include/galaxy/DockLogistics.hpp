#pragma once
// DOCK-02: durable, bounded service appointments for ORIGINAL station NPCs.
// Lives in the SAME SQLite file as PersistentWorld, but never owns inventory,
// creates residents, or invents passenger voyages. NativeWriterLease serializes
// the station host. In-flight cargo receipts are reconciled, never replayed.
#include "galaxy/Persistence.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::dock {
class Statement {
 public:
  Statement(sqlite3* db,const char* sql):db_(db) {
    if(sqlite3_prepare_v2(db,sql,-1,&stmt_,nullptr)!=SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Statement(){sqlite3_finalize(stmt_);}
  Statement(const Statement&)=delete;
  void bind(int col,std::uint64_t n){
    if(n>std::uint64_t(std::numeric_limits<sqlite3_int64>::max()) ||
       sqlite3_bind_int64(stmt_,col,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
      throw std::runtime_error("invalid dock SQLite integer");
  }
  int next(){
    const int rc=sqlite3_step(stmt_);
    if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
    return rc;
  }
  std::uint64_t number(int col) const {
    const auto n=sqlite3_column_int64(stmt_,col);
    if(n<0)throw std::runtime_error("negative dock SQLite count");
    return static_cast<std::uint64_t>(n);
  }
 private:
  sqlite3* db_=nullptr;
  sqlite3_stmt* stmt_=nullptr;
};
struct Snapshot {
  std::uint64_t station=0,staff=0,passengers=0,freight_tonnes=0;
  std::uint64_t freight_jobs=0,held_jobs=0,completed_passengers=0,completed_tonnes=0;
};
class Ledger {
 public:
  static constexpr std::uint64_t kMaxStaff=16,kMaxJobs=128,kMaxFreightTonnes=64;
  explicit Ledger(const std::string& world_path) {
    if(sqlite3_open_v2(world_path.c_str(),&db_,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
      const std::string error=db_?sqlite3_errmsg(db_):"cannot open world";
      sqlite3_close(db_);db_=nullptr;throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_,2000);
    exec("CREATE TABLE IF NOT EXISTS dock_staff_v1("
         "npc_id INTEGER PRIMARY KEY CHECK(npc_id>0),"
         "station INTEGER NOT NULL CHECK(station>0),"
         "owner INTEGER NOT NULL CHECK(owner>0),"
         "assigned_tick INTEGER NOT NULL,"
         "shift_end_tick INTEGER NOT NULL)");
    exec("CREATE INDEX IF NOT EXISTS dock_staff_station_v1 ON dock_staff_v1(station,owner)");
    exec("CREATE TABLE IF NOT EXISTS dock_jobs_v1("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "station INTEGER NOT NULL CHECK(station>0),"
         "owner INTEGER NOT NULL CHECK(owner>0),"
         "kind INTEGER NOT NULL CHECK(kind IN(1,2)),"
         "source_id INTEGER NOT NULL CHECK(source_id>0),"
         "requested INTEGER NOT NULL CHECK(requested BETWEEN 1 AND 64),"
         "completed INTEGER NOT NULL DEFAULT 0 CHECK(completed>=0 AND completed<=requested),"
         "state INTEGER NOT NULL DEFAULT 0 CHECK(state IN(0,1,2,3)),"
         "pending_command INTEGER NOT NULL DEFAULT 0,"
         "created_tick INTEGER NOT NULL)");
    exec("CREATE INDEX IF NOT EXISTS dock_jobs_station_v1 ON dock_jobs_v1(station,owner,state,id)");
  }
  ~Ledger(){sqlite3_close(db_);}
  Ledger(const Ledger&)=delete;
  Ledger& operator=(const Ledger&)=delete;

  std::uint64_t active_staff(const Simulation& sim,EntityId station,
                             PlayerId owner) const {
    const auto occupied=workforce02::occupied_named_workers(sim,sim.tick());
    Statement s(db_,"SELECT npc_id FROM dock_staff_v1 WHERE station=? AND owner=? "
                    "AND assigned_tick<=? AND shift_end_tick>? LIMIT 17");
    s.bind(1,station);s.bind(2,owner);s.bind(3,sim.tick());s.bind(4,sim.tick());
    std::uint64_t count=0;
    while(s.next()==SQLITE_ROW){
      const auto id=s.number(0);
      const auto it=sim.npc_records().find(id);
      if(it==sim.npc_records().end())continue;
      const auto* home=sim.find(it->second.home_ship);
      if(home && home->owner_id==owner && it->second.location==station &&
         it->second.role==NpcRole::Ship &&
         (it->second.assigned_site==0 || it->second.assigned_site==station) &&
         !it->second.transport_id && !occupied.count(id))++count;
    }
    return count;
  }
  bool assign(const Simulation& sim,EntityId station,PlayerId owner,
              std::uint64_t npc_id) {
    if(!station||!owner||!npc_id || active_staff(sim,station,owner)>=kMaxStaff ||
       workforce02::already_on_named_duty(sim,npc_id,sim.tick()))return false;
    const auto it=sim.npc_records().find(npc_id);
    if(it==sim.npc_records().end()||it->second.location!=station ||
       it->second.role!=NpcRole::Ship || it->second.transport_id ||
       (it->second.assigned_site && it->second.assigned_site!=station))return false;
    const auto* home=sim.find(it->second.home_ship);
    if(!home || home->owner_id!=owner)return false;
    // An expired shift can be renewed without claiming a second worker.
    {Statement expire(db_,"DELETE FROM dock_staff_v1 WHERE npc_id=? AND shift_end_tick<=?");
     expire.bind(1,npc_id);expire.bind(2,sim.tick());expire.next();}
    Statement s(db_,"INSERT OR IGNORE INTO dock_staff_v1 "
                    "(npc_id,station,owner,assigned_tick,shift_end_tick) VALUES(?,?,?,?,?)");
    s.bind(1,npc_id);s.bind(2,station);s.bind(3,owner);
    s.bind(4,sim.tick());s.bind(5,sim.tick()+256);
    if(s.next()!=SQLITE_DONE)throw std::runtime_error("dock assignment SQL");
    return sqlite3_changes(db_)==1;
  }
  bool release(EntityId station,PlayerId owner,std::uint64_t npc_id) {
    Statement s(db_,"DELETE FROM dock_staff_v1 WHERE npc_id=? AND station=? AND owner=?");
    s.bind(1,npc_id);s.bind(2,station);s.bind(3,owner);
    s.next();return sqlite3_changes(db_)==1;
  }
  bool release_first(EntityId station,PlayerId owner) {
    Statement q(db_,"SELECT npc_id FROM dock_staff_v1 WHERE station=? AND owner=? "
                    "ORDER BY npc_id LIMIT 1");
    q.bind(1,station);q.bind(2,owner);
    if(q.next()!=SQLITE_ROW)return false;
    return release(station,owner,q.number(0));
  }
  Snapshot snapshot(const Simulation& sim,EntityId station,PlayerId owner) const {
    Snapshot v{};v.station=station;v.staff=active_staff(sim,station,owner);
    Statement a(db_,"SELECT kind,requested-completed,state FROM dock_jobs_v1 "
                    "WHERE station=? AND owner=? AND state IN(0,1,3)");
    a.bind(1,station);a.bind(2,owner);
    while(a.next()==SQLITE_ROW){
      if(a.number(2)==3){++v.held_jobs;continue;}
      if(a.number(0)==1)v.passengers+=a.number(1);
      else{++v.freight_jobs;v.freight_tonnes+=a.number(1);}
    }
    Statement b(db_,"SELECT kind,SUM(completed) FROM dock_jobs_v1 "
                    "WHERE station=? AND owner=? GROUP BY kind");
    b.bind(1,station);b.bind(2,owner);
    while(b.next()==SQLITE_ROW){
      if(b.number(0)==1)v.completed_passengers=b.number(1);
      else v.completed_tonnes=b.number(1);
    }
    return v;
  }
  // A passenger appointment is only a check-in slot for an actual named
  // original-world station resident, NEVER an automatic voyage or migration.
  bool enqueue_passenger(const Simulation& sim,EntityId station,PlayerId owner,
                         std::uint64_t npc_id) {
    const auto it=sim.npc_records().find(npc_id);
    if(!station||!owner||it==sim.npc_records().end() ||
       it->second.location!=station || it->second.transport_id ||
       !sim.find(it->second.home_ship) ||
       sim.find(it->second.home_ship)->owner_id!=owner ||
       is_staff(npc_id)|| active_jobs(station,owner,0)>=kMaxJobs ||
       source_pending(station,owner,1,npc_id))return false;
    insert_job(station,owner,1,npc_id,1,sim.tick());return true;
  }
  bool enqueue_freight(const Simulation& sim,EntityId station,PlayerId owner,
                       EntityId ship,std::uint64_t tonnes) {
    const auto* vessel=sim.find(ship);
    if(!station||!owner||!ship||tonnes==0||tonnes>kMaxFreightTonnes ||
       !vessel||vessel->owner_id!=owner||vessel->dock_station!=station||
       vessel->dock_state!=DockState::Docked||vessel->cargo<1||
       active_jobs(station,owner,0)>=kMaxJobs ||
       source_pending(station,owner,2,ship))return false;
    const auto outstanding=outstanding_freight(ship);
    if(outstanding>kMaxFreightTonnes-tonnes ||
       static_cast<std::uint64_t>(vessel->cargo)<outstanding+tonnes)return false;
    insert_job(station,owner,2,ship,tonnes,sim.tick());return true;
  }
  // Resolve a crash after original-world STEP but before queue acknowledgement.
  // An ambiguous command is held, NEVER replayed or silently dropped.
  void reconcile(const PersistentWorld& world,EntityId station,PlayerId owner) {
    Statement q(db_,"SELECT id,completed,kind,pending_command FROM dock_jobs_v1 "
                    "WHERE station=? AND owner=? AND state=1");
    q.bind(1,station);q.bind(2,owner);
    struct Pending{std::uint64_t id,completed,kind,cmd;};
    std::vector<Pending> pending;
    while(q.next()==SQLITE_ROW)pending.push_back({q.number(0),q.number(1),q.number(2),q.number(3)});
    for(const auto& job:pending){
      const auto& events=world.world().events();
      const auto it=std::find_if(events.begin(),events.end(),
        [&](const Event& e){return e.command_id==job.cmd;});
      if(it==events.end()){hold(job.id);continue;}
      if(it->result!=Outcome::Applied || it->transferred!=1){hold(job.id);continue;}
      finish(job.id,job.completed+1,job.kind==1?1:2);
    }
  }
  bool serve_one(PersistentWorld& world,EntityId station,PlayerId owner) {
    reconcile(world,station,owner);
    if(!active_staff(world.world(),station,owner))return false;
    Statement q(db_,"SELECT id,kind,source_id,requested,completed FROM dock_jobs_v1 "
                    "WHERE station=? AND owner=? AND state=0 ORDER BY id LIMIT 1");
    q.bind(1,station);q.bind(2,owner);
    if(q.next()!=SQLITE_ROW)return false;
    const auto id=q.number(0),kind=q.number(1),source=q.number(2);
    const auto requested=q.number(3),done=q.number(4);
    if(kind==1){
      const auto it=world.world().npc_records().find(source);
      const auto* home=it==world.world().npc_records().end() ?
          nullptr:world.world().find(it->second.home_ship);
      if(it==world.world().npc_records().end()||it->second.location!=station||
         it->second.transport_id||!home||home->owner_id!=owner){
        hold(id);return false;
      }
      // Check-in completes an appointment ONLY. No fictional voyage, world
      // tick, NPC location update or passenger materialization occurs here.
      Statement checked(db_,"UPDATE dock_jobs_v1 SET completed=1,state=2 "
                            "WHERE id=? AND kind=1 AND state=0 AND completed=0");
      checked.bind(1,id);checked.next();
      return sqlite3_changes(db_)==1;
    }
    const auto* vessel=world.world().find(source);
    if(!vessel||vessel->owner_id!=owner||vessel->dock_station!=station||
       vessel->dock_state!=DockState::Docked || vessel->cargo<1){hold(id);return false;}
    // Stable reserved command identity, one per actual tonne and job.
    if(id>0x0000FFFFFFFFFFFFULL||done>=requested||done>=64){hold(id);return false;}
    const std::uint64_t command_id=0x5000000000000000ULL|(id<<8)|(done+1);
    {
      Statement mark(db_,"UPDATE dock_jobs_v1 SET state=1,pending_command=? "
                         "WHERE id=? AND state=0 AND completed=?");
      mark.bind(1,command_id);mark.bind(2,id);mark.bind(3,done);mark.next();
      if(sqlite3_changes(db_)!=1)throw std::runtime_error("dock job reservation changed");
    }
    Command c{};c.id=command_id;c.sequence=command_id;
    c.execute_tick=world.world().tick()+1;
    c.issuer=owner;c.actor=source;c.target=station;c.amount=1;
    c.action=Action::UnloadOre;
    if(!world.submit(c)){hold(id);return false;}
    world.step();
    const auto& events=world.world().events();
    const auto it=std::find_if(events.begin(),events.end(),
      [&](const Event& e){return e.command_id==command_id;});
    if(it==events.end()||it->result!=Outcome::Applied||it->transferred!=1) {
      hold(id);return false;
    }
    finish(id,done+1,2);
    return true;
  }
 private:
  sqlite3* db_=nullptr;
  void exec(const char* sql)const{
    char* error=nullptr;
    if(sqlite3_exec(db_,sql,nullptr,nullptr,&error)!=SQLITE_OK){
      const std::string reason=error?error:"dock SQLite failure";
      sqlite3_free(error);throw std::runtime_error(reason);
    }
  }
  bool is_staff(std::uint64_t npc)const {
    Statement q(db_,"SELECT 1 FROM dock_staff_v1 WHERE npc_id=? LIMIT 1");
    q.bind(1,npc);return q.next()==SQLITE_ROW;
  }
  std::uint64_t active_jobs(EntityId station,PlayerId owner,int kind)const {
    Statement q(db_,"SELECT COUNT(*) FROM dock_jobs_v1 WHERE station=? AND owner=? "
                    "AND (?=0 OR kind=?) AND state IN(0,1,3)");
    q.bind(1,station);q.bind(2,owner);q.bind(3,kind);q.bind(4,kind);
    return q.next()==SQLITE_ROW?q.number(0):0;
  }
  bool source_pending(EntityId station,PlayerId owner,int kind,std::uint64_t src)const {
    Statement q(db_,"SELECT 1 FROM dock_jobs_v1 WHERE station=? AND owner=? "
                    "AND kind=? AND source_id=? AND state IN(0,1,3) LIMIT 1");
    q.bind(1,station);q.bind(2,owner);q.bind(3,kind);q.bind(4,src);
    return q.next()==SQLITE_ROW;
  }
  std::uint64_t outstanding_freight(EntityId source)const{
    Statement q(db_,"SELECT COALESCE(SUM(requested-completed),0) FROM dock_jobs_v1 "
                    "WHERE kind=2 AND source_id=? AND state IN(0,1,3)");
    q.bind(1,source);
    return q.next()==SQLITE_ROW?q.number(0):0;
  }
  void insert_job(EntityId station,PlayerId owner,int kind,std::uint64_t src,
                  std::uint64_t requested,std::uint64_t tick){
    Statement s(db_,"INSERT INTO dock_jobs_v1 "
                   "(station,owner,kind,source_id,requested,created_tick) VALUES(?,?,?,?,?,?)");
    s.bind(1,station);s.bind(2,owner);s.bind(3,kind);s.bind(4,src);
    s.bind(5,requested);s.bind(6,tick);s.next();
  }
  void hold(std::uint64_t id){
    Statement s(db_,"UPDATE dock_jobs_v1 SET state=3 WHERE id=? AND state IN(0,1)");
    s.bind(1,id);s.next();
  }
  void finish(std::uint64_t id,std::uint64_t completed,int kind){
    Statement s(db_,"UPDATE dock_jobs_v1 SET completed=?,state="
                    "(CASE WHEN requested=? THEN 2 ELSE 0 END),pending_command=0 "
                    "WHERE id=? AND state=1");
    s.bind(1,completed);s.bind(2,completed);s.bind(3,id);s.next();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("dock reconciliation mismatch");
    (void)kind;
  }
};
} // namespace galaxy::dock
