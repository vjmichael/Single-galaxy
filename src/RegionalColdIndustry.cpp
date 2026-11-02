#include "galaxy/RegionalColdIndustry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::cold_industry {
namespace {
constexpr std::int64_t kMax=std::numeric_limits<std::int64_t>::max();
constexpr std::uint64_t kTicksPerHour=18'000;
void sql(sqlite3* db,const char* command){
    char* err=nullptr;
    if(sqlite3_exec(db,command,nullptr,nullptr,&err)!=SQLITE_OK){
        std::string why=err?err:sqlite3_errmsg(db);sqlite3_free(err);
        throw std::runtime_error("cold industry SQLite: "+why);
    }
}
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::int64_t add(std::int64_t a,std::int64_t b){
    if(a<0||b<0||a>kMax-b)throw std::overflow_error("cold industry quantity overflow");
    return a+b;
}
void validate(const Goods& g,bool nonzero=true){
    if(g.food<0||g.water<0||g.oxygen<0||g.ore<0||g.metal<0||g.parts<0)
        throw std::invalid_argument("negative freight stock");
    if(nonzero&&!g.mass())throw std::invalid_argument("empty freight");
}
void validate(const Cargo& g){
    if(g.food_kg<0||g.water_kg<0||g.oxygen_kg<0)
        throw std::invalid_argument("negative life support stock");
}
void auth(const HostAuthorization& fn,const std::string& actor,const std::string& colony){
    if(actor.empty()||!fn(actor,colony))throw std::runtime_error("cold host authorization denied");
}
struct Query{
    sqlite3* db=nullptr;sqlite3_stmt* q=nullptr;
    Query(sqlite3* d,const char* source):db(d){
        if(sqlite3_prepare_v2(db,source,-1,&q,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("cold prepare: ")+sqlite3_errmsg(db));
    }
    ~Query(){sqlite3_finalize(q);}
    void txt(int i,const std::string& s){
        check(sqlite3_bind_text(q,i,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT)==SQLITE_OK,"text bind failed");
    }
    void num(int i,std::int64_t n){check(sqlite3_bind_int64(q,i,n)==SQLITE_OK,"integer bind failed");}
    void run(){check(sqlite3_step(q)==SQLITE_DONE,"cold SQL statement failed");}
    bool next(){const int rc=sqlite3_step(q);if(rc==SQLITE_ROW)return true;
        if(rc==SQLITE_DONE)return false;
        throw std::runtime_error(std::string("cold query: ")+sqlite3_errmsg(db));}
    std::int64_t n(int i)const{return sqlite3_column_int64(q,i);}
    std::string s(int i)const{const auto* t=sqlite3_column_text(q,i);return t?reinterpret_cast<const char*>(t):"";}
};
void txn(sqlite3* db,const std::function<void()>& fn){
    sql(db,"BEGIN IMMEDIATE");
    try{fn();sql(db,"COMMIT");}
    catch(...){try{sql(db,"ROLLBACK");}catch(...){}throw;}
}
std::string canonical(const std::string& type,const std::vector<std::string>& fields,
                      const std::vector<std::int64_t>& numbers){
    std::ostringstream ss;ss<<type<<'|';
    for(const auto& f:fields)ss<<f.size()<<':'<<f<<';';
    for(auto n:numbers)ss<<n<<';';
    return ss.str();
}
struct Location {std::string scope; std::int64_t x=0,y=0,z=0;};
Location locate(sqlite3* db,const std::string& colony){
    Query q(db,"SELECT scope,x_m,y_m,z_m FROM sg_cold_site WHERE colony=?1");q.txt(1,colony);
    check(q.next(),"unknown physical colony location");
    return {q.s(0),q.n(1),q.n(2),q.n(3)};
}
SiteState site_row(sqlite3* db,const std::string& scope,const std::string& colony){
    Query q(db,"SELECT deposit,ore,metal,parts,tailings,scrap,battery,battery_max,solar_per_hour,workforce,last_work_tick "
               "FROM sg_cold_site WHERE scope=?1 AND colony=?2");q.txt(1,scope);q.txt(2,colony);
    check(q.next(),"cold colony not initialized");
    SiteState s{q.n(0),q.n(1),q.n(2),q.n(3),q.n(4),q.n(5),q.n(6),q.n(7),q.n(8),q.n(9),
                static_cast<std::uint64_t>(q.n(10))};
    return s;
}
struct Manifest {Freight f;std::string source_scope,target_scope;};
Manifest find_freight(sqlite3* db,const std::string& id){
    Query q(db,"SELECT origin_scope,dest_scope,origin,destination,carrier,food,water,oxygen,ore,metal,parts,"
              "depart_tick,arrive_tick,return_tick,stage FROM sg_cold_freight WHERE freight_id=?1");q.txt(1,id);
    check(q.next(),"unknown freight manifest");
    Manifest r;
    r.source_scope=q.s(0);r.target_scope=q.s(1);
    r.f.id=id;r.f.origin=q.s(2);r.f.destination=q.s(3);r.f.carrier=q.s(4);
    r.f.cargo={q.n(5),q.n(6),q.n(7),q.n(8),q.n(9),q.n(10)};
    r.f.departed=static_cast<std::uint64_t>(q.n(11));
    r.f.arrives=static_cast<std::uint64_t>(q.n(12));
    r.f.returns=static_cast<std::uint64_t>(q.n(13));r.f.stage=static_cast<int>(q.n(14));
    return r;
}
std::uint64_t to_tick(std::int64_t raw){
    if(raw<0)throw std::runtime_error("negative persisted time");
    return static_cast<std::uint64_t>(raw);
}
std::int64_t as_sqltick(std::uint64_t t){
    if(t>static_cast<std::uint64_t>(kMax))throw std::overflow_error("simulation tick outside SQLite domain");
    return static_cast<std::int64_t>(t);
}
}
std::int64_t Goods::mass() const {
    return add(add(add(food,water),add(oxygen,ore)),add(metal,parts));
}
Economy::Economy(CheckpointStore& store,const std::string& file,HostAuthorization authorize)
 :store_(store),scope_(store.scope()),authorize_(std::move(authorize)){
    if(file.empty()||!authorize_)throw std::invalid_argument("cold industry requires database and host policy");
    if(sqlite3_open_v2(file.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        sqlite3_close(db_);db_=nullptr;throw std::runtime_error("could not open cold industry ledger");
    }
    try{
        sqlite3_busy_timeout(db_,3000);
        // Do not accept a different database path for the industry sidecar.
        // The checkpoint and industry effect must share one SQLite transaction.
        {
            Query probe(db_,"SELECT revision FROM sg_regional_checkpoint WHERE scope=?1");
            probe.txt(1,scope_);
            check(probe.next(),"cold economy must share a seeded regional SQLite database");
        }
        sql(db_,"PRAGMA journal_mode=WAL");sql(db_,"PRAGMA synchronous=FULL");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_cold_site("
                "scope TEXT NOT NULL,colony TEXT NOT NULL UNIQUE,x_m INTEGER NOT NULL,y_m INTEGER NOT NULL,z_m INTEGER NOT NULL,"
                "deposit INTEGER NOT NULL CHECK(deposit>=0),ore INTEGER NOT NULL CHECK(ore>=0),"
                "metal INTEGER NOT NULL CHECK(metal>=0),parts INTEGER NOT NULL CHECK(parts>=0),"
                "tailings INTEGER NOT NULL CHECK(tailings>=0),scrap INTEGER NOT NULL CHECK(scrap>=0),"
                "battery INTEGER NOT NULL CHECK(battery>=0),battery_max INTEGER NOT NULL CHECK(battery_max>=0),"
                "solar_per_hour INTEGER NOT NULL CHECK(solar_per_hour>=0),workforce INTEGER NOT NULL CHECK(workforce>0),"
                "last_work_tick INTEGER NOT NULL,PRIMARY KEY(scope,colony))");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_cold_plan("
                "scope TEXT NOT NULL,colony TEXT NOT NULL,mine INTEGER NOT NULL CHECK(mine>=0),"
                "farm INTEGER NOT NULL CHECK(farm>=0),organic INTEGER NOT NULL CHECK(organic>=0),"
                "water INTEGER NOT NULL CHECK(water>=0),oxygen INTEGER NOT NULL CHECK(oxygen>=0),"
                "smelt INTEGER NOT NULL CHECK(smelt>=0),fabricate INTEGER NOT NULL CHECK(fabricate>=0),"
                "feed INTEGER NOT NULL CHECK(feed>=0),hydrate INTEGER NOT NULL CHECK(hydrate>=0),"
                "breathe INTEGER NOT NULL CHECK(breathe>=0),PRIMARY KEY(scope,colony))");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_cold_carrier("
                "id TEXT PRIMARY KEY,scope TEXT NOT NULL,home TEXT NOT NULL,"
                "capacity INTEGER NOT NULL CHECK(capacity>0),speed INTEGER NOT NULL CHECK(speed>0),"
                "fuel INTEGER NOT NULL CHECK(fuel>=0),crew INTEGER NOT NULL CHECK(crew>=2),"
                "busy_until INTEGER NOT NULL DEFAULT 0)");
        sql(db_,"CREATE TABLE IF NOT EXISTS sg_cold_freight("
                "freight_id TEXT PRIMARY KEY,origin_scope TEXT NOT NULL,dest_scope TEXT NOT NULL,"
                "origin TEXT NOT NULL,destination TEXT NOT NULL,carrier TEXT NOT NULL,"
                "food INTEGER NOT NULL CHECK(food>=0),water INTEGER NOT NULL CHECK(water>=0),"
                "oxygen INTEGER NOT NULL CHECK(oxygen>=0),ore INTEGER NOT NULL CHECK(ore>=0),"
                "metal INTEGER NOT NULL CHECK(metal>=0),parts INTEGER NOT NULL CHECK(parts>=0),"
                "depart_tick INTEGER NOT NULL,arrive_tick INTEGER NOT NULL,return_tick INTEGER NOT NULL,"
                "stage INTEGER NOT NULL CHECK(stage IN (0,1)),"
                "CHECK(arrive_tick>depart_tick AND return_tick>arrive_tick))");
        sql(db_,"CREATE INDEX IF NOT EXISTS sg_cold_freight_due ON sg_cold_freight(stage,arrive_tick)");
    }catch(...){sqlite3_close(db_);db_=nullptr;throw;}
}
Economy::~Economy(){if(db_)sqlite3_close(db_);}
void Economy::seed(const SiteSeed& cfg){
    if(cfg.colony.empty()||cfg.deposit_ore_kg<0||cfg.battery_kwh<0||cfg.battery_max_kwh<cfg.battery_kwh||
       cfg.solar_kwh_per_hour<0||cfg.workforce<1)throw std::invalid_argument("invalid cold site seed");
    const auto cp=store_.load();
    check(cp&&cp->scheduler.colony(cfg.colony).population>=static_cast<std::uint64_t>(cfg.workforce),
          "seed must reference existing physical population cohort");
    txn(db_,[&]{
        Query q(db_,"INSERT INTO sg_cold_site(scope,colony,x_m,y_m,z_m,deposit,ore,metal,parts,tailings,scrap,"
                    "battery,battery_max,solar_per_hour,workforce,last_work_tick)"
                    "VALUES (?1,?2,?3,?4,?5,?6,0,0,0,0,0,?7,?8,?9,?10,?11)");
        q.txt(1,scope_);q.txt(2,cfg.colony);q.num(3,cfg.x_m);q.num(4,cfg.y_m);q.num(5,cfg.z_m);
        q.num(6,cfg.deposit_ore_kg);q.num(7,cfg.battery_kwh);q.num(8,cfg.battery_max_kwh);
        q.num(9,cfg.solar_kwh_per_hour);q.num(10,cfg.workforce);q.num(11,as_sqltick(cp->scheduler.tick()));q.run();
    });
}
void Economy::seed_carrier(const CarrierSeed& c){
    if(c.id.empty()||c.home.empty()||c.capacity_kg<=0||c.speed_m_per_hour<=0||c.fuel_units<0||c.crew<2)
        throw std::invalid_argument("invalid carrier seed");
    const auto home=locate(db_,c.home);
    check(home.scope==scope_,"carrier assigned to another physical region");
    txn(db_,[&]{
        Query q(db_,"INSERT INTO sg_cold_carrier(id,scope,home,capacity,speed,fuel,crew,busy_until) "
                    "VALUES(?1,?2,?3,?4,?5,?6,?7,0)");
        q.txt(1,c.id);q.txt(2,scope_);q.txt(3,c.home);q.num(4,c.capacity_kg);
        q.num(5,c.speed_m_per_hour);q.num(6,c.fuel_units);q.num(7,c.crew);q.run();
    });
}
void Economy::set_initial_plan(const std::string& colony,const AutoPlan& p){
    const auto s=site(colony);
    if(s.last_work_tick!=0)throw std::runtime_error("cannot rewrite active automated NPC work policy");
    const std::array<std::int64_t,10> ns={p.mine_kg,p.harvest_food_kg,p.recycle_organic_kg,
       p.reclaim_water_kg,p.reclaim_oxygen_kg,p.smelt_ore_kg,p.fabricate_metal_kg,
       p.habitat_supply.food_kg,p.habitat_supply.water_kg,p.habitat_supply.oxygen_kg};
    for(const auto n:ns)if(n<0)throw std::invalid_argument("negative automated work policy");
    txn(db_,[&]{
       Query q(db_,"INSERT INTO sg_cold_plan(scope,colony,mine,farm,organic,water,oxygen,smelt,"
                   "fabricate,feed,hydrate,breathe) VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
       q.txt(1,scope_);q.txt(2,colony);
       for(int i=0;i<10;++i)q.num(i+3,ns.at(static_cast<std::size_t>(i)));
       q.run();
    });
}
std::uint64_t Economy::advance_autonomous(const std::string& actor,std::uint64_t until,
                                           std::uint64_t owner_generation){
    if(until>static_cast<std::uint64_t>(kMax))throw std::invalid_argument("invalid autonomous horizon");
    // Read fixed plant policies, never invent an assignment for a missing site.
    struct Planned {std::string id;AutoPlan plan;};
    std::vector<Planned> policies;
    {
      Query q(db_,"SELECT colony,mine,farm,organic,water,oxygen,smelt,fabricate,feed,hydrate,breathe "
                  "FROM sg_cold_plan WHERE scope=?1 ORDER BY colony");q.txt(1,scope_);
      while(q.next()){
        Planned p;p.id=q.s(0);p.plan={q.n(1),q.n(2),q.n(3),q.n(4),q.n(5),q.n(6),q.n(7),
                                        {q.n(8),q.n(9),q.n(10)}};
        policies.push_back(p);
      }
    }
    if(policies.empty())throw std::runtime_error("autonomous region has no work policies");
    std::uint64_t count=0;
    while(true){
      std::uint64_t earliest=std::numeric_limits<std::uint64_t>::max();
      std::vector<std::uint64_t> next;
      for(const auto& p:policies){
        const auto tick=site(p.id).last_work_tick;
        check(tick<=static_cast<std::uint64_t>(kMax-4500),"industry time overflow");
        const auto due=tick+4500;
        next.push_back(due);earliest=std::min(earliest,due);
      }
      if(earliest>until)break;
      for(std::size_t i=0;i<policies.size();++i){
        if(next.at(i)!=earliest)continue;
        const auto& p=policies[i];const auto cp=store_.load();
        check(bool(cp),"no regional checkpoint for autonomous work");
        const auto& life=cp->scheduler.colony(p.id).habitat;
        Work w;w.actor=actor;w.colony=p.id;w.tick=earliest;
        w.command_id="autoshift:"+scope_+":"+p.id+":"+std::to_string(earliest);
        w.mine_kg=p.plan.mine_kg;w.harvest_food_kg=p.plan.harvest_food_kg;
        w.smelt_ore_kg=p.plan.smelt_ore_kg;w.fabricate_metal_kg=p.plan.fabricate_metal_kg;
        w.reclaim={std::min(p.plan.recycle_organic_kg,life.organic_waste_kg),
                   std::min(p.plan.reclaim_water_kg,life.wastewater_kg),
                   std::min(p.plan.reclaim_oxygen_kg,life.co2_kg)};
        w.habitat_supply=p.plan.habitat_supply;
        const auto r=work(w,cp->revision,owner_generation);
        if(!r.replayed)++count;
      }
    }
    return count;
}
SiteState Economy::site(const std::string& colony) const{return site_row(db_,scope_,colony);}
std::vector<Freight> Economy::freight() const{
    Query q(db_,"SELECT freight_id FROM sg_cold_freight ORDER BY depart_tick,freight_id");
    std::vector<std::string> keys;
    while(q.next())keys.push_back(q.s(0));
    std::vector<Freight> out;out.reserve(keys.size());
    for(const auto& key:keys)out.push_back(find_freight(db_,key).f);
    return out;
}
CommandReceipt Economy::work(const Work& w,std::uint64_t revision,std::uint64_t owner_generation){
    auth(authorize_,w.actor,w.colony);
    if(w.command_id.empty()||w.colony.empty()||!w.tick||w.tick>=static_cast<std::uint64_t>(kMax)||
       w.mine_kg<0||w.harvest_food_kg<0||w.smelt_ore_kg<0||w.fabricate_metal_kg<0)
        throw std::invalid_argument("invalid cold work order");
    validate(w.reclaim);validate(w.habitat_supply);
    const auto canonical_request=canonical("work",{w.colony},{as_sqltick(w.tick),w.mine_kg,w.harvest_food_kg,
               w.reclaim.food_kg,w.reclaim.water_kg,w.reclaim.oxygen_kg,w.smelt_ore_kg,
               w.fabricate_metal_kg,w.habitat_supply.food_kg,w.habitat_supply.water_kg,w.habitat_supply.oxygen_kg});
    return store_.commit_command(w.command_id,w.actor,canonical_request,revision,
        [&](regional_events::Scheduler& s)->std::uint64_t{
            s.advance_to(w.tick);
            s.cold_life_work(w.colony,w.harvest_food_kg,w.reclaim);
            if(w.habitat_supply.food_kg||w.habitat_supply.water_kg||w.habitat_supply.oxygen_kg)
                s.schedule_habitat_delivery(w.colony,w.habitat_supply,w.tick+1);
            return 0;
        },[&](sqlite3* db,const regional_events::Scheduler&,const regional_events::Scheduler&,std::uint64_t){
            auto state=site_row(db,scope_,w.colony);
            check(w.tick>state.last_work_tick,"zero or reversed industry interval");
            const auto dt=w.tick-state.last_work_tick;
            // Each facility runs at most one budgeted work batch per 15-min interval;
            // long intervals cannot silently multiply personnel through repeated calls.
            check(dt>=4500,"industry work ordered before next 15-minute shift");
            const auto tasks=add(add(w.mine_kg,w.harvest_food_kg),
                add(add(w.reclaim.food_kg,w.reclaim.water_kg),
                    add(w.reclaim.oxygen_kg,add(w.smelt_ore_kg,w.fabricate_metal_kg))));
            // 100 kg of physical processing per assigned worker per shift.
            check(state.workforce<=kMax/100,"unrepresentable NPC work budget");
            check(tasks<=state.workforce*100,"not enough NPC work assignments");
            // Electricity is harvested over elapsed logical time, capped in the
            // actual battery. A work batch consumes finite metered kWh.
            check(state.solar_per_hour<=kMax/static_cast<std::int64_t>(dt),"solar counter overflow");
            const auto generated=static_cast<std::int64_t>(state.solar_per_hour*static_cast<std::int64_t>(dt)/kTicksPerHour);
            const auto battery=std::min(state.battery_max,add(state.battery,generated));
            const auto cost=(tasks+19)/20;
            check(battery>=cost,"insufficient physically allocated power");
            check(state.deposit>=w.mine_kg,"mineral deposit depleted");
            const auto ore=add(state.ore,w.mine_kg);
            check(ore>=w.smelt_ore_kg,"unfunded smelting");
            const auto metal_yield=w.smelt_ore_kg-w.smelt_ore_kg/20; // 95% yield
            const auto metal=add(state.metal,metal_yield);
            check(metal>=w.fabricate_metal_kg,"unfunded manufacturing");
            const auto parts_yield=w.fabricate_metal_kg-w.fabricate_metal_kg/10; // 90%
            Query upd(db,"UPDATE sg_cold_site SET deposit=?1,ore=?2,metal=?3,parts=?4,tailings=?5,"
                         "scrap=?6,battery=?7,last_work_tick=?8 WHERE scope=?9 AND colony=?10 AND last_work_tick=?11");
            upd.num(1,state.deposit-w.mine_kg);upd.num(2,ore-w.smelt_ore_kg);
            upd.num(3,metal-w.fabricate_metal_kg);
            upd.num(4,add(state.parts,parts_yield));
            upd.num(5,add(state.tailings,w.smelt_ore_kg-metal_yield));
            upd.num(6,add(state.scrap,w.fabricate_metal_kg-parts_yield));
            upd.num(7,battery-cost);upd.num(8,as_sqltick(w.tick));
            upd.txt(9,scope_);upd.txt(10,w.colony);upd.num(11,as_sqltick(state.last_work_tick));
            upd.run();check(sqlite3_changes(db)==1,"stale industry work state");
        },false,owner_generation);
}
CommandReceipt Economy::dispatch(const std::string& command_id,const std::string& actor,
                 const std::string& freight_id,const std::string& ship,
                 const std::string& origin,const std::string& dest,Goods cargo,
                 std::uint64_t depart,std::uint64_t rev,std::uint64_t gen){
    auth(authorize_,actor,origin);validate(cargo);
    if(command_id.empty()||freight_id.empty()||ship.empty()||origin==dest||!depart)
        throw std::invalid_argument("invalid freight contract");
    const auto request=canonical("dispatch",{freight_id,ship,origin,dest},
          {cargo.food,cargo.water,cargo.oxygen,cargo.ore,cargo.metal,cargo.parts,as_sqltick(depart)});
    return store_.commit_command(command_id,actor,request,rev,
      [&](regional_events::Scheduler& s)->std::uint64_t{
          s.advance_to(depart);
          if(cargo.food||cargo.water||cargo.oxygen)
              s.debit_depot(origin,{cargo.food,cargo.water,cargo.oxygen});
          return 0;
      },[&](sqlite3* db,const regional_events::Scheduler&,const regional_events::Scheduler&,std::uint64_t){
          const auto from=locate(db,origin),to=locate(db,dest);
          check(from.scope==scope_,"source facility not in owning region");
          Query fleet(db,"SELECT home,capacity,speed,fuel,crew,busy_until FROM sg_cold_carrier WHERE id=?1 AND scope=?2");
          fleet.txt(1,ship);fleet.txt(2,scope_);
          check(fleet.next(),"physical carrier does not exist");
          check(fleet.s(0)==origin,"carrier is not at its home port");
          check(cargo.mass()<=fleet.n(1),"physical carrier over capacity");
          check(fleet.n(4)>=2,"carrier has insufficient crew");
          check(depart>=to_tick(fleet.n(5)),"carrier still en route or returning");
          const auto dx=static_cast<long double>(from.x)-to.x;
          const auto dy=static_cast<long double>(from.y)-to.y;
          const auto dz=static_cast<long double>(from.z)-to.z;
          const auto distance=std::sqrt(dx*dx+dy*dy+dz*dz);
          const auto leg=std::ceil(distance*static_cast<long double>(kTicksPerHour)/fleet.n(2));
          check(leg>0 && leg<3'600'000,"carrier route unsupported / too long");
          const auto travel=static_cast<std::uint64_t>(leg)+900; // physical dock/undock/transfer window
          check(depart<static_cast<std::uint64_t>(kMax)-3*travel,"carrier ETA overflow");
          const auto arrive=depart+travel,return_tick=arrive+travel+900;
          const auto fuelCost=static_cast<std::int64_t>(std::ceil(distance/1'000'000.0L))*200;
          check(fleet.n(3)>=fuelCost,"carrier has insufficient round-trip propellant");
          const auto prev=site_row(db,scope_,origin);
          check(prev.ore>=cargo.ore&&prev.metal>=cargo.metal&&prev.parts>=cargo.parts,
                "freight has no physical industrial inventory");
          Query site(db,"UPDATE sg_cold_site SET ore=ore-?1,metal=metal-?2,parts=parts-?3 "
                        "WHERE scope=?4 AND colony=?5 AND ore>=?1 AND metal>=?2 AND parts>=?3");
          site.num(1,cargo.ore);site.num(2,cargo.metal);site.num(3,cargo.parts);
          site.txt(4,scope_);site.txt(5,origin);site.run();check(sqlite3_changes(db)==1,"industrial cargo was concurrently withdrawn");
          Query vehicle(db,"UPDATE sg_cold_carrier SET fuel=fuel-?1,busy_until=?2 WHERE id=?3 "
                           "AND scope=?4 AND busy_until<=?5 AND fuel>=?1");
          vehicle.num(1,fuelCost);vehicle.num(2,as_sqltick(return_tick));
          vehicle.txt(3,ship);vehicle.txt(4,scope_);vehicle.num(5,as_sqltick(depart));vehicle.run();
          check(sqlite3_changes(db)==1,"carrier was concurrently assigned");
          Query f(db,"INSERT INTO sg_cold_freight(freight_id,origin_scope,dest_scope,origin,destination,carrier,"
                     "food,water,oxygen,ore,metal,parts,depart_tick,arrive_tick,return_tick,stage) "
                     "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,0)");
          f.txt(1,freight_id);f.txt(2,scope_);f.txt(3,to.scope);f.txt(4,origin);f.txt(5,dest);f.txt(6,ship);
          f.num(7,cargo.food);f.num(8,cargo.water);f.num(9,cargo.oxygen);
          f.num(10,cargo.ore);f.num(11,cargo.metal);f.num(12,cargo.parts);
          f.num(13,as_sqltick(depart));f.num(14,as_sqltick(arrive));
          f.num(15,as_sqltick(return_tick));f.run();
      },false,gen);
}
CommandReceipt Economy::deliver(const std::string& id,const std::string& actor,
                   const std::string& freight_id,std::uint64_t revision,std::uint64_t gen){
    if(id.empty()||freight_id.empty())throw std::invalid_argument("missing delivery identity");
    const auto m=find_freight(db_,freight_id);
    auth(authorize_,actor,m.f.destination);
    check(m.target_scope==scope_,"wrong region for carrier arrival");
    const auto request=canonical("deliver",{freight_id,m.f.destination},
                                   {as_sqltick(m.f.arrives)});
    return store_.commit_command(id,actor,request,revision,
        [&](regional_events::Scheduler& s)->std::uint64_t{
            check(s.tick()<=m.f.arrives,"late delivery would rewrite past population consumption");
            s.advance_to(m.f.arrives);
            const auto g=m.f.cargo;
            if(g.food||g.water||g.oxygen)
                s.credit_depot(m.f.destination,{g.food,g.water,g.oxygen});
            return 0;
        },[&](sqlite3* db,const regional_events::Scheduler&,const regional_events::Scheduler&,std::uint64_t){
            const auto actual=find_freight(db,freight_id);
            check(actual.f.stage==0&&actual.target_scope==scope_&&actual.f.arrives==m.f.arrives,
                  "replayed or forged physical arrival");
            Query q(db,"UPDATE sg_cold_site SET ore=ore+?1,metal=metal+?2,parts=parts+?3 "
                       "WHERE scope=?4 AND colony=?5 AND ore<=?6 AND metal<=?7 AND parts<=?8");
            q.num(1,m.f.cargo.ore);q.num(2,m.f.cargo.metal);q.num(3,m.f.cargo.parts);
            q.txt(4,scope_);q.txt(5,m.f.destination);
            q.num(6,kMax-m.f.cargo.ore);q.num(7,kMax-m.f.cargo.metal);q.num(8,kMax-m.f.cargo.parts);
            q.run();check(sqlite3_changes(db)==1,"destination industrial site missing or overflow");
            Query done(db,"UPDATE sg_cold_freight SET stage=1 WHERE freight_id=?1 AND stage=0 AND dest_scope=?2");
            done.txt(1,freight_id);done.txt(2,scope_);done.run();
            check(sqlite3_changes(db)==1,"freight was already delivered");
        },false,gen);
}
} // namespace galaxy::cold_industry
