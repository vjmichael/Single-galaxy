#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace galaxy::colony_safety {
namespace {
void exec(sqlite3* db,const char* sql){
    if(!db)throw std::invalid_argument("canonical SQLite writer required");
    char* error=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){
        std::string msg=error?error:"SQLite safety journal failure";
        sqlite3_free(error); throw std::runtime_error(msg);
    }
}
void bind(sqlite3_stmt* stmt,int i,std::uint64_t n){
    if(n>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
        throw std::overflow_error("colony safety event ID overflow");
    if(sqlite3_bind_int64(stmt,i,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
        throw std::runtime_error("colony safety SQLite bind failure");
}
}
Assessment assess(const Simulation& world,EntityId home_ship,EntityId site,
                  std::uint64_t warn,std::uint64_t critical){
    if(!home_ship||!site||!warn||critical>=warn)
        throw std::invalid_argument("invalid colony-safety assessment scope or thresholds");
    const auto* entity=world.find(home_ship);
    const auto* ship=world.colony_ship(home_ship);
    if(!entity||!ship||!entity->owner_id)
        throw std::invalid_argument("colony-safety home ship not found");
    const ColonyLifeSupport* stock=nullptr;
    std::uint64_t population=0;
    if(site==home_ship){
        stock=world.life_support(home_ship);
        if(ship->crew<0)throw std::logic_error("negative colony crew");
        // An embarked passenger is a real local consumer, not invisible cargo.
        // Only physically onboard named passengers count; native crew is
        // already represented by the authoritative aggregate headcount.
        const auto passengers=world.passenger_needs_enabled()?world.passengers_aboard(home_ship):0;
        if(passengers>std::numeric_limits<std::uint64_t>::max()-
                          static_cast<std::uint64_t>(ship->crew))
            throw std::overflow_error("life-safety population overflow");
        population=static_cast<std::uint64_t>(ship->crew)+passengers;
    }else if(const auto* habitat=world.habitat(site)){
        if(habitat->source_ship!=home_ship || habitat->residents<0)
            throw std::invalid_argument("foreign or invalid residential site");
        stock=&habitat->life;
        population=static_cast<std::uint64_t>(habitat->residents);
    }else if(const auto* farm=world.farm(site)){
        if(farm->source_ship!=home_ship || farm->workers<0)
            throw std::invalid_argument("foreign or invalid farm");
        stock=&farm->life;
        population=static_cast<std::uint64_t>(farm->workers);
    }else{
        throw std::invalid_argument("not a staffed life-support compartment");
    }
    if(!stock)throw std::invalid_argument("unprovisioned life-support compartment");
    Assessment a;
    a.site=site;a.home_ship=home_ship;a.owner=entity->owner_id;
    a.population=population;a.world_tick=world.tick();
    a.remaining=macro_needs::runway(*stock,population,world.tick());
    if(!population)return a;
    const auto least=std::min({a.remaining.food_periods,a.remaining.water_periods,
                              a.remaining.oxygen_periods});
    if(least==0 || (stock->shortages>0 && stock->consecutive_supplied_cycles==0))
        a.risk=Risk::Emergency;
    else if(least<=critical)a.risk=Risk::Critical;
    else if(least<=warn)a.risk=Risk::ForecastShortage;
    a.emergency_override=a.risk==Risk::Emergency || a.risk==Risk::Critical;
    a.should_prepare_departure=a.risk!=Risk::Safe;
    return a;
}
Decision preflight(const Simulation& world,EntityId home_ship,EntityId site,
                   PlayerId issuer,Order order,std::uint64_t warn,std::uint64_t critical){
    Decision d;
    d.assessment=assess(world,home_ship,site,warn,critical);
    d.order=order;d.requesting_player=issuer;
    if(!issuer||issuer!=d.assessment.owner){d.reason="issuer is not colony owner";return d;}
    switch(order){
    case Order::SetLocalProduction:
        d.allowed=true;
        d.reason=d.assessment.risk==Risk::Safe ? "production policy preflight accepted; consumption unaffected" :
             "production policy preflight accepted with mandatory safety warning; consumption unaffected";
        return d;
    case Order::SealOccupiedExit:
    case Order::DisableDistress:
    case Order::DumpOccupiedAtmosphere:
    case Order::PreventEmergencyRepair:
        if(d.assessment.population){
            d.allowed=false;
            d.deliberate_hazard_attempt=true; // attempted *order*, not inferred malicious intent
            d.reason="occupied colony life-safety controls cannot be disabled by player order";
        }else{
            d.allowed=true;
            d.reason="unoccupied compartment; physical action still requires separate world authorization";
        }
        return d;
    default:throw std::invalid_argument("unknown colony-safety order");
    }
}
ResponsePlan plan_response(const Assessment& a){
    if(!a.site || !a.home_ship || !a.owner)
        throw std::invalid_argument("response intent requires authoritative compartment identity");
    ResponsePlan p;
    p.site=a.site;p.home_ship=a.home_ship;p.world_tick=a.world_tick;
    p.population=a.population;p.risk=a.risk;
    if(!a.population){
        if(a.risk!=Risk::Safe)
            throw std::invalid_argument("unoccupied compartment cannot have occupant emergency");
        return p;
    }
    switch(a.risk){
    case Risk::Safe:break;
    case Risk::ForecastShortage:
        p.level=ResponseLevel::MonitorAndRequestSupply;
        p.request_supply=true;
        break;
    case Risk::Critical:
        p.level=ResponseLevel::UrgentRepairAndDepartureSearch;
        p.request_supply=true;
        p.seek_physical_repairs=true;
        p.search_verified_departure=true;
        p.bypass_player_production_preferences=true;
        break;
    case Risk::Emergency:
        p.level=ResponseLevel::EmergencySurvivalAndDistress;
        p.request_supply=true;
        p.seek_physical_repairs=true;
        p.search_verified_departure=true;
        p.activate_distress=true;
        p.bypass_player_production_preferences=true;
        break;
    default:throw std::invalid_argument("unrecognized risk in colony response");
    }
    // A planning result never mints provisions or declares an evacuation.
    return p;
}
ResponsePlan plan_response(const Simulation& w,EntityId ship,EntityId site){
    return plan_response(assess(w,ship,site));
}
ResponseTransition response_transition(const ResponsePlan& prior,
                                       const ResponsePlan& now){
    if(prior.site!=now.site || prior.home_ship!=now.home_ship ||
       !prior.site || !prior.home_ship || now.world_tick<prior.world_tick)
        throw std::invalid_argument("response transition across site or backwards time");
    if(static_cast<int>(prior.risk)<0||static_cast<int>(prior.risk)>3 ||
       static_cast<int>(now.risk)<0||static_cast<int>(now.risk)>3)
        throw std::invalid_argument("invalid risk transition");
    ResponseTransition t;
    t.previous=prior;t.current=now;
    t.risk_changed=prior.risk!=now.risk;
    t.escalated=static_cast<int>(now.risk)>static_cast<int>(prior.risk);
    // A notice is generated ONLY on a risk threshold transition; no repeat
    // per every tick and no fictitious evacuation-arrival event.
    t.requires_new_notice=t.risk_changed;
    return t;
}
void create_evidence_table(sqlite3* db){
    exec(db,"CREATE TABLE IF NOT EXISTS sg_colony_safety_evidence("
        "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
        "world_tick INTEGER NOT NULL CHECK(world_tick>=0),"
        "ship_id INTEGER NOT NULL CHECK(ship_id>0),"
        "site_id INTEGER NOT NULL CHECK(site_id>0),"
        "actor_id INTEGER NOT NULL CHECK(actor_id>0),"
        "owner_id INTEGER NOT NULL CHECK(owner_id>0),"
        "order_id INTEGER NOT NULL,"
        "allowed INTEGER NOT NULL CHECK(allowed IN(0,1)),"
        "attempted_hazard INTEGER NOT NULL CHECK(attempted_hazard IN(0,1)),"
        "risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3),"
        "population INTEGER NOT NULL CHECK(population>=0),"
        "reason TEXT NOT NULL);");
    exec(db,"CREATE TRIGGER IF NOT EXISTS sg_safety_no_update "
        "BEFORE UPDATE ON sg_colony_safety_evidence BEGIN "
        "SELECT RAISE(ABORT,'immutable safety evidence'); END;");
    exec(db,"CREATE TRIGGER IF NOT EXISTS sg_safety_no_delete "
        "BEFORE DELETE ON sg_colony_safety_evidence BEGIN "
        "SELECT RAISE(ABORT,'immutable safety evidence'); END;");
}
void record_decision(sqlite3* db,std::uint64_t id,const Decision& d){
    if(!db || !id || !d.requesting_player || !d.assessment.owner ||
       !d.assessment.home_ship || !d.assessment.site)
        throw std::invalid_argument("invalid safety record");
    static constexpr auto sql="INSERT INTO sg_colony_safety_evidence("
      "event_id,world_tick,ship_id,site_id,actor_id,owner_id,order_id,allowed,attempted_hazard,risk,population,reason) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt=nullptr;
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("safety table not initialized");
    try{
        bind(stmt,1,id);bind(stmt,2,d.assessment.world_tick);
        bind(stmt,3,d.assessment.home_ship);bind(stmt,4,d.assessment.site);
        bind(stmt,5,d.requesting_player);bind(stmt,6,d.assessment.owner);
        if(d.assessment.population>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::overflow_error("population exceeds SQLite integer capacity");
        sqlite3_bind_int(stmt,7,static_cast<int>(d.order));
        sqlite3_bind_int(stmt,8,d.allowed?1:0);
        sqlite3_bind_int(stmt,9,d.deliberate_hazard_attempt?1:0);
        sqlite3_bind_int(stmt,10,static_cast<int>(d.assessment.risk));
        bind(stmt,11,d.assessment.population);
        if(sqlite3_bind_text(stmt,12,d.reason.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("invalid safety reason");
        if(sqlite3_step(stmt)!=SQLITE_DONE)
            throw std::runtime_error(std::string("safety decision journal: ")+sqlite3_errmsg(db));
    }catch(...){sqlite3_finalize(stmt);throw;}
    sqlite3_finalize(stmt);
}
std::uint64_t evidence_count(sqlite3* db){
    if(!db)throw std::invalid_argument("canonical SQLite writer required");
    sqlite3_stmt* stmt=nullptr;
    if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM sg_colony_safety_evidence",-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("safety evidence table missing");
    const auto step=sqlite3_step(stmt);
    const auto count=step==SQLITE_ROW?sqlite3_column_int64(stmt,0):-1;
    sqlite3_finalize(stmt);
    if(count<0)throw std::runtime_error("safety evidence count failed");
    return static_cast<std::uint64_t>(count);
}
} // namespace galaxy::colony_safety
