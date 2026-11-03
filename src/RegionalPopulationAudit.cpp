#include "galaxy/RegionalPopulationAudit.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace galaxy {
namespace {
struct Database {
    sqlite3* db=nullptr;
    explicit Database(const std::string& path){
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
            throw std::runtime_error("region audit source DB unavailable");
        sqlite3_busy_timeout(db,5000);
    }
    ~Database(){sqlite3_close(db);}
};
struct Statement {
    sqlite3_stmt* s=nullptr;
    explicit Statement(sqlite3* db,const char* query){
        if(sqlite3_prepare_v2(db,query,-1,&s,nullptr)!=SQLITE_OK)
            throw std::runtime_error("regional population audit SQL invalid");
    }
    ~Statement(){sqlite3_finalize(s);}
    void text(int n,const std::string& v){
        if(sqlite3_bind_text(s,n,v.c_str(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("regional population text bind failed");
    }
    void number(int n,std::uint64_t v){
        if(v>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())||
           sqlite3_bind_int64(s,n,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
            throw std::runtime_error("regional population integer bind failed");
    }
    bool row(){
        const auto rc=sqlite3_step(s);
        if(rc==SQLITE_ROW)return true;
        if(rc==SQLITE_DONE)return false;
        throw std::runtime_error("regional population SQL read failed");
    }
    std::string str(int n){
        const auto* t=sqlite3_column_text(s,n);
        if(!t||sqlite3_column_type(s,n)!=SQLITE_TEXT)
            throw std::runtime_error("regional population text missing");
        return reinterpret_cast<const char*>(t);
    }
    std::uint64_t integer(int n){
        if(sqlite3_column_type(s,n)!=SQLITE_INTEGER||sqlite3_column_int64(s,n)<0)
            throw std::runtime_error("regional population integer missing");
        return static_cast<std::uint64_t>(sqlite3_column_int64(s,n));
    }
};
std::uint64_t parse_id(const std::string& text){
    if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("invalid ART02 identity");
    std::size_t used=0;
    const auto result=std::stoull(text,&used);
    if(!result||used!=text.size()||std::to_string(result)!=text)
        throw std::runtime_error("noncanonical ART02 identity");
    return result;
}
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
}
RegionalPopulationProof verify_regional_population(const PersistentWorld& world,
        const std::string& path,EntityId home,const std::vector<std::string>& scopes,
        const std::string& region_scope,const std::string& colony,
        std::uint64_t expected_revision,std::uint64_t expected_owner,
        bool require_named_present){
    require(!path.empty()&&home&&!scopes.empty()&&scopes.size()<=64&&
            !region_scope.empty()&&!colony.empty()&&expected_revision&&expected_owner,
            "regional population proof arguments incomplete");
    std::set<std::string> unique_scopes(scopes.begin(),scopes.end());
    require(unique_scopes.size()==scopes.size()&&!unique_scopes.count(""),
            "duplicate/blank ART02 family scope");
    const auto& sim=world.world();
    require(sim.colony_ship(home)!=nullptr,"regional population home colony missing");
    const auto medical=sim.assess_medical_population();
    require(medical.reconciled,"source workforce identity/headcount not reconciled");
    RegionalPopulationProof out{};
    out.world_generation=world.generation();out.world_tick=sim.tick();
    Database db(path);
    require(sqlite3_exec(db.db,"BEGIN",nullptr,nullptr,nullptr)==SQLITE_OK,
            "failed to pin source audit reader transaction");
    {Statement snapshot(db.db,"SELECT state FROM snapshots WHERE generation=?");
     snapshot.number(1,out.world_generation);
     require(snapshot.row(),"regional audit source is not same world generation");
     const auto serial=sim.canonical_state();
     const auto length=sqlite3_column_bytes(snapshot.s,0);
     require(length==static_cast<int>(serial.size())&&
          std::memcmp(sqlite3_column_blob(snapshot.s,0),serial.data(),serial.size())==0 &&
          !snapshot.row(),"regional audit world snapshot does not match source");}
    std::set<std::uint64_t> named_seen,dependent_seen;
    std::uint64_t summed=0;
    for(const auto& scope:scopes){
        Statement census(db.db,"SELECT current_population FROM npc_family_census "
                            "WHERE scope=? AND settlement=?");
        census.text(1,scope);census.text(2,std::to_string(home));
        require(census.row(),"shared family lacks census for actual home");
        const auto population=census.integer(0);
        require(!census.row(),"shared family has duplicate home census");
        if(population>std::numeric_limits<std::uint64_t>::max()-summed)
            throw std::runtime_error("regional family population overflow");
        summed+=population;
        std::uint64_t counted=0;
        Statement members(db.db,"SELECT npc_id,settlement FROM npc_family_people "
                              "WHERE scope=? ORDER BY npc_id");
        members.text(1,scope);
        while(members.row()){
            const auto id=parse_id(members.str(0));
            const auto original=parse_id(members.str(1));
            Statement moved(db.db,"SELECT destination,tick FROM npc_family_moves "
                "WHERE scope=? AND npc_id=? ORDER BY tick DESC,rowid DESC LIMIT 1");
            moved.text(1,scope);moved.text(2,std::to_string(id));
            auto residence=original;
            if(moved.row()){
                residence=parse_id(moved.str(0));
                require(moved.integer(1)<=sim.tick(),"family move from the future");
            }
            if(residence!=home)continue;
            Statement died(db.db,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=?");
            died.text(1,scope);died.text(2,std::to_string(id));
            const bool death=died.row();
            if(death){
                const auto canonical=sim.npc_casualties().find(id);
                require(canonical!=sim.npc_casualties().end()&&
                        canonical->second.identity.home_ship==home,
                        "family death not backed by canonical NPC casualty");
                continue;
            }
            require(!sim.npc_casualties().count(id),"unrecorded canonical family casualty");
            const auto npc=sim.npc_records().find(id);
            if(npc!=sim.npc_records().end()){
                require(npc->second.home_ship==home&&named_seen.insert(id).second,
                        "duplicate or wrong-home named NPC in shared region family scopes");
                ++out.living_named;
            }else{
                require(id>=(1ULL<<62)&&dependent_seen.insert(id).second,
                        "duplicate, missing or invented shared-region dependent");
                Statement birth(db.db,"SELECT 1 FROM npc_family_births WHERE scope=? AND child_id=?");
                birth.text(1,scope);birth.text(2,std::to_string(id));
                require(birth.row(),"dependent has no real ART02 birth certificate");
                ++out.dependents;
            }
            ++counted;
        }
        require(counted==population,"family scope census includes uninstantiated or unassigned residents");
    }
    for(const auto& [id,n]:sim.npc_records())if(n.home_ship==home){
        require(named_seen.count(id)!=0,"living home NPC omitted from regional family scopes");
        if(n.location!=n.assigned_site||n.transport_id)++out.named_away_or_transit;
        else ++out.named_present_for_duty;
    }
    require(out.living_named==named_seen.size(),
            "regional named person accounting internal error");
    out.registered=out.living_named+out.dependents;
    require(out.registered==summed,"ART02 scoped population does not match people");
    if(require_named_present)
        require(!out.named_away_or_transit,"workers physically absent during detail handoff");
    // ECON03 remains the sole authority for registered regional population.
    regional_events::CheckpointStore checkpoint(path,region_scope);
    const auto versioned=checkpoint.load();
    require(versioned.has_value()&&versioned->revision==expected_revision&&
            versioned->scheduler.tick()<=sim.tick()&&
            versioned->scheduler.pending_events()==0,
            "regional checkpoint stale, ahead of world, or carrying uncommitted events");
    out.checkpoint_revision=versioned->revision;
    require(versioned->scheduler.colony(colony).population==out.registered,
            "ECON03 resident census double counted or mismatched ART02/world");
    regional_events::RegionalModeAuthority authority(checkpoint);
    const auto state=authority.state();
    require(state.phase==regional_events::TransitionPhase::Stable&&
            state.generation==expected_owner&&
            state.checkpoint_revision==expected_revision&&
            state.boundary_tick==versioned->scheduler.tick(),
            "owner generation or prepared warm/cold handoff prevents reconciliation");
    out.owner_generation=state.generation;
    require(world.generation()==out.world_generation&&sim.tick()==out.world_tick,
            "source world changed during population audit");
    return out;
}
}
