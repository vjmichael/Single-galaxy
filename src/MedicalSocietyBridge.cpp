#include "galaxy/MedicalSocietyBridge.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy {
namespace {
struct Connection {
    sqlite3* db=nullptr;
    explicit Connection(const std::string& path) {
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            if(db)sqlite3_close(db);
            throw std::runtime_error("family SQLite read-only open failed");
        }
        sqlite3_busy_timeout(db,1000);
    }
    ~Connection(){sqlite3_close(db);}
    Connection(const Connection&)=delete;
    Connection& operator=(const Connection&)=delete;
};
struct Query {
    sqlite3_stmt* stmt=nullptr;
    explicit Query(sqlite3* db,const char* sql){
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("family schema or query invalid");
    }
    ~Query(){sqlite3_finalize(stmt);}
    void text(int i,const std::string& value){
        if(sqlite3_bind_text(stmt,i,value.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("family bind failed");
    }
    int next(){
        const int rc=sqlite3_step(stmt);
        if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)throw std::runtime_error("family query failed");
        return rc;
    }
    Query(const Query&)=delete; Query& operator=(const Query&)=delete;
};
std::uint64_t parse_id(const unsigned char* p){
    if(!p || !*p)throw std::runtime_error("missing family identity");
    std::uint64_t n=0;
    for(auto c=p;*c;++c){
        if(*c<'0'||*c>'9')throw std::runtime_error("non-decimal family identity");
        const auto d=static_cast<std::uint64_t>(*c-'0');
        if(n>(std::numeric_limits<std::uint64_t>::max()-d)/10)
            throw std::runtime_error("family identity overflow");
        n=n*10+d;
    }
    if(!n)throw std::runtime_error("zero family identity");
    return n;
}
std::string id_text(std::uint64_t id){return std::to_string(id);}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
} // namespace

SocietyMedicalProjection reconcile_medical_society(
    const PersistentWorld& world,const NpcAppearanceStore& family,
    const std::string& family_sqlite_path,const std::string& family_scope,
    EntityId home_ship,const regional_events::CheckpointStore& region,
    const std::string& regional_colony,std::uint64_t expected_region_revision,
    std::uint64_t expected_world_generation) {
    require(home_ship && !family_scope.empty() && !regional_colony.empty() &&
            expected_region_revision && expected_world_generation,
            "incomplete medical society binding");
    SocietyMedicalProjection p;
    p.settlement=home_ship;
    p.world_generation=world.generation();
    require(p.world_generation==expected_world_generation,"stale world generation");
    p.world_tick=world.world().tick();
    const auto& sim=world.world();
    transport::Endpoint address{transport::LocationKind::ColonyShip,home_ship};
    if(!sim.colony_ship(home_ship)) {
        bool registered_surface=false;
        for(const auto& [key,site]:sim.planetary_sites()) {
            (void)key;
            if(site.surface_location==home_ship && site.spaceport_phase==3)
                registered_surface=true;
        }
        if(registered_surface)
            address.kind=transport::LocationKind::PlanetarySite;
        else if(sim.built_outposts().count(home_ship) &&
                sim.habitat(home_ship))
            address.kind=transport::LocationKind::SpaceColony;
        else
            throw std::runtime_error("missing authoritative settlement location");
    }
    const auto audit=sim.assess_medical_population();
    const auto legal=sim.assess_npc_residence();
    require(audit.reconciled && legal.reconciled,
            "world workforce or legal residence census fails reconciliation");
    const auto residents=legal.living_by_residence.find(address);
    if(residents!=legal.living_by_residence.end())
        p.living_named=residents->second;
    for(const auto& [id,n]:sim.npc_records()){
        if(n.legal_residence()!=address)continue;
        const auto role=static_cast<unsigned>(n.role);
        require(role<=static_cast<unsigned>(NpcRole::PlanetarySurface),
                "unknown named role");
        const bool ready=sim.npc_available_for_duty(id);
        if(role==static_cast<unsigned>(NpcRole::PlanetarySurface)) {
            ++p.assigned_planetary_workers;
            if(ready)++p.ready_planetary_workers;
        }else{
            ++p.assigned_workers[role];
            if(ready)++p.ready_workers[role];
        }
        if(!ready)++p.unavailable;
        if(n.transport_id || n.location!=n.assigned_site)++p.traveling;
        const auto status=sim.npc_health_status(id);
        if(status==NpcHealthStatus::Injured || status==NpcHealthStatus::Stabilized ||
           status==NpcHealthStatus::Recovering)++p.medically_unavailable;
    }
    for(const auto& [id,death]:sim.npc_casualties()){
        (void)id;
        if(death.identity.legal_residence()==address)++p.dead_named;
    }
    require(p.ready_workers[0]+p.ready_workers[1]+p.ready_workers[2]+
            p.ready_planetary_workers+p.unavailable==p.living_named,
            "job readiness does not partition actual legal residents");

    Connection conn(family_sqlite_path);
    // BEGIN holds one consistent SQLite WAL reader snapshot, without writing.
    require(sqlite3_exec(conn.db,"BEGIN",nullptr,nullptr,nullptr)==SQLITE_OK,
            "family read transaction failed");
    // Pin the family source to the exact committed PersistentWorld snapshot.
    // A lookalike external family database is not sufficient evidence.
    {
        Query q(conn.db,"SELECT state FROM snapshots WHERE generation=?");
        if(p.world_generation>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(q.stmt,1,static_cast<sqlite3_int64>(p.world_generation))!=SQLITE_OK)
            throw std::runtime_error("world generation bind failed");
        require(q.next()==SQLITE_ROW,"family source lacks matching world generation");
        const auto snapshot=sim.canonical_state();
        require(sqlite3_column_bytes(q.stmt,0)==static_cast<int>(snapshot.size()) &&
                std::memcmp(sqlite3_column_blob(q.stmt,0),snapshot.data(),snapshot.size())==0,
                "family source does not match authoritative world snapshot");
    }
    const auto census=family.family_census(home_ship);
    require(census.settlement==home_ship && census.revision<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            "family settlement identity invalid");
    p.family_revision=census.revision;
    p.family_population=census.current_population;
    std::set<std::uint64_t> family_deaths;
    {
        Query q(conn.db,"SELECT event_id,npc_id,tick FROM npc_family_deaths "
                        "WHERE scope=? AND settlement=? ORDER BY npc_id");
        q.text(1,family_scope);q.text(2,id_text(home_ship));
        while(q.next()==SQLITE_ROW){
            const auto event=parse_id(sqlite3_column_text(q.stmt,0));
            const auto npc=parse_id(sqlite3_column_text(q.stmt,1));
            const auto tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.stmt,2));
            require(family_deaths.insert(npc).second,"duplicate family death");
            const auto d=sim.npc_casualties().find(npc);
            require(d!=sim.npc_casualties().end() && d->second.identity.legal_residence()==address &&
                    d->second.death_tick==tick,"family death lacks matching canonical casualty");
            const auto canonical=family.get_death(event);
            require(canonical.npc_id==npc && canonical.settlement==home_ship &&
                    canonical.tick==tick && canonical.census_revision<=census.revision,
                    "family death receipt contradiction");
        }
    }
    for(const auto& [id,d]:sim.npc_casualties())
        if(d.identity.legal_residence()==address)
            require(family_deaths.count(id)!=0,"canonical NPC death missing from family ledger");
    require(family_deaths.size()==p.dead_named,"family deaths do not exhaust canonical casualties");
    std::set<std::uint64_t> children;
    {
        Query q(conn.db,"SELECT event_id,child_id,parent_a,parent_b FROM npc_family_births "
                        "WHERE scope=? ORDER BY child_id");
        q.text(1,family_scope);
        while(q.next()==SQLITE_ROW){
            const auto event=parse_id(sqlite3_column_text(q.stmt,0));
            const auto child=parse_id(sqlite3_column_text(q.stmt,1));
            if(family.current_residence(child)!=home_ship)continue;
            const auto pa=parse_id(sqlite3_column_text(q.stmt,2));
            const auto pb=parse_id(sqlite3_column_text(q.stmt,3));
            require(children.insert(child).second && pa!=pb,"invalid family child set");
            const auto birth=family.get_birth(event);
            require(birth.child_id==child && birth.parent_a==pa && birth.parent_b==pb &&
                    birth.census_revision<=family.family_census(birth.settlement).revision,
                    "family birth receipt contradiction");
            const bool live=sim.npc_records().count(child)!=0;
            const bool dead=sim.npc_casualties().count(child)!=0;
            require(!(live && dead),"child both alive and dead");
            if(!live && !dead){
                require(child>=(1ULL<<62),"uninstantiated dependent not in ART02 reserved namespace");
                ++p.dependent_births;
                // Dependent remains a real ART02 census identity, not a free worker.
                const auto at_risk=[&](std::uint64_t parent){
                    return sim.npc_casualties().count(parent)!=0 ||
                        !sim.npc_records().count(parent) ||
                        !sim.npc_available_for_duty(parent);
                };
                if(at_risk(pa)||at_risk(pb))++p.affected_dependents;
            }
        }
    }
    require(p.living_named<=std::numeric_limits<std::uint64_t>::max()-p.dependent_births &&
            p.family_population==p.living_named+p.dependent_births,
            "family census differs from living named plus uninstantiated dependents");
    // CheckpointStore must come from the same committed SQLite world, not a
    // caller-supplied region DB with convenient matching headcounts.
    regional_events::CheckpointStore bound(family_sqlite_path,region.scope());
    const auto versioned=bound.load();
    const auto supplied=region.load();
    require(versioned.has_value() && supplied.has_value() &&
            versioned->revision==expected_region_revision &&
            supplied->revision==versioned->revision &&
            supplied->scheduler.tick()==versioned->scheduler.tick() &&
            supplied->scheduler.colony(regional_colony).population==
                versioned->scheduler.colony(regional_colony).population,
            "stale, detached or absent regional checkpoint revision");
    p.regional_revision=versioned->revision;
    require(versioned->scheduler.tick()<=p.world_tick &&
            versioned->scheduler.pending_events()==0,
            "regional checkpoint is ahead of detailed world or has pending population changes");
    require(versioned->scheduler.colony(regional_colony).population==p.family_population,
            "regional cohort disagrees with canonical family and living NPC population");
    const auto last_region=bound.load();
    require(last_region && last_region->revision==p.regional_revision,
            "regional revision changed during medical society read");
    require(world.generation()==p.world_generation && world.world().tick()==p.world_tick &&
            family.family_census(home_ship).revision==p.family_revision,
            "medical society evidence changed during read");
    p.reconciled=true;
    return p;
}
} // namespace galaxy
