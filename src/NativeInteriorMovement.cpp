#include "galaxy/NativeInteriorMovement.hpp"
#include "galaxy/NativeDockingTarget.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy::fp3d {
namespace {
void check(int rc,sqlite3* db,const char* where){
    if(rc!=SQLITE_OK && rc!=SQLITE_DONE && rc!=SQLITE_ROW)
        throw std::runtime_error(std::string(where)+": "+sqlite3_errmsg(db));
}
void exec(sqlite3* db,const char* sql){check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr),db,"SQLite");}
class Stmt {
public:
    sqlite3_stmt* p=nullptr;
    sqlite3* db=nullptr;
    Stmt(sqlite3* d,const char* q):db(d){check(sqlite3_prepare_v2(d,q,-1,&p,nullptr),d,"prepare");}
    ~Stmt(){sqlite3_finalize(p);}
    Stmt(const Stmt&)=delete;
    void val(int i,std::int64_t v){check(sqlite3_bind_int64(p,i,v),db,"bind");}
    void str(int i,const std::string& v){check(sqlite3_bind_text(p,i,v.c_str(),-1,SQLITE_TRANSIENT),db,"bind");}
    bool next(){int x=sqlite3_step(p);if(x==SQLITE_DONE)return false;check(x,db,"step");return true;}
    void done(){if(next())throw std::runtime_error("unexpected SQLite return row");}
    std::int64_t i(int n)const{return sqlite3_column_int64(p,n);}
    std::string s(int n)const{const unsigned char* v=sqlite3_column_text(p,n);return v?reinterpret_cast<const char*>(v):"";}
};
std::int64_t signed_id(std::uint64_t n){
    if(n>std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("SQLite identity exceeds signed 64-bit");
    return std::int64_t(n);
}
std::string digest(PlayerId owner,EntityId ship,std::uint64_t seq,
                   std::uint64_t tick,std::uint64_t generation,
                   std::uint64_t receipt,std::int64_t x0,std::int64_t z0,
                   std::int64_t x1,std::int64_t z1,const std::string& prev){
    std::ostringstream o;
    o<<"FP3D02C-v1|"<<owner<<'|'<<ship<<'|'<<seq<<'|'<<tick<<'|'<<generation
     <<'|'<<receipt<<'|'<<x0<<'|'<<z0<<'|'<<x1<<'|'<<z1<<'|'<<prev;
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char c:o.str()){h^=c;h*=1099511628211ULL;}
    return std::to_string(h); // corruption consistency only, NOT a signature
}
// 340mm radius capsule. Native graybox interior test bounds and one crate,
// NOT yet Godot station physics. All coordinates are mm relative to own ship.
constexpr std::int64_t kMaxStep=100, kRoom=8000, kRadius=340;
bool inside(std::int64_t x,std::int64_t z){
    if(x< -kRoom+kRadius || x>kRoom-kRadius || z< -kRoom+kRadius || z>kRoom-kRadius)return false;
    if(x>=1200-kRadius && x<=2000+kRadius && z>=-500-kRadius && z<=500+kRadius)return false;
    return true;
}
bool physical_step(std::int64_t a,std::int64_t b,std::int64_t x,std::int64_t z){
    const auto dx=x-a,dz=z-b;
    if(dx==0 && dz==0)return false;
    if(dx>kMaxStep||dx< -kMaxStep||dz>kMaxStep||dz< -kMaxStep||
        dx*dx+dz*dz>kMaxStep*kMaxStep)return false;
    // Each command <=100mm; cannot tunnel through a 340mm capsule obstacle.
    return inside(a,b)&&inside(x,z);
}

std::string seat_digest(PlayerId owner,EntityId ship,std::uint64_t seq,
                        std::uint64_t tick,std::uint64_t generation,std::uint64_t receipt,
                        bool before,bool after,const std::string& prev){
    std::ostringstream out;
    out<<"FP3D03A-v1|"<<owner<<'|'<<ship<<'|'<<seq<<'|'<<tick<<'|'<<generation
       <<'|'<<receipt<<'|'<<int(before)<<'|'<<int(after)<<'|'<<prev;
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char c:out.str()){h^=c;h*=1099511628211ULL;}
    return std::to_string(h); // consistency check, not a cryptographic signature
}
bool near_cockpit(std::int64_t x,std::int64_t z){
    return x>=-600&&x<=600&&z>=-600&&z<=600;
}

class Transaction {
    sqlite3* db_;
    bool complete_=false;
public:
    explicit Transaction(sqlite3* db):db_(db){exec(db_,"BEGIN IMMEDIATE");}
    ~Transaction(){if(!complete_)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
    void commit(){exec(db_,"COMMIT");complete_=true;}
};
}
InteriorMovement::InteriorMovement(const std::string& path,PersistentWorld& world):world_(world){
    if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        const auto s=std::string("SQLite open failed: ")+(db_?sqlite3_errmsg(db_):"unknown");
        sqlite3_close(db_);db_=nullptr;throw std::runtime_error(s);
    }
    try {
        sqlite3_busy_timeout(db_,5000);
        exec(db_,"PRAGMA foreign_keys=ON");
        Transaction transaction(db_);
        verify_world_generation();
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_fp3d02c_pose("
            "owner INTEGER PRIMARY KEY CHECK(owner>0),ship INTEGER NOT NULL CHECK(ship>0),"
            "x_mm INTEGER NOT NULL,y_mm INTEGER NOT NULL CHECK(y_mm=0),z_mm INTEGER NOT NULL,"
            "seq INTEGER NOT NULL CHECK(seq>=0),world_tick INTEGER NOT NULL CHECK(world_tick>=0),"
            "world_gen INTEGER NOT NULL CHECK(world_gen>0),tip TEXT NOT NULL)");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_fp3d02c_step("
            "owner INTEGER NOT NULL,seq INTEGER NOT NULL CHECK(seq>0),receipt INTEGER NOT NULL UNIQUE CHECK(receipt>0),"
            "ship INTEGER NOT NULL,from_x INTEGER NOT NULL,from_z INTEGER NOT NULL,"
            "to_x INTEGER NOT NULL,to_z INTEGER NOT NULL,world_tick INTEGER NOT NULL,"
            "world_gen INTEGER NOT NULL,prev_tip TEXT NOT NULL,digest TEXT NOT NULL,"
            "PRIMARY KEY(owner,seq),FOREIGN KEY(owner) REFERENCES sg_fp3d02c_pose(owner))");
        exec(db_,"CREATE TRIGGER IF NOT EXISTS sg_fp3d02c_step_no_update BEFORE UPDATE ON sg_fp3d02c_step "
             "BEGIN SELECT RAISE(ABORT,'immutable movement receipts'); END");
        exec(db_,"CREATE TRIGGER IF NOT EXISTS sg_fp3d02c_step_no_delete BEFORE DELETE ON sg_fp3d02c_step "
             "BEGIN SELECT RAISE(ABORT,'immutable movement receipts'); END");

        exec(db_,"CREATE TABLE IF NOT EXISTS sg_fp3d03a_seat("
            "owner INTEGER PRIMARY KEY CHECK(owner>0),ship INTEGER NOT NULL CHECK(ship>0),"
            "seated INTEGER NOT NULL CHECK(seated IN(0,1)),seq INTEGER NOT NULL CHECK(seq>=0),"
            "world_tick INTEGER NOT NULL CHECK(world_tick>=0),world_gen INTEGER NOT NULL CHECK(world_gen>0),"
            "tip TEXT NOT NULL,FOREIGN KEY(owner) REFERENCES sg_fp3d02c_pose(owner))");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_fp3d03a_seat_event("
            "owner INTEGER NOT NULL,seq INTEGER NOT NULL CHECK(seq>0),"
            "receipt INTEGER NOT NULL UNIQUE CHECK(receipt>0),ship INTEGER NOT NULL,"
            "from_seated INTEGER NOT NULL CHECK(from_seated IN(0,1)),"
            "to_seated INTEGER NOT NULL CHECK(to_seated IN(0,1)),"
            "world_tick INTEGER NOT NULL,world_gen INTEGER NOT NULL,"
            "prev_tip TEXT NOT NULL,digest TEXT NOT NULL,"
            "PRIMARY KEY(owner,seq),FOREIGN KEY(owner) REFERENCES sg_fp3d03a_seat(owner))");
        exec(db_,"CREATE TRIGGER IF NOT EXISTS sg_fp3d03a_seat_event_no_update "
            "BEFORE UPDATE ON sg_fp3d03a_seat_event BEGIN "
            "SELECT RAISE(ABORT,'immutable seat receipts'); END");
        exec(db_,"CREATE TRIGGER IF NOT EXISTS sg_fp3d03a_seat_event_no_delete "
            "BEFORE DELETE ON sg_fp3d03a_seat_event BEGIN "
            "SELECT RAISE(ABORT,'immutable seat receipts'); END");
        transaction.commit();
    } catch(...) {sqlite3_close(db_);db_=nullptr;throw;}
}
InteriorMovement::~InteriorMovement(){sqlite3_close(db_);}
void InteriorMovement::verify_world_generation()const{
    Stmt q(db_,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
    if(!q.next() || q.i(0)<1 || std::uint64_t(q.i(0))!=world_.generation())
        throw std::runtime_error("world generation mismatch: reject stale or independent pose writer");
}
void InteriorMovement::verify_step_receipt(std::uint64_t generation,std::uint64_t tick)const{
    if(generation==0 || tick==0)
        throw std::runtime_error("movement requires committed native world tick");
    // Snapshot compaction removes older checkpoint rows while retaining the
    // authoritative operation_log history. Never require a compacted snapshot;
    // compare checksums when one remains, otherwise validate the exact native
    // operation kind/tick and its persisted hash.
    Stmt op(db_,"SELECT o.kind,o.tick,o.state_hash,s.checksum,s.format "
                "FROM operation_log o LEFT JOIN snapshots s "
                "ON s.generation=o.generation WHERE o.generation=?");
    op.val(1,signed_id(generation));
    if(!op.next() || op.i(0)!=2 || std::uint64_t(op.i(1))!=tick ||
       op.s(2).empty() ||
       (sqlite3_column_type(op.p,3)!=SQLITE_NULL &&
           (op.s(2)!=op.s(3) || op.i(4)!=1)) || op.next())
        throw std::runtime_error("movement lacks native committed STEP journal proof");
}
bool InteriorMovement::valid_person(PlayerId owner,EntityId ship)const{
    if(!owner || !ship)return false;
    const auto& people=world_.world().player_people();
    auto it=people.find(owner);
    if(it==people.end())return false;
    const auto& p=it->second;
    const auto* vessel=world_.world().find(ship);
    return p.owner==owner && p.state==PlayerPersonState::AboardOwnShip &&
           p.home_ship==ship && p.physical_ship==ship &&
           vessel && vessel->type==EntityType::Ship && vessel->owner_id==owner;
}
Pose InteriorMovement::load_or_origin(PlayerId owner){
    Stmt get(db_,"SELECT owner,ship,x_mm,y_mm,z_mm,seq,world_tick,world_gen,tip "
                 "FROM sg_fp3d02c_pose WHERE owner=?");
    get.val(1,signed_id(owner));
    if(get.next()) {
        Pose p{std::uint64_t(get.i(0)),std::uint64_t(get.i(1)),get.i(2),get.i(3),get.i(4),
               std::uint64_t(get.i(5)),std::uint64_t(get.i(6)),std::uint64_t(get.i(7)),get.s(8)};
        if(p.owner!=owner || !valid_person(owner,p.ship) || p.y_mm!=0 || !inside(p.x_mm,p.z_mm) ||
           p.world_generation>world_.generation() || p.world_tick>world_.world().tick())
            throw std::runtime_error("pose identity/custody mismatch");
        verify_chain(p);
        return p;
    }
    // No implicit player! Only the actual registered player in their own hull
    // may receive the zero-sequence interior origin.
    const auto it=world_.world().player_people().find(owner);
    if(it==world_.world().player_people().end() || !valid_person(owner,it->second.physical_ship))
        throw std::runtime_error("missing authorized physical player");
    Pose p{owner,it->second.physical_ship,0,0,0,0,world_.world().tick(),world_.generation(),"genesis-v1"};
    Stmt put(db_,"INSERT INTO sg_fp3d02c_pose VALUES(?,?,?,?,?,?,?,?,?)");
    put.val(1,signed_id(p.owner));put.val(2,signed_id(p.ship));put.val(3,p.x_mm);put.val(4,0);
    put.val(5,p.z_mm);put.val(6,0);put.val(7,signed_id(p.world_tick));
    put.val(8,signed_id(p.world_generation));put.str(9,p.digest);put.done();
    return p;
}
void InteriorMovement::verify_chain(const Pose& p){
    Stmt r(db_,"SELECT seq,receipt,ship,from_x,from_z,to_x,to_z,world_tick,world_gen,prev_tip,digest "
               "FROM sg_fp3d02c_step WHERE owner=? ORDER BY seq");
    r.val(1,signed_id(p.owner));
    std::uint64_t seq=0,tick=0,gen=0;
    std::int64_t x=0,z=0;
    std::string prev="genesis-v1";
    while(r.next()){
        const std::uint64_t n=std::uint64_t(r.i(0)),receipt=std::uint64_t(r.i(1)),ship=std::uint64_t(r.i(2));
        const auto from_x=r.i(3),from_z=r.i(4),to_x=r.i(5),to_z=r.i(6);
        const std::uint64_t current_tick=std::uint64_t(r.i(7)),current_gen=std::uint64_t(r.i(8));
        const auto prior=r.s(9),d=r.s(10);
        if(n!=seq+1 || !receipt || ship!=p.ship || from_x!=x || from_z!=z ||
           !physical_step(x,z,to_x,to_z) || current_tick<=tick || current_gen<=gen ||
           current_tick>p.world_tick || current_gen>p.world_generation || prior!=prev ||
           d!=digest(p.owner,p.ship,n,current_tick,current_gen,receipt,x,z,to_x,to_z,prior))
            throw std::runtime_error("movement receipt chain invalid");
        verify_step_receipt(current_gen,current_tick);
        seq=n;tick=current_tick;gen=current_gen;x=to_x;z=to_z;prev=d;
    }
    if(seq!=p.sequence || x!=p.x_mm || z!=p.z_mm || prev!=p.digest ||
       (seq!=0 && (tick!=p.world_tick || gen!=p.world_generation)) ||
       (seq==0 && p.digest!="genesis-v1"))
        throw std::runtime_error("pose does not match independently replayed receipts");
}

void InteriorMovement::verify_seat_chain(const Pose& pose)const{
    Stmt events(db_,"SELECT seq,receipt,ship,from_seated,to_seated,world_tick,"
                    "world_gen,prev_tip,digest FROM sg_fp3d03a_seat_event "
                    "WHERE owner=? ORDER BY seq");
    events.val(1,signed_id(pose.owner));
    std::uint64_t n=0,last_tick=0,last_gen=0;
    bool seated=false;
    std::string tip="seat-genesis-v1";
    while(events.next()){
        const auto seq=std::uint64_t(events.i(0)),receipt=std::uint64_t(events.i(1));
        const auto ship=std::uint64_t(events.i(2));
        const auto from=events.i(3),to=events.i(4);
        const auto tick=std::uint64_t(events.i(5)),gen=std::uint64_t(events.i(6));
        if(seq!=n+1||!receipt||ship!=pose.ship||from!=int(seated)||
           to==from||tick<=last_tick||gen<=last_gen||
           tick>world_.world().tick()||gen>world_.generation()||
           events.s(7)!=tip||
           events.s(8)!=seat_digest(pose.owner,pose.ship,seq,tick,gen,receipt,
                                    from!=0,to!=0,tip))
            throw std::runtime_error("cockpit transition receipt chain invalid");
        verify_step_receipt(gen,tick);
        n=seq;last_tick=tick;last_gen=gen;seated=to!=0;tip=events.s(8);
    }
    if(n!=pose.seat_sequence||seated!=pose.seated)
        throw std::runtime_error("cockpit state differs from immutable receipts");
    Stmt row(db_,"SELECT world_tick,world_gen,tip FROM sg_fp3d03a_seat WHERE owner=?");
    row.val(1,signed_id(pose.owner));
    if(!row.next()||row.s(2)!=tip||
       (n>0&&(std::uint64_t(row.i(0))!=last_tick||
              std::uint64_t(row.i(1))!=last_gen))||row.next())
        throw std::runtime_error("cockpit state tip mismatch");
}
void InteriorMovement::load_seat(Pose& pose){
    Stmt seat(db_,"SELECT ship,seated,seq,world_tick,world_gen,tip "
                  "FROM sg_fp3d03a_seat WHERE owner=?");
    seat.val(1,signed_id(pose.owner));
    if(seat.next()){
        const auto ship=std::uint64_t(seat.i(0));
        const auto seated=seat.i(1),seq=seat.i(2),tick=seat.i(3),gen=seat.i(4);
        if(ship!=pose.ship||(seated!=0&&seated!=1)||seq<0||tick<0||gen<1||
           std::uint64_t(tick)>world_.world().tick()||
           std::uint64_t(gen)>world_.generation()||seat.s(5).empty()||seat.next())
            throw std::runtime_error("invalid persisted cockpit state");
        pose.seated=seated!=0;
        pose.seat_sequence=std::uint64_t(seq);
    }else{
        Stmt put(db_,"INSERT INTO sg_fp3d03a_seat VALUES(?,?,?,?,?,?,?)");
        put.val(1,signed_id(pose.owner));put.val(2,signed_id(pose.ship));
        put.val(3,0);put.val(4,0);
        put.val(5,signed_id(world_.world().tick()));
        put.val(6,signed_id(world_.generation()));
        put.str(7,"seat-genesis-v1");put.done();
        pose.seated=false;pose.seat_sequence=0;
    }
    verify_seat_chain(pose);
}

Pose InteriorMovement::current(PlayerId owner){
    Transaction transaction(db_);
    verify_world_generation();
    Pose p=load_or_origin(owner);
    load_seat(p);
    transaction.commit();
    return p;
}
bool InteriorMovement::move(PlayerId owner,std::uint64_t expected,std::uint64_t receipt,
                            std::int64_t dx,std::int64_t dz,MoveFailPoint fault){
    // The host supplies trusted_owner, never a client-selected identity.
    // Bad control messages do not advance the canonical simulation tick.
    if(!owner || !receipt ||
       receipt>std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
       dx>kMaxStep||dx< -kMaxStep||dz>kMaxStep||dz< -kMaxStep ||
       !world_.world().player_people().count(owner))return false;
    const auto p=current(owner); // independently replays receipts before admission
    if(!valid_person(owner,p.ship) || p.seated || expected!=p.sequence ||
       !physical_step(p.x_mm,p.z_mm,p.x_mm+dx,p.z_mm+dz))return false;
    {
        Stmt duplicate(db_,"SELECT 1 FROM sg_fp3d02c_step WHERE receipt=?");
        duplicate.val(1,signed_id(receipt));
        if(duplicate.next())return false;
        Stmt seat_duplicate(db_,"SELECT 1 FROM sg_fp3d03a_seat_event WHERE receipt=?");
        seat_duplicate.val(1,signed_id(receipt));
        if(seat_duplicate.next())return false;
    }
    // One ORIGINAL world tick, its operation_log STEP and the interior pose
    // all commit with a SINGLE SQLite Store::commit writer transaction.
    // An exception at either injection point rolls back ALL THREE records.
    world_.step_with_cell_worker(nullptr,FailPoint::None,
        [&](sqlite3* writer,std::uint64_t generation){
            const auto t=world_.world().tick()+1;
            const auto n=p.sequence+1;
            Stmt op(writer,"SELECT kind,tick,state_hash FROM operation_log WHERE generation=?");
            op.val(1,signed_id(generation));
            if(!op.next() || op.i(0)!=2 || std::uint64_t(op.i(1))!=t ||
               op.s(2).empty() || op.next())
                throw std::runtime_error("atomic native STEP missing before movement receipt");
            Stmt old(writer,"SELECT ship,x_mm,y_mm,z_mm,seq,world_tick,world_gen,tip "
                            "FROM sg_fp3d02c_pose WHERE owner=?");
            old.val(1,signed_id(owner));
            if(!old.next() || std::uint64_t(old.i(0))!=p.ship ||
               old.i(1)!=p.x_mm || old.i(2)!=0 || old.i(3)!=p.z_mm ||
               std::uint64_t(old.i(4))!=p.sequence || old.s(7)!=p.digest ||
               old.i(5)!=static_cast<std::int64_t>(p.world_tick) ||
               old.i(6)!=static_cast<std::int64_t>(p.world_generation) || old.next())
                throw std::runtime_error("atomic FPS pose compare-and-swap failed");
            const auto next_digest=digest(owner,p.ship,n,t,generation,receipt,
                p.x_mm,p.z_mm,p.x_mm+dx,p.z_mm+dz,p.digest);
            Stmt insert(writer,"INSERT INTO sg_fp3d02c_step VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
            insert.val(1,signed_id(owner));insert.val(2,signed_id(n));
            insert.val(3,signed_id(receipt));insert.val(4,signed_id(p.ship));
            insert.val(5,p.x_mm);insert.val(6,p.z_mm);
            insert.val(7,p.x_mm+dx);insert.val(8,p.z_mm+dz);
            insert.val(9,signed_id(t));insert.val(10,signed_id(generation));
            insert.str(11,p.digest);insert.str(12,next_digest);insert.done();
            if(fault==MoveFailPoint::AfterStepReceipt)
                throw std::runtime_error("injected crash after atomic movement receipt");
            Stmt put(writer,"UPDATE sg_fp3d02c_pose SET x_mm=?,z_mm=?,seq=?,world_tick=?,"
                            "world_gen=?,tip=? WHERE owner=? AND seq=?");
            put.val(1,p.x_mm+dx);put.val(2,p.z_mm+dz);put.val(3,signed_id(n));
            put.val(4,signed_id(t));put.val(5,signed_id(generation));
            put.str(6,next_digest);put.val(7,signed_id(owner));put.val(8,signed_id(expected));
            put.done();
            if(sqlite3_changes(writer)!=1)
                throw std::runtime_error("atomic FPS pose update lost writer race");
            if(fault==MoveFailPoint::AfterPoseUpdate)
                throw std::runtime_error("injected crash after atomic movement pose");
        });
    return true;
}

bool InteriorMovement::set_seated(PlayerId owner,std::uint64_t expected,
                                  std::uint64_t receipt,bool desired,SeatFailPoint fault){
    if(!owner||!receipt||
       receipt>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       !world_.world().player_people().count(owner))return false;
    const auto p=current(owner);
    // The cockpit is a real bounded zone of THIS ship interior, not a teleport.
    // Standing leaves the player at the same validated physical position.
    if(!valid_person(owner,p.ship)||p.seat_sequence!=expected||
       p.seated==desired||!near_cockpit(p.x_mm,p.z_mm))return false;
    {
        Stmt used_seat(db_,"SELECT 1 FROM sg_fp3d03a_seat_event WHERE receipt=?");
        used_seat.val(1,signed_id(receipt));
        if(used_seat.next())return false;
        Stmt used_move(db_,"SELECT 1 FROM sg_fp3d02c_step WHERE receipt=?");
        used_move.val(1,signed_id(receipt));
        if(used_move.next())return false;
    }
    world_.step_with_cell_worker(nullptr,FailPoint::None,
        [&](sqlite3* writer,std::uint64_t generation){
            const auto tick=world_.world().tick()+1;
            const auto seq=p.seat_sequence+1;
            Stmt op(writer,"SELECT kind,tick,state_hash FROM operation_log WHERE generation=?");
            op.val(1,signed_id(generation));
            if(!op.next()||op.i(0)!=2||std::uint64_t(op.i(1))!=tick||
               op.s(2).empty()||op.next())
                throw std::runtime_error("cockpit change missing native STEP");
            Stmt before(writer,"SELECT ship,seated,seq,tip FROM sg_fp3d03a_seat WHERE owner=?");
            before.val(1,signed_id(owner));
            if(!before.next()||std::uint64_t(before.i(0))!=p.ship||
               before.i(1)!=int(p.seated)||std::uint64_t(before.i(2))!=expected)
                throw std::runtime_error("atomic cockpit state compare-and-swap failed");
            const std::string prior=before.s(3);
            if(before.next()||prior.empty())
                throw std::runtime_error("atomic cockpit state ambiguous");
            // No intervening walking in a different persisted pose.
            Stmt old(writer,"SELECT ship,x_mm,z_mm,seq FROM sg_fp3d02c_pose WHERE owner=?");
            old.val(1,signed_id(owner));
            if(!old.next()||std::uint64_t(old.i(0))!=p.ship||
               old.i(1)!=p.x_mm||old.i(2)!=p.z_mm||
               std::uint64_t(old.i(3))!=p.sequence||old.next())
                throw std::runtime_error("atomic cockpit pose changed");
            const auto next=seat_digest(owner,p.ship,seq,tick,generation,receipt,
                                        p.seated,desired,prior);
            Stmt event(writer,"INSERT INTO sg_fp3d03a_seat_event VALUES(?,?,?,?,?,?,?,?,?,?)");
            event.val(1,signed_id(owner));event.val(2,signed_id(seq));
            event.val(3,signed_id(receipt));event.val(4,signed_id(p.ship));
            event.val(5,p.seated?1:0);event.val(6,desired?1:0);
            event.val(7,signed_id(tick));event.val(8,signed_id(generation));
            event.str(9,prior);event.str(10,next);event.done();
            if(fault==SeatFailPoint::AfterSeatReceipt)
                throw std::runtime_error("injected cockpit receipt rollback");
            Stmt update(writer,"UPDATE sg_fp3d03a_seat SET seated=?,seq=?,"
                        "world_tick=?,world_gen=?,tip=? WHERE owner=? AND seq=?");
            update.val(1,desired?1:0);update.val(2,signed_id(seq));
            update.val(3,signed_id(tick));update.val(4,signed_id(generation));
            update.str(5,next);update.val(6,signed_id(owner));
            update.val(7,signed_id(expected));update.done();
            if(sqlite3_changes(writer)!=1)
                throw std::runtime_error("atomic cockpit writer race");
            if(fault==SeatFailPoint::AfterSeatUpdate)
                throw std::runtime_error("injected cockpit pose rollback");
        });
    return true;
}


FlightState InteriorMovement::flight_state(PlayerId owner){
    const auto pose=current(owner);
    const auto* ship=world_.world().find(pose.ship);
    if(!ship||ship->type!=EntityType::Ship||ship->owner_id!=owner)
        throw std::runtime_error("authoritative flight ship unavailable");
    return FlightState{ship->id,world_.world().tick(),world_.generation(),
        ship->position.x,ship->position.y,ship->position_z_mm,
        ship->velocity.x,ship->velocity.y,ship->velocity_z_mm_per_tick,
        ship->heading_deg,ship->dock_state};
}
std::optional<Outcome> InteriorMovement::pilot(PlayerId owner,
        std::uint64_t expected_seat_sequence,std::uint64_t command_id,
        PilotControl control,PilotFailPoint fault){
    // This path never accepts an actor, ship ID, player ID or thrust vector
    // from the client. The original player-person and seat receipt decide.
    constexpr std::uint64_t cockpit_tag=std::uint64_t{1}<<60;
    constexpr std::uint64_t other_tags=std::uint64_t{3}<<61;
    if(!owner||!command_id||!(command_id&cockpit_tag)||
       (command_id&other_tags))return std::nullopt;
    const auto pose=current(owner);
    if(!valid_person(owner,pose.ship)||!pose.seated||
       pose.seat_sequence!=expected_seat_sequence ||
       world_.world().has_pending_cockpit_flight())return std::nullopt;
    const auto* ship=world_.world().find(pose.ship);
    if(!ship||ship->type!=EntityType::Ship||ship->owner_id!=owner)
        return std::nullopt;
    // The original flight engine models yaw in the XY plane. Quantize
    // horizontal thrust to the closest cardinal axis; no local translation.
    // Counterthrust opposes ORIGINAL velocity, without overshooting a
    // sub-250 mm/tick residual. Finer braking is future physics work.
    const int quarter=((int(ship->heading_deg)+45)/90)%4;
    int forward_x=0,forward_y=0;
    switch(quarter){
        case 0:forward_x=1;break;
        case 1:forward_y=1;break;
        case 2:forward_x=-1;break;
        default:forward_y=-1;break;
    }
    Command c{};
    c.id=command_id;
    c.sequence=command_id;
    c.execute_tick=world_.world().tick()+1;
    c.issuer=owner;
    c.actor=pose.ship;
    c.action=Action::Thrust;
    switch(control){
        case PilotControl::Undock:
            c.action=Action::RequestUndock;break;
        case PilotControl::Dock: {
            const auto station=docking::nearest_original_station(world_.world(),*ship);
            if(!station)return std::nullopt;
            c.action=Action::RequestDock;
            c.target=*station;
            break;
        }
        case PilotControl::YawLeft:c.amount=-15;break;
        case PilotControl::YawRight:c.amount=15;break;
        case PilotControl::Forward:
            c.thrust_x=forward_x;c.thrust_y=forward_y;break;
        case PilotControl::Reverse:
            c.thrust_x=-forward_x;c.thrust_y=-forward_y;break;
        case PilotControl::StrafePort:
            c.thrust_x=forward_y;c.thrust_y=-forward_x;break;
        case PilotControl::StrafeStarboard:
            c.thrust_x=-forward_y;c.thrust_y=forward_x;break;
        case PilotControl::Counterthrust:
            if(ship->velocity.x>=Simulation::kAccelerationMmPerTick2)c.thrust_x=-1;
            else if(ship->velocity.x<=-Simulation::kAccelerationMmPerTick2)c.thrust_x=1;
            if(ship->velocity.y>=Simulation::kAccelerationMmPerTick2)c.thrust_y=-1;
            else if(ship->velocity.y<=-Simulation::kAccelerationMmPerTick2)c.thrust_y=1;
            // Do not claim exact zero or reverse tiny residual velocity.
            if(!c.thrust_x&&!c.thrust_y)return std::nullopt;
            break;
        default:return std::nullopt;
    }
    if(!world_.submit_cockpit_flight_checked(c,expected_seat_sequence))
        return std::nullopt;
    if(fault==PilotFailPoint::AfterAcceptance)
        throw std::runtime_error("injected cockpit flight after original acceptance");
    // Separate original STEP log, not a phantom client-authoritative update.
    // If interrupted between submit and step, the pending cockpit command is
    // recoverably visible and the next host refuses to process new inputs.
    const auto before=world_.world().events().size();
    world_.step();
    const auto& events=world_.world().events();
    if(events.size()<=before)
        throw std::runtime_error("flight accepted but original STEP event missing");
    std::optional<Outcome> result;
    for(std::size_t i=before;i<events.size();++i)
        if(events[i].command_id==c.id)result=events[i].result;
    if(!result)throw std::runtime_error("flight command lacks execution receipt");
    if((control==PilotControl::Undock || control==PilotControl::Dock) &&
       *result==Outcome::Applied){
        // Original docking state transitions require two further world ticks.
        // Only advance bounded canonical simulation; never set dock_state
        // directly or fabricate a completion receipt.
        for(int i=0;i<Simulation::kDockTransitionTicks+1;++i){
            const auto* current=world_.world().find(pose.ship);
            if(!current || (control==PilotControl::Undock ?
                current->dock_state!=DockState::Undocking :
                current->dock_state!=DockState::Docking))break;
            world_.step();
        }
    }
    return result;
}

} // namespace galaxy::fp3d
