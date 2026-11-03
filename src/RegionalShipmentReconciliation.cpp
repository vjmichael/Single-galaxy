#include "galaxy/RegionalShipmentReconciliation.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::shipping {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("regional shipping reconciliation: ")+why);}
void sql(sqlite3* db,const std::string& query){char* err=nullptr;
 if(sqlite3_exec(db,query.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){std::string message=err?err:"unknown";sqlite3_free(err);throw std::runtime_error("regional shipping SQL: "+message);}}
struct Query {
 sqlite3_stmt* stmt=nullptr;
 explicit Query(sqlite3* db,const std::string& q){if(sqlite3_prepare_v2(db,q.c_str(),-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error(std::string("regional shipping query: ")+sqlite3_errmsg(db)+" "+q);}
 ~Query(){sqlite3_finalize(stmt);}
 Query(const Query&)=delete;Query& operator=(const Query&)=delete;
 void s(int i,const std::string& v){require(sqlite3_bind_text(stmt,i,v.c_str(),-1,SQLITE_TRANSIENT)==SQLITE_OK,"text bind failed");}
 void n(int i,std::int64_t v){require(sqlite3_bind_int64(stmt,i,v)==SQLITE_OK,"number bind failed");}
 bool row(){int rc=sqlite3_step(stmt);require(rc==SQLITE_ROW||rc==SQLITE_DONE,"query step failed");return rc==SQLITE_ROW;}
 void done(){require(sqlite3_step(stmt)==SQLITE_DONE,"write failed");}
 std::string t(int i){auto* v=sqlite3_column_text(stmt,i);return v?reinterpret_cast<const char*>(v):"";}
 std::int64_t v(int i){return sqlite3_column_int64(stmt,i);}
};
bool table(sqlite3* db,const std::string& name){Query q(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");q.s(1,name);return q.row();}
bool column(sqlite3* db,const std::string& table_name,const std::string& field){
 // Constants are supplied only by trusted code (never client input).
 Query q(db,"PRAGMA table_info("+table_name+")");while(q.row())if(q.t(1)==field)return true;return false;
}
std::uint64_t checked(std::int64_t n){require(n>=0,"negative historical generation");return static_cast<std::uint64_t>(n);}
void mix(std::uint64_t& h,const std::string& s){
 for(unsigned char c:s){h^=c;h*=1099511628211ULL;}
 h^=0xff;h*=1099511628211ULL;
}
struct Receipt {bool present=false;std::int64_t generation=0,tick=0,amount=0;std::string source,destination,command;};
Receipt receipt(sqlite3* db,const std::string& command,std::uint64_t cutoff){
 if(command.empty())return {};
 Query q(db,"SELECT generation,tick,amount,source_id,destination_id FROM resource_ledger WHERE command_id=? AND reason='storage_stack_transfer_kg' AND generation<=?");
 q.s(1,command);q.n(2,static_cast<std::int64_t>(cutoff));
 Receipt r;if(q.row()){r.present=true;r.generation=q.v(0);r.tick=q.v(1);r.amount=q.v(2);r.source=q.t(3);r.destination=q.t(4);r.command=command;}
 return r;
}
struct Fingerprint {std::string digest;std::int64_t loaded=0,transit=0,original=0,returned=0,alternate=0;};
Fingerprint fingerprint(sqlite3* db,const std::string& scope,std::uint64_t cutoff){
 Fingerprint out;std::uint64_t h=14695981039346656037ULL;mix(h,scope);
 if(!table(db,"sg_v56_contract")){mix(h,"no-shipping-schema");}else{
  require(table(db,"sg_v57_return_plan")&&table(db,"sg_v58_alternate"),"incomplete installed shipping schema");
  Query contracts(db,"SELECT c.contract_id,c.carrier_ship,c.item_index,c.quantity_kg,c.load_command,c.unload_command,"
     "COALESCE(r.return_command,''),COALESCE(a.unload_command,'') "
     "FROM sg_v56_contract c JOIN sg_v53_world_return f ON f.command_id=c.return_command "
     "LEFT JOIN sg_v57_return_plan r ON r.contract_id=c.contract_id "
     "LEFT JOIN sg_v58_alternate a ON a.contract_id=c.contract_id WHERE f.scope=? ORDER BY c.contract_id");
  contracts.s(1,scope);
  while(contracts.row()){
   const auto id=contracts.t(0),carrier=contracts.t(1),item=contracts.t(2),qty=contracts.t(3);
   auto load=receipt(db,contracts.t(4),cutoff);
   if(!load.present)continue; // no custody was physically transferred at this world generation
   auto direct=receipt(db,contracts.t(5),cutoff);
   auto returned=receipt(db,contracts.t(6),cutoff);
   auto alternate=receipt(db,contracts.t(7),cutoff);
   auto terminal=static_cast<int>(direct.present)+static_cast<int>(returned.present)+static_cast<int>(alternate.present);
   require(terminal<=1,"one loaded consignment has more than one completed destination");
   require(load.amount==std::stoll(qty),"physical loading quantity differs from manifest");
   for(const auto* terminal_receipt:{&direct,&returned,&alternate})if(terminal_receipt->present){
     require(terminal_receipt->generation>load.generation,"cargo delivered before physical load");
     require(terminal_receipt->amount==load.amount,"delivered mass differs from loaded mass");
   }
   ++out.loaded;
   if(!terminal)++out.transit;
   else if(direct.present)++out.original;
   else if(returned.present)++out.returned;
   else ++out.alternate;
   for(const auto& word:{id,carrier,item,qty})mix(h,word);
   for(const auto* r:{&load,&direct,&returned,&alternate}){
    mix(h,r->present?"1":"0");
    if(!r->present)continue;
    for(const auto& field:{r->command,std::to_string(r->generation),std::to_string(r->tick),
                           std::to_string(r->amount),r->source,r->destination})mix(h,field);
   }
  }
 }
 std::ostringstream ss;ss<<std::hex<<std::setfill('0')<<std::setw(16)<<h;out.digest=ss.str();return out;
}
std::uint64_t latest_world(sqlite3* db){Query q(db,"SELECT COALESCE(MAX(generation),0) FROM snapshots");require(q.row(),"missing world snapshots");auto n=checked(q.v(0));require(n>0,"no committed world snapshot");return n;}
void install(sqlite3* db){
 if(!column(db,"world_meta","v59_shipment_handoff_count"))
  sql(db,"ALTER TABLE world_meta ADD COLUMN v59_shipment_handoff_count INTEGER NOT NULL DEFAULT 0 CHECK(v59_shipment_handoff_count>=0)");
 sql(db,"CREATE TABLE IF NOT EXISTS sg_v59_shipment_handoff("
  "scope TEXT NOT NULL,handoff_id TEXT NOT NULL,from_generation INTEGER NOT NULL,"
  "to_generation INTEGER NOT NULL,checkpoint_revision INTEGER NOT NULL,target_mode INTEGER NOT NULL,"
  "world_generation INTEGER NOT NULL,loaded INTEGER NOT NULL,transit INTEGER NOT NULL,"
  "original INTEGER NOT NULL,returned INTEGER NOT NULL,alternate INTEGER NOT NULL,"
  "digest TEXT NOT NULL,PRIMARY KEY(scope,handoff_id),UNIQUE(scope,to_generation))");
 for(const auto& [name,op]:std::vector<std::pair<const char*,const char*>>{{"sg_v59_no_update","UPDATE"},{"sg_v59_no_delete","DELETE"}}){
  sql(db,std::string("CREATE TRIGGER IF NOT EXISTS ")+name+" BEFORE "+op+" ON sg_v59_shipment_handoff BEGIN SELECT RAISE(ABORT,'immutable regional shipment cutover');END");
 }
}
} // namespace
void seal_regional_shipment_boundary(sqlite3* db,const std::string& scope,
 const std::string& id,std::uint64_t from_generation,std::uint64_t revision,int target_mode){
 if(!table(db,"sg_v56_contract"))return; // no shipping subsystem enrolled in this world
 require(from_generation>0&&from_generation<static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),"invalid owner generation");
 require(target_mode==0||target_mode==1,"invalid target owner");
 install(db);const auto world_gen=latest_world(db);const auto seal=fingerprint(db,scope,world_gen);
 Query insert(db,"INSERT INTO sg_v59_shipment_handoff VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
 insert.s(1,scope);insert.s(2,id);insert.n(3,static_cast<std::int64_t>(from_generation));
 insert.n(4,static_cast<std::int64_t>(from_generation+1));insert.n(5,static_cast<std::int64_t>(revision));
 insert.n(6,target_mode);insert.n(7,static_cast<std::int64_t>(world_gen));
 insert.n(8,seal.loaded);insert.n(9,seal.transit);insert.n(10,seal.original);
 insert.n(11,seal.returned);insert.n(12,seal.alternate);insert.s(13,seal.digest);insert.done();
 sql(db,"UPDATE world_meta SET v59_shipment_handoff_count=v59_shipment_handoff_count+1 WHERE singleton=1");
 require(sqlite3_changes(db)==1,"handoff count anchor update missing");
}
void verify_regional_shipment_boundaries(const std::string& path){
 sqlite3* db=nullptr;
 if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
  if(db)sqlite3_close(db);
  throw std::runtime_error("regional shipping cannot open world database");}
 try{
  const bool has_table=table(db,"sg_v59_shipment_handoff"),has_anchor=column(db,"world_meta","v59_shipment_handoff_count");
  require(has_table==has_anchor,"missing regional shipment witness schema or metadata anchor");
  if(has_table){
   Query anchor(db,"SELECT v59_shipment_handoff_count FROM world_meta WHERE singleton=1");
   require(anchor.row(),"world count anchor missing");const auto count=anchor.v(0);
   Query rows(db,"SELECT scope,handoff_id,from_generation,to_generation,checkpoint_revision,target_mode,world_generation,loaded,transit,original,returned,alternate,digest FROM sg_v59_shipment_handoff ORDER BY scope,to_generation");
   std::int64_t actual=0;std::string scope;std::int64_t last=0,mode=-1;
   std::map<std::string,std::int64_t> final_modes;
   while(rows.row()){
    const auto next_scope=rows.t(0);
    if(next_scope!=scope){scope=next_scope;last=0;mode=-1;}
    require(!rows.t(1).empty()&&rows.v(2)>0&&rows.v(3)==rows.v(2)+1&&rows.v(4)>0,"invalid stored regional handoff identity");
    require(rows.v(3)>last&&rows.v(5)>=0&&rows.v(5)<=1,"stale or nonmonotonic regional handoff");
    if(mode!=-1)require(mode!=rows.v(5),"repeated same-owner shipping handoff");
    require(checked(rows.v(6))<=latest_world(db),"historical handoff references future world generation");
    const auto calculated=fingerprint(db,scope,checked(rows.v(6)));
    require(rows.v(7)==calculated.loaded&&rows.v(8)==calculated.transit&&rows.v(9)==calculated.original&&
      rows.v(10)==calculated.returned&&rows.v(11)==calculated.alternate&&rows.t(12)==calculated.digest,
      "historical world cargo witness mismatch");
    last=rows.v(3);mode=rows.v(5);final_modes[scope]=mode;++actual;
    Query owner(db,"SELECT generation,owner FROM sg_regional_mode_authority WHERE scope=?");owner.s(1,scope);
    require(owner.row()&&owner.v(0)>=last,"missing or stale current region owner");
   }
   require(count==actual,"regional handoff record deletion or insertion detected");
   for(const auto& [name,expected_mode]:final_modes){
    Query state(db,"SELECT owner FROM sg_regional_mode_authority WHERE scope=?");state.s(1,name);
    require(state.row()&&state.v(0)==expected_mode,"current region owner differs from last committed cargo handoff");
   }
  }
 }catch(...){sqlite3_close(db);throw;}
 sqlite3_close(db);
}
} // namespace galaxy::shipping
