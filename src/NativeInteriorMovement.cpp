#include "galaxy/NativeInteriorMovement.hpp"
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
Pose InteriorMovement::current(PlayerId owner){
    Transaction transaction(db_);
    verify_world_generation();
    Pose p=load_or_origin(owner);
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
    if(!valid_person(owner,p.ship) || expected!=p.sequence ||
       !physical_step(p.x_mm,p.z_mm,p.x_mm+dx,p.z_mm+dz))return false;
    {
        Stmt duplicate(db_,"SELECT 1 FROM sg_fp3d02c_step WHERE receipt=?");
        duplicate.val(1,signed_id(receipt));
        if(duplicate.next())return false;
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

} // namespace galaxy::fp3d
