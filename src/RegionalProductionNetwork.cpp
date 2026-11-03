#include "galaxy/RegionalProductionNetwork.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace galaxy::industry {
namespace {
constexpr auto sql_max=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
void execute(sqlite3* d,const char* s){char* err=nullptr;if(sqlite3_exec(d,s,nullptr,nullptr,&err)!=SQLITE_OK){const std::string why=err?err:sqlite3_errmsg(d);sqlite3_free(err);throw std::runtime_error("network SQL: "+why);}}
struct Tx {sqlite3* d;bool committed=false;explicit Tx(sqlite3* db):d(db){execute(d,"BEGIN IMMEDIATE");}~Tx(){if(!committed){try{execute(d,"ROLLBACK");}catch(...){}}}void commit(){execute(d,"COMMIT");committed=true;}};
struct Q {sqlite3* d;sqlite3_stmt* s=nullptr;
 Q(sqlite3* db,const char* stmt):d(db){if(sqlite3_prepare_v2(d,stmt,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(d));}
 ~Q(){sqlite3_finalize(s);}void str(int n,const std::string& v){if(sqlite3_bind_text(s,n,v.data(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("network text bind");}
 void num(int n,std::uint64_t v){if(v>sql_max||sqlite3_bind_int64(s,n,static_cast<sqlite3_int64>(v))!=SQLITE_OK)throw std::runtime_error("network integer bind");}
 int next(){int n=sqlite3_step(s);if(n!=SQLITE_DONE&&n!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(d));return n;}
 void done(){if(next()!=SQLITE_DONE)throw std::runtime_error("unexpected network result");}};
std::string cell(Q& q,int i){auto p=sqlite3_column_text(q.s,i);const int n=sqlite3_column_bytes(q.s,i);if(!p||n<0||n>131072)throw std::runtime_error("invalid network cell");return {reinterpret_cast<const char*>(p),static_cast<std::size_t>(n)};}
std::uint64_t ucell(Q& q,int i){const auto v=sqlite3_column_int64(q.s,i);if(v<0)throw std::runtime_error("negative network cell");return static_cast<std::uint64_t>(v);}
void name(const std::string& s){if(s.empty()||s.size()>128||!std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';}))throw std::invalid_argument("noncanonical network identifier");}
std::int64_t checked_add(std::int64_t a,std::int64_t b){if(a<0||b<0||a>std::numeric_limits<std::int64_t>::max()-b)throw std::overflow_error("network mass overflow");return a+b;}
std::int64_t total(const NetworkState& state){auto mass=state.warehouse.used();for(const auto& node:state.machines){QuantifiedLine l(node.recipe,node.machine,node.state);mass=checked_add(mass,l.total_mass_grams());}return mass;}
void validate(const NetworkState& n){
 if(n.initial_mass<0||n.machines.size()<2||n.machines.size()>8||n.warehouse.capacity_grams<=0||n.warehouse.used()>n.warehouse.capacity_grams)throw std::invalid_argument("invalid network mass or size");
 std::set<std::uint64_t> all_workers;std::uint64_t previous=0;
 for(const auto& m:n.machines){
  if(!m.id||m.id<=previous||!m.worksite||m.workers.empty()||m.workers.size()>32||!std::is_sorted(m.workers.begin(),m.workers.end())||std::adjacent_find(m.workers.begin(),m.workers.end())!=m.workers.end()||m.workers.front()==0||m.recipe.inputs.size()!=1||m.state.elapsed_ticks!=n.tick)throw std::invalid_argument("invalid network machine or workers");
  previous=m.id;name(m.recipe.name);name(m.recipe.operation);name(m.recipe.inputs[0].code);name(m.recipe.product.code);name(m.recipe.scrap.code);
  for(auto worker:m.workers)if(!all_workers.insert(worker).second)throw std::invalid_argument("one worker assigned to two machines");
  (void)QuantifiedLine(m.recipe,m.machine,m.state);
 }
 for(const auto& [code,grams]:n.warehouse.grams){name(code);if(grams<0)throw std::invalid_argument("negative warehouse stack");}
 if(total(n)!=n.initial_mass)throw std::runtime_error("network material was created or lost");
}
std::string encode(const NetworkState& n){validate(n);std::ostringstream o;o<<"SG-NET-51 1 "<<n.tick<<' '<<n.initial_mass<<' '<<n.warehouse.capacity_grams<<' '<<n.warehouse.grams.size();
 for(const auto& [code,amount]:n.warehouse.grams)o<<' '<<std::quoted(code)<<' '<<amount;
 o<<' '<<n.machines.size();
 for(const auto& m:n.machines){
  o<<' '<<m.id<<' '<<m.worksite<<' '<<m.workers.size();for(auto w:m.workers)o<<' '<<w;
  o<<' '<<std::quoted(m.recipe.name)<<' '<<std::quoted(m.recipe.operation)
   <<' '<<std::quoted(m.recipe.inputs[0].code)<<' '<<m.recipe.inputs[0].grams
   <<' '<<std::quoted(m.recipe.product.code)<<' '<<m.recipe.product.grams
   <<' '<<std::quoted(m.recipe.scrap.code)<<' '<<m.recipe.scrap.grams
   <<' '<<m.machine.tier<<' '<<std::quoted(m.machine.operation)<<' '
   <<int(m.machine.powered)<<' '<<int(m.machine.staffed)<<' '<<int(m.machine.maintained);
  for(const auto* s:{&m.state.feed,&m.state.finished,&m.state.scrap}){
   o<<' '<<s->capacity_grams<<' '<<s->grams.size();for(const auto& [code,amount]:s->grams)o<<' '<<std::quoted(code)<<' '<<amount;
  }
  o<<' '<<int(m.state.has_work_in_progress)<<' '<<m.state.ticks_remaining
   <<' '<<m.state.reserved_inputs.size();
  for(const auto& r:m.state.reserved_inputs)o<<' '<<std::quoted(r.code)<<' '<<r.grams;
  o<<' '<<m.state.elapsed_ticks<<' '<<m.state.completed_batches;
 }
 return o.str();}
NetworkState decode(const std::string& raw){if(raw.empty()||raw.size()>131072)throw std::runtime_error("network state size invalid");std::istringstream in(raw);std::string tag;int ver=0;NetworkState n;std::size_t cnt=0;
 if(!(in>>tag>>ver>>n.tick>>n.initial_mass>>n.warehouse.capacity_grams>>cnt)||tag!="SG-NET-51"||ver!=1||cnt>128)throw std::runtime_error("invalid network header");
 for(std::size_t i=0;i<cnt;++i){std::string c;std::int64_t v=0;if(!(in>>std::quoted(c)>>v)||!n.warehouse.grams.emplace(c,v).second)throw std::runtime_error("invalid network warehouse");}
 if(!(in>>cnt)||cnt<2||cnt>8)throw std::runtime_error("invalid network machine count");
 for(std::size_t i=0;i<cnt;++i){NetworkMachine m;std::size_t wc=0;if(!(in>>m.id>>m.worksite>>wc)||wc<1||wc>32)throw std::runtime_error("invalid network worker count");
  for(std::size_t j=0;j<wc;++j){std::uint64_t w=0;if(!(in>>w))throw std::runtime_error("truncated worker");m.workers.push_back(w);}
  Material input,product,scrap;int powered=0,staffed=0,maintained=0;unsigned tier=0;
  if(!(in>>std::quoted(m.recipe.name)>>std::quoted(m.recipe.operation)>>std::quoted(input.code)>>input.grams>>std::quoted(product.code)>>product.grams>>std::quoted(scrap.code)>>scrap.grams>>tier>>std::quoted(m.machine.operation)>>powered>>staffed>>maintained)||powered<0||powered>1||staffed<0||staffed>1||maintained<0||maintained>1)throw std::runtime_error("invalid machine recipe");
  m.recipe.inputs={input};m.recipe.product=product;m.recipe.scrap=scrap;m.machine.tier=tier;m.machine.powered=powered;m.machine.staffed=staffed;m.machine.maintained=maintained;
  for(auto* s:{&m.state.feed,&m.state.finished,&m.state.scrap}){std::size_t count=0;if(!(in>>s->capacity_grams>>count)||count>32)throw std::runtime_error("invalid machine store");for(std::size_t j=0;j<count;++j){std::string c;std::int64_t v=0;if(!(in>>std::quoted(c)>>v)||!s->grams.emplace(c,v).second)throw std::runtime_error("invalid machine stock");}}
  int wip=0;std::size_t rc=0;if(!(in>>wip>>m.state.ticks_remaining>>rc)||wip<0||wip>1||rc>8)throw std::runtime_error("invalid WIP");m.state.has_work_in_progress=wip==1;
  for(std::size_t j=0;j<rc;++j){Material r;if(!(in>>std::quoted(r.code)>>r.grams))throw std::runtime_error("bad reservation");m.state.reserved_inputs.push_back(r);}
  if(!(in>>m.state.elapsed_ticks>>m.state.completed_batches))throw std::runtime_error("bad machine ticks");
  n.machines.push_back(std::move(m));
 }
 in>>std::ws;if(!in.eof())throw std::runtime_error("network trailing bytes");validate(n);return n;
}
struct Payload {std::string raw;std::uint64_t rev=0,gen=0;};
Payload fetch(sqlite3* db,const std::string& scope,bool warm){
 Q q(db,warm?"SELECT payload,revision,generation FROM sg_v51_warm WHERE scope=?1":"SELECT payload,revision,0 FROM sg_v51_network WHERE scope=?1");q.str(1,scope);
 if(q.next()!=SQLITE_ROW)throw std::runtime_error("network payload missing");
 return {cell(q,0),ucell(q,1),ucell(q,2)};
}
void owner_check(sqlite3* db,const std::string& scope,int mode,std::uint64_t generation,std::uint64_t revision){
 Q q(db,"SELECT owner,phase,generation,checkpoint_revision FROM sg_regional_mode_authority WHERE scope=?1");q.str(1,scope);
 if(q.next()!=SQLITE_ROW||sqlite3_column_int(q.s,0)!=mode||sqlite3_column_int(q.s,1)!=0||ucell(q,2)!=generation||ucell(q,3)!=revision)throw std::runtime_error("stale network owner fence");
}
void update(sqlite3* db,const std::string& scope,bool warm,const Payload& before,const std::string& after){
 Q q(db,warm?"UPDATE sg_v51_warm SET payload=?3,revision=revision+1 WHERE scope=?1 AND revision=?2 AND generation=?5 AND payload=?4":"UPDATE sg_v51_network SET payload=?3,revision=revision+1 WHERE scope=?1 AND revision=?2 AND payload=?4");
 q.str(1,scope);q.num(2,before.rev);q.str(3,after);q.str(4,before.raw);if(warm)q.num(5,before.gen);q.done();if(sqlite3_changes(db)!=1)throw std::runtime_error("network CAS rejected outdated payload");
}
std::uint64_t world_gen(sqlite3* db,const NetworkState& n,std::uint64_t* tick,std::uint64_t* staff_mask){
 Q q(db,"SELECT generation,state FROM snapshots ORDER BY generation DESC LIMIT 1");if(q.next()!=SQLITE_ROW)throw std::runtime_error("network canonical world missing");
 const auto gen=ucell(q,0);const auto* data=sqlite3_column_blob(q.s,1);const auto len=sqlite3_column_bytes(q.s,1);if(!data||len<1||len>16*1024*1024)throw std::runtime_error("invalid world snapshot");
 auto sim=Simulation::from_snapshot(std::string(static_cast<const char*>(data),static_cast<std::size_t>(len)));
 if(tick)*tick=sim.tick();
 std::uint64_t mask=0;
 for(std::size_t i=0;i<n.machines.size();++i){const auto& m=n.machines[i];bool available=true;
  for(auto worker:m.workers){const auto found=sim.npc_records().find(worker);if(found==sim.npc_records().end()||found->second.home_ship!=m.worksite||found->second.assigned_site!=m.worksite)throw std::runtime_error("unregistered or reassigned factory worker");if(found->second.location!=m.worksite||found->second.transport_id)available=false;}
  if(available)mask|=(std::uint64_t(1)<<i);
 }
 if(staff_mask)*staff_mask=mask;
 return gen;
}
void production(NetworkState& n,std::uint64_t ticks,std::uint64_t mask){
 if(!ticks||ticks>1000000||n.tick>std::numeric_limits<std::uint64_t>::max()-ticks)throw std::invalid_argument("invalid network production time");
 for(std::size_t i=0;i<n.machines.size();++i){auto& m=n.machines[i];QuantifiedLine l(m.recipe,m.machine,m.state);l.set_staff((mask&(std::uint64_t(1)<<i))!=0);(void)l.advance(ticks);m.state=l.state();m.machine=l.machine();}
 n.tick+=ticks;validate(n);
}
Store& bin(NetworkState& n,std::uint64_t id,NetworkBin which){
 if(which==NetworkBin::Warehouse){if(id!=0)throw std::runtime_error("warehouse ID mismatch");return n.warehouse;}
 if(id==0)throw std::runtime_error("machine ID absent");
 auto it=std::find_if(n.machines.begin(),n.machines.end(),[&](const auto& m){return m.id==id;});if(it==n.machines.end())throw std::runtime_error("unknown machine ID");
 if(which==NetworkBin::Feed)return it->state.feed;
 if(which==NetworkBin::Finished)return it->state.finished;
 if(which==NetworkBin::Scrap)return it->state.scrap;
 throw std::runtime_error("bad material bin");
}
void movement(NetworkState& n,const NetworkTransfer& t){
 name(t.material);if(t.grams<=0||(t.from==t.to&&t.from_machine==t.to_machine)||t.from==NetworkBin::Feed||t.to==NetworkBin::Finished||t.to==NetworkBin::Scrap||(t.from==NetworkBin::Warehouse&&t.to==NetworkBin::Warehouse))throw std::invalid_argument("invalid production logistics direction");
 // Material codes must match destination recipes; misrouted ore is not useful stock.
 if(t.to==NetworkBin::Feed){auto it=std::find_if(n.machines.begin(),n.machines.end(),[&](const auto& m){return m.id==t.to_machine;});if(it==n.machines.end()||it->recipe.inputs[0].code!=t.material)throw std::invalid_argument("material incompatible with machine input");}
 if(t.from==NetworkBin::Finished||t.from==NetworkBin::Scrap){auto it=std::find_if(n.machines.begin(),n.machines.end(),[&](const auto& m){return m.id==t.from_machine;});if(it==n.machines.end()||t.material!=(t.from==NetworkBin::Finished?it->recipe.product.code:it->recipe.scrap.code))throw std::invalid_argument("source has no such output");}
 auto& source=bin(n,t.from_machine,t.from);auto& target=bin(n,t.to_machine,t.to);
 if(source.amount(t.material)<t.grams||target.free_space()<t.grams)throw std::invalid_argument("transfer shortage or insufficient warehouse capacity");
 source.withdraw(t.material,t.grams);target.deposit(t.material,t.grams);validate(n);
}
void returned(NetworkState& n,const std::string& code,std::int64_t kg){
 if((code!="plates"&&code!="frames")||kg<=0||kg>4000||
    n.initial_mass<kg*1000||n.warehouse.amount(code)<kg*1000)
    throw std::invalid_argument("world return is not backed by finished network stock");
 n.warehouse.withdraw(code,kg*1000);
 n.initial_mass-=kg*1000;
 validate(n);
}
std::string instruction(const NetworkTransfer& t){std::ostringstream out;out<<t.from_machine<<' '<<int(t.from)<<' '<<t.to_machine<<' '<<int(t.to)<<' '<<std::quoted(t.material)<<' '<<t.grams;return out.str();}
NetworkTransfer parse_instruction(const std::string& raw){std::istringstream in(raw);NetworkTransfer t;int a=0,b=0;if(!(in>>t.from_machine>>a>>t.to_machine>>b>>std::quoted(t.material)>>t.grams)||a<0||a>3||b<0||b>3)throw std::runtime_error("corrupt network transfer instruction");t.from=static_cast<NetworkBin>(a);t.to=static_cast<NetworkBin>(b);in>>std::ws;if(!in.eof())throw std::runtime_error("noncanonical network transfer");return t;}
void receipt(sqlite3* db,const std::string& scope,const std::string& event,const std::string& kind,const std::string& before,const std::string& after,const std::string& detail,std::uint64_t ticks,std::uint64_t mask,std::uint64_t world,std::uint64_t generation){
 name(event);Q q(db,"INSERT INTO sg_v51_event(scope,seq,event_id,kind,before_payload,after_payload,detail,ticks,staff_mask,world_generation,owner_generation) VALUES(?1,(SELECT COALESCE(MAX(seq),0)+1 FROM sg_v51_event WHERE scope=?1),?2,?3,?4,?5,?6,?7,?8,?9,?10)");q.str(1,scope);q.str(2,event);q.str(3,kind);q.str(4,before);q.str(5,after);q.str(6,detail);q.num(7,ticks);q.num(8,mask);q.num(9,world);q.num(10,generation);q.done();}
void fault(NetworkFault actual,NetworkFault wanted){if(actual==wanted)throw std::runtime_error("injected network transaction interruption");}
}
RegionalProductionNetwork::RegionalProductionNetwork(PersistentWorld& w,regional_events::CheckpointStore& region,regional_events::RegionalModeAuthority& authority,std::string path,std::string colony,std::vector<std::string> family_scopes)
:world_(w),region_(region),authority_(authority),colony_(std::move(colony)),families_(std::move(family_scopes)){
 name(colony_);if(families_.empty()||families_.size()>32||!std::is_sorted(families_.begin(),families_.end())||std::adjacent_find(families_.begin(),families_.end())!=families_.end())throw std::invalid_argument("invalid network family registry");
 if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){if(db_)sqlite3_close(db_);db_=nullptr;throw std::runtime_error("network sqlite open failed");}
 try{
  execute(db_,"PRAGMA busy_timeout=3000");
  execute(db_,"CREATE TABLE IF NOT EXISTS sg_v51_network(scope TEXT PRIMARY KEY,payload TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),stock_receipt TEXT NOT NULL UNIQUE)");
  execute(db_,"CREATE TABLE IF NOT EXISTS sg_v51_warm(scope TEXT PRIMARY KEY,payload TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),generation INTEGER NOT NULL CHECK(generation>0))");
  execute(db_,"CREATE TABLE IF NOT EXISTS sg_v51_event(scope TEXT NOT NULL,seq INTEGER NOT NULL CHECK(seq>0),event_id TEXT NOT NULL,kind TEXT NOT NULL,before_payload TEXT NOT NULL,after_payload TEXT NOT NULL,detail TEXT NOT NULL,ticks INTEGER NOT NULL CHECK(ticks>=0),staff_mask INTEGER NOT NULL CHECK(staff_mask>=0),world_generation INTEGER NOT NULL CHECK(world_generation>0),owner_generation INTEGER NOT NULL CHECK(owner_generation>0),PRIMARY KEY(scope,seq),UNIQUE(scope,event_id))");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v51_event_immutable_up BEFORE UPDATE ON sg_v51_event BEGIN SELECT RAISE(ABORT,'immutable production effect');END");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v51_event_immutable_del BEFORE DELETE ON sg_v51_event BEGIN SELECT RAISE(ABORT,'immutable production effect');END");
  execute(db_,"CREATE TABLE IF NOT EXISTS sg_v52_world_escrow(scope TEXT PRIMARY KEY,site INTEGER NOT NULL UNIQUE,source_ship INTEGER NOT NULL,command_id INTEGER NOT NULL UNIQUE,stock_kg INTEGER NOT NULL CHECK(stock_kg>0),world_generation INTEGER NOT NULL CHECK(world_generation>0),owner_generation INTEGER NOT NULL CHECK(owner_generation>0))");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v52_escrow_immutable_up BEFORE UPDATE ON sg_v52_world_escrow BEGIN SELECT RAISE(ABORT,'immutable world stock escrow');END");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v52_escrow_immutable_del BEFORE DELETE ON sg_v52_world_escrow BEGIN SELECT RAISE(ABORT,'immutable world stock escrow');END");
  execute(db_,"CREATE TABLE IF NOT EXISTS sg_v53_world_return(scope TEXT NOT NULL,event_id TEXT NOT NULL,site INTEGER NOT NULL,source_ship INTEGER NOT NULL,command_id INTEGER NOT NULL UNIQUE,material TEXT NOT NULL,stock_kg INTEGER NOT NULL CHECK(stock_kg>0),world_generation INTEGER NOT NULL CHECK(world_generation>0),owner_generation INTEGER NOT NULL CHECK(owner_generation>0),before_payload TEXT NOT NULL,after_payload TEXT NOT NULL,PRIMARY KEY(scope,event_id))");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v53_return_immutable_up BEFORE UPDATE ON sg_v53_world_return BEGIN SELECT RAISE(ABORT,'immutable world finished return');END");
  execute(db_,"CREATE TRIGGER IF NOT EXISTS sg_v53_return_immutable_del BEFORE DELETE ON sg_v53_world_return BEGIN SELECT RAISE(ABORT,'immutable world finished return');END");
  Q prior(db_,"SELECT 1 FROM sg_v51_network WHERE scope=?1");prior.str(1,region_.scope());if(prior.next()==SQLITE_ROW)verify();
 }catch(...){sqlite3_close(db_);db_=nullptr;throw;}
}
RegionalProductionNetwork::~RegionalProductionNetwork(){if(db_)sqlite3_close(db_);}
void RegionalProductionNetwork::preflight(const regional_events::RegionalAuthorityState& o)const{
 if(o.phase!=regional_events::TransitionPhase::Stable)throw std::runtime_error("region network ownership transition in progress");
 const auto cp=region_.load();if(!cp||cp->revision!=o.checkpoint_revision||!cp->scheduler.colony(colony_).population)throw std::runtime_error("network has no regional population checkpoint");
 const auto data=decode(fetch(db_,region_.scope(),false).raw);
 (void)verify_regional_population(world_,sqlite3_db_filename(db_,"main"),data.machines[0].worksite,families_,region_.scope(),colony_,o.checkpoint_revision,o.generation,false);
}
void RegionalProductionNetwork::bootstrap(const NetworkState& state,const std::string& id){
 name(id);const auto raw=encode(state);const auto o=authority_.state();const auto cp=region_.load();if(o.owner!=regional_events::SimulationOwner::Cold||o.phase!=regional_events::TransitionPhase::Stable||!cp||cp->revision!=o.checkpoint_revision||cp->scheduler.tick()!=state.tick)throw std::runtime_error("network seed requires stable cold boundary");
 (void)verify_regional_population(world_,sqlite3_db_filename(db_,"main"),state.machines[0].worksite,families_,region_.scope(),colony_,o.checkpoint_revision,o.generation,true);
 if(state.warehouse.used()!=state.initial_mass)throw std::invalid_argument("initial materials must enter through warehouse escrow");
 for(const auto& m:state.machines){if(m.worksite!=state.machines[0].worksite||m.state.feed.used()||m.state.finished.used()||m.state.scrap.used()||m.state.has_work_in_progress||m.state.completed_batches)throw std::invalid_argument("network seed has unbacked machine stock");}
 Tx tx(db_);owner_check(db_,region_.scope(),0,o.generation,o.checkpoint_revision);
 std::uint64_t tick=0,mask=0;const auto gen=world_gen(db_,state,&tick,&mask);if(tick<state.tick||mask!=(std::uint64_t(1)<<state.machines.size())-1)throw std::runtime_error("network seed lacks real on-site workers");
 {Q q(db_,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v50_factory'");if(q.next()==SQLITE_ROW){Q c(db_,"SELECT 1 FROM sg_v50_factory WHERE scope=?1 OR source_receipt=?2");c.str(1,region_.scope());c.str(2,id);if(c.next()==SQLITE_ROW)throw std::runtime_error("stock receipt or region already assigned to older factory");}}
 {Q q(db_,"INSERT INTO sg_v51_network(scope,payload,revision,stock_receipt) VALUES(?1,?2,1,?3)");q.str(1,region_.scope());q.str(2,raw);q.str(3,id);q.done();}
 receipt(db_,region_.scope(),id,"seed","",raw,id,0,mask,gen,o.generation);tx.commit();verify();
}
void RegionalProductionNetwork::bootstrap_from_constructed_storage(const NetworkState& state,
    const std::string& id,EntityId site,std::uint64_t command_id,NetworkFault f){
 name(id);const std::string label="world-escrow-"+id;
 if(!site||!command_id||state.initial_mass<=0||state.initial_mass%1000||
    state.warehouse.used()!=state.initial_mass||state.warehouse.grams.size()!=1||
    state.warehouse.amount("aluminum")!=state.initial_mass||
    state.initial_mass/1000>4000)
     throw std::invalid_argument("world bootstrap requires exact whole-kg aluminum stock");
 const auto raw=encode(state);const auto o=authority_.state();const auto cp=region_.load();
 if(o.owner!=regional_events::SimulationOwner::Cold||o.phase!=regional_events::TransitionPhase::Stable||
    !cp||cp->revision!=o.checkpoint_revision||cp->scheduler.tick()!=state.tick)
     throw std::runtime_error("constructed warehouse requires stable cold regional boundary");
 (void)verify_regional_population(world_,sqlite3_db_filename(db_,"main"),state.machines[0].worksite,
     families_,region_.scope(),colony_,o.checkpoint_revision,o.generation,true);
 for(const auto& m:state.machines){
    if(m.worksite!=state.machines[0].worksite||m.state.feed.used()||
       m.state.finished.used()||m.state.scrap.used()||m.state.has_work_in_progress||
       m.state.completed_batches)throw std::invalid_argument("nonempty world-seeded machine");
 }
 const auto* actual=world_.world().storage(site);
 if(!actual||!actual->source_ship||actual->goods_kg.count("aluminum")==0||
    actual->goods_kg.at("aluminum")<state.initial_mass/1000||
    actual->network_escrow_kg)
    throw std::runtime_error("world-constructed storage lacks available physical stock");
 // No second writer may run between this validation and SQL world/network COMMIT.
 Command cmd{};cmd.id=command_id;cmd.sequence=command_id;
 cmd.issuer=Simulation::kServerAuthority;cmd.actor=actual->source_ship;
 cmd.target=site;cmd.action=Action::HostEscrowConstructedStock;
 cmd.execute_tick=world_.world().tick()+1;cmd.amount=state.initial_mass/1000;
 world_.settle_constructed_stock(cmd,[&](sqlite3* db,std::uint64_t gen){
  owner_check(db,region_.scope(),0,o.generation,o.checkpoint_revision);
  const auto* current=world_.world().storage(site);
  if(!current||current->network_escrow_kg!=0)throw std::runtime_error("warehouse escrow changed during commit");
  // Check the committed canonical snapshot, not the pre-transaction client state.
  Q snapshot(db,"SELECT state FROM snapshots WHERE generation=?1");snapshot.num(1,gen);
  if(snapshot.next()!=SQLITE_ROW)throw std::runtime_error("missing physical warehouse world snapshot");
  const auto* blob=sqlite3_column_blob(snapshot.s,0);const auto bytes=sqlite3_column_bytes(snapshot.s,0);
  if(!blob||bytes<1||bytes>16*1024*1024)throw std::runtime_error("invalid material world snapshot");
  const auto canonical=Simulation::from_snapshot(std::string(static_cast<const char*>(blob),static_cast<std::size_t>(bytes)));
  const auto* source=canonical.storage(site);
  if(!source||source->source_ship!=cmd.actor||source->network_escrow_kg!=state.initial_mass/1000||
     (source->goods_kg.count("aluminum") && source->goods_kg.at("aluminum")<0))
     throw std::runtime_error("world/network inventory reservation mismatch");
  {Q table(db,"SELECT 1 FROM sqlite_master WHERE name='sg_v50_factory' AND type='table'");
   if(table.next()==SQLITE_ROW){Q existing(db,"SELECT 1 FROM sg_v50_factory WHERE source_receipt=?1");
    existing.str(1,label);if(existing.next()==SQLITE_ROW)throw std::runtime_error("network receipt reused by older factory");}}
  {Q ins(db,"INSERT INTO sg_v51_network(scope,payload,revision,stock_receipt) VALUES(?1,?2,1,?3)");
   ins.str(1,region_.scope());ins.str(2,raw);ins.str(3,label);ins.done();}
  fault(f,NetworkFault::AfterPayload);
  {Q esc(db,"INSERT INTO sg_v52_world_escrow(scope,site,source_ship,command_id,stock_kg,world_generation,owner_generation) VALUES(?1,?2,?3,?4,?5,?6,?7)");
   esc.str(1,region_.scope());esc.num(2,site);esc.num(3,cmd.actor);esc.num(4,cmd.id);
   esc.num(5,static_cast<std::uint64_t>(cmd.amount));esc.num(6,gen);esc.num(7,o.generation);esc.done();}
  std::uint64_t tick=0,mask=0;const auto replay_world=world_gen(db,state,&tick,&mask);
  if(replay_world!=gen||tick<state.tick||mask!=(std::uint64_t(1)<<state.machines.size())-1)
     throw std::runtime_error("network world custody or staffing changed");
  receipt(db,region_.scope(),label,"seed","",raw,label,0,mask,gen,o.generation);
  fault(f,NetworkFault::AfterEvent);
 },f==NetworkFault::AfterWorldSnapshot?FailPoint::AfterSnapshotWrite:FailPoint::None);
 verify();
}
void RegionalProductionNetwork::return_finished_to_constructed_storage(
    const std::string& id,EntityId site,std::uint64_t command_id,
    const std::string& code,std::int64_t kg,NetworkFault f){
 name(id);
 if(!site||!command_id||(code!="plates"&&code!="frames")||kg<=0||kg>4000)
    throw std::invalid_argument("invalid physical finished-goods return request");
 const auto o=authority_.state();preflight(o);
 const bool warm=o.owner==regional_events::SimulationOwner::Detailed;
 const auto before=fetch(db_,region_.scope(),warm);const auto state=decode(before.raw);
 if(warm&&before.gen!=o.generation)throw std::runtime_error("stale detailed warehouse authority");
 const auto* original=world_.world().storage(site);
 if(!original||original->network_escrow_kg<kg)throw std::runtime_error("world escrow lacks returnable material");
 auto after=state;
 returned(after,code,kg);
 const auto final=encode(after);
 {Q source(db_,"SELECT site,source_ship FROM sg_v52_world_escrow WHERE scope=?1");
  source.str(1,region_.scope());
  if(source.next()!=SQLITE_ROW||ucell(source,0)!=site||ucell(source,1)!=original->source_ship)
     throw std::runtime_error("constructed source has no matching original escrow");}
 // Finalize every SELECT on this connection before the separate world SQL
 // writer commits; a live WAL reader would otherwise verify stale rows.
 Command cmd{};cmd.id=command_id;cmd.sequence=command_id;
 cmd.issuer=Simulation::kServerAuthority;cmd.actor=original->source_ship;
 cmd.target=site;cmd.action=Action::HostReturnConstructedStock;
 cmd.execute_tick=world_.world().tick()+1;cmd.amount=kg;
 cmd.thrust_x=code=="plates"?1:2;
 world_.settle_constructed_stock(cmd,[&](sqlite3* db,std::uint64_t gen){
   owner_check(db,region_.scope(),warm?1:0,o.generation,o.checkpoint_revision);
   auto latest=fetch(db,region_.scope(),warm);
   if(latest.raw!=before.raw||latest.rev!=before.rev||latest.gen!=before.gen)
      throw std::runtime_error("network state changed during physical return");
   Q snapshot(db,"SELECT state FROM snapshots WHERE generation=?1");snapshot.num(1,gen);
   if(snapshot.next()!=SQLITE_ROW)throw std::runtime_error("missing committed world return snapshot");
   const auto* blob=sqlite3_column_blob(snapshot.s,0);
   const auto nbytes=sqlite3_column_bytes(snapshot.s,0);
   if(!blob||nbytes<1||nbytes>16*1024*1024)throw std::runtime_error("invalid physical return snapshot");
   const auto canonical=Simulation::from_snapshot(std::string(
       static_cast<const char*>(blob),static_cast<std::size_t>(nbytes)));
   const auto* physical=canonical.storage(site);
   const std::string world_code=code=="plates"?"aluminum_panels":"aluminum_frames";
   const auto it=physical?physical->goods_kg.find(world_code):
        std::map<std::string,std::int64_t>::const_iterator{};
   const auto prior=original->goods_kg.find(world_code);
   const auto previous=prior==original->goods_kg.end()?0:prior->second;
   if(!physical||physical->source_ship!=cmd.actor||
      physical->network_escrow_kg!=original->network_escrow_kg-kg||
      it==physical->goods_kg.end()||it->second!=previous+kg)
       throw std::runtime_error("world return destination or escrow did not commit");
   update(db,region_.scope(),warm,latest,final);
   fault(f,NetworkFault::AfterPayload);
   {Q r(db,"INSERT INTO sg_v53_world_return(scope,event_id,site,source_ship,command_id,material,stock_kg,world_generation,owner_generation,before_payload,after_payload) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
    r.str(1,region_.scope());r.str(2,id);r.num(3,site);r.num(4,cmd.actor);
    r.num(5,cmd.id);r.str(6,code);r.num(7,static_cast<std::uint64_t>(kg));
    r.num(8,gen);r.num(9,o.generation);r.str(10,before.raw);r.str(11,final);r.done();}
   std::uint64_t tick=0,mask=0;
   if(world_gen(db,after,&tick,&mask)!=gen)throw std::runtime_error("world return generation mismatch");
   receipt(db,region_.scope(),id,"world-return",before.raw,final,
           code+" "+std::to_string(kg),0,0,gen,o.generation);
   fault(f,NetworkFault::AfterEvent);
 },f==NetworkFault::AfterWorldSnapshot?FailPoint::AfterSnapshotWrite:FailPoint::None);
 verify();
}
NetworkState RegionalProductionNetwork::active()const{const auto o=authority_.state();return decode(fetch(db_,region_.scope(),o.owner==regional_events::SimulationOwner::Detailed).raw);}
void RegionalProductionNetwork::move(const std::string& id,const NetworkTransfer& tr,NetworkFault f){
 name(id);const auto o=authority_.state();preflight(o);const bool warm=o.owner==regional_events::SimulationOwner::Detailed;Tx tx(db_);owner_check(db_,region_.scope(),warm?1:0,o.generation,o.checkpoint_revision);
 auto before=fetch(db_,region_.scope(),warm);auto data=decode(before.raw);const auto generation=world_gen(db_,data,nullptr,nullptr);
 movement(data,tr);const auto next=encode(data);update(db_,region_.scope(),warm,before,next);fault(f,NetworkFault::AfterPayload);
 receipt(db_,region_.scope(),id,"transfer",before.raw,next,instruction(tr),0,0,generation,o.generation);fault(f,NetworkFault::AfterEvent);tx.commit();verify();
}
std::uint64_t RegionalProductionNetwork::advance_cold(std::uint64_t ticks,const std::string& id,NetworkFault f){
 name(id);if(!ticks||ticks>1000000)throw std::invalid_argument("invalid cold network ticks");const auto o=authority_.state();if(o.owner!=regional_events::SimulationOwner::Cold)throw std::runtime_error("wrong cold network owner");preflight(o);
 // Regional replay receipts permit returning the original batch count.
 {Q q(db_,"SELECT actor,request,outcome FROM sg_regional_command_receipt WHERE scope=?1 AND command_id=?2");q.str(1,region_.scope());q.str(2,id);if(q.next()==SQLITE_ROW){if(cell(q,0)!="network-host"||cell(q,1)!="network-cold-v51:"+std::to_string(ticks))throw std::runtime_error("network command ID collision");verify();return ucell(q,2);}}
 const auto prior=decode(fetch(db_,region_.scope(),false).raw);std::uint64_t mask=0,world_tick=0;const auto gen=world_gen(db_,prior,&world_tick,&mask);if(prior.tick>world_tick||ticks>world_tick-prior.tick)throw std::runtime_error("network cold outran world clock");
 auto predicted=prior;production(predicted,ticks,mask);std::uint64_t result=0;for(std::size_t i=0;i<prior.machines.size();++i)result+=predicted.machines[i].state.completed_batches-prior.machines[i].state.completed_batches;
 const auto receipt_record=region_.commit_command(id,"network-host","network-cold-v51:"+std::to_string(ticks),o.checkpoint_revision,
 [&](regional_events::Scheduler& s){s.advance_to(s.tick()+ticks);return result;},
 [&](sqlite3* db,const regional_events::Scheduler& was,const regional_events::Scheduler& now,std::uint64_t){
  owner_check(db,region_.scope(),0,o.generation,o.checkpoint_revision);auto previous=fetch(db,region_.scope(),false);if(decode(previous.raw).tick!=was.tick()||now.tick()!=was.tick()+ticks)throw std::runtime_error("network regional clock detached");
  std::uint64_t current_tick=0,current_mask=0;const auto world=world_gen(db,prior,&current_tick,&current_mask);if(world!=gen||current_mask!=mask||now.tick()>current_tick)throw std::runtime_error("network workforce changed during cold commit");
  update(db,region_.scope(),false,previous,encode(predicted));fault(f,NetworkFault::AfterPayload);receipt(db,region_.scope(),id,"cold",previous.raw,encode(predicted),"",ticks,mask,world,o.generation);fault(f,NetworkFault::AfterEvent);
 },true,o.generation,0);
 if(receipt_record.replayed)return receipt_record.outcome;
 verify();return result;
}
std::uint64_t RegionalProductionNetwork::advance_detailed(std::uint64_t ticks,const std::string& id,NetworkFault f){
 name(id);if(!ticks||ticks>1000000)throw std::invalid_argument("invalid detailed network ticks");const auto o=authority_.state();if(o.owner!=regional_events::SimulationOwner::Detailed)throw std::runtime_error("wrong detailed network owner");preflight(o);Tx tx(db_);owner_check(db_,region_.scope(),1,o.generation,o.checkpoint_revision);
 auto previous=fetch(db_,region_.scope(),true);if(previous.gen!=o.generation)throw std::runtime_error("network detailed generation mismatch");auto state=decode(previous.raw);std::uint64_t tick=0,mask=0;const auto gen=world_gen(db_,state,&tick,&mask);if(state.tick>tick||ticks>tick-state.tick)throw std::runtime_error("detailed network outran world time");
 production(state,ticks,mask);std::uint64_t count=0;const auto before=decode(previous.raw);for(std::size_t i=0;i<state.machines.size();++i)count+=state.machines[i].state.completed_batches-before.machines[i].state.completed_batches;
 const auto after=encode(state);update(db_,region_.scope(),true,previous,after);fault(f,NetworkFault::AfterPayload);receipt(db_,region_.scope(),id,"detailed",previous.raw,after,"",ticks,mask,gen,o.generation);fault(f,NetworkFault::AfterEvent);tx.commit();verify();return count;
}
regional_events::RegionalAuthorityState RegionalProductionNetwork::promote(const std::string& id,NetworkFault f){
 name(id);auto o=authority_.state();if(o.owner!=regional_events::SimulationOwner::Cold)throw std::runtime_error("not cold network owner");
 if(o.phase==regional_events::TransitionPhase::Stable){preflight(o);o=authority_.prepare(id,regional_events::SimulationOwner::Detailed,o.generation,o.checkpoint_revision,o.boundary_tick);}else if(o.phase!=regional_events::TransitionPhase::ToDetailed||o.handoff_id!=id)throw std::runtime_error("unexpected network promotion");
 return authority_.commit(id,o.generation,o.checkpoint_revision,[&](sqlite3* db,regional_events::Scheduler& s){
  const auto cold=fetch(db,region_.scope(),false);if(decode(cold.raw).tick!=s.tick())throw std::runtime_error("stale network cold clock");const auto gen=world_gen(db,decode(cold.raw),nullptr,nullptr);
  {Q q(db,"INSERT INTO sg_v51_warm(scope,payload,revision,generation) VALUES(?1,?2,1,?3)");q.str(1,region_.scope());q.str(2,cold.raw);q.num(3,o.generation+1);q.done();}
  fault(f,NetworkFault::AfterPayload);receipt(db,region_.scope(),id,"promote",cold.raw,cold.raw,"",0,0,gen,o.generation+1);fault(f,NetworkFault::AfterEvent);
 });
}
regional_events::RegionalAuthorityState RegionalProductionNetwork::demote(const std::string& id,NetworkFault f){
 name(id);auto o=authority_.state();if(o.owner!=regional_events::SimulationOwner::Detailed)throw std::runtime_error("not detailed network owner");
 if(o.phase==regional_events::TransitionPhase::Stable){preflight(o);o=authority_.prepare(id,regional_events::SimulationOwner::Cold,o.generation,o.checkpoint_revision,o.boundary_tick);}else if(o.phase!=regional_events::TransitionPhase::ToCold||o.handoff_id!=id)throw std::runtime_error("unexpected network demotion");
 return authority_.commit(id,o.generation,o.checkpoint_revision,[&](sqlite3* db,regional_events::Scheduler& s){
  const auto previous=fetch(db,region_.scope(),false),warm=fetch(db,region_.scope(),true);if(warm.gen!=o.generation||decode(previous.raw).tick!=s.tick()||decode(warm.raw).tick<s.tick())throw std::runtime_error("invalid detailed network reconciliation");
  std::uint64_t world_tick=0;const auto gen=world_gen(db,decode(warm.raw),&world_tick,nullptr);if(decode(warm.raw).tick>world_tick)throw std::runtime_error("demotion ahead of world");s.advance_to(decode(warm.raw).tick);
  update(db,region_.scope(),false,previous,warm.raw);{Q q(db,"DELETE FROM sg_v51_warm WHERE scope=?1 AND generation=?2 AND revision=?3");q.str(1,region_.scope());q.num(2,warm.gen);q.num(3,warm.rev);q.done();if(sqlite3_changes(db)!=1)throw std::runtime_error("stale detailed network on demotion");}
  fault(f,NetworkFault::AfterPayload);receipt(db,region_.scope(),id,"demote",warm.raw,warm.raw,"",0,0,gen,o.generation+1);fault(f,NetworkFault::AfterEvent);
 });
}
void RegionalProductionNetwork::verify()const{
 const auto o=authority_.state();const auto cp=region_.load();
 for(const auto* trigger:{"sg_v51_event_immutable_up","sg_v51_event_immutable_del",
                           "sg_v53_return_immutable_up","sg_v53_return_immutable_del"}){
  Q present(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?1");present.str(1,trigger);
  if(present.next()!=SQLITE_ROW)throw std::runtime_error("network effect immutability trigger missing");
 }
 if(!cp||cp->revision!=o.checkpoint_revision)throw std::runtime_error("network regional owner divergence");
 const auto cold=fetch(db_,region_.scope(),false);const auto baseline=decode(cold.raw);
 if(baseline.tick!=cp->scheduler.tick()||baseline.tick>world_.world().tick())throw std::runtime_error("network regional clock not aligned");
 Q seeded(db_,"SELECT stock_receipt FROM sg_v51_network WHERE scope=?1");seeded.str(1,region_.scope());
 if(seeded.next()!=SQLITE_ROW)throw std::runtime_error("network seed provenance missing");
 const auto seed_label=cell(seeded,0);
 if(seed_label.rfind("world-escrow-",0)==0){
  for(const char* tr:{"sg_v52_escrow_immutable_up","sg_v52_escrow_immutable_del"}){
   Q q(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?1");q.str(1,tr);
   if(q.next()!=SQLITE_ROW)throw std::runtime_error("world inventory escrow ledger is not protected");
  }
  Q q(db_,"SELECT site,source_ship,command_id,stock_kg,world_generation FROM sg_v52_world_escrow WHERE scope=?1");
  q.str(1,region_.scope());if(q.next()!=SQLITE_ROW)throw std::runtime_error("missing canonical world inventory debit");
  const auto site=ucell(q,0),ship=ucell(q,1),command=ucell(q,2),kg=ucell(q,3),gen=ucell(q,4);
  const auto* store=world_.world().storage(site);
  std::uint64_t returns=0;
  {Q qsum(db_,"SELECT COALESCE(SUM(stock_kg),0) FROM sg_v53_world_return WHERE scope=?1");
   qsum.str(1,region_.scope());if(qsum.next()!=SQLITE_ROW)throw std::runtime_error("return ledger absent");
   returns=ucell(qsum,0);}
  const auto now=decode(fetch(db_,region_.scope(),o.owner==regional_events::SimulationOwner::Detailed).raw);
  if(returns>kg||!store||store->source_ship!=ship||
      store->network_escrow_kg!=static_cast<std::int64_t>(kg-returns)||
      now.initial_mass!=static_cast<std::int64_t>((kg-returns)*1000)||
      gen>world_.generation())
      throw std::runtime_error("world storage reserved mass is inconsistent with factory ledger: kg="+std::to_string(kg)+" returns="+std::to_string(returns)+" escrow="+std::to_string(store?store->network_escrow_kg:-100)+" network="+std::to_string(now.initial_mass)+" gen="+std::to_string(gen)+" world="+std::to_string(world_.generation()));
  Q history(db_,"SELECT 1 FROM receipts WHERE id=?1 AND accepted_generation=?2");
  history.str(1,std::to_string(command));history.num(2,gen);
  if(history.next()!=SQLITE_ROW)throw std::runtime_error("world inventory debit journal missing");
 }

 std::string head;std::uint64_t seq=1;Q e(db_,"SELECT seq,event_id,kind,before_payload,after_payload,detail,ticks,staff_mask,world_generation,owner_generation FROM sg_v51_event WHERE scope=?1 ORDER BY seq");e.str(1,region_.scope());
 while(e.next()==SQLITE_ROW){if(ucell(e,0)!=seq++)throw std::runtime_error("missing network event");const auto event=cell(e,1),kind=cell(e,2),before=cell(e,3),after=cell(e,4),detail=cell(e,5);const auto ticks=ucell(e,6),mask=ucell(e,7),world=ucell(e,8),owner=ucell(e,9);
  if(before!=head||!world||world>world_.generation()||!owner)throw std::runtime_error("broken network effect continuity");
  if(kind=="seed"){if(seq!=2||!before.empty()||ticks||detail!=seed_label)throw std::runtime_error("invalid seed/provenance event");
   auto initial=decode(after);if(initial.warehouse.used()!=initial.initial_mass)throw std::runtime_error("network seeding invented unwarehoused inventory");
   for(const auto& m:initial.machines)if(m.state.feed.used()||m.state.finished.used()||m.state.scrap.used()||m.state.has_work_in_progress)throw std::runtime_error("network seed contains unbacked machine inventory");}
  else if(kind=="cold"||kind=="detailed"){auto state=decode(before);production(state,ticks,mask);if(encode(state)!=after)throw std::runtime_error("network production event fails deterministic replay");}
  else if(kind=="transfer"){if(ticks)throw std::runtime_error("transfer advanced clock");auto state=decode(before);movement(state,parse_instruction(detail));if(encode(state)!=after)throw std::runtime_error("network transfer effects inconsistent");}
  else if(kind=="world-return"){
   if(ticks||mask)throw std::runtime_error("world return advanced factory clock");
   std::istringstream decoded(detail);std::string code;std::int64_t kg=0;std::string extra;
   if(!(decoded>>code>>kg)||(decoded>>extra))throw std::runtime_error("invalid world return effect");
   auto previous=decode(before);returned(previous,code,kg);
   if(encode(previous)!=after)throw std::runtime_error("world return breaks material conservation");
   Q r(db_,"SELECT site,source_ship,command_id,material,stock_kg,world_generation,owner_generation,before_payload,after_payload FROM sg_v53_world_return WHERE scope=?1 AND event_id=?2");
   r.str(1,region_.scope());r.str(2,event);
   if(r.next()!=SQLITE_ROW||cell(r,3)!=code||ucell(r,4)!=static_cast<std::uint64_t>(kg)||
      ucell(r,5)!=world||ucell(r,6)!=owner||cell(r,7)!=before||cell(r,8)!=after)
      throw std::runtime_error("missing or forged immutable world return receipt");
   Q origin(db_,"SELECT site,source_ship FROM sg_v52_world_escrow WHERE scope=?1");
   origin.str(1,region_.scope());
   if(origin.next()!=SQLITE_ROW||ucell(r,0)!=ucell(origin,0)||
      ucell(r,1)!=ucell(origin,1))
      throw std::runtime_error("world return forged original constructed storage");
   Q w(db_,"SELECT 1 FROM receipts WHERE id=?1 AND accepted_generation=?2");
   w.str(1,std::to_string(ucell(r,2)));w.num(2,world);
   if(w.next()!=SQLITE_ROW)throw std::runtime_error("world return has no committed command");
  }
  else if(kind=="promote"||kind=="demote"){if(ticks||before!=after)throw std::runtime_error("network handoff created stock");}
  else throw std::runtime_error("unknown network effect");
  head=after;
 }
 if(seq==1)throw std::runtime_error("network seed history absent");
 {Q entries(db_,"SELECT COUNT(*) FROM sg_v53_world_return WHERE scope=?1");
  entries.str(1,region_.scope());if(entries.next()!=SQLITE_ROW)throw std::runtime_error("return counter missing");
  Q linked(db_,"SELECT COUNT(*) FROM sg_v51_event WHERE scope=?1 AND kind='world-return'");
  linked.str(1,region_.scope());
  if(linked.next()!=SQLITE_ROW||ucell(entries,0)!=ucell(linked,0))
     throw std::runtime_error("world return command and factory effect counts disagree");}

 if(o.owner==regional_events::SimulationOwner::Detailed){const auto warm=fetch(db_,region_.scope(),true);if(warm.gen!=o.generation||head!=warm.raw||decode(warm.raw).tick<baseline.tick)throw std::runtime_error("detailed network payload/owner divergence");}
 else {if(head!=cold.raw)throw std::runtime_error("cold network effects do not match checkpoint");Q q(db_,"SELECT 1 FROM sg_v51_warm WHERE scope=?1");q.str(1,region_.scope());if(q.next()==SQLITE_ROW)throw std::runtime_error("orphaned warm network scope");}
}
} // namespace galaxy::industry
