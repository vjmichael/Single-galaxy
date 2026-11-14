#pragma once
// DOCK-05: cold regional population demand is read only when a HUMAN has
// bound one exact real station to one exact existing cold owner+colony.
// No guessed scope/settlement mapping, no synthesized tickets or NPCs.
#include "galaxy/Dock04Traffic.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
namespace galaxy::dock05 {

struct PopulationOption {
    std::uint64_t index=0,population=0,authority_generation=0,revision=0;
    std::string scope,colony;
};
struct PopulationState {
    std::uint64_t population=0,demand=0;
    bool source_bound=false;
};
class RegionalPopulation {
public:
    explicit RegionalPopulation(std::string path):file_(std::move(path)){
        if(sqlite3_open_v2(file_.c_str(),&db_,
           SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const std::string error=db_?sqlite3_errmsg(db_):"regional population SQLite";
            sqlite3_close(db_);db_=nullptr;throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(db_,2000);
        execute("CREATE TABLE IF NOT EXISTS dock05_region_binding_v1("
            "station INTEGER NOT NULL CHECK(station>0),"
            "owner INTEGER NOT NULL CHECK(owner>0),"
            "scope TEXT NOT NULL,colony TEXT NOT NULL,"
            "authority_generation INTEGER NOT NULL CHECK(authority_generation>0),"
            "checkpoint_revision INTEGER NOT NULL CHECK(checkpoint_revision>0),"
            "travel_rate INTEGER NOT NULL CHECK(travel_rate BETWEEN 0 AND 1000000),"
            "PRIMARY KEY(station,owner))");
    }
    ~RegionalPopulation(){sqlite3_close(db_);}
    RegionalPopulation(const RegionalPopulation&)=delete;
    RegionalPopulation& operator=(const RegionalPopulation&)=delete;

    std::vector<PopulationOption> options() const {
        std::vector<PopulationOption> out;
        if(!has_table("sg_regional_checkpoint") ||
           !has_table("sg_regional_mode_authority") ||
           !has_table("sg_world_region_gate"))
            return out;
        Sql q(db_,"SELECT cp.scope,a.generation,cp.revision "
            "FROM sg_regional_checkpoint cp "
            "JOIN sg_regional_mode_authority a ON a.scope=cp.scope "
            "JOIN sg_world_region_gate g ON g.scope=cp.scope "
            "WHERE a.owner=0 AND a.phase=0 AND a.generation>0 "
            "ORDER BY cp.scope LIMIT 24");
        while(q.next()==SQLITE_ROW){
            const auto scope=q.string(0);
            const auto generation=q.positive(1),rev=q.positive(2);
            if(!generation||!rev||scope.empty()||scope.size()>100)continue;
            regional_events::CheckpointStore cp(file_,scope);
            const auto state=cp.load();
            if(!state || state->revision!=rev)continue; // stale ownership
            if(state->scheduler.colony_count()>64)continue;
            for(const auto& [colony,source]:state->scheduler.colonies()){
                if(out.size()>=16)return out;
                if(colony.empty()||colony.size()>100 ||
                   source.population==0 || !label_safe(scope) ||
                   !label_safe(colony))continue;
                out.push_back({static_cast<std::uint64_t>(out.size()+1),
                    source.population,generation,rev,scope,colony});
            }
        }
        return out;
    }
    // Ordinal comes from live owner-reviewed list, never a user-submitted
    // SQLite scope or population. Requires original local PERSON access.
    bool bind(const PersistentWorld& world,EntityId station,PlayerId owner,
              std::uint64_t index,std::uint64_t authority_generation,
              std::uint64_t checkpoint_revision,
              std::uint32_t travel_per_million) {
        if(!station||!owner||index==0 || index>16 ||
           travel_per_million>1000000)return false;
        const auto* port=world.world().find(station);
        const auto person=world.world().player_people().find(owner);
        if(!port || port->type!=EntityType::Station ||
           person==world.world().player_people().end() ||
           person->second.state!=PlayerPersonState::AtStation ||
           person->second.physical_station!=station)return false;
        const auto candidates=options();
        if(index>candidates.size())return false;
        const auto& choice=candidates[index-1];
        if(!authority_generation || !checkpoint_revision ||
           choice.authority_generation!=authority_generation ||
           choice.revision!=checkpoint_revision)return false;
        execute("BEGIN IMMEDIATE");
        try{
            Sql s(db_,"INSERT INTO dock05_region_binding_v1 "
                "(station,owner,scope,colony,authority_generation,"
                "checkpoint_revision,travel_rate) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(station,owner) DO UPDATE SET "
                "scope=excluded.scope,"
                "colony=excluded.colony,"
                "authority_generation=excluded.authority_generation,"
                "checkpoint_revision=excluded.checkpoint_revision,"
                "travel_rate=excluded.travel_rate");
            s.num(1,station);s.num(2,owner);s.text(3,choice.scope);
            s.text(4,choice.colony);s.num(5,choice.authority_generation);
            s.num(6,choice.revision);s.num(7,travel_per_million);s.next();
            execute("COMMIT");return true;
        }catch(...){execute("ROLLBACK");throw;}
    }
    PopulationState projection(const Simulation& world,EntityId station,
                               PlayerId owner,std::uint64_t physical_seats) const{
        PopulationState response{};
        const auto* port=world.find(station);
        if(!port || port->type!=EntityType::Station ||
           !owner||physical_seats>128)return response;
        Sql q(db_,"SELECT scope,colony,authority_generation,"
                 "checkpoint_revision,travel_rate FROM dock05_region_binding_v1 "
                 "WHERE station=? AND owner=?");
        q.num(1,station);q.num(2,owner);
        if(q.next()!=SQLITE_ROW)return response;
        const auto scope=q.string(0),colony=q.string(1);
        const auto generation=q.positive(2),minimum_revision=q.positive(3),
                   rate=q.positive(4);
        if(!generation||!minimum_revision||rate>1000000||
           !has_table("sg_regional_mode_authority"))return response;
        Sql mode(db_,"SELECT owner,phase,generation FROM "
             "sg_regional_mode_authority WHERE scope=?");
        mode.text(1,scope);
        if(mode.next()!=SQLITE_ROW ||
           mode.nonnegative(0)!=0 || mode.nonnegative(1)!=0 ||
           mode.positive(2)!=generation)return response;
        regional_events::CheckpointStore cp(file_,scope);
        const auto source=cp.load();
        if(!source || source->revision<minimum_revision)return response;
        const auto& colonies=source->scheduler.colonies();
        const auto it=colonies.find(colony);
        if(it==colonies.end())return response;
        response.source_bound=true;
        response.population=it->second.population;
        response.demand=dock04::projected_demand(
            response.population,static_cast<std::uint32_t>(rate),physical_seats);
        return response;
    }
private:
    class Sql {
    public:
        Sql(sqlite3* db,const char* sql):db_(db){
            if(sqlite3_prepare_v2(db,sql,-1,&q_,nullptr)!=SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
        }
        ~Sql(){sqlite3_finalize(q_);}
        Sql(const Sql&)=delete;
        void num(int i,std::uint64_t v){
            if(v>static_cast<std::uint64_t>(
               std::numeric_limits<sqlite3_int64>::max()) ||
               sqlite3_bind_int64(q_,i,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
                throw std::runtime_error("invalid regional source integer");
        }
        void text(int i,const std::string& v){
            if(v.size()>100 ||
               sqlite3_bind_text(q_,i,v.data(),static_cast<int>(v.size()),
                                 SQLITE_TRANSIENT)!=SQLITE_OK)
                throw std::runtime_error("invalid regional source label");
        }
        int next(){
            const auto rc=sqlite3_step(q_);
            if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db_));
            return rc;
        }
        std::uint64_t positive(int i)const {
            const auto v=sqlite3_column_int64(q_,i);
            return v>0?static_cast<std::uint64_t>(v):0;
        }
        std::uint64_t nonnegative(int i)const {
            const auto v=sqlite3_column_int64(q_,i);
            return v>=0?static_cast<std::uint64_t>(v):
               std::numeric_limits<std::uint64_t>::max();
        }
        std::string string(int i)const {
            const auto* p=sqlite3_column_text(q_,i);
            const auto len=sqlite3_column_bytes(q_,i);
            if(!p||len<0||len>100)
                throw std::runtime_error("invalid regional source identity");
            return {reinterpret_cast<const char*>(p),
                    static_cast<std::size_t>(len)};
        }
    private:
        sqlite3* db_;
        sqlite3_stmt* q_=nullptr;
    };
    sqlite3* db_=nullptr;
    std::string file_;
    void execute(const char* sql)const{
        char* e=nullptr;
        if(sqlite3_exec(db_,sql,nullptr,nullptr,&e)!=SQLITE_OK){
            const std::string why=e?e:sqlite3_errmsg(db_);
            sqlite3_free(e);throw std::runtime_error(why);
        }
    }
    bool has_table(const char* name)const {
        Sql q(db_,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
        q.text(1,name);return q.next()==SQLITE_ROW;
    }
    static bool label_safe(const std::string& s) {
        if(s.empty()||s.size()>100)return false;
        for(unsigned char c:s)
            if(!((c>='A'&&c<='Z') || (c>='a'&&c<='z') ||
                 (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.'))
                return false;
        return true;
    }
};
} // namespace galaxy::dock05
