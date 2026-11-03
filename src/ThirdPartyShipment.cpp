#include "galaxy/ShipmentManifest.hpp"
#include "galaxy/RegionalShipmentReconciliation.hpp"
#include "galaxy/RegionalProductionNetwork.hpp"
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace galaxy::shipping {
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("third-party shipping: ")+why);}
void sql(sqlite3* db,const char* q){char* e=nullptr;int rc=sqlite3_exec(db,q,nullptr,nullptr,&e);
 if(rc!=SQLITE_OK){std::string s=e?e:"SQL error";sqlite3_free(e);throw std::runtime_error("third-party shipping SQL: "+s);}}
struct DB{sqlite3* d=nullptr;explicit DB(const std::string& path){check(sqlite3_open_v2(path.c_str(),&d,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"open database");sqlite3_busy_timeout(d,5000);sql(d,"PRAGMA foreign_keys=ON");}~DB(){sqlite3_close(d);}DB(const DB&)=delete;DB& operator=(const DB&)=delete;};
struct Q{sqlite3_stmt* q=nullptr;Q(sqlite3* db,const char* stmt){if(sqlite3_prepare_v2(db,stmt,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error(std::string("third-party shipping: prepare: ")+sqlite3_errmsg(db)+" SQL="+stmt);}~Q(){sqlite3_finalize(q);}void n(int i,std::int64_t v){check(sqlite3_bind_int64(q,i,v)==SQLITE_OK,"number parameter");}void s(int i,const std::string& v){check(sqlite3_bind_text(q,i,v.c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK,"string parameter");}bool row(){int r=sqlite3_step(q);check(r==SQLITE_ROW||r==SQLITE_DONE,"read query");return r==SQLITE_ROW;}void done(){check(sqlite3_step(q)==SQLITE_DONE,"write query");}std::int64_t num(int i){return sqlite3_column_int64(q,i);}std::string str(int i){auto* t=sqlite3_column_text(q,i);return t?reinterpret_cast<const char*>(t):"";}};
std::string str(std::uint64_t n){return std::to_string(n);}
std::uint64_t uid(const std::string& v){check(!v.empty()&&v[0]!='-'&&v.find_first_not_of("0123456789")==std::string::npos,"invalid identity");auto n=std::stoull(v);check(n>0,"zero identity");return n;}
const char* mat(int i){return i==1?"aluminum_panels":i==2?"aluminum_frames":"";}
bool has_table(sqlite3* db,const char* name){Q q(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?");q.s(1,name);check(q.row(),"table check");return q.num(0)!=0;}
bool schema(sqlite3* db){Q q(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='sg_v56_contract'");check(q.row(),"schema query");bool exists=q.num(0)!=0;
 if(!exists){Q columns(db,"PRAGMA table_info(world_meta)");bool anchor=false;while(columns.row())if(columns.str(1)=="third_party_contract_count")anchor=true;
 if(anchor){Q count(db,"SELECT third_party_contract_count FROM world_meta WHERE singleton=1");check(count.row()&&count.num(0)==0,"missing shipping contracts with positive world anchor");}}
 return exists;}
struct Record {ThirdPartyBooking b;std::uint64_t generation=0;};
Record record(sqlite3* db,const std::string& id){Q q(db,"SELECT contract_id,source_ship,carrier_ship,receiving_ship,origin_site,destination_site,source_owner,carrier_owner,receiving_owner,dest_system,item_index,quantity_kg,return_command,load_command,unload_command,generation FROM sg_v56_contract WHERE contract_id=?");q.s(1,id);check(q.row(),"missing shipment contract");Record r;r.b.id=q.str(0);r.b.source_ship=uid(q.str(1));r.b.carrier_ship=uid(q.str(2));r.b.receiving_ship=uid(q.str(3));r.b.origin_site=uid(q.str(4));r.b.destination_site=uid(q.str(5));r.b.source_owner=uid(q.str(6));r.b.carrier_owner=uid(q.str(7));r.b.receiving_owner=uid(q.str(8));r.b.destination_system=static_cast<std::uint32_t>(q.num(9));r.b.item_index=static_cast<int>(q.num(10));r.b.quantity_kg=q.num(11);r.b.factory_return_command=uid(q.str(12));r.b.load_command=uid(q.str(13));r.b.unload_command=uid(q.str(14));r.generation=static_cast<std::uint64_t>(q.num(15));return r;}
struct Proof{std::uint64_t generation=0,tick=0;};
Proof transfer(sqlite3* db,const ThirdPartyBooking& b,bool unload){const auto command=unload?b.unload_command:b.load_command;
 const auto from=unload?"goods:"+str(b.carrier_ship)+":"+mat(b.item_index):"storage:"+str(b.origin_site)+":"+mat(b.item_index);
 const auto to=unload?"storage:"+str(b.destination_site)+":"+mat(b.item_index):"goods:"+str(b.carrier_ship)+":"+mat(b.item_index);
 Q q(db,"SELECT l.generation,l.tick,l.amount,l.source_id,l.destination_id,h.action,h.outcome,h.actor_id,h.target_id,h.transferred FROM resource_ledger l JOIN historical_events h ON h.command_id=l.command_id WHERE l.command_id=? AND l.reason='storage_stack_transfer_kg'");q.s(1,str(command));if(!q.row())return {};
 check(q.num(0)>0&&q.num(1)>0&&q.num(2)==b.quantity_kg&&q.str(3)==from&&q.str(4)==to&&q.num(5)==static_cast<int>(Action::HostTransferConsignedStock)&&q.num(6)==static_cast<int>(Outcome::Applied)&&q.str(7)==str(b.carrier_ship)&&q.str(8)==str(unload?b.destination_site:b.origin_site)&&q.num(9)==b.quantity_kg,"physical custody receipt mismatch");const auto gen=static_cast<std::uint64_t>(q.num(0)),tick=static_cast<std::uint64_t>(q.num(1));check(!q.row(),"duplicated physical receipt");return {gen,tick};}
// v57: a secondary authorized receipt to the original, independently owned
// constructed site. The ledger remains the sole source of custody truth.
bool v57_policy(sqlite3* db,const std::string& id){
 if(!has_table(db,"sg_v57_policy"))return false;
 Q q(db,"SELECT COUNT(*) FROM sg_v57_policy WHERE contract_id=?");q.s(1,id);check(q.row(),"v57 policy lookup");return q.num(0)==1;
}
std::uint64_t return_command(sqlite3* db,const std::string& id){
 if(!has_table(db,"sg_v57_return_plan"))return 0;
 Q q(db,"SELECT return_command FROM sg_v57_return_plan WHERE contract_id=?");q.s(1,id);
 return q.row()?uid(q.str(0)):0;
}
Proof returned(sqlite3* db,const ThirdPartyBooking& b){
 const auto cmd=return_command(db,b.id);if(!cmd)return {};
 Q q(db,"SELECT l.generation,l.tick,l.amount,l.source_id,l.destination_id,h.action,h.outcome,h.actor_id,h.target_id,h.transferred FROM resource_ledger l JOIN historical_events h ON h.command_id=l.command_id WHERE l.command_id=? AND l.reason='storage_stack_transfer_kg'");
 q.s(1,str(cmd));if(!q.row())return {};
 check(q.num(0)>0&&q.num(1)>0&&q.num(2)==b.quantity_kg&&
       q.str(3)=="goods:"+str(b.carrier_ship)+":"+mat(b.item_index)&&
       q.str(4)=="storage:"+str(b.origin_site)+":"+mat(b.item_index)&&
       q.num(5)==static_cast<int>(Action::HostTransferConsignedStock)&&
       q.num(6)==static_cast<int>(Outcome::Applied)&&
       q.str(7)==str(b.carrier_ship)&&q.str(8)==str(b.origin_site)&&
       q.num(9)==b.quantity_kg,"returned shipment world receipt mismatch");
 const Proof p{static_cast<std::uint64_t>(q.num(0)),static_cast<std::uint64_t>(q.num(1))};
 check(!q.row(),"duplicated return receipt");return p;
}

struct Alternate {EntityId ship=0,site=0; PlayerId owner=0; std::uint32_t system=0;
 std::uint64_t command=0,generation=0,tick=0; std::string proposer;};
Alternate alternate(sqlite3* db,const std::string& id){
 Alternate a; if(!has_table(db,"sg_v58_alternate"))return a;
 Q q(db,"SELECT proposer,receiver_ship,receiver_site,receiver_owner,receiver_system,unload_command,generation,world_tick FROM sg_v58_alternate WHERE contract_id=?");q.s(1,id);
 if(q.row()){a.proposer=q.str(0);a.ship=uid(q.str(1));a.site=uid(q.str(2));a.owner=uid(q.str(3));a.system=static_cast<std::uint32_t>(q.num(4));a.command=uid(q.str(5));a.generation=static_cast<std::uint64_t>(q.num(6));a.tick=static_cast<std::uint64_t>(q.num(7));}
 return a;
}
Proof alternate_receipt(sqlite3* db,const ThirdPartyBooking& b,const Alternate& a){
 if(!a.command)return {};
 Q q(db,"SELECT l.generation,l.tick,l.amount,l.source_id,l.destination_id,h.action,h.outcome,h.actor_id,h.target_id,h.transferred FROM resource_ledger l JOIN historical_events h ON h.command_id=l.command_id WHERE l.command_id=? AND l.reason='storage_stack_transfer_kg'");q.s(1,str(a.command));
 if(!q.row())return {};
 check(q.num(0)>0&&q.num(1)>0&&q.num(2)==b.quantity_kg&&
   q.str(3)=="goods:"+str(b.carrier_ship)+":"+mat(b.item_index)&&
   q.str(4)=="storage:"+str(a.site)+":"+mat(b.item_index)&&
   q.num(5)==static_cast<int>(Action::HostTransferConsignedStock)&&
   q.num(6)==static_cast<int>(Outcome::Applied)&&
   q.str(7)==str(b.carrier_ship)&&q.str(8)==str(a.site)&&q.num(9)==b.quantity_kg,
   "v58 redirected delivery world receipt mismatch");
 const Proof p{static_cast<std::uint64_t>(q.num(0)),static_cast<std::uint64_t>(q.num(1))};
 check(!q.row(),"duplicated redirected world receipt");return p;
}
void verify_one(sqlite3* db,const Record& r){const auto& b=r.b;
 check(b.id.size()>0&&b.id.size()<=100&&b.quantity_kg>0&&b.quantity_kg<=1000&&b.destination_system>0&&b.destination_system<=1000000&&(b.item_index==1||b.item_index==2)&&b.source_owner!=b.carrier_owner&&b.carrier_owner!=b.receiving_owner&&b.source_owner!=b.receiving_owner&&b.source_ship!=b.carrier_ship&&b.carrier_ship!=b.receiving_ship&&b.source_ship!=b.receiving_ship&&b.origin_site!=b.destination_site&&b.load_command!=b.unload_command&&r.generation>0,"invalid third-party contract identity");
 unsigned approvals=0;
 if(has_table(db,"sg_v56_approval")){
   Q a(db,"SELECT role,principal,generation FROM sg_v56_approval WHERE contract_id=? ORDER BY role");a.s(1,b.id);
   while(a.row()){
      const auto role=a.num(0);const auto expected=role==1?b.source_owner:role==2?b.carrier_owner:role==3?b.receiving_owner:0;
      check(expected && a.str(1)==str(expected)&&a.num(2)>=static_cast<std::int64_t>(r.generation),"forged third-party approval identity");++approvals;
   }
 }else{check(!has_table(db,"sg_v56_contract"),"third-party approvals table was deleted");}
 check(approvals<=3,"duplicate approvals");
 bool cancelled=false;
 if(has_table(db,"sg_v56_cancel")){
    Q cancel(db,"SELECT principal,generation FROM sg_v56_cancel WHERE contract_id=?");cancel.s(1,b.id);
    if(cancel.row()){cancelled=true;auto principal=uid(cancel.str(0));check((principal==b.source_owner||principal==b.carrier_owner||principal==b.receiving_owner)&&cancel.num(1)>=static_cast<std::int64_t>(r.generation),"invalid cancellation witness");}
 }
 Q source(db,"SELECT site,source_ship,material,stock_kg,world_generation FROM sg_v53_world_return WHERE command_id=?");source.s(1,str(b.factory_return_command));check(source.row()&&source.num(0)==static_cast<std::int64_t>(b.origin_site)&&source.num(1)==static_cast<std::int64_t>(b.source_ship)&&source.str(2)==(b.item_index==1?"plates":"frames")&&source.num(3)>=b.quantity_kg&&source.num(4)>0&&source.num(4)<static_cast<std::int64_t>(r.generation),"no physical factory return provenance");
 for(const auto& [site,ship]:std::vector<std::pair<EntityId,EntityId>>{{b.origin_site,b.source_ship},{b.destination_site,b.receiving_ship}}){
 Q built(db,"SELECT generation FROM historical_events WHERE target_id=? AND actor_id=? AND action=? AND outcome=?");built.s(1,str(site));built.s(2,str(ship));built.n(3,static_cast<int>(Action::ConstructStorage));built.n(4,static_cast<int>(Outcome::Applied));check(built.row()&&built.num(0)>0&&built.num(0)<=static_cast<std::int64_t>(r.generation),"warehouse not constructed before booking");}
 const auto l=transfer(db,b,false),u=transfer(db,b,true),ret=returned(db,b);
 const auto alt=alternate(db,b.id);const auto redirected=alternate_receipt(db,b,alt);
 check(!l.generation || (approvals==3 && !cancelled),"unapproved or cancelled goods loading");
 check(!cancelled||(!l.generation&&!u.generation&&!ret.generation),"revocation after physical handoff");
 check(!l.generation || r.generation<l.generation,"loading predates contract");
 check(!u.generation || (l.generation&&l.generation<u.generation&&l.tick<u.tick),"delivery precedes loading");
 check(!ret.generation || (l.generation&&l.generation<ret.generation&&l.tick<ret.tick&&!u.generation),"return precedes loading or double delivery");
 check(!redirected.generation || (l.generation&&l.generation<redirected.generation&&l.tick<redirected.tick&&!ret.generation&&!u.generation),"redirected receipt has no unique loaded cargo");
 check(!alt.command || (l.generation && !ret.generation && !u.generation && alt.generation>=l.generation &&
    alt.site!=b.destination_site && alt.site!=b.origin_site && alt.owner!=b.carrier_owner &&
    alt.owner!=b.source_owner && alt.owner!=b.receiving_owner && alt.system>0 &&
    alt.tick>=l.tick && (!redirected.generation || (alt.tick<redirected.tick && alt.generation<redirected.generation)) &&
    (alt.proposer==str(b.source_owner)||alt.proposer==str(b.carrier_owner)||alt.proposer==str(b.receiving_owner))),
   "v58 destination amendment invalid or conflicting");
 if(alt.command){
   check(has_table(db,"sg_v58_alternate_approval")&&has_table(db,"sg_v58_vote")&&has_table(db,"sg_v58_settlement"),"v58 destination tables missing");
   Q built(db,"SELECT generation FROM historical_events WHERE target_id=? AND actor_id=? AND action=? AND outcome=?");
   built.s(1,str(alt.site));built.s(2,str(alt.ship));built.n(3,static_cast<int>(Action::ConstructStorage));built.n(4,static_cast<int>(Outcome::Applied));
   check(built.row()&&built.num(0)>0&&built.num(0)<=static_cast<std::int64_t>(alt.generation),"alternate receiver warehouse not built");
   Q alt_approvals(db,"SELECT role,principal,generation FROM sg_v58_alternate_approval WHERE contract_id=? ORDER BY role");alt_approvals.s(1,b.id);
   unsigned approvals58=0;while(alt_approvals.row()){
      const auto role=alt_approvals.num(0);
      const auto principal=role==1?b.source_owner:role==2?b.carrier_owner:role==3?b.receiving_owner:role==4?alt.owner:0;
      check(principal&&alt_approvals.str(1)==str(principal)&&alt_approvals.num(2)>=static_cast<std::int64_t>(alt.generation),"altered alternate-route approval");++approvals58;
   }
   check(approvals58<=4 && (!redirected.generation || approvals58==4),"redirected cargo bypassed four-party consent");
   if(redirected.generation){
      Q jump(db,"SELECT COUNT(*) FROM jump_ledger WHERE ship_id=? AND source_system<>dest_system AND dest_system=? AND completed_tick IS NOT NULL AND accepted_tick>? AND completed_tick<?");
      jump.s(1,str(b.carrier_ship));jump.n(2,alt.system);jump.n(3,static_cast<std::int64_t>(l.tick));jump.n(4,static_cast<std::int64_t>(redirected.tick));
      check(jump.row()&&jump.num(0)>0,"redirected goods lack completed physical carrier jump");
   }
   Q votes58(db,"SELECT role,principal,witness_command,witness_generation,witness_tick FROM sg_v58_vote WHERE contract_id=? ORDER BY role");votes58.s(1,b.id);
   unsigned votes_count=0;while(votes58.row()){
      const auto role=votes58.num(0);const auto principal=role==2?b.carrier_owner:role==4?alt.owner:0;
      check(principal&&votes58.str(1)==str(principal)&&redirected.generation&&
        votes58.str(2)==str(alt.command)&&votes58.num(3)==static_cast<std::int64_t>(redirected.generation)&&
        votes58.num(4)==static_cast<std::int64_t>(redirected.tick),"v58 settlement vote without world receipt");++votes_count;
   }
   Q closure(db,"SELECT receipt_command,receipt_generation,receipt_tick,site,quantity_kg FROM sg_v58_settlement WHERE contract_id=?");closure.s(1,b.id);
   const bool settled58=closure.row();check(settled58==(votes_count==2),"v58 settlement vote/receipt mismatch");
   if(settled58)check(closure.str(0)==str(alt.command)&&closure.num(1)==static_cast<std::int64_t>(redirected.generation)&&
      closure.num(2)==static_cast<std::int64_t>(redirected.tick)&&closure.str(3)==str(alt.site)&&closure.num(4)==b.quantity_kg,"v58 receipt has forged endpoint");
 }
 if(u.generation){Q jump(db,"SELECT COUNT(*) FROM jump_ledger WHERE ship_id=? AND source_system<>dest_system AND dest_system=? AND completed_tick IS NOT NULL AND accepted_tick>? AND completed_tick<?");jump.s(1,str(b.carrier_ship));jump.n(2,b.destination_system);jump.n(3,static_cast<std::int64_t>(l.tick));jump.n(4,static_cast<std::int64_t>(u.tick));check(jump.row()&&jump.num(0)>0,"no actual inter-system carrier voyage");}
 if(l.generation){Q other(db,"SELECT command_id FROM resource_ledger WHERE reason='storage_stack_transfer_kg' AND generation>=? AND (source_id=? OR destination_id=?)");other.n(1,static_cast<std::int64_t>(l.generation));other.s(2,"goods:"+str(b.carrier_ship)+":"+mat(b.item_index));other.s(3,"goods:"+str(b.carrier_ship)+":"+mat(b.item_index));unsigned seen=0;while(other.row()){const auto id=other.str(0);check(id==str(b.load_command)||id==str(b.unload_command)||(return_command(db,b.id)&&id==str(return_command(db,b.id)))||(alt.command&&id==str(alt.command)),"mixed third-party cargo custody");++seen;}check(seen==(u.generation||ret.generation||redirected.generation?2u:1u),"missing or duplicated cargo endpoints");}
 // v57 is opt-in; older v56 contracts are readable without dispatch rows.
 if(v57_policy(db,b.id)){
  check(has_table(db,"sg_v57_dispatch")&&has_table(db,"sg_v57_return_plan")&&has_table(db,"sg_v57_return_approval")&&has_table(db,"sg_v57_vote")&&has_table(db,"sg_v57_settlement"),"missing v57 tables");
  Q dispatch(db,"SELECT principal,generation,world_tick FROM sg_v57_dispatch WHERE contract_id=?");dispatch.s(1,b.id);
  const bool dispatched=dispatch.row();
  if(dispatched)check(dispatch.str(0)==str(b.carrier_owner)&&dispatch.num(1)>=static_cast<std::int64_t>(r.generation)&&dispatch.num(2)>0,"forged dispatch identity or chronology");
  check(!l.generation || (dispatched&&dispatch.num(1)<=static_cast<std::int64_t>(l.generation)&&dispatch.num(2)<static_cast<std::int64_t>(l.tick)),"undispatched or time-reversed cargo loading");
  Q plan(db,"SELECT proposer,return_command,generation FROM sg_v57_return_plan WHERE contract_id=?");plan.s(1,b.id);
  const bool planned=plan.row();
  if(planned)check(l.generation&&!u.generation&&
     (plan.str(0)==str(b.source_owner)||plan.str(0)==str(b.carrier_owner)||plan.str(0)==str(b.receiving_owner))&&
     plan.num(2)>=static_cast<std::int64_t>(l.generation)&&
     uid(plan.str(1))!=b.load_command&&uid(plan.str(1))!=b.unload_command,"unbacked or forged return plan");
  unsigned return_approvals=0;
  Q approvals57(db,"SELECT role,principal,generation FROM sg_v57_return_approval WHERE contract_id=? ORDER BY role");approvals57.s(1,b.id);
  while(approvals57.row()){
   auto role=approvals57.num(0);
   const auto expected=role==1?b.source_owner:role==2?b.carrier_owner:role==3?b.receiving_owner:0;
   check(planned&&expected&&approvals57.str(1)==str(expected)&&approvals57.num(2)>=static_cast<std::int64_t>(plan.num(2)),"forged return consent");++return_approvals;
  }
  check(!ret.generation || (planned&&return_approvals==3),"unapproved return of physical cargo");
  if(planned)check(!u.generation&&!alt.command,"original delivery executed after return diversion request");
  Q votes(db,"SELECT role,principal,outcome,witness_command,witness_generation,witness_tick FROM sg_v57_vote WHERE contract_id=? ORDER BY role");votes.s(1,b.id);
  const auto finished=u.generation?u:ret;
  const auto outcome=u.generation?"delivered":ret.generation?"returned":"";
  const auto witness=u.generation?b.unload_command:return_command(db,b.id);
  unsigned vote_count=0;
  while(votes.row()){
    const auto role=votes.num(0);
    const auto expected=outcome==std::string("delivered")?(role==2?b.carrier_owner:role==3?b.receiving_owner:0):
          outcome==std::string("returned")?(role==1?b.source_owner:role==2?b.carrier_owner:0):0;
    check(expected&&votes.str(1)==str(expected)&&votes.str(2)==outcome&&
      votes.str(3)==str(witness)&&votes.num(4)==static_cast<std::int64_t>(finished.generation)&&
      votes.num(5)==static_cast<std::int64_t>(finished.tick),"settlement signer or receipt forged");++vote_count;
  }
  check(vote_count<=2,"duplicate settlement votes");
  Q settlement(db,"SELECT outcome,receipt_command,receipt_generation,receipt_tick,site,quantity_kg FROM sg_v57_settlement WHERE contract_id=?");settlement.s(1,b.id);
  const bool settled=settlement.row();
  check(settled==(vote_count==2),"unmatched two-party settlement");
  if(settled)check(settlement.str(0)==outcome&&settlement.str(1)==str(witness)&&
    settlement.num(2)==static_cast<std::int64_t>(finished.generation)&&
    settlement.num(3)==static_cast<std::int64_t>(finished.tick)&&
    settlement.str(4)==str(u.generation?b.destination_site:b.origin_site)&&
    settlement.num(5)==b.quantity_kg,"settlement not bound to canonical world receipt");
 }
}
void preflight(PersistentWorld& world,const ThirdPartyBooking& b,bool unload,bool returning=false){
 auto ship=world.world().find(b.carrier_ship);
 auto site=world.world().find(returning?b.origin_site:unload?b.destination_site:b.origin_site);
 auto st=world.world().storage(returning?b.origin_site:unload?b.destination_site:b.origin_site);
 check(ship&&site&&st&&ship->owner_id==b.carrier_owner&&site->owner_id==(unload&&!returning?b.receiving_owner:b.source_owner)&&site->system_id==ship->system_id&&ship->jump_state==ShipJumpState::Ready&&(ship->dock_state==DockState::InSpace||ship->dock_station==site->id),"physical ownership, local presence or flight state failed");
 auto dx=ship->position.x-site->position.x,dy=ship->position.y-site->position.y;
 if(!(dx>=-Simulation::kDockingRangeMm&&dx<=Simulation::kDockingRangeMm&&dy>=-Simulation::kDockingRangeMm&&dy<=Simulation::kDockingRangeMm&&dx*dx+dy*dy<=Simulation::kDockingRangeMm*Simulation::kDockingRangeMm))throw std::runtime_error("third-party shipping: carrier not close to physical warehouse ship=("+str(ship->position.x)+","+str(ship->position.y)+") site=("+str(site->position.x)+","+str(site->position.y)+") bound="+str(Simulation::kDockingRangeMm));
 auto cargo=world.world().ship_inventory_snapshot(b.carrier_ship);
 auto bin=world.world().storage_inventory_snapshot(site->id);
 std::string code=std::string("good/")+mat(b.item_index);
 check(((unload||returning)?cargo.transfer_to(bin,code,b.quantity_kg):bin.transfer_to(cargo,code,b.quantity_kg))==inventory::TransferStatus::Applied,"insufficient goods or full finite hold");
}
} // namespace

bool third_party_transfer_authorized(sqlite3* db,const Command& c){
 if(!schema(db))return c.action!=Action::HostTransferConsignedStock;
 Q reserved(db,"SELECT contract_id,carrier_ship,origin_site,destination_site,carrier_owner,item_index,quantity_kg,load_command,unload_command FROM sg_v56_contract WHERE load_command=? OR unload_command=?");
 reserved.s(1,str(c.id));reserved.s(2,str(c.id));
 if(reserved.row()){
  const bool loading=reserved.str(7)==str(c.id);
  if(c.action!=Action::HostTransferConsignedStock || c.issuer!=Simulation::kServerAuthority || c.actor!=uid(reserved.str(1))||c.target!=uid(reserved.str(loading?2:3))||c.amount!=reserved.num(6)||c.thrust_x!=reserved.num(5)||c.thrust_y!=(loading?1:0))return false;
  Q permission(db,"SELECT COUNT(*) FROM sg_v56_approval WHERE contract_id=?");permission.s(1,reserved.str(0));
  check(permission.row(),"authorization query");if(permission.num(0)!=3)return false;
  Q cancelled(db,"SELECT COUNT(*) FROM sg_v56_cancel WHERE contract_id=?");cancelled.s(1,reserved.str(0));
  check(cancelled.row(),"revocation query");if(cancelled.num(0)!=0)return false;
  if(v57_policy(db,reserved.str(0))){
    Q sent(db,"SELECT COUNT(*) FROM sg_v57_dispatch WHERE contract_id=?");sent.s(1,reserved.str(0));check(sent.row(),"dispatch lookup");if(sent.num(0)!=1)return false;
    Q plan(db,"SELECT COUNT(*) FROM sg_v57_return_plan WHERE contract_id=?");plan.s(1,reserved.str(0));check(plan.row(),"diversion lookup");if(!loading&&plan.num(0)!=0)return false;
    if(!loading&&has_table(db,"sg_v58_alternate")){Q amend(db,"SELECT COUNT(*) FROM sg_v58_alternate WHERE contract_id=?");amend.s(1,reserved.str(0));check(amend.row(),"alternate route lookup");if(amend.num(0))return false;}
  }
  if(!loading){Q proof(db,"SELECT COUNT(*) FROM resource_ledger WHERE command_id=? AND reason='storage_stack_transfer_kg'");proof.s(1,reserved.str(7));check(proof.row(),"missing loading precondition query");if(proof.num(0)!=1)return false;}
  return true;
 }
 if(has_table(db,"sg_v58_alternate")){
   Q amended(db,"SELECT a.contract_id,m.carrier_ship,a.receiver_site,m.item_index,m.quantity_kg,m.load_command,m.unload_command FROM sg_v58_alternate a JOIN sg_v56_contract m USING(contract_id) WHERE a.unload_command=?");amended.s(1,str(c.id));
   if(amended.row()){
      if(c.action!=Action::HostTransferConsignedStock||c.issuer!=Simulation::kServerAuthority||
         c.actor!=uid(amended.str(1))||c.target!=uid(amended.str(2))||c.thrust_x!=amended.num(3)||c.thrust_y!=0||c.amount!=amended.num(4))return false;
      Q votes(db,"SELECT COUNT(*) FROM sg_v58_alternate_approval WHERE contract_id=?");votes.s(1,amended.str(0));check(votes.row(),"alternate consent lookup");if(votes.num(0)!=4)return false;
      Q loaded(db,"SELECT COUNT(*) FROM resource_ledger WHERE command_id=? AND reason='storage_stack_transfer_kg'");loaded.s(1,amended.str(5));check(loaded.row(),"load witness query");if(loaded.num(0)!=1)return false;
      Q delivered(db,"SELECT COUNT(*) FROM resource_ledger WHERE command_id=? AND reason='storage_stack_transfer_kg'");delivered.s(1,amended.str(6));check(delivered.row(),"old-delivery query");if(delivered.num(0))return false;
      Q back(db,"SELECT COUNT(*) FROM sg_v57_return_plan WHERE contract_id=?");back.s(1,amended.str(0));check(back.row(),"return conflict query");return back.num(0)==0;
   }
 }
 if(has_table(db,"sg_v57_return_plan")){
   Q diverted(db,"SELECT p.contract_id,m.carrier_ship,m.origin_site,m.item_index,m.quantity_kg,m.load_command,m.unload_command FROM sg_v57_return_plan p JOIN sg_v56_contract m USING(contract_id) WHERE p.return_command=?");diverted.s(1,str(c.id));
   if(diverted.row()){
      if(c.action!=Action::HostTransferConsignedStock||c.issuer!=Simulation::kServerAuthority||
        c.actor!=uid(diverted.str(1))||c.target!=uid(diverted.str(2))||
        c.thrust_x!=diverted.num(3)||c.thrust_y!=0||c.amount!=diverted.num(4))return false;
      Q consent(db,"SELECT COUNT(*) FROM sg_v57_return_approval WHERE contract_id=?");consent.s(1,diverted.str(0));check(consent.row(),"return consent lookup");if(consent.num(0)!=3)return false;
      Q world(db,"SELECT command_id FROM resource_ledger WHERE command_id IN (?,?) AND reason='storage_stack_transfer_kg'");world.s(1,diverted.str(5));world.s(2,diverted.str(6));unsigned loaded=0;while(world.row()){if(world.str(0)==diverted.str(6))return false;++loaded;}
      return loaded==1;
   }
 }
 if(c.action==Action::HostTransferConsignedStock)return false;
 int item=-1;
 if(c.action==Action::TransferStorageStack||c.action==Action::TransferWorkshopStack)item=c.thrust_x;
 else if(c.action==Action::TransferStorageNodeStack)item=c.thrust_x%4;
 else if(c.action==Action::TransferWorkshopOutput)item=1;
 else if(c.action==Action::TransferWorkshopFinalOutput)item=2;
 if(item<0)return true;
 // Protect both sides of the custody claim: the reserved carrier cargo and
 // the source warehouse stock before the reserved load commits.
 Q origin(db,"SELECT COUNT(*) FROM sg_v56_contract m WHERE origin_site=? AND item_index=? AND (SELECT COUNT(*) FROM sg_v56_approval a WHERE a.contract_id=m.contract_id)=3 AND NOT EXISTS(SELECT 1 FROM sg_v56_cancel k WHERE k.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.load_command AND l.reason='storage_stack_transfer_kg')");
 origin.s(1,str(c.target));origin.n(2,item);check(origin.row(),"origin reservation query");if(origin.num(0)!=0)return false;
 const auto active_sql=std::string("SELECT COUNT(*) FROM sg_v56_contract m WHERE carrier_ship=? AND item_index=? AND (SELECT COUNT(*) FROM sg_v56_approval a WHERE a.contract_id=m.contract_id)=3 AND NOT EXISTS(SELECT 1 FROM sg_v56_cancel k WHERE k.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_command AND l.reason='storage_stack_transfer_kg')")+
  (has_table(db,"sg_v57_return_plan")?" AND NOT EXISTS(SELECT 1 FROM sg_v57_return_plan rp JOIN resource_ledger r ON r.command_id=rp.return_command AND r.reason='storage_stack_transfer_kg' WHERE rp.contract_id=m.contract_id)":"")+
  (has_table(db,"sg_v58_alternate")?" AND NOT EXISTS(SELECT 1 FROM sg_v58_alternate a JOIN resource_ledger r ON r.command_id=a.unload_command AND r.reason='storage_stack_transfer_kg' WHERE a.contract_id=m.contract_id)":"");
 Q active(db,active_sql.c_str());active.s(1,str(c.actor));active.n(2,item);check(active.row(),"active consignment query");return active.num(0)==0;
}

void verify_third_party_registry(const std::string& path){DB db(path);if(!schema(db.d))return;
 Q all(db.d,"SELECT contract_id FROM sg_v56_contract ORDER BY contract_id");std::int64_t count=0;while(all.row()){verify_one(db.d,record(db.d,all.str(0)));++count;}
 Q anchor(db.d,"SELECT third_party_contract_count FROM world_meta WHERE singleton=1");check(anchor.row()&&anchor.num(0)==count,"missing, duplicated or deleted third-party contract anchor");
 Q approver(db.d,"SELECT COUNT(*) FROM sg_v56_approval");check(approver.row(),"approval count");
 Q approval_anchor(db.d,"SELECT third_party_approval_count FROM world_meta WHERE singleton=1");check(approval_anchor.row()&&approval_anchor.num(0)==approver.num(0),"approval anchor is missing or altered");
 Q canceled(db.d,"SELECT COUNT(*) FROM sg_v56_cancel");check(canceled.row(),"revocation count");
 Q cancel_anchor(db.d,"SELECT third_party_revocation_count FROM world_meta WHERE singleton=1");check(cancel_anchor.row()&&cancel_anchor.num(0)==canceled.num(0),"revocation anchor is missing or altered");
 const auto uniques=std::string("SELECT cid FROM (SELECT load_command cid FROM sg_v56_contract UNION ALL SELECT unload_command cid FROM sg_v56_contract")+
   (has_table(db.d,"sg_v57_return_plan")?" UNION ALL SELECT return_command cid FROM sg_v57_return_plan":"")+
   (has_table(db.d,"sg_v58_alternate")?" UNION ALL SELECT unload_command cid FROM sg_v58_alternate":"")+
   ") GROUP BY cid HAVING COUNT(*)>1";
 Q doubles(db.d,uniques.c_str());check(!doubles.row(),"shipment command identity overlap");
 const auto active_sql=std::string("SELECT carrier_ship,item_index FROM sg_v56_contract m WHERE NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_command AND l.reason='storage_stack_transfer_kg')")+
   (has_table(db.d,"sg_v57_return_plan")?" AND NOT EXISTS(SELECT 1 FROM sg_v57_return_plan rp JOIN resource_ledger r ON r.command_id=rp.return_command AND r.reason='storage_stack_transfer_kg' WHERE rp.contract_id=m.contract_id)":"")+
   " AND NOT EXISTS(SELECT 1 FROM sg_v58_alternate a JOIN resource_ledger l ON l.command_id=a.unload_command AND l.reason='storage_stack_transfer_kg' WHERE a.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM sg_v58_alternate a JOIN resource_ledger z ON z.command_id=a.unload_command AND z.reason='storage_stack_transfer_kg' WHERE a.contract_id=m.contract_id) AND NOT (EXISTS(SELECT 1 FROM sg_v56_cancel k WHERE k.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.load_command AND l.reason='storage_stack_transfer_kg')) GROUP BY carrier_ship,item_index HAVING COUNT(*)>1";
 Q active(db.d,active_sql.c_str());check(!active.row(),"double active consignment");
 for(const auto& [table,column]:std::vector<std::pair<std::string,std::string>>{
   {"sg_v57_policy","v57_policy_count"},{"sg_v57_dispatch","v57_dispatch_count"},
   {"sg_v57_return_plan","v57_return_plan_count"},{"sg_v57_return_approval","v57_return_approval_count"},
   {"sg_v57_vote","v57_vote_count"},{"sg_v57_settlement","v57_settlement_count"},
   {"sg_v58_alternate","v58_alternate_count"},{"sg_v58_alternate_approval","v58_alternate_approval_count"},
   {"sg_v58_vote","v58_vote_count"},{"sg_v58_settlement","v58_settlement_count"}}){
    Q has(db.d,"SELECT COUNT(*) FROM pragma_table_info('world_meta') WHERE name=?");has.s(1,column);check(has.row(),"metadata field check");
    if(!has.num(0))continue;
    Q meta(db.d,("SELECT "+column+" FROM world_meta WHERE singleton=1").c_str());check(meta.row(),"v57 world meta missing");
    std::int64_t actual=0;
    if(has_table(db.d,table.c_str())){Q amount(db.d,("SELECT COUNT(*) FROM "+table).c_str());check(amount.row(),"v57 table count");actual=amount.num(0);}
    check(meta.num(0)==actual,"missing or forged v57 immutable event anchor");
 }
 verify_regional_shipment_boundaries(path);
}
ThirdPartyRegistry::ThirdPartyRegistry(PersistentWorld& world,std::string path,std::function<bool(PlayerId)> authenticate):world_(world),path_(std::move(path)),authenticate_(std::move(authenticate)){
 check(static_cast<bool>(authenticate_),"missing session-authenticator adapter");
 DB db(path_);
 sql(db.d,"CREATE TABLE IF NOT EXISTS sg_v56_contract(contract_id TEXT PRIMARY KEY,source_ship TEXT NOT NULL,carrier_ship TEXT NOT NULL,receiving_ship TEXT NOT NULL,origin_site TEXT NOT NULL,destination_site TEXT NOT NULL,source_owner TEXT NOT NULL,carrier_owner TEXT NOT NULL,receiving_owner TEXT NOT NULL,dest_system INTEGER NOT NULL,item_index INTEGER NOT NULL CHECK(item_index IN (1,2)),quantity_kg INTEGER NOT NULL CHECK(quantity_kg BETWEEN 1 AND 1000),return_command TEXT NOT NULL UNIQUE,load_command TEXT NOT NULL UNIQUE,unload_command TEXT NOT NULL UNIQUE,generation INTEGER NOT NULL CHECK(generation>0),CHECK(load_command<>unload_command))");
 sql(db.d,"CREATE TABLE IF NOT EXISTS sg_v56_approval(contract_id TEXT NOT NULL REFERENCES sg_v56_contract(contract_id),role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 3),principal TEXT NOT NULL,generation INTEGER NOT NULL,PRIMARY KEY(contract_id,role))");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_approval_immutable_update BEFORE UPDATE ON sg_v56_approval BEGIN SELECT RAISE(ABORT,'immutable third-party approval');END");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_approval_immutable_delete BEFORE DELETE ON sg_v56_approval BEGIN SELECT RAISE(ABORT,'immutable third-party approval');END");
 sql(db.d,"CREATE TABLE IF NOT EXISTS sg_v56_cancel(contract_id TEXT PRIMARY KEY REFERENCES sg_v56_contract(contract_id),principal TEXT NOT NULL,generation INTEGER NOT NULL)");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_cancel_immutable_update BEFORE UPDATE ON sg_v56_cancel BEGIN SELECT RAISE(ABORT,'immutable third-party revocation');END");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_cancel_immutable_delete BEFORE DELETE ON sg_v56_cancel BEGIN SELECT RAISE(ABORT,'immutable third-party revocation');END");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_immutable_u BEFORE UPDATE ON sg_v56_contract BEGIN SELECT RAISE(ABORT,'immutable third-party contract');END");
 sql(db.d,"CREATE TRIGGER IF NOT EXISTS sg_v56_immutable_d BEFORE DELETE ON sg_v56_contract BEGIN SELECT RAISE(ABORT,'immutable third-party contract');END");
 bool anchor=false;Q columns(db.d,"PRAGMA table_info(world_meta)");while(columns.row())if(columns.str(1)=="third_party_contract_count")anchor=true;
 if(!anchor)sql(db.d,"ALTER TABLE world_meta ADD COLUMN third_party_contract_count INTEGER NOT NULL DEFAULT 0 CHECK(third_party_contract_count>=0)");
 for(const auto* name:{"third_party_approval_count","third_party_revocation_count"}){
  bool found=false;Q checkcols(db.d,"PRAGMA table_info(world_meta)");while(checkcols.row())if(checkcols.str(1)==name)found=true;
  if(!found){const auto ddl=std::string("ALTER TABLE world_meta ADD COLUMN ")+name+" INTEGER NOT NULL DEFAULT 0 CHECK("+name+">=0)";sql(db.d,ddl.c_str());}
 }
 // v57: all auxiliary mutations are immutable and anchored to world_meta.
 for(const char* ddl:{
  "CREATE TABLE IF NOT EXISTS sg_v57_policy(contract_id TEXT PRIMARY KEY REFERENCES sg_v56_contract(contract_id),generation INTEGER NOT NULL)",
  "CREATE TABLE IF NOT EXISTS sg_v57_dispatch(contract_id TEXT PRIMARY KEY REFERENCES sg_v57_policy(contract_id),dispatch_id TEXT NOT NULL UNIQUE,principal TEXT NOT NULL,generation INTEGER NOT NULL,world_tick INTEGER NOT NULL)",
  "CREATE TABLE IF NOT EXISTS sg_v57_return_plan(contract_id TEXT PRIMARY KEY REFERENCES sg_v57_policy(contract_id),proposer TEXT NOT NULL,return_command TEXT NOT NULL UNIQUE,generation INTEGER NOT NULL)",
  "CREATE TABLE IF NOT EXISTS sg_v57_return_approval(contract_id TEXT NOT NULL REFERENCES sg_v57_return_plan(contract_id),role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 3),principal TEXT NOT NULL,generation INTEGER NOT NULL,PRIMARY KEY(contract_id,role))",
  "CREATE TABLE IF NOT EXISTS sg_v57_vote(contract_id TEXT NOT NULL REFERENCES sg_v57_policy(contract_id),role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 3),principal TEXT NOT NULL,outcome TEXT NOT NULL CHECK(outcome IN ('delivered','returned')),witness_command TEXT NOT NULL,witness_generation INTEGER NOT NULL,witness_tick INTEGER NOT NULL,PRIMARY KEY(contract_id,role))",
  "CREATE TABLE IF NOT EXISTS sg_v57_settlement(contract_id TEXT PRIMARY KEY REFERENCES sg_v57_policy(contract_id),outcome TEXT NOT NULL CHECK(outcome IN ('delivered','returned')),receipt_command TEXT NOT NULL,receipt_generation INTEGER NOT NULL,receipt_tick INTEGER NOT NULL,site TEXT NOT NULL,quantity_kg INTEGER NOT NULL)"
 })sql(db.d,ddl);
 for(const char* table:{"sg_v57_policy","sg_v57_dispatch","sg_v57_return_plan","sg_v57_return_approval","sg_v57_vote","sg_v57_settlement"}){
  const auto trigger=std::string("CREATE TRIGGER IF NOT EXISTS ")+table+"_no_update BEFORE UPDATE ON "+table+" BEGIN SELECT RAISE(ABORT,'immutable v57 event');END";sql(db.d,trigger.c_str());
  const auto del=std::string("CREATE TRIGGER IF NOT EXISTS ")+table+"_no_delete BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,'immutable v57 event');END";sql(db.d,del.c_str());
 }
 for(const char* name:{"v57_policy_count","v57_dispatch_count","v57_return_plan_count","v57_return_approval_count","v57_vote_count","v57_settlement_count"}){
  bool exists=false;Q columns57(db.d,"PRAGMA table_info(world_meta)");while(columns57.row())if(columns57.str(1)==name)exists=true;
  if(!exists){const auto ddl=std::string("ALTER TABLE world_meta ADD COLUMN ")+name+" INTEGER NOT NULL DEFAULT 0 CHECK("+name+">=0)";sql(db.d,ddl.c_str());}
 }
 // v58: immutable alternative-destination amendment and final delivery witness.
 for(const char* ddl:{
   "CREATE TABLE IF NOT EXISTS sg_v58_alternate(contract_id TEXT PRIMARY KEY REFERENCES sg_v57_policy(contract_id),proposer TEXT NOT NULL,receiver_ship TEXT NOT NULL,receiver_site TEXT NOT NULL,receiver_owner TEXT NOT NULL,receiver_system INTEGER NOT NULL,unload_command TEXT NOT NULL UNIQUE,generation INTEGER NOT NULL,world_tick INTEGER NOT NULL)",
   "CREATE TABLE IF NOT EXISTS sg_v58_alternate_approval(contract_id TEXT NOT NULL REFERENCES sg_v58_alternate(contract_id),role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 4),principal TEXT NOT NULL,generation INTEGER NOT NULL,PRIMARY KEY(contract_id,role))",
   "CREATE TABLE IF NOT EXISTS sg_v58_vote(contract_id TEXT NOT NULL REFERENCES sg_v58_alternate(contract_id),role INTEGER NOT NULL CHECK(role IN (2,4)),principal TEXT NOT NULL,witness_command TEXT NOT NULL,witness_generation INTEGER NOT NULL,witness_tick INTEGER NOT NULL,PRIMARY KEY(contract_id,role))",
   "CREATE TABLE IF NOT EXISTS sg_v58_settlement(contract_id TEXT PRIMARY KEY REFERENCES sg_v58_alternate(contract_id),receipt_command TEXT NOT NULL,receipt_generation INTEGER NOT NULL,receipt_tick INTEGER NOT NULL,site TEXT NOT NULL,quantity_kg INTEGER NOT NULL)"
 })sql(db.d,ddl);
 for(const char* table:{"sg_v58_alternate","sg_v58_alternate_approval","sg_v58_vote","sg_v58_settlement"}){
   const auto update=std::string("CREATE TRIGGER IF NOT EXISTS ")+table+"_no_update BEFORE UPDATE ON "+table+" BEGIN SELECT RAISE(ABORT,'immutable v58 shipping event');END";sql(db.d,update.c_str());
   const auto deletion=std::string("CREATE TRIGGER IF NOT EXISTS ")+table+"_no_delete BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,'immutable v58 shipping event');END";sql(db.d,deletion.c_str());
 }
 for(const char* name:{"v58_alternate_count","v58_alternate_approval_count","v58_vote_count","v58_settlement_count"}){
   bool found=false;Q columns58(db.d,"PRAGMA table_info(world_meta)");while(columns58.row())if(columns58.str(1)==name)found=true;
   if(!found){const auto ddl=std::string("ALTER TABLE world_meta ADD COLUMN ")+name+" INTEGER NOT NULL DEFAULT 0 CHECK("+name+">=0)";sql(db.d,ddl.c_str());}
 }
 // v57 returned cargo releases the physical stack for future bookings.
 sql(db.d,"DROP TRIGGER IF EXISTS sg_v56_one_active");
 sql(db.d,"CREATE TRIGGER sg_v56_one_active BEFORE INSERT ON sg_v56_contract WHEN EXISTS(SELECT 1 FROM sg_v56_contract m WHERE m.carrier_ship=NEW.carrier_ship AND m.item_index=NEW.item_index AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.unload_command AND l.reason='storage_stack_transfer_kg') AND NOT EXISTS(SELECT 1 FROM sg_v57_return_plan rp JOIN resource_ledger r ON r.command_id=rp.return_command AND r.reason='storage_stack_transfer_kg' WHERE rp.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM sg_v58_alternate a JOIN resource_ledger z ON z.command_id=a.unload_command AND z.reason='storage_stack_transfer_kg' WHERE a.contract_id=m.contract_id) AND NOT (EXISTS(SELECT 1 FROM sg_v56_cancel k WHERE k.contract_id=m.contract_id) AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=m.load_command AND l.reason='storage_stack_transfer_kg'))) BEGIN SELECT RAISE(ABORT,'duplicate carrier cargo reservation');END");
 verify();
}
void ThirdPartyRegistry::book(const ThirdPartyBooking& b){
 check(b.source_owner&&authenticate_(b.source_owner),"unverified source-proposer principal");
 check(!b.id.empty()&&b.id.size()<=100&&b.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos&&b.factory_return_command&&b.load_command&&b.unload_command&&b.load_command!=b.unload_command&&b.item_index>=1&&b.item_index<=2&&b.quantity_kg>=1&&b.quantity_kg<=1000,"invalid consignment identity");
 auto ss=world_.world().find(b.source_ship),cs=world_.world().find(b.carrier_ship),rs=world_.world().find(b.receiving_ship);
 auto origin=world_.world().find(b.origin_site),dest=world_.world().find(b.destination_site);
 auto first=world_.world().storage(b.origin_site),last=world_.world().storage(b.destination_site);
 check(ss&&cs&&rs&&origin&&dest&&first&&last&&
 b.source_owner&&b.carrier_owner&&b.receiving_owner&&b.source_owner!=b.carrier_owner&&b.source_owner!=b.receiving_owner&&b.carrier_owner!=b.receiving_owner&&
 ss->owner_id==b.source_owner&&cs->owner_id==b.carrier_owner&&rs->owner_id==b.receiving_owner&&
 origin->owner_id==b.source_owner&&dest->owner_id==b.receiving_owner&&
 first->source_ship==b.source_ship&&last->source_ship==b.receiving_ship&&
 origin->system_id==cs->system_id&&dest->system_id==b.destination_system&&origin->system_id!=b.destination_system&&
 first->goods_kg.count(mat(b.item_index))&&first->goods_kg.at(mat(b.item_index))>=b.quantity_kg&&
 world_.world().ship_inventory_snapshot(b.carrier_ship).amount(std::string("good/")+mat(b.item_index))==0,"physical independent owners or available stock missing");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");
 Q generation(db.d,"SELECT COALESCE(MAX(generation),0) FROM snapshots");check(generation.row()&&generation.num(0)==static_cast<std::int64_t>(world_.generation()),"stale world generation");
 Q used(db.d,"SELECT COUNT(*) FROM receipts WHERE id IN (?,?)");used.s(1,str(b.load_command));used.s(2,str(b.unload_command));check(used.row()&&used.num(0)==0,"already issued command identity");
 Q factory(db.d,"SELECT COUNT(*) FROM sg_v53_world_return WHERE command_id=? AND site=? AND source_ship=? AND material=? AND stock_kg>=?");factory.s(1,str(b.factory_return_command));factory.n(2,static_cast<std::int64_t>(b.origin_site));factory.n(3,static_cast<std::int64_t>(b.source_ship));factory.s(4,b.item_index==1?"plates":"frames");factory.n(5,b.quantity_kg);check(factory.row()&&factory.num(0)==1,"factory-produced item source missing");
 Q old_schema(db.d,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='sg_v55_manifest'");
 check(old_schema.row(),"v55 schema lookup");
 if(old_schema.num(0)!=0){Q old(db.d,"SELECT COUNT(*) FROM sg_v55_manifest WHERE factory_return_id=? OR load_id IN (?,?) OR unload_id IN (?,?)");old.s(1,str(b.factory_return_command));old.s(2,str(b.load_command));old.s(3,str(b.unload_command));old.s(4,str(b.load_command));old.s(5,str(b.unload_command));check(old.row()&&old.num(0)==0,"v55 custody conflict");}
 Q duplicate(db.d,"SELECT COUNT(*) FROM sg_v56_contract WHERE return_command=? OR load_command IN (?,?) OR unload_command IN (?,?) OR (carrier_ship=? AND item_index=? AND (SELECT COUNT(*) FROM sg_v56_approval a WHERE a.contract_id=sg_v56_contract.contract_id)=3 AND NOT EXISTS(SELECT 1 FROM sg_v56_cancel k WHERE k.contract_id=sg_v56_contract.contract_id) AND NOT EXISTS(SELECT 1 FROM resource_ledger l WHERE l.command_id=sg_v56_contract.unload_command AND l.reason='storage_stack_transfer_kg') AND NOT EXISTS(SELECT 1 FROM sg_v57_return_plan rp JOIN resource_ledger r ON r.command_id=rp.return_command AND r.reason='storage_stack_transfer_kg' WHERE rp.contract_id=sg_v56_contract.contract_id))");duplicate.s(1,str(b.factory_return_command));duplicate.s(2,str(b.load_command));duplicate.s(3,str(b.unload_command));duplicate.s(4,str(b.load_command));duplicate.s(5,str(b.unload_command));duplicate.s(6,str(b.carrier_ship));duplicate.n(7,b.item_index);check(duplicate.row()&&duplicate.num(0)==0,"duplicate booking or shared active cargo");
 Q reserved_return(db.d,"SELECT COUNT(*) FROM sg_v57_return_plan WHERE return_command IN (?,?)");reserved_return.s(1,str(b.load_command));reserved_return.s(2,str(b.unload_command));check(reserved_return.row()&&reserved_return.num(0)==0,"v57 return command reservation collision");
 Q reserved58(db.d,"SELECT COUNT(*) FROM sg_v58_alternate WHERE unload_command IN (?,?)");reserved58.s(1,str(b.load_command));reserved58.s(2,str(b.unload_command));check(reserved58.row()&&reserved58.num(0)==0,"v58 alternate command collision");
 Q insert(db.d,"INSERT INTO sg_v56_contract VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");insert.s(1,b.id);insert.s(2,str(b.source_ship));insert.s(3,str(b.carrier_ship));insert.s(4,str(b.receiving_ship));insert.s(5,str(b.origin_site));insert.s(6,str(b.destination_site));insert.s(7,str(b.source_owner));insert.s(8,str(b.carrier_owner));insert.s(9,str(b.receiving_owner));insert.n(10,b.destination_system);insert.n(11,b.item_index);insert.n(12,b.quantity_kg);insert.s(13,str(b.factory_return_command));insert.s(14,str(b.load_command));insert.s(15,str(b.unload_command));insert.n(16,world_.generation());insert.done();
 if(b.require_dispatch){Q mode(db.d,"INSERT INTO sg_v57_policy(contract_id,generation) VALUES(?,?)");mode.s(1,b.id);mode.n(2,world_.generation());mode.done();
   sql(db.d,"UPDATE world_meta SET v57_policy_count=v57_policy_count+1 WHERE singleton=1");check(sqlite3_changes(db.d)==1,"missing v57 policy anchor");}
 sql(db.d,"UPDATE world_meta SET third_party_contract_count=third_party_contract_count+1 WHERE singleton=1");check(sqlite3_changes(db.d)==1,"world anchor not updated");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::approve(const std::string& id,PlayerId principal,int role){
 check(role>=1&&role<=3&&principal&&authenticate_(principal),"unverified principal approval");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");auto r=record(db.d,id);const auto& b=r.b;
 check(principal==(role==1?b.source_owner:role==2?b.carrier_owner:b.receiving_owner),"approver does not own this role");
 Q revoked(db.d,"SELECT COUNT(*) FROM sg_v56_cancel WHERE contract_id=?");revoked.s(1,id);check(revoked.row()&&revoked.num(0)==0,"revoked consignment cannot be approved");
 Q generation(db.d,"SELECT COALESCE(MAX(generation),0) FROM snapshots");check(generation.row()&&generation.num(0)==static_cast<std::int64_t>(world_.generation()),"stale world when approving");
 Q approve(db.d,"INSERT INTO sg_v56_approval(contract_id,role,principal,generation) VALUES(?,?,?,?)");approve.s(1,id);approve.n(2,role);approve.s(3,str(principal));approve.n(4,world_.generation());approve.done();
 sql(db.d,"UPDATE world_meta SET third_party_approval_count=third_party_approval_count+1 WHERE singleton=1");check(sqlite3_changes(db.d)==1,"missing approval world anchor");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::revoke(const std::string& id,PlayerId principal){
 check(principal&&authenticate_(principal),"unverified principal revocation");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");auto r=record(db.d,id);
 check(principal==r.b.source_owner||principal==r.b.carrier_owner||principal==r.b.receiving_owner,"revoker lacks shipment standing");
 check(!transfer(db.d,r.b,false).generation,"loaded consignment cannot be revoked without physical return");
 Q generation(db.d,"SELECT COALESCE(MAX(generation),0) FROM snapshots");check(generation.row()&&generation.num(0)==static_cast<std::int64_t>(world_.generation()),"stale world when revoking");
 Q cancel(db.d,"INSERT INTO sg_v56_cancel(contract_id,principal,generation) VALUES(?,?,?)");cancel.s(1,id);cancel.s(2,str(principal));cancel.n(3,world_.generation());cancel.done();
 sql(db.d,"UPDATE world_meta SET third_party_revocation_count=third_party_revocation_count+1 WHERE singleton=1");check(sqlite3_changes(db.d)==1,"missing revocation world anchor");sql(db.d,"COMMIT");
}
static void move(PersistentWorld& world,const std::string& path,const std::string& contract,bool unload,FailPoint fail){
 DB db(path);auto r=record(db.d,contract);verify_one(db.d,r);
 if(transfer(db.d,r.b,unload).generation)return;
 if(unload)check(transfer(db.d,r.b,false).generation!=0,"unloading before physical loading");
 preflight(world,r.b,unload);
 Command cmd{};cmd.id=unload?r.b.unload_command:r.b.load_command;cmd.sequence=cmd.id;cmd.execute_tick=world.world().tick()+1;cmd.issuer=Simulation::kServerAuthority;cmd.actor=r.b.carrier_ship;cmd.target=unload?r.b.destination_site:r.b.origin_site;cmd.action=Action::HostTransferConsignedStock;cmd.amount=r.b.quantity_kg;cmd.thrust_x=r.b.item_index;cmd.thrust_y=unload?0:1;
 if(!world.submit_host_consigned_transfer(cmd)){
  Q pending(db.d,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");pending.s(1,str(cmd.id));check(pending.row()&&pending.num(0)==1,"not accepted and not pending retry");
 }
 world.step(fail);
 DB after(path);check(transfer(after.d,r.b,unload).generation>0,"consignment world transfer refused");verify_one(after.d,r);
}
void ThirdPartyRegistry::require_carrier(const std::string& id,PlayerId principal) const {
 check(principal&&authenticate_(principal),"unauthenticated carrier movement");
 DB db(path_);
 const auto r=record(db.d,id);
 check(r.b.carrier_owner==principal,"caller does not own consignment carrier");
 verify_one(db.d,r);
}
void ThirdPartyRegistry::load(const std::string& id,FailPoint f){move(world_,path_,id,false,f);}
void ThirdPartyRegistry::unload(const std::string& id,FailPoint f){move(world_,path_,id,true,f);}
void ThirdPartyRegistry::verify() const {verify_third_party_registry(path_);}
namespace {
void bump(sqlite3* db,const char* field){
 const std::string query=std::string("UPDATE world_meta SET ")+field+"="+field+"+1 WHERE singleton=1";
 sql(db,query.c_str());check(sqlite3_changes(db)==1,"v57 world counter update refused");
}
void current(sqlite3* db,const PersistentWorld& world){
 Q q(db,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
 check(q.row()&&q.num(0)==static_cast<std::int64_t>(world.generation()),"stale v57 world generation");
}
void require_v57(sqlite3* db,const std::string& id){check(v57_policy(db,id),"v57 dispatch policy absent");}
unsigned role_count(sqlite3* db,const char* table,const std::string& id){
 const auto query=std::string("SELECT COUNT(*) FROM ")+table+" WHERE contract_id=?";
 Q q(db,query.c_str());q.s(1,id);check(q.row(),"v57 count query failed");return static_cast<unsigned>(q.num(0));
}
}
void ThirdPartyRegistry::dispatch(const std::string& id,PlayerId principal,const std::string& dispatch_id){
 check(principal&&authenticate_(principal),"unauthenticated dispatch principal");
 check(!dispatch_id.empty()&&dispatch_id.size()<=100&&dispatch_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos,"invalid dispatch identity");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);
 check(principal==r.b.carrier_owner,"only assigned carrier owner may dispatch");
 check(role_count(db.d,"sg_v56_approval",id)==3&&role_count(db.d,"sg_v56_cancel",id)==0,"dispatch has no three-party consent");
 check(!transfer(db.d,r.b,false).generation,"cannot dispatch already loaded shipment");
 preflight(world_,r.b,false);
 Q insert(db.d,"INSERT INTO sg_v57_dispatch(contract_id,dispatch_id,principal,generation,world_tick) VALUES(?,?,?,?,?)");
 insert.s(1,id);insert.s(2,dispatch_id);insert.s(3,str(principal));insert.n(4,world_.generation());insert.n(5,world_.world().tick());insert.done();
 bump(db.d,"v57_dispatch_count");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::request_return(const std::string& id,PlayerId principal,std::uint64_t command){
 check(principal&&authenticate_(principal),"unauthenticated return requester");
 check(command>0,"missing return world command");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);const auto& b=r.b;
 check(principal==b.source_owner||principal==b.carrier_owner||principal==b.receiving_owner,"return requester is unrelated to consignment");
 check(transfer(db.d,b,false).generation&&!transfer(db.d,b,true).generation,"return requires loaded, undelivered physical cargo");
 check(role_count(db.d,"sg_v57_dispatch",id)==1,"never dispatched cargo");
 check(!alternate(db.d,id).command,"destination already amended; cannot request origin return");
 Q pending(db.d,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");pending.s(1,str(b.unload_command));check(pending.row()&&pending.num(0)==0,"delivery already queued; cannot divert");
 Q used(db.d,"SELECT COUNT(*) FROM receipts WHERE id=?");used.s(1,str(command));check(used.row()&&used.num(0)==0,"return command previously used");
 Q conflict(db.d,"SELECT COUNT(*) FROM sg_v56_contract WHERE load_command=? OR unload_command=?");conflict.s(1,str(command));conflict.s(2,str(command));check(conflict.row()&&conflict.num(0)==0,"reserved delivery identity reused");
 if(has_table(db.d,"sg_v55_manifest")){Q clash(db.d,"SELECT COUNT(*) FROM sg_v55_manifest WHERE load_id=? OR unload_id=?");clash.s(1,str(command));clash.s(2,str(command));check(clash.row()&&clash.num(0)==0,"existing v55 shipment identity reused");}
 Q alt_collision(db.d,"SELECT COUNT(*) FROM sg_v58_alternate WHERE unload_command=?");alt_collision.s(1,str(command));check(alt_collision.row()&&alt_collision.num(0)==0,"alternate reserved this return command");
 Q plan(db.d,"INSERT INTO sg_v57_return_plan(contract_id,proposer,return_command,generation) VALUES(?,?,?,?)");plan.s(1,id);plan.s(2,str(principal));plan.s(3,str(command));plan.n(4,world_.generation());plan.done();
 bump(db.d,"v57_return_plan_count");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::approve_return(const std::string& id,PlayerId principal,int role){
 check(principal&&authenticate_(principal)&&role>=1&&role<=3,"unauthenticated or invalid return approver");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);const auto& b=r.b;
 check(principal==(role==1?b.source_owner:role==2?b.carrier_owner:b.receiving_owner),"wrong return approver role");
 check(return_command(db.d,id)&&!transfer(db.d,b,true).generation&&!returned(db.d,b).generation,"return plan not available for consent");
 Q insert(db.d,"INSERT INTO sg_v57_return_approval(contract_id,role,principal,generation) VALUES(?,?,?,?)");insert.s(1,id);insert.n(2,role);insert.s(3,str(principal));insert.n(4,world_.generation());insert.done();
 bump(db.d,"v57_return_approval_count");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::return_to_origin(const std::string& id,FailPoint fail){
 DB db(path_);const auto r=record(db.d,id);require_v57(db.d,id);verify_one(db.d,r);
 if(returned(db.d,r.b).generation)return;
 const auto command=return_command(db.d,id);
 check(command&&role_count(db.d,"sg_v57_return_approval",id)==3,"return requires three independent consent records");
 check(transfer(db.d,r.b,false).generation&&!transfer(db.d,r.b,true).generation,"cargo was never loaded or already delivered");
 preflight(world_,r.b,true,true); // physically present at the original warehouse
 Command cmd{};cmd.id=command;cmd.sequence=command;cmd.execute_tick=world_.world().tick()+1;
 cmd.issuer=Simulation::kServerAuthority;cmd.actor=r.b.carrier_ship;cmd.target=r.b.origin_site;
 cmd.action=Action::HostTransferConsignedStock;cmd.amount=r.b.quantity_kg;cmd.thrust_x=r.b.item_index;cmd.thrust_y=0;
 if(!world_.submit_host_consigned_transfer(cmd)){
  Q pending(db.d,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");pending.s(1,str(command));check(pending.row()&&pending.num(0)==1,"return world command was not admitted");
 }
 world_.step(fail);
 DB after(path_);check(returned(after.d,r.b).generation,"return has no committed physical world receipt");verify_one(after.d,r);
}
void ThirdPartyRegistry::acknowledge_settlement(const std::string& id,PlayerId principal,int role){
 check(principal&&authenticate_(principal),"unauthenticated delivery acknowledgment");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);verify_one(db.d,r);const auto& b=r.b;
 const auto delivered=transfer(db.d,b,true),ret=returned(db.d,b);
 check((bool(delivered.generation)!=bool(ret.generation)),"no unique completed world cargo transfer");
 const bool back=ret.generation!=0;
 const auto expected=back?(role==1?b.source_owner:role==2?b.carrier_owner:0):
                            (role==2?b.carrier_owner:role==3?b.receiving_owner:0);
 check(expected&&principal==expected,"settlement signer is not a physical custody endpoint");
 const auto proof=back?ret:delivered;
 const auto cmd=back?return_command(db.d,id):b.unload_command;
 const char* outcome=back?"returned":"delivered";
 Q vote(db.d,"INSERT INTO sg_v57_vote(contract_id,role,principal,outcome,witness_command,witness_generation,witness_tick) VALUES(?,?,?,?,?,?,?)");
 vote.s(1,id);vote.n(2,role);vote.s(3,str(principal));vote.s(4,outcome);vote.s(5,str(cmd));vote.n(6,proof.generation);vote.n(7,proof.tick);vote.done();
 bump(db.d,"v57_vote_count");
 if(role_count(db.d,"sg_v57_vote",id)==2){
  Q receipt(db.d,"INSERT INTO sg_v57_settlement(contract_id,outcome,receipt_command,receipt_generation,receipt_tick,site,quantity_kg) VALUES(?,?,?,?,?,?,?)");
  receipt.s(1,id);receipt.s(2,outcome);receipt.s(3,str(cmd));receipt.n(4,proof.generation);receipt.n(5,proof.tick);receipt.s(6,str(back?b.origin_site:b.destination_site));receipt.n(7,b.quantity_kg);receipt.done();
  bump(db.d,"v57_settlement_count");
 }
 sql(db.d,"COMMIT");
}

void ThirdPartyRegistry::propose_alternate(const std::string& id,PlayerId principal,
      EntityId receiving_ship,EntityId receiving_site,PlayerId receiving_owner,
      std::uint32_t system,std::uint64_t command){
 check(principal&&authenticate_(principal),"unauthenticated alternate proposer");
 check(command&&receiving_ship&&receiving_site&&receiving_owner&&system,"incomplete alternate route");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);const auto& b=r.b;
 check(principal==b.source_owner||principal==b.carrier_owner||principal==b.receiving_owner,"proposer has no custody interest");
 check(transfer(db.d,b,false).generation&&!transfer(db.d,b,true).generation&&!returned(db.d,b).generation,"shipment not in transit");
 check(!alternate(db.d,id).command&&!return_command(db.d,id),"conflicting diversion plan");
 check(receiving_owner!=b.source_owner&&receiving_owner!=b.carrier_owner&&receiving_owner!=b.receiving_owner&&
       receiving_site!=b.origin_site&&receiving_site!=b.destination_site&&receiving_ship!=b.source_ship&&
       receiving_ship!=b.carrier_ship&&receiving_ship!=b.receiving_ship,"alternate must be independent destination");
 const auto* ship=world_.world().find(receiving_ship);const auto* site=world_.world().find(receiving_site);
 const auto* store=world_.world().storage(receiving_site);
 check(ship&&site&&store&&ship->owner_id==receiving_owner&&site->owner_id==receiving_owner&&
       store->source_ship==receiving_ship&&ship->system_id==system&&site->system_id==system,
       "alternate warehouse not materially constructed and independently owned");
 Q pending(db.d,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");pending.s(1,str(b.unload_command));check(pending.row()&&pending.num(0)==0,"original unload already queued");
 Q used(db.d,"SELECT COUNT(*) FROM receipts WHERE id=?");used.s(1,str(command));check(used.row()&&used.num(0)==0,"alternate world command already used");
 Q reserved(db.d,"SELECT COUNT(*) FROM sg_v56_contract WHERE load_command=? OR unload_command=?");reserved.s(1,str(command));reserved.s(2,str(command));check(reserved.row()&&reserved.num(0)==0,"alternate world command collides with booked shipment");
 Q returns(db.d,"SELECT COUNT(*) FROM sg_v57_return_plan WHERE return_command=?");returns.s(1,str(command));check(returns.row()&&returns.num(0)==0,"alternate command collides with return");
 if(has_table(db.d,"sg_v55_manifest")){Q previous(db.d,"SELECT COUNT(*) FROM sg_v55_manifest WHERE load_id=? OR unload_id=?");previous.s(1,str(command));previous.s(2,str(command));check(previous.row()&&previous.num(0)==0,"alternate command conflicts with v55 manifest");}
 Q route(db.d,"INSERT INTO sg_v58_alternate VALUES(?,?,?,?,?,?,?,?,?)");route.s(1,id);route.s(2,str(principal));route.s(3,str(receiving_ship));route.s(4,str(receiving_site));route.s(5,str(receiving_owner));route.n(6,system);route.s(7,str(command));route.n(8,world_.generation());route.n(9,world_.world().tick());route.done();
 bump(db.d,"v58_alternate_count");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::approve_alternate(const std::string& id,PlayerId principal,int role){
 check(principal&&authenticate_(principal)&&role>=1&&role<=4,"invalid authenticated alternate approver");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);const auto a=alternate(db.d,id);
 check(a.command&&!alternate_receipt(db.d,r.b,a).generation,"amendment absent or already delivered");
 const auto expected=role==1?r.b.source_owner:role==2?r.b.carrier_owner:role==3?r.b.receiving_owner:a.owner;
 check(principal==expected,"alternate approval from wrong owner");
 Q vote(db.d,"INSERT INTO sg_v58_alternate_approval(contract_id,role,principal,generation) VALUES(?,?,?,?)");vote.s(1,id);vote.n(2,role);vote.s(3,str(principal));vote.n(4,world_.generation());vote.done();
 bump(db.d,"v58_alternate_approval_count");sql(db.d,"COMMIT");
}
void ThirdPartyRegistry::unload_alternate(const std::string& id,FailPoint fail){
 DB db(path_);const auto r=record(db.d,id);require_v57(db.d,id);verify_one(db.d,r);
 const auto a=alternate(db.d,id);check(a.command,"no independently authorized alternate route");
 if(alternate_receipt(db.d,r.b,a).generation)return;
 check(role_count(db.d,"sg_v58_alternate_approval",id)==4,"four owners have not approved route");
 check(transfer(db.d,r.b,false).generation&&!transfer(db.d,r.b,true).generation&&!returned(db.d,r.b).generation,"unavailable consigned stock");
 auto redirected=r.b;redirected.destination_site=a.site;redirected.receiving_owner=a.owner;
 preflight(world_,redirected,true);
 Command cmd{};cmd.id=a.command;cmd.sequence=a.command;cmd.execute_tick=world_.world().tick()+1;
 cmd.issuer=Simulation::kServerAuthority;cmd.actor=r.b.carrier_ship;cmd.target=a.site;
 cmd.action=Action::HostTransferConsignedStock;cmd.amount=r.b.quantity_kg;cmd.thrust_x=r.b.item_index;cmd.thrust_y=0;
 if(!world_.submit_host_consigned_transfer(cmd)){
   Q pending(db.d,"SELECT COUNT(*) FROM receipts WHERE id=? AND execution_tick IS NULL");pending.s(1,str(cmd.id));check(pending.row()&&pending.num(0)==1,"alternate world transfer not admitted");
 }
 world_.step(fail);
 DB after(path_);check(alternate_receipt(after.d,r.b,a).generation,"physical alternate receipt absent");verify_one(after.d,r);
}
void ThirdPartyRegistry::acknowledge_alternate_settlement(const std::string& id,PlayerId principal,int role){
 check(principal&&authenticate_(principal),"unauthenticated alternate receipt signer");
 DB db(path_);sql(db.d,"BEGIN IMMEDIATE");current(db.d,world_);require_v57(db.d,id);
 const auto r=record(db.d,id);verify_one(db.d,r);const auto a=alternate(db.d,id);
 const auto proof=alternate_receipt(db.d,r.b,a);
 check(a.command&&proof.generation&&role>=2&&role<=4&&(role==2||role==4),"physical redirect not received");
 const auto expected=role==2?r.b.carrier_owner:a.owner;
 check(principal==expected,"alternate signer not a physical custody endpoint");
 Q vote(db.d,"INSERT INTO sg_v58_vote(contract_id,role,principal,witness_command,witness_generation,witness_tick) VALUES(?,?,?,?,?,?)");
 vote.s(1,id);vote.n(2,role);vote.s(3,str(principal));vote.s(4,str(a.command));vote.n(5,proof.generation);vote.n(6,proof.tick);vote.done();
 bump(db.d,"v58_vote_count");
 if(role_count(db.d,"sg_v58_vote",id)==2){
   Q settle(db.d,"INSERT INTO sg_v58_settlement VALUES(?,?,?,?,?,?)");
   settle.s(1,id);settle.s(2,str(a.command));settle.n(3,proof.generation);settle.n(4,proof.tick);
   settle.s(5,str(a.site));settle.n(6,r.b.quantity_kg);settle.done();bump(db.d,"v58_settlement_count");
 }
 sql(db.d,"COMMIT");
}
ProductionShippingHost::ProductionShippingHost(PersistentWorld& w,industry::RegionalProductionNetwork& net,
   ThirdPartyRegistry& registry,std::string database):world_(w),network_(net),shipping_(registry),path_(std::move(database)){}
void ProductionShippingHost::book_factory_goods(const ThirdPartyBooking& b){
 network_.verify();
 DB db(path_);
 Q source(db.d,"SELECT COUNT(*) FROM sg_v53_world_return WHERE command_id=? AND site=? AND source_ship=? AND material=? AND stock_kg>=?");
 source.s(1,str(b.factory_return_command));source.n(2,static_cast<std::int64_t>(b.origin_site));
 source.n(3,static_cast<std::int64_t>(b.source_ship));source.s(4,b.item_index==1?"plates":"frames");source.n(5,b.quantity_kg);
 check(source.row()&&source.num(0)==1,"production-host source has no completed factory return");
 const auto* site=world_.world().storage(b.origin_site);
 check(site&&site->goods_kg.count(mat(b.item_index))&&site->goods_kg.at(mat(b.item_index))>=b.quantity_kg,"production-host physical warehouse lacks goods");
 shipping_.book(b);shipping_.verify();
}
void ProductionShippingHost::dispatch_factory_goods(const std::string& id,PlayerId carrier,const std::string& dispatch_id){
 network_.verify();shipping_.dispatch(id,carrier,dispatch_id);shipping_.verify();
}
void ProductionShippingHost::deliver_redirected_goods(const std::string& id,FailPoint fail){
 network_.verify();shipping_.unload_alternate(id,fail);shipping_.verify();network_.verify();
}
void ProductionShippingHost::verify() const {network_.verify();shipping_.verify();}

} // namespace galaxy::shipping
