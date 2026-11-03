#include "galaxy/RegionalFactoryHandoff.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::industry {
namespace {
constexpr auto max_i=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
void sql(sqlite3* db,const char* command){
    char* err=nullptr;
    if(sqlite3_exec(db,command,nullptr,nullptr,&err)!=SQLITE_OK){
        std::string why=err?err:sqlite3_errmsg(db);sqlite3_free(err);
        throw std::runtime_error("factory SQL: "+why);
    }
}
struct Tx {
    sqlite3* db;bool finished=false;
    explicit Tx(sqlite3* d):db(d){sql(db,"BEGIN IMMEDIATE");}
    ~Tx(){if(!finished){try{sql(db,"ROLLBACK");}catch(...){}}}
    void commit(){sql(db,"COMMIT");finished=true;}
};
struct Q {
    sqlite3* db;sqlite3_stmt* stmt=nullptr;
    Q(sqlite3* d,const char* s):db(d){
        if(sqlite3_prepare_v2(db,s,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("factory prepare: "+std::string(sqlite3_errmsg(db)));
    }
    ~Q(){sqlite3_finalize(stmt);}
    void str(int i,const std::string& s){
        if(s.size()>65536||sqlite3_bind_text(stmt,i,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("factory bind text");
    }
    void num(int i,std::uint64_t n){
        if(n>max_i||sqlite3_bind_int64(stmt,i,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
            throw std::runtime_error("factory bind int");
    }
    int next(){const int r=sqlite3_step(stmt);
        if(r!=SQLITE_ROW&&r!=SQLITE_DONE)throw std::runtime_error("factory query: "+std::string(sqlite3_errmsg(db)));
        return r;
    }
    void done(){if(next()!=SQLITE_DONE)throw std::runtime_error("factory expected done");}
};
std::string string_column(Q& q,int i){
    const auto* p=sqlite3_column_text(q.stmt,i);const int n=sqlite3_column_bytes(q.stmt,i);
    if(!p||n<0||n>65536)throw std::runtime_error("invalid factory string cell");
    return std::string(reinterpret_cast<const char*>(p),static_cast<std::size_t>(n));
}
std::uint64_t integer_column(Q& q,int i){
    const auto n=sqlite3_column_int64(q.stmt,i);
    if(n<0)throw std::runtime_error("negative factory integer");
    return static_cast<std::uint64_t>(n);
}
void label(const std::string& s){
    if(s.empty()||s.size()>128||!std::all_of(s.begin(),s.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';}))
        throw std::invalid_argument("noncanonical factory name/receipt");
}
void complete(const DurableFactory& s){
    if(!s.home_ship||s.workers.empty()||s.workers.size()>32||
       !std::is_sorted(s.workers.begin(),s.workers.end())||
       std::adjacent_find(s.workers.begin(),s.workers.end())!=s.workers.end()||
       s.workers.front()==0 ||s.recipe.inputs.size()!=1 ||s.initial_mass<=0)
        throw std::invalid_argument("invalid bounded factory/worker inventory");
    label(s.recipe.name);label(s.recipe.operation);label(s.recipe.inputs.at(0).code);
    label(s.recipe.product.code);label(s.recipe.scrap.code);
    if(s.state.feed.grams.size()>1||s.state.finished.grams.size()>1||
       s.state.scrap.grams.size()>1)
        throw std::invalid_argument("factory v50 one-material-store limit");
    for(const auto& kv:s.state.feed.grams)
        if(kv.first!=s.recipe.inputs.front().code)throw std::invalid_argument("wrong input material");
    for(const auto& kv:s.state.finished.grams)
        if(kv.first!=s.recipe.product.code)throw std::invalid_argument("wrong product material");
    for(const auto& kv:s.state.scrap.grams)
        if(kv.first!=s.recipe.scrap.code)throw std::invalid_argument("wrong scrap material");
    QuantifiedLine line(s.recipe,s.machine,s.state);
    if(line.total_mass_grams()!=s.initial_mass)
        throw std::runtime_error("factory feed/WIP/product/scrap mass violation");
}
std::string encode(const DurableFactory& s){
    complete(s);
    std::ostringstream out;
    out<<"SG-IND-50 1 "<<s.home_ship<<' '<<s.workers.size();
    for(auto n:s.workers)out<<' '<<n;
    out<<' '<<std::quoted(s.recipe.name)<<' '<<std::quoted(s.recipe.operation)
       <<' '<<std::quoted(s.recipe.inputs[0].code)<<' '<<s.recipe.inputs[0].grams
       <<' '<<std::quoted(s.recipe.product.code)<<' '<<s.recipe.product.grams
       <<' '<<std::quoted(s.recipe.scrap.code)<<' '<<s.recipe.scrap.grams
       <<' '<<s.machine.tier<<' '<<std::quoted(s.machine.operation)
       <<' '<<int(s.machine.powered)<<' '<<int(s.machine.staffed)<<' '
       <<int(s.machine.maintained)<<' '
       <<s.state.feed.capacity_grams<<' '<<s.state.finished.capacity_grams<<' '
       <<s.state.scrap.capacity_grams<<' '
       <<s.state.feed.amount(s.recipe.inputs[0].code)<<' '
       <<s.state.finished.amount(s.recipe.product.code)<<' '
       <<s.state.scrap.amount(s.recipe.scrap.code)<<' '
       <<int(s.state.has_work_in_progress)<<' '<<s.state.ticks_remaining<<' '
       <<(s.state.reserved_inputs.empty()?0:s.state.reserved_inputs[0].grams)<<' '
       <<s.state.elapsed_ticks<<' '<<s.state.completed_batches<<' '
       <<s.initial_mass;
    return out.str();
}
DurableFactory decode(const std::string& raw){
    if(raw.empty()||raw.size()>16384)throw std::runtime_error("invalid factory payload size");
    std::istringstream in(raw);std::string tag;int schema=0;
    DurableFactory s;std::size_t count=0;
    if(!(in>>tag>>schema>>s.home_ship>>count)||tag!="SG-IND-50"||schema!=1||count<1||count>32)
        throw std::runtime_error("unrecognized factory checkpoint");
    for(std::size_t i=0;i<count;++i){std::uint64_t id=0;
        if(!(in>>id))throw std::runtime_error("truncated factory workers");
        s.workers.push_back(id);}
    Material input,product,scrap;int powered=0,staffed=0,maintained=0,wip=0;
    int tier=0;std::uint32_t rem=0;std::int64_t reserved=0;
    std::int64_t feed=0,finished=0,waste=0;
    if(!(in>>std::quoted(s.recipe.name)>>std::quoted(s.recipe.operation)
       >>std::quoted(input.code)>>input.grams>>std::quoted(product.code)>>product.grams
       >>std::quoted(scrap.code)>>scrap.grams>>tier>>std::quoted(s.machine.operation)
       >>powered>>staffed>>maintained>>s.state.feed.capacity_grams
       >>s.state.finished.capacity_grams>>s.state.scrap.capacity_grams
       >>feed>>finished>>waste>>wip>>rem>>reserved
       >>s.state.elapsed_ticks>>s.state.completed_batches>>s.initial_mass))
        throw std::runtime_error("truncated factory checkpoint");
    in>>std::ws;if(!in.eof()||tier<1||tier>5||powered<0||powered>1||
       staffed<0||staffed>1||maintained<0||maintained>1||wip<0||wip>1||
       feed<0||finished<0||waste<0||reserved<0)
        throw std::runtime_error("malformed factory checkpoint");
    s.recipe.inputs={input};s.recipe.product=product;s.recipe.scrap=scrap;
    s.machine.tier=static_cast<unsigned>(tier);s.machine.powered=powered==1;
    s.machine.staffed=staffed==1;s.machine.maintained=maintained==1;
    if(feed)s.state.feed.grams[input.code]=feed;
    if(finished)s.state.finished.grams[product.code]=finished;
    if(waste)s.state.scrap.grams[scrap.code]=waste;
    s.state.has_work_in_progress=wip==1;s.state.ticks_remaining=rem;
    if(reserved)s.state.reserved_inputs={{input.code,reserved}};
    complete(s);return s;
}
std::uint64_t current_world_gen(sqlite3* db,const DurableFactory& f,
                                bool* present=nullptr,std::uint64_t* logical_tick=nullptr){
    Q q(db,"SELECT generation,state FROM snapshots ORDER BY generation DESC LIMIT 1");
    if(q.next()!=SQLITE_ROW)throw std::runtime_error("factory world snapshot missing");
    const auto gen=integer_column(q,0);
    const auto* bytes=sqlite3_column_blob(q.stmt,1);
    const auto n=sqlite3_column_bytes(q.stmt,1);
    if(!bytes||n<1||n>16*1024*1024)throw std::runtime_error("invalid authoritative NPC snapshot");
    const auto sim=Simulation::from_snapshot(std::string(static_cast<const char*>(bytes),static_cast<std::size_t>(n)));
    bool all=true;
    for(auto id:f.workers){
        const auto it=sim.npc_records().find(id);
        if(it==sim.npc_records().end()||it->second.home_ship!=f.home_ship ||
           it->second.assigned_site!=f.home_ship)
            throw std::runtime_error("factory worker not in authoritative employment roster");
        if(it->second.transport_id||it->second.location!=f.home_ship)all=false;
    }
    if(present)*present=all;
    if(logical_tick)*logical_tick=sim.tick();
    return gen;
}
struct Stored {std::string raw;std::uint64_t revision=0;};
Stored get_cold(sqlite3* db,const std::string& scope){
    Q q(db,"SELECT payload,revision FROM sg_v50_factory WHERE scope=?1");q.str(1,scope);
    if(q.next()!=SQLITE_ROW)throw std::runtime_error("cold factory checkpoint missing");
    const Stored result{string_column(q,0),integer_column(q,1)};
    if(result.revision<1)throw std::runtime_error("invalid factory revision");
    return result;
}
struct Warm {std::string raw;std::uint64_t revision=0,generation=0;};
Warm get_warm(sqlite3* db,const std::string& scope){
    Q q(db,"SELECT payload,revision,generation FROM sg_warm_active WHERE scope=?1");q.str(1,scope);
    if(q.next()!=SQLITE_ROW)throw std::runtime_error("detailed factory checkpoint missing");
    return {string_column(q,0),integer_column(q,1),integer_column(q,2)};
}
void update_cold(sqlite3* db,const std::string& scope,const Stored& before,const std::string& after){
    Q q(db,"UPDATE sg_v50_factory SET payload=?3,revision=revision+1 "
        "WHERE scope=?1 AND revision=?2 AND payload=?4");
    q.str(1,scope);q.num(2,before.revision);q.str(3,after);q.str(4,before.raw);q.done();
    if(sqlite3_changes(db)!=1)throw std::runtime_error("factory CAS rejected stale material checkpoint");
}
void add_event(sqlite3* db,const std::string& scope,const std::string& id,
    const std::string& kind,const std::string& before,const std::string& after,
    std::uint64_t ticks,bool staffed,std::uint64_t generation,std::uint64_t owner_gen){
    label(id);
    Q q(db,"INSERT INTO sg_v50_factory_event "
      "(scope,seq,event_id,kind,before_payload,after_payload,delta_ticks,staffed,world_generation,owner_generation) "
      "VALUES (?1,(SELECT COALESCE(MAX(seq),0)+1 FROM sg_v50_factory_event WHERE scope=?1),"
      "?2,?3,?4,?5,?6,?7,?8,?9)");
    q.str(1,scope);q.str(2,id);q.str(3,kind);q.str(4,before);q.str(5,after);
    q.num(6,ticks);q.num(7,staffed?1:0);q.num(8,generation);q.num(9,owner_gen);q.done();
}
void failure(HandoffFailure fault,HandoffFailure point){
    if(fault==point)throw std::runtime_error("injected factory transaction crash");
}
void ensure_owner(sqlite3* db,const std::string& scope,int mode,
                  std::uint64_t expected_gen,std::uint64_t rev){
    Q q(db,"SELECT owner,phase,generation,checkpoint_revision FROM sg_regional_mode_authority WHERE scope=?1");
    q.str(1,scope);
    if(q.next()!=SQLITE_ROW||sqlite3_column_int(q.stmt,0)!=mode||
       sqlite3_column_int(q.stmt,1)!=0||integer_column(q,2)!=expected_gen||
       integer_column(q,3)!=rev)
        throw std::runtime_error("stale factory region ownership fence");
}
std::string factory_scope(const regional_events::CheckpointStore& store){return store.scope();}
}
RegionalFactoryHandoff::RegionalFactoryHandoff(PersistentWorld& w,
    regional_events::CheckpointStore& region,regional_events::RegionalModeAuthority& authority,
    std::string path,std::string colony,std::vector<std::string> family_scopes)
    :world_(w),region_(region),authority_(authority),colony_(std::move(colony)),
     families_(std::move(family_scopes)){
    label(colony_);
    if(families_.empty()||families_.size()>32||
       !std::is_sorted(families_.begin(),families_.end())||
       std::adjacent_find(families_.begin(),families_.end())!=families_.end())
        throw std::invalid_argument("incomplete factory census registry scopes");
    if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db_)sqlite3_close(db_);
        db_=nullptr;
        throw std::runtime_error("factory database cannot open");
    }
    try{
        sql(db_,"PRAGMA busy_timeout=3000");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_v50_factory("
            "scope TEXT PRIMARY KEY,payload TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),"
            "source_receipt TEXT NOT NULL UNIQUE);");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_warm_active("
            "scope TEXT PRIMARY KEY,payload TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),"
            "generation INTEGER NOT NULL CHECK(generation>0));");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_v50_factory_event("
            "scope TEXT NOT NULL,seq INTEGER NOT NULL CHECK(seq>0),event_id TEXT NOT NULL,"
            "kind TEXT NOT NULL,before_payload TEXT NOT NULL,after_payload TEXT NOT NULL,"
            "delta_ticks INTEGER NOT NULL CHECK(delta_ticks>=0),"
            "staffed INTEGER NOT NULL CHECK(staffed IN (0,1)),"
            "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
            "owner_generation INTEGER NOT NULL CHECK(owner_generation>0),"
            "PRIMARY KEY(scope,seq),UNIQUE(scope,event_id));");
        sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_v50_factory_event_immutable_update "
            "BEFORE UPDATE ON sg_v50_factory_event BEGIN SELECT RAISE(ABORT,'immutable industry effect');END;");
        sql(db_,"CREATE TRIGGER IF NOT EXISTS sg_v50_factory_event_immutable_delete "
            "BEFORE DELETE ON sg_v50_factory_event BEGIN SELECT RAISE(ABORT,'immutable industry effect');END;");
        Q installed(db_,"SELECT 1 FROM sg_v50_factory WHERE scope=?1");
        installed.str(1,region_.scope());
        if(installed.next()==SQLITE_ROW)verify();
    }catch(...){sqlite3_close(db_);db_=nullptr;throw;}
}
RegionalFactoryHandoff::~RegionalFactoryHandoff(){if(db_)sqlite3_close(db_);}
void RegionalFactoryHandoff::preflight(const regional_events::RegionalAuthorityState& owner,
                                       bool stable)const {
    if(stable&&owner.phase!=regional_events::TransitionPhase::Stable)
        throw std::runtime_error("factory ownership handoff pending");
    const auto store=region_.load();
    if(!store||store->revision!=owner.checkpoint_revision||
       store->scheduler.colony(colony_).population==0)
        throw std::runtime_error("factory lacks real regional population/checkpoint");
    auto cold=get_cold(db_,factory_scope(region_));
    const auto factory=decode(cold.raw);
    (void)verify_regional_population(world_,sqlite3_db_filename(db_,"main"),
        factory.home_ship,families_,region_.scope(),colony_,
        owner.checkpoint_revision,owner.generation,false);
}
void RegionalFactoryHandoff::bootstrap(const DurableFactory& initial,const std::string& receipt){
    label(receipt);const auto raw=encode(initial);
    const auto owner=authority_.state();const auto checkpoint=region_.load();
    if(owner.owner!=regional_events::SimulationOwner::Cold||
       owner.phase!=regional_events::TransitionPhase::Stable||!checkpoint||
       owner.checkpoint_revision!=checkpoint->revision||
       initial.state.elapsed_ticks!=checkpoint->scheduler.tick())
        throw std::runtime_error("factory bootstrap needs stable cold boundary");
    (void)verify_regional_population(world_,sqlite3_db_filename(db_,"main"),
        initial.home_ship,families_,region_.scope(),colony_,
        owner.checkpoint_revision,owner.generation,true);
    Tx tx(db_);ensure_owner(db_,region_.scope(),0,owner.generation,owner.checkpoint_revision);
    // v51 reciprocal stock-receipt/region fence: legacy single-machine owners
    // may not duplicate a warehouse source receipt into another factory.
    {Q table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v51_network'");
     if(table.next()==SQLITE_ROW){
         Q used(db_,"SELECT 1 FROM sg_v51_network WHERE scope=?1 OR stock_receipt=?2");
         used.str(1,region_.scope());used.str(2,receipt);
         if(used.next()==SQLITE_ROW)
             throw std::runtime_error("warehouse stock already committed to a production network");
     }}
    bool on_site=false;const auto gen=current_world_gen(db_,initial,&on_site);
    if(!on_site)throw std::runtime_error("factory bootstrap missing working employees");
    {Q q(db_,"INSERT INTO sg_v50_factory(scope,payload,revision,source_receipt) VALUES(?1,?2,1,?3)");
     q.str(1,region_.scope());q.str(2,raw);q.str(3,receipt);q.done();}
    add_event(db_,region_.scope(),receipt,"seed","",raw,0,true,gen,owner.generation);
    tx.commit();
}
DurableFactory RegionalFactoryHandoff::active()const{
    const auto owner=authority_.state();
    if(owner.owner==regional_events::SimulationOwner::Detailed)
        return decode(get_warm(db_,region_.scope()).raw);
    return decode(get_cold(db_,region_.scope()).raw);
}
std::uint64_t RegionalFactoryHandoff::advance_cold(std::uint64_t ticks,
    const std::string& id,HandoffFailure fault){
    label(id);
    if(ticks==0||ticks>1000000)throw std::invalid_argument("bounded factory cold tick required");
    const auto owner=authority_.state();
    if(owner.owner!=regional_events::SimulationOwner::Cold)throw std::runtime_error("not cold owner");
    preflight(owner);
    { // Exact idempotent retry must not demand new world ticks.
        Q prior(db_,"SELECT actor,request,outcome FROM sg_regional_command_receipt "
              "WHERE scope=?1 AND command_id=?2");
        prior.str(1,region_.scope());prior.str(2,id);
        if(prior.next()==SQLITE_ROW){
            if(string_column(prior,0)!="factory-host"||
               string_column(prior,1)!="industrial-cold-v50:"+std::to_string(ticks))
                throw std::runtime_error("factory command ID collision");
            const auto recorded=integer_column(prior,2);
            verify();return recorded;
        }
    }
    std::uint64_t result=0;
    std::uint64_t predicted=0;
    {
        auto datum=decode(get_cold(db_,region_.scope()).raw);
        bool available=false;std::uint64_t world_tick=0;
        (void)current_world_gen(db_,datum,&available,&world_tick);
        if(datum.state.elapsed_ticks>world_tick||ticks>world_tick-datum.state.elapsed_ticks)
            throw std::runtime_error("cold factory cannot advance ahead of authoritative world");
        QuantifiedLine forecast(datum.recipe,datum.machine,datum.state);
        forecast.set_staff(available);
        predicted=forecast.advance(ticks).completed_batches;
    }
    const auto receipt=region_.commit_command(id,"factory-host",
        "industrial-cold-v50:"+std::to_string(ticks),owner.checkpoint_revision,
        [&](regional_events::Scheduler& scheduler){
            if(scheduler.tick()>max_i-ticks)throw std::overflow_error("regional tick overflow");
            scheduler.advance_to(scheduler.tick()+ticks);return predicted;
        },
        [&](sqlite3* db,const regional_events::Scheduler& prior,
            const regional_events::Scheduler& after,std::uint64_t){
            ensure_owner(db,region_.scope(),0,owner.generation,owner.checkpoint_revision);
            const auto before=get_cold(db,region_.scope());
            auto data=decode(before.raw);
            if(data.state.elapsed_ticks!=prior.tick()||after.tick()!=prior.tick()+ticks)
                throw std::runtime_error("factory cold clock mismatch");
            bool available=false;std::uint64_t world_tick=0;
            const auto gen=current_world_gen(db,data,&available,&world_tick);
            if(after.tick()>world_tick)
                throw std::runtime_error("industry scheduler outran authoritative world");
            QuantifiedLine line(data.recipe,data.machine,data.state);
            line.set_staff(available);
            result=line.advance(ticks).completed_batches;
            if(result!=predicted)throw std::runtime_error("factory production changed during commit");
            data.state=line.state();data.machine=line.machine();
            const auto next=encode(data);
            update_cold(db,region_.scope(),before,next);
            failure(fault,HandoffFailure::AfterFactoryWrite);
            add_event(db,region_.scope(),id,"cold",before.raw,next,ticks,available,gen,owner.generation);
            failure(fault,HandoffFailure::AfterEventWrite);
        },true,owner.generation,0);
    if(receipt.replayed)return receipt.outcome; // regional duplicate replay only
    verify();return result;
}
regional_events::RegionalAuthorityState RegionalFactoryHandoff::promote(const std::string& id,
    HandoffFailure fault){
    label(id);auto owner=authority_.state();
    if(owner.owner!=regional_events::SimulationOwner::Cold)throw std::runtime_error("not cold");
    if(owner.phase==regional_events::TransitionPhase::Stable){
        preflight(owner);
        owner=authority_.prepare(id,regional_events::SimulationOwner::Detailed,
            owner.generation,owner.checkpoint_revision,owner.boundary_tick);
    }else if(owner.phase!=regional_events::TransitionPhase::ToDetailed||owner.handoff_id!=id)
        throw std::runtime_error("different promotion pending");
    return authority_.commit(id,owner.generation,owner.checkpoint_revision,
        [&](sqlite3* db,regional_events::Scheduler& sched){
            const auto before=get_cold(db,region_.scope());const auto data=decode(before.raw);
            if(data.state.elapsed_ticks!=sched.tick())throw std::runtime_error("cold factory clock stale");
            const auto gen=current_world_gen(db,data);
            {Q q(db,"INSERT INTO sg_warm_active(scope,payload,revision,generation) VALUES(?1,?2,1,?3)");
             q.str(1,region_.scope());q.str(2,before.raw);q.num(3,owner.generation+1);q.done();}
            failure(fault,HandoffFailure::AfterFactoryWrite);
            add_event(db,region_.scope(),id,"promote",before.raw,before.raw,0,
                data.machine.staffed,gen,owner.generation+1);
            failure(fault,HandoffFailure::AfterEventWrite);
        });
}
std::uint64_t RegionalFactoryHandoff::advance_detailed(std::uint64_t ticks,
    const std::string& id,HandoffFailure fault){
    label(id);if(ticks==0||ticks>1000000)throw std::invalid_argument("bounded detailed tick required");
    const auto owner=authority_.state();
    if(owner.owner!=regional_events::SimulationOwner::Detailed)throw std::runtime_error("not detailed");
    preflight(owner);
    Tx tx(db_);ensure_owner(db_,region_.scope(),1,owner.generation,owner.checkpoint_revision);
    const auto before=get_warm(db_,region_.scope());
    if(before.generation!=owner.generation)throw std::runtime_error("stale warm worker generation");
    auto data=decode(before.raw);
    bool available=false;std::uint64_t world_tick=0;
    const auto gen=current_world_gen(db_,data,&available,&world_tick);
    if(data.state.elapsed_ticks>world_tick||ticks>world_tick-data.state.elapsed_ticks)
        throw std::runtime_error("detailed factory cannot run ahead of authoritative world");
    QuantifiedLine line(data.recipe,data.machine,data.state);line.set_staff(available);
    const auto result=line.advance(ticks).completed_batches;
    data.state=line.state();data.machine=line.machine();const auto next=encode(data);
    {Q q(db_,"UPDATE sg_warm_active SET payload=?3,revision=revision+1 "
        "WHERE scope=?1 AND revision=?2 AND generation=?4 AND payload=?5");
     q.str(1,region_.scope());q.num(2,before.revision);q.str(3,next);
     q.num(4,owner.generation);q.str(5,before.raw);q.done();
     if(sqlite3_changes(db_)!=1)throw std::runtime_error("stale detailed factory revision");}
    failure(fault,HandoffFailure::AfterFactoryWrite);
    add_event(db_,region_.scope(),id,"detailed",before.raw,next,ticks,available,gen,owner.generation);
    failure(fault,HandoffFailure::AfterEventWrite);
    tx.commit();verify();return result;
}
regional_events::RegionalAuthorityState RegionalFactoryHandoff::demote(const std::string& id,
    HandoffFailure fault){
    label(id);auto owner=authority_.state();
    if(owner.owner!=regional_events::SimulationOwner::Detailed)throw std::runtime_error("not detailed");
    if(owner.phase==regional_events::TransitionPhase::Stable){
        preflight(owner);
        owner=authority_.prepare(id,regional_events::SimulationOwner::Cold,
            owner.generation,owner.checkpoint_revision,owner.boundary_tick);
    }else if(owner.phase!=regional_events::TransitionPhase::ToCold||owner.handoff_id!=id)
        throw std::runtime_error("different demotion pending");
    return authority_.commit(id,owner.generation,owner.checkpoint_revision,
        [&](sqlite3* db,regional_events::Scheduler& sched){
            const auto previous=get_cold(db,region_.scope());
            const auto warm=get_warm(db,region_.scope());
            if(warm.generation!=owner.generation)
                throw std::runtime_error("detailed generation changed during flush");
            const auto active=decode(warm.raw),old=decode(previous.raw);
            if(old.state.elapsed_ticks!=sched.tick()||
               active.state.elapsed_ticks<sched.tick())
                throw std::runtime_error("detailed factory timeline not reconciled");
            std::uint64_t world_tick=0;
            const auto gen=current_world_gen(db,active,nullptr,&world_tick);
            if(active.state.elapsed_ticks>world_tick)
                throw std::runtime_error("detailed reconciliation outran authoritative world");
            sched.advance_to(active.state.elapsed_ticks);
            update_cold(db,region_.scope(),previous,warm.raw);
            {Q q(db,"DELETE FROM sg_warm_active WHERE scope=?1 AND generation=?2 AND revision=?3");
             q.str(1,region_.scope());q.num(2,warm.generation);q.num(3,warm.revision);q.done();
             if(sqlite3_changes(db)!=1)throw std::runtime_error("warm rev changed during demotion");}
            failure(fault,HandoffFailure::AfterFactoryWrite);
            add_event(db,region_.scope(),id,"demote",warm.raw,warm.raw,0,
                active.machine.staffed,gen,owner.generation+1);
            failure(fault,HandoffFailure::AfterEventWrite);
        });
}
void RegionalFactoryHandoff::verify()const{
    const auto owner=authority_.state();const auto region=region_.load();
    if(!region||region->revision!=owner.checkpoint_revision)
        throw std::runtime_error("factory region checkpoint/owner mismatch");
    const auto cold=get_cold(db_,region_.scope());const auto cold_data=decode(cold.raw);
    if(cold_data.state.elapsed_ticks>world_.world().tick())
        throw std::runtime_error("factory checkpoint ahead of canonical world tick");
    if(cold_data.state.elapsed_ticks!=region->scheduler.tick())
        throw std::runtime_error("factory cold checkpoint timeline detached from region");
    std::string current;std::uint64_t expected_seq=1;
    Q events(db_,"SELECT seq,kind,before_payload,after_payload,delta_ticks,staffed,"
        "world_generation,owner_generation FROM sg_v50_factory_event WHERE scope=?1 ORDER BY seq");
    events.str(1,region_.scope());
    while(events.next()==SQLITE_ROW){
        if(integer_column(events,0)!=expected_seq++)throw std::runtime_error("missing factory event");
        const auto kind=string_column(events,1),before=string_column(events,2),after=string_column(events,3);
        const auto ticks=integer_column(events,4),staffed=integer_column(events,5),world_gen=integer_column(events,6);
        const auto own=integer_column(events,7);
        if(staffed>1||!world_gen||world_gen>world_.generation()||!own||before!=current)
            throw std::runtime_error("factory effect chain broken");
        if(kind=="seed"){
            if(expected_seq!=2||!before.empty()||ticks!=0)throw std::runtime_error("invalid factory seeding");
            (void)decode(after);
        }else if(kind=="cold"||kind=="detailed"){
            if(!ticks||ticks>1000000)throw std::runtime_error("bad factory effect tick");
            auto old=decode(before);QuantifiedLine line(old.recipe,old.machine,old.state);
            line.set_staff(staffed==1);line.advance(ticks);
            old.machine=line.machine();old.state=line.state();
            if(encode(old)!=after)throw std::runtime_error("factory effects do not replay deterministically");
        }else if(kind=="promote"||kind=="demote"){
            if(ticks||before!=after)throw std::runtime_error("handoff invented material");
        }else throw std::runtime_error("unknown factory effect kind");
        current=after;
    }
    if(expected_seq<=1)throw std::runtime_error("factory lacks genesis material event");
    if(owner.owner==regional_events::SimulationOwner::Detailed){
        const auto warm=get_warm(db_,region_.scope());
        if(warm.generation!=owner.generation||warm.raw!=current||
           decode(warm.raw).state.elapsed_ticks<cold_data.state.elapsed_ticks)
            throw std::runtime_error("detailed factory head/owner mismatch");
    }else{
        if(current!=cold.raw)throw std::runtime_error("cold factory lost committed effect");
        Q q(db_,"SELECT 1 FROM sg_warm_active WHERE scope=?1");q.str(1,region_.scope());
        if(q.next()!=SQLITE_DONE)throw std::runtime_error("orphaned detailed factory remains");
    }
}
} // namespace galaxy::industry
