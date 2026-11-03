#include "galaxy/ShipmentManifest.hpp"
#include <stdexcept>
#include <limits>
#include <utility>
#include <vector>
#include <memory>
#include <algorithm>

namespace galaxy::shipping {
namespace {
void require(bool ok, const char* why){if(!ok)throw std::runtime_error(std::string("shipment: ")+why);}
void run(sqlite3* db, const char* sql){
 char* err=nullptr;const auto rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
 if(rc!=SQLITE_OK){std::string s=err?err:"SQL error";sqlite3_free(err);throw std::runtime_error("shipment SQL: "+s);}
}
struct Db {
 sqlite3* db=nullptr;
 explicit Db(const std::string& p){
  require(sqlite3_open_v2(p.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"open SQLite");
  sqlite3_busy_timeout(db,5000);run(db,"PRAGMA foreign_keys=ON");
 }
 ~Db(){sqlite3_close(db);}
 Db(const Db&)=delete;Db& operator=(const Db&)=delete;
};
struct Q {
 sqlite3_stmt* q=nullptr;
 Q(sqlite3* db,const char* sql){require(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"prepare statement");}
 ~Q(){sqlite3_finalize(q);}
 Q(const Q&)=delete;Q& operator=(const Q&)=delete;
 void i(int n,std::int64_t v){require(sqlite3_bind_int64(q,n,v)==SQLITE_OK,"bind number");}
 void t(int n,const std::string& v){require(sqlite3_bind_text(q,n,v.c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK,"bind text");}
 bool row(){const int r=sqlite3_step(q);require(r==SQLITE_ROW||r==SQLITE_DONE,"read record");return r==SQLITE_ROW;}
 void exec(){require(sqlite3_step(q)==SQLITE_DONE,"write record");}
 std::int64_t number(int n){return sqlite3_column_int64(q,n);}
 std::string str(int n){const auto* p=sqlite3_column_text(q,n);return p?reinterpret_cast<const char*>(p):"";}
};
std::string id(std::uint64_t x){return std::to_string(x);}
std::uint64_t uid(const std::string& x){
 require(!x.empty()&&x[0]!='-'&&x.find_first_not_of("0123456789")==std::string::npos,"invalid numeric identity");
 auto v=std::stoull(x);require(v>0,"zero numeric identity");return v;
}
const char* item(int index){return index==1?"aluminum_panels":index==2?"aluminum_frames":"";}
bool has_schema(sqlite3* db){
 Q q(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='sg_v55_manifest'");
 require(q.row(),"schema lookup");const bool table=q.number(0)!=0;
 if(!table){
  Q cols(db,"PRAGMA table_info(world_meta)");bool marked=false;
  while(cols.row())if(cols.str(1)=="shipment_manifest_count")marked=true;
  if(marked){Q anchor(db,"SELECT shipment_manifest_count FROM world_meta WHERE singleton=1");
   require(anchor.row()&&anchor.number(0)==0,"deleted shipping registry with outstanding durable anchors");}
 }
 return table;
}
struct Record {Booking b; EntityId destination=0; std::uint64_t created_generation=0, destination_generation=0;};
Record read(sqlite3* db,const std::string& manifest){
 Q q(db,"SELECT m.manifest_id,m.ship_id,m.origin_site,m.pilot_id,m.dest_system,m.item_index,m.qty_kg,m.factory_return_id,m.load_id,m.unload_id,COALESCE(d.site_id,''),m.created_generation,COALESCE(d.world_generation,0) FROM sg_v55_manifest m LEFT JOIN sg_v55_destination d ON d.manifest_id=m.manifest_id WHERE m.manifest_id=?");
 q.t(1,manifest);require(q.row(),"missing manifest"); Record r;
 r.b.id=q.str(0);r.b.ship=uid(q.str(1));r.b.origin_site=uid(q.str(2));r.b.pilot=uid(q.str(3));
 r.b.destination_system=static_cast<std::uint32_t>(q.number(4));
 r.b.item_index=static_cast<int>(q.number(5));r.b.quantity_kg=q.number(6);
 r.b.factory_return_command=uid(q.str(7));r.b.load_command=uid(q.str(8));r.b.unload_command=uid(q.str(9));
 if(!q.str(10).empty())r.destination=uid(q.str(10));
 r.created_generation=static_cast<std::uint64_t>(q.number(11));
 r.destination_generation=static_cast<std::uint64_t>(q.number(12));
 return r;
}
struct Witness {std::uint64_t generation=0,tick=0;};
// World only emits resource-ledger rows for successfully applied effects, and
// its normal recovery replays and checks both historical_events and endpoints.
Witness witness(sqlite3* db,const Record& r,bool unload){
 const auto& b=r.b;const auto cmd=unload?b.unload_command:b.load_command;
 const auto from=unload?"goods:"+id(b.ship)+":"+item(b.item_index):
   "storage:"+id(b.origin_site)+":"+item(b.item_index);
 const auto to=unload?"storage:"+id(r.destination)+":"+item(b.item_index):
   "goods:"+id(b.ship)+":"+item(b.item_index);
 Q q(db,"SELECT l.generation,l.tick,l.amount,l.source_id,l.destination_id,h.outcome,h.transferred,h.actor_id,h.target_id,h.action FROM resource_ledger l JOIN historical_events h ON h.command_id=l.command_id WHERE l.reason='storage_stack_transfer_kg' AND l.command_id=?");
 q.t(1,id(cmd)); if(!q.row())return {};
 require(q.number(0)>0&&q.number(1)>0&&q.number(2)==b.quantity_kg&&
         q.str(3)==from&&q.str(4)==to&&q.number(5)==static_cast<int>(Outcome::Applied)&&
         q.number(6)==b.quantity_kg&&q.str(7)==id(b.ship)&&
         q.str(8)==id(unload?r.destination:b.origin_site)&&
         q.number(9)==static_cast<int>(Action::TransferStorageStack),"world custody witness mismatch");
 return {static_cast<std::uint64_t>(q.number(0)),static_cast<std::uint64_t>(q.number(1))};
}
void verify_one(sqlite3* db,const Record& r){
 const auto& b=r.b;require(!b.id.empty()&&b.id.size()<=100&&b.pilot&&b.ship&&b.origin_site&&
 b.destination_system&&b.destination_system<=1000000&&b.quantity_kg>0&&b.quantity_kg<=1000&&
 (b.item_index==1||b.item_index==2)&&b.load_command&&b.unload_command&&
 b.load_command!=b.unload_command,"invalid immutable manifest attributes");
 Q fact(db,"SELECT site,source_ship,material,stock_kg FROM sg_v53_world_return WHERE command_id=?");
 fact.i(1,static_cast<std::int64_t>(b.factory_return_command));
 require(fact.row()&&fact.number(0)==static_cast<std::int64_t>(b.origin_site)&&
 fact.number(1)==static_cast<std::int64_t>(b.ship)&&fact.str(2)==(b.item_index==1?"plates":"frames")&&
 fact.number(3)>=b.quantity_kg,"unbacked finished-goods provenance");
 const auto l=witness(db,r,false);
 require(r.created_generation>0&&(!l.generation||r.created_generation<l.generation),"manifest issuance order impossible");
 if(!r.destination){
  Q orphan(db,"SELECT COUNT(*) FROM resource_ledger WHERE reason='storage_stack_transfer_kg' AND command_id=?");
  orphan.t(1,id(b.unload_command));
  require(orphan.row()&&orphan.number(0)==0,
          "unload world receipt exists but receiving-site designation was deleted");
 } else {
  require(r.destination_generation>l.generation && r.destination_generation>r.created_generation,
          "destination designation predates verified cargo loading");
  require(r.destination!=b.origin_site,"same-site delivery is not haulage");
  Q constructed(db,"SELECT COUNT(*) FROM historical_events WHERE action=? AND target_id=? AND actor_id=? AND outcome=?");
  // The destination is also verified by the world construction and physical
  // ship/site relationship during designate_destination and unloading.
  constructed.i(1,static_cast<int>(Action::ConstructStorage));constructed.t(2,id(r.destination));
  constructed.t(3,id(b.ship));constructed.i(4,static_cast<int>(Outcome::Applied));
  require(constructed.row()&&constructed.number(0)==1,"missing commissioned warehouse");
 }
 const auto u=r.destination?witness(db,r,true):Witness{};
 require(!u.generation||l.generation,"unload without loading");
 if(u.generation){
  require(l.generation<u.generation&&l.tick<u.tick&&
          r.destination_generation<u.generation,"noncausal shipping timestamps");
  Q jump(db,"SELECT COUNT(*) FROM jump_ledger WHERE ship_id=? AND source_system<>dest_system AND dest_system=? AND accepted_tick>? AND completed_tick IS NOT NULL AND completed_tick<?");
  jump.t(1,id(b.ship));jump.i(2,b.destination_system);jump.i(3,static_cast<std::int64_t>(l.tick));jump.i(4,static_cast<std::int64_t>(u.tick));
  require(jump.row()&&jump.number(0)>0,"delivery had no completed physical inter-system voyage");
 }
 if(l.generation){
  const auto end=u.generation?u.generation:std::numeric_limits<std::uint64_t>::max();
  Q others(db,"SELECT command_id FROM resource_ledger WHERE reason='storage_stack_transfer_kg' AND generation>=? AND generation<=? AND (source_id=? OR destination_id=?)");
  others.i(1,static_cast<std::int64_t>(l.generation));
  others.i(2,end==std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::int64_t>::max():static_cast<std::int64_t>(end));
  others.t(3,"goods:"+id(b.ship)+":"+item(b.item_index));others.t(4,"goods:"+id(b.ship)+":"+item(b.item_index));
  unsigned count=0;while(others.row()){const auto v=others.str(0);require(v==id(b.load_command)||v==id(b.unload_command),"unmanifested transfer mixed consigned cargo");++count;}
  require(count==(u.generation?2u:1u),"duplicate or missing shipment cargo endpoint");
  // A differently numbered transfer route must not silently mix a
  // consigned stack, even if it uses an extra storage node or workshop.
  Q bypass(db,"SELECT COUNT(*) FROM historical_events WHERE generation>=? AND generation<=? AND actor_id=? AND outcome=? AND action IN (?,?,?,?)");
  bypass.i(1,static_cast<std::int64_t>(l.generation));
  bypass.i(2,u.generation?static_cast<std::int64_t>(u.generation):std::numeric_limits<std::int64_t>::max());
  bypass.t(3,id(b.ship));bypass.i(4,static_cast<int>(Outcome::Applied));
  bypass.i(5,static_cast<int>(Action::TransferStorageNodeStack));
  bypass.i(6,static_cast<int>(Action::TransferWorkshopStack));
  bypass.i(7,static_cast<int>(Action::TransferWorkshopOutput));
  bypass.i(8,static_cast<int>(Action::TransferWorkshopFinalOutput));
  require(bypass.row()&&bypass.number(0)==0,"alternate cargo transaction contaminated consigned lot");
 }
}
}

bool manifest_transfer_authorized(sqlite3* db,const Command& c){
 if(!has_schema(db))return true;
 // A reserved identity can never be consumed as a different command/action.
 Q reserve(db,"SELECT m.ship_id,m.origin_site,m.pilot_id,m.item_index,m.qty_kg,m.load_id,m.unload_id,COALESCE(d.site_id,'') FROM sg_v55_manifest m LEFT JOIN sg_v55_destination d ON d.manifest_id=m.manifest_id WHERE m.load_id=? OR m.unload_id=?");
 reserve.t(1,id(c.id));reserve.t(2,id(c.id));
 if(reserve.row()){
  const bool loading=reserve.str(5)==id(c.id);
  const auto target=loading?reserve.str(1):reserve.str(7);
  if(c.action!=Action::TransferStorageStack||target.empty()||
     id(c.actor)!=reserve.str(0)||id(c.target)!=target||
     id(c.issuer)!=reserve.str(2)||c.thrust_x!=reserve.number(3)||
     c.amount!=reserve.number(4)||c.thrust_y!=(loading?1:0))return false;
  if(!loading){Q loaded(db,"SELECT COUNT(*) FROM resource_ledger WHERE reason='storage_stack_transfer_kg' AND command_id=?");loaded.t(1,reserve.str(5));require(loaded.row(),"load witness check");if(loaded.number(0)!=1)return false;}
  return true;
 }
 int index=-1;
 if(c.action==Action::TransferStorageStack||c.action==Action::TransferWorkshopStack||
    c.action==Action::HostTransferConsignedStock)index=c.thrust_x;
 else if(c.action==Action::TransferStorageNodeStack)index=c.thrust_x%4;
 else if(c.action==Action::TransferWorkshopOutput)index=1;
 else if(c.action==Action::TransferWorkshopFinalOutput)index=2;
 if(index<0)return true;
 Q active(db,"SELECT COUNT(*) FROM sg_v55_manifest m WHERE m.ship_id=? AND m.item_index=? AND NOT EXISTS (SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_id AND l.reason='storage_stack_transfer_kg')");
 active.t(1,id(c.actor));active.i(2,index);require(active.row(),"active booking check");
 return active.number(0)==0;
}

void verify_manifest_registry(const std::string& path){
 Db d(path);if(!has_schema(d.db))return;
 Q all(d.db,"SELECT manifest_id FROM sg_v55_manifest ORDER BY manifest_id");
 std::int64_t seen=0;
 while(all.row()){verify_one(d.db,read(d.db,all.str(0)));++seen;}
 Q anchor(d.db,"SELECT shipment_manifest_count FROM world_meta WHERE singleton=1");
 require(anchor.row()&&anchor.number(0)==seen,"missing or duplicated anchored manifest rows");
 Q duplicate(d.db,"SELECT COUNT(*) FROM (SELECT load_id AS cid FROM sg_v55_manifest UNION ALL SELECT unload_id AS cid FROM sg_v55_manifest) GROUP BY cid HAVING COUNT(*)>1");
 require(!duplicate.row(),"colliding shipment world command identity");
 Q active(d.db,"SELECT COUNT(*) FROM sg_v55_manifest m WHERE NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_id AND l.reason='storage_stack_transfer_kg') GROUP BY ship_id,item_index HAVING COUNT(*)>1");
 require(!active.row(),"multiple active custody claims on same cargo stack");
 // The world core verifies all applied resource-ledger effects independently.
}
ShipmentRegistry::ShipmentRegistry(PersistentWorld& world,std::string path):world_(world),path_(std::move(path)){
 Db d(path_);
 run(d.db,"CREATE TABLE IF NOT EXISTS sg_v55_manifest(manifest_id TEXT PRIMARY KEY,ship_id TEXT NOT NULL,origin_site TEXT NOT NULL,pilot_id TEXT NOT NULL,dest_system INTEGER NOT NULL CHECK(dest_system>0),item_index INTEGER NOT NULL CHECK(item_index IN (1,2)),qty_kg INTEGER NOT NULL CHECK(qty_kg>0 AND qty_kg<=1000),factory_return_id TEXT NOT NULL UNIQUE,load_id TEXT NOT NULL UNIQUE,unload_id TEXT NOT NULL UNIQUE,created_generation INTEGER NOT NULL CHECK(created_generation>0),CHECK(load_id<>unload_id))");
 run(d.db,"CREATE TABLE IF NOT EXISTS sg_v55_destination(manifest_id TEXT PRIMARY KEY REFERENCES sg_v55_manifest(manifest_id),site_id TEXT NOT NULL,world_generation INTEGER NOT NULL CHECK(world_generation>0))");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_unique_command BEFORE INSERT ON sg_v55_manifest WHEN EXISTS(SELECT 1 FROM sg_v55_manifest WHERE load_id IN (NEW.load_id,NEW.unload_id) OR unload_id IN (NEW.load_id,NEW.unload_id)) BEGIN SELECT RAISE(ABORT,'reused shipment command');END");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_one_active BEFORE INSERT ON sg_v55_manifest WHEN EXISTS(SELECT 1 FROM sg_v55_manifest m WHERE m.ship_id=NEW.ship_id AND m.item_index=NEW.item_index AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_id AND l.reason='storage_stack_transfer_kg')) BEGIN SELECT RAISE(ABORT,'cargo stack already reserved');END");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_manifest_immutable_update BEFORE UPDATE ON sg_v55_manifest BEGIN SELECT RAISE(ABORT,'immutable manifest');END");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_manifest_immutable_delete BEFORE DELETE ON sg_v55_manifest BEGIN SELECT RAISE(ABORT,'immutable manifest');END");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_destination_immutable_update BEFORE UPDATE ON sg_v55_destination BEGIN SELECT RAISE(ABORT,'immutable destination');END");
 run(d.db,"CREATE TRIGGER IF NOT EXISTS sg_v55_destination_immutable_delete BEFORE DELETE ON sg_v55_destination BEGIN SELECT RAISE(ABORT,'immutable destination');END");
 bool anchor_exists=false;
 {Q cols(d.db,"PRAGMA table_info(world_meta)");
  while(cols.row())if(cols.str(1)=="shipment_manifest_count")anchor_exists=true;}
 if(!anchor_exists)run(d.db,"ALTER TABLE world_meta ADD COLUMN shipment_manifest_count INTEGER NOT NULL DEFAULT 0 CHECK(shipment_manifest_count>=0)");
 verify();
}
void ShipmentRegistry::book(const Booking& b){
 require(!b.id.empty()&&b.id.size()<=100&&b.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos,"invalid manifest identifier");
 require((b.item_index==1||b.item_index==2)&&b.quantity_kg>0&&b.quantity_kg<=1000&&b.load_command&&b.unload_command&&b.factory_return_command&&b.ship&&b.origin_site&&b.pilot&&b.destination_system,"invalid booking");
 const auto* ship=world_.world().find(b.ship);const auto* source=world_.world().find(b.origin_site);
 const auto* bin=world_.world().storage(b.origin_site);
 require(ship&&source&&bin&&ship->owner_id==b.pilot&&source->owner_id==b.pilot&&bin->source_ship==b.ship&&
 ship->system_id==source->system_id&&ship->system_id!=b.destination_system,"unowned or unconstructed origin");
 require(bin->goods_kg.count(item(b.item_index))&&bin->goods_kg.at(item(b.item_index))>=b.quantity_kg&&
 world_.world().ship_inventory_snapshot(b.ship).amount(std::string("good/")+item(b.item_index))==0,"shipment needs fresh identified cargo and physical stock");
 Db d(path_);run(d.db,"BEGIN IMMEDIATE");
 Q generation(d.db,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
 require(generation.row()&&generation.number(0)==static_cast<std::int64_t>(world_.generation()),
         "stale world generation at booking");
 Q reused(d.db,"SELECT COUNT(*) FROM receipts WHERE id IN (?,?)");
 reused.t(1,id(b.load_command));reused.t(2,id(b.unload_command));
 require(reused.row()&&reused.number(0)==0,"reserved transfer command was already used");
 Q active(d.db,"SELECT COUNT(*) FROM sg_v55_manifest m WHERE ship_id=? AND item_index=? AND NOT EXISTS (SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_id AND l.reason='storage_stack_transfer_kg')");
 active.t(1,id(b.ship));active.i(2,b.item_index);require(active.row()&&active.number(0)==0,"ship/item already consigned");
 Q factory(d.db,"SELECT COUNT(*) FROM sg_v53_world_return WHERE command_id=? AND site=? AND source_ship=? AND material=? AND stock_kg>=?");
 factory.t(1,id(b.factory_return_command));factory.i(2,static_cast<std::int64_t>(b.origin_site));
 factory.i(3,static_cast<std::int64_t>(b.ship));factory.t(4,b.item_index==1?"plates":"frames");factory.i(5,b.quantity_kg);
 require(factory.row()&&factory.number(0)==1,"no authoritative factory return backing lot");
 Q ins(d.db,"INSERT INTO sg_v55_manifest(manifest_id,ship_id,origin_site,pilot_id,dest_system,item_index,qty_kg,factory_return_id,load_id,unload_id,created_generation) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
 ins.t(1,b.id);ins.t(2,id(b.ship));ins.t(3,id(b.origin_site));ins.t(4,id(b.pilot));ins.i(5,b.destination_system);
 ins.i(6,b.item_index);ins.i(7,b.quantity_kg);ins.t(8,id(b.factory_return_command));ins.t(9,id(b.load_command));ins.t(10,id(b.unload_command));ins.i(11,world_.generation());ins.exec();
 run(d.db,"UPDATE world_meta SET shipment_manifest_count=shipment_manifest_count+1 WHERE singleton=1");
 require(sqlite3_changes(d.db)==1,"failed to anchor manifest count to world metadata");
 run(d.db,"COMMIT");
}
void ShipmentRegistry::designate_destination(const std::string& manifest_id,EntityId site){
 Db d(path_);run(d.db,"BEGIN IMMEDIATE");
 Q generation(d.db,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
 require(generation.row()&&generation.number(0)==static_cast<std::int64_t>(world_.generation()),
         "stale world generation at destination designation");
 const auto r=read(d.db,manifest_id);require(!r.destination,"already designated");
 const auto* ship=world_.world().find(r.b.ship);const auto* dest=world_.world().find(site);
 const auto* store=world_.world().storage(site);
 require(dest&&store&&ship&&site!=r.b.origin_site&&dest->system_id==r.b.destination_system&&
 ship->system_id==dest->system_id&&ship->owner_id==r.b.pilot&&dest->owner_id==r.b.pilot&&store->source_ship==r.b.ship,"destination not legitimately constructed or independently located");
 require(witness(d.db,r,false).generation!=0,"cannot assign destination before physical loading");
 Q insert(d.db,"INSERT INTO sg_v55_destination(manifest_id,site_id,world_generation) VALUES(?,?,?)");
 insert.t(1,manifest_id);insert.t(2,id(site));insert.i(3,world_.generation());insert.exec();
 run(d.db,"COMMIT");
}
static void move(PersistentWorld& w,const std::string& path,const std::string& manifest,bool unload,FailPoint fail){
 Db d(path);const auto r=read(d.db,manifest);verify_one(d.db,r);
 if(unload)require(r.destination!=0,"destination not designated");
 const auto prior=witness(d.db,r,unload);if(prior.generation)return;
 if(unload)require(witness(d.db,r,false).generation,"cannot unload before load");
 // Fail-closed preflight avoids consuming an immutable command identity for
 // ordinary full-bin, wrong-location or insufficient-stock errors. The actual
 // world command repeats every check under its authoritative transaction.
 const auto* ship=w.world().find(r.b.ship);
 const auto site_id=unload?r.destination:r.b.origin_site;
 const auto* site=w.world().find(site_id);
 const auto* store=w.world().storage(site_id);
 require(ship&&site&&store&&ship->owner_id==r.b.pilot&&
         site->owner_id==r.b.pilot&&store->source_ship==r.b.ship&&
         ship->system_id==site->system_id&&
         ship->jump_state==ShipJumpState::Ready&&
         (ship->dock_state==DockState::InSpace||ship->dock_station==site_id),
         "unauthorized or physically unavailable warehouse transfer");
 const auto dx=ship->position.x-site->position.x;
 const auto dy=ship->position.y-site->position.y;
 require(dx>=-Simulation::kDockingRangeMm&&dx<=Simulation::kDockingRangeMm&&
         dy>=-Simulation::kDockingRangeMm&&dy<=Simulation::kDockingRangeMm&&
         dx*dx+dy*dy<=Simulation::kDockingRangeMm*Simulation::kDockingRangeMm,
         "ship not physically near loading point");
 auto cargo=w.world().ship_inventory_snapshot(r.b.ship);
 auto bin=w.world().storage_inventory_snapshot(site_id);
 const std::string code=std::string("good/")+item(r.b.item_index);
 const auto status=unload?cargo.transfer_to(bin,code,r.b.quantity_kg):
                          bin.transfer_to(cargo,code,r.b.quantity_kg);
 require(status==inventory::TransferStatus::Applied,
         "preflight refused unavailable cargo or full physical storage");
 Command c{};c.id=unload?r.b.unload_command:r.b.load_command;c.sequence=c.id;
 c.execute_tick=w.world().tick()+1;c.issuer=r.b.pilot;c.actor=r.b.ship;
 c.target=unload?r.destination:r.b.origin_site;c.action=Action::TransferStorageStack;
 c.amount=r.b.quantity_kg;c.thrust_x=r.b.item_index;c.thrust_y=unload?0:1;
 if(!w.submit(c)){
  // An interrupted step leaves the already accepted command queued. The
  // recovery path must NOT advance an unrelated simulation tick if admission
  // failed for any other reason.
  Q pending(d.db,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");
  pending.t(1,id(c.id));require(pending.row()&&pending.number(0)==1,
          "shipment command neither accepted nor pending for retry");
 }
 w.step(fail);
 Db after(path);const auto present=witness(after.db,r,unload);
 require(present.generation,"world refused the consigned physical transfer");
 verify_one(after.db,r);
}
void ShipmentRegistry::load(const std::string& m,FailPoint f){move(world_,path_,m,false,f);}
void ShipmentRegistry::unload(const std::string& m,FailPoint f){move(world_,path_,m,true,f);}
void ShipmentRegistry::verify() const{verify_manifest_registry(path_);}
}
