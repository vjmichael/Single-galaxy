#include "galaxy/ShipmentStatusView.hpp"
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <utility>
namespace galaxy::shipping {
namespace {
struct Db {
 sqlite3* db=nullptr;
 explicit Db(const std::string& file){
  if(sqlite3_open_v2(file.c_str(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
   const std::string why=db?sqlite3_errmsg(db):"sqlite open failed";
   if(db)sqlite3_close(db);
   db=nullptr;
   throw std::runtime_error("shipment view: "+why);
  }
  sqlite3_busy_timeout(db,2500);
  if(sqlite3_exec(db,"PRAGMA query_only=ON; BEGIN",nullptr,nullptr,nullptr)!=SQLITE_OK){
   sqlite3_close(db);db=nullptr;
   throw std::runtime_error("shipment view: read transaction failed");
  }
 }
 ~Db(){if(db){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);}}
 Db(const Db&)=delete;Db& operator=(const Db&)=delete;
};
struct Query {
 sqlite3_stmt* stmt=nullptr;
 explicit Query(sqlite3* db,const char* text){if(sqlite3_prepare_v2(db,text,-1,&stmt,nullptr)!=SQLITE_OK)
  throw std::runtime_error(std::string("shipment view: query prepare failed: ")+sqlite3_errmsg(db));}
 ~Query(){sqlite3_finalize(stmt);}
 void text(int index,const std::string& value){if(sqlite3_bind_text(stmt,index,value.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
  throw std::runtime_error("shipment view: bind failed");}
 bool next(){const int rc=sqlite3_step(stmt);if(rc==SQLITE_ROW)return true;
  if(rc!=SQLITE_DONE)throw std::runtime_error("shipment view: read failed");
  return false;}
 std::string str(int index)const {const auto* ptr=sqlite3_column_text(stmt,index);return ptr?reinterpret_cast<const char*>(ptr):"";}
 std::int64_t num(int index)const{return sqlite3_column_int64(stmt,index);}
};
bool table(sqlite3* db,const std::string& name){Query q(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");q.text(1,name);return q.next();}
bool witness(sqlite3* db,const std::string& tbl,const std::string& id){
 // Names chosen from fixed local constants only; never client-provided SQL.
 Query q(db,("SELECT 1 FROM "+tbl+" WHERE contract_id=?").c_str());q.text(1,id);return q.next();
}
bool movement(sqlite3* db,const std::string& command){
 if(command.empty())return false;
 Query q(db,"SELECT 1 FROM resource_ledger WHERE command_id=? AND reason='storage_stack_transfer_kg' LIMIT 1");
 q.text(1,command);return q.next();
}
std::string lookup(sqlite3* db,const char* sql,const std::string& id){
 Query q(db,sql);q.text(1,id);return q.next()?q.str(0):"";
}
std::string escape(const std::string& s){
 const char* hex="0123456789abcdef";std::string out;out.reserve(s.size()+6);
 for(unsigned char c:s){switch(c){case '"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;
 case '\r':out+="\\r";break;case '\t':out+="\\t";break;
 default:if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}}
 return out;
}
const char* truth(bool v){return v?"true":"false";}
} // namespace
ShipmentStatusView::ShipmentStatusView(std::string database):database_(std::move(database)){
 if(database_.empty())throw std::invalid_argument("shipment view requires database");
}
std::vector<ShipmentStatus> ShipmentStatusView::for_principal(PlayerId principal)const{
 if(!principal)throw std::invalid_argument("shipment view: unauthenticated principal");
 Db db(database_);
 if(!table(db.db,"sg_v56_contract"))return {};
 const bool alt=table(db.db,"sg_v58_alternate"),v57=table(db.db,"sg_v57_dispatch");
 if(!table(db.db,"sg_v56_approval")||!table(db.db,"resource_ledger"))
  throw std::runtime_error("shipment view: incomplete canonical ledger schema");
 const auto subject=std::to_string(principal);
 std::string sql="SELECT m.contract_id,m.item_index,m.quantity_kg,m.origin_site,m.destination_site,"
  "m.carrier_ship,m.source_owner,m.carrier_owner,m.receiving_owner,m.load_command,m.unload_command";
 if(alt)sql+=",COALESCE(a.receiver_owner,''),COALESCE(a.receiver_site,''),COALESCE(a.unload_command,'')";
 sql+=" FROM sg_v56_contract m";
 if(alt)sql+=" LEFT JOIN sg_v58_alternate a ON a.contract_id=m.contract_id";
 sql+=" WHERE m.source_owner=? OR m.carrier_owner=? OR m.receiving_owner=?";
 if(alt)sql+=" OR a.receiver_owner=?";
 sql+=" ORDER BY m.contract_id LIMIT 101";
 Query rows(db.db,sql.c_str());for(int i=1;i<=(alt?4:3);++i)rows.text(i,subject);
 std::vector<ShipmentStatus> result;
 while(rows.next()){
  if(result.size()>=100)throw std::runtime_error("shipment view: result limit; pagination required");
  ShipmentStatus v;v.id=rows.str(0);v.material=rows.num(1)==1?"aluminum_panels":rows.num(1)==2?"aluminum_frames":"unknown";
  v.quantity_kg=rows.num(2);v.source_site=std::stoull(rows.str(3));v.destination_site=std::stoull(rows.str(4));
  v.carrier_ship=std::stoull(rows.str(5));v.source_owner=std::stoull(rows.str(6));
  v.carrier_owner=std::stoull(rows.str(7));v.receiver_owner=std::stoull(rows.str(8));
  const auto load=rows.str(9),unload=rows.str(10);
  const std::string alternate_owner=alt?rows.str(11):"",alternate_site=alt?rows.str(12):"",alternate_command=alt?rows.str(13):"";
  v.redirected=!alternate_command.empty();
  if(v.redirected)v.destination_site=std::stoull(alternate_site);
  if(subject==rows.str(6))v.role="source";
  else if(subject==rows.str(7))v.role="carrier";
  else if(v.redirected&&subject==alternate_owner)v.role="alternate_receiver";
  else if(subject==rows.str(8))v.role="receiver";
  else throw std::runtime_error("shipment view: role filter failure");
  // Per-principal consent indicators prevent telling a player to sign an
  // already-signed approval or settlement. All IDs come from the server-side
  // role membership filter above; no browser-supplied principal is inspected.
  int role=v.role=="source"?1:v.role=="carrier"?2:v.role=="receiver"?3:4;
  {
   const bool alt_approval=v.role=="alternate_receiver";
   Query approved(db.db,alt_approval?
    "SELECT 1 FROM sg_v58_alternate_approval WHERE contract_id=? AND role=? AND principal=?":
    "SELECT 1 FROM sg_v56_approval WHERE contract_id=? AND role=? AND principal=?");
   approved.text(1,v.id);
   if(sqlite3_bind_int64(approved.stmt,2,role)!=SQLITE_OK)throw std::runtime_error("shipment view: approval bind failed");
   approved.text(3,subject);v.my_approval_recorded=approved.next();
  }
  if(v.redirected&&table(db.db,"sg_v58_vote")&&(role==2||role==4)){
   Query voted(db.db,"SELECT 1 FROM sg_v58_vote WHERE contract_id=? AND role=? AND principal=?");voted.text(1,v.id);
   if(sqlite3_bind_int64(voted.stmt,2,role)!=SQLITE_OK)throw std::runtime_error("shipment view: vote bind failed");
   voted.text(3,subject);v.my_settlement_recorded=voted.next();
  }else if(table(db.db,"sg_v57_vote")&&role<=3){
   Query voted(db.db,"SELECT 1 FROM sg_v57_vote WHERE contract_id=? AND role=? AND principal=?");voted.text(1,v.id);
   if(sqlite3_bind_int64(voted.stmt,2,role)!=SQLITE_OK)throw std::runtime_error("shipment view: vote bind failed");
   voted.text(3,subject);v.my_settlement_recorded=voted.next();
  }
  v.loaded=movement(db.db,load);
  v.delivered=movement(db.db,v.redirected?alternate_command:unload);
  const auto return_command=table(db.db,"sg_v57_return_plan")?
   lookup(db.db,"SELECT return_command FROM sg_v57_return_plan WHERE contract_id=?",v.id):"";
  v.returned=movement(db.db,return_command);
  v.revoked=table(db.db,"sg_v56_cancel")&&witness(db.db,"sg_v56_cancel",v.id);
  v.dispatched=v57&&witness(db.db,"sg_v57_dispatch",v.id);
  const bool alt_settled=v.redirected&&table(db.db,"sg_v58_settlement")&&witness(db.db,"sg_v58_settlement",v.id);
  const bool base_settled=table(db.db,"sg_v57_settlement")&&witness(db.db,"sg_v57_settlement",v.id);
  v.settled=alt_settled||base_settled;
  const bool return_plan=!return_command.empty();
  const bool alt_pending=v.redirected&&!v.delivered;
  if(v.settled)v.phase=v.returned?"returned_settled":v.redirected?"redirected_settled":"delivered_settled";
  else if(v.returned)v.phase="returned_awaiting_settlement";
  else if(v.delivered)v.phase=v.redirected?"redirected_awaiting_settlement":"delivered_awaiting_settlement";
  else if(v.loaded&&return_plan)v.phase="return_requested";
  else if(v.loaded&&alt_pending)v.phase="redirect_requested";
  else if(v.loaded)v.phase="in_transit";
  else if(v.revoked)v.phase="revoked";
  else if(v.dispatched)v.phase="dispatched_awaiting_loading";
  else {Query approvals(db.db,"SELECT COUNT(*) FROM sg_v56_approval WHERE contract_id=?");approvals.text(1,v.id);
   if(!approvals.next())throw std::runtime_error("shipment view: approvals missing");
   v.phase=approvals.num(0)==3?"approved_awaiting_dispatch":"awaiting_approval";}
  result.push_back(std::move(v));
 }
 return result;
}
std::string ShipmentStatusView::to_json(const std::vector<ShipmentStatus>& rows){
 std::string out="{\"ok\":true,\"shipments\":[";
 for(const auto& v:rows){if(out.back()!='[')out+=',';
  out+="{\"id\":\""+escape(v.id)+"\",\"material\":\""+escape(v.material)+"\",\"phase\":\""+escape(v.phase)+
   "\",\"role\":\""+escape(v.role)+"\",\"quantityKg\":"+std::to_string(v.quantity_kg)+
   ",\"originSite\":"+std::to_string(v.source_site)+",\"destinationSite\":"+std::to_string(v.destination_site)+
   ",\"carrierShip\":"+std::to_string(v.carrier_ship)+",\"sourceOwner\":"+std::to_string(v.source_owner)+
   ",\"carrierOwner\":"+std::to_string(v.carrier_owner)+",\"receiverOwner\":"+std::to_string(v.receiver_owner)+
   ",\"loaded\":"+truth(v.loaded)+",\"delivered\":"+truth(v.delivered)+",\"returned\":"+truth(v.returned)+
   ",\"settled\":"+truth(v.settled)+",\"redirected\":"+truth(v.redirected)+",\"dispatched\":"+truth(v.dispatched)+
   ",\"revoked\":"+truth(v.revoked)+",\"myApprovalRecorded\":"+truth(v.my_approval_recorded)+",\"mySettlementRecorded\":"+truth(v.my_settlement_recorded)+"}";
 }
 return out+"]}";
}
} // namespace galaxy::shipping
