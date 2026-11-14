#include "galaxy/ColonyFamilyDeparture.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
namespace galaxy::colony_family {
namespace {
// ART02 borrowed family handles are valid only inside an active world-owned
// SQLite transaction. A standalone audit opens a consistent deferred snapshot;
// preparation already holds BEGIN IMMEDIATE and reuses that lock.
struct ReadTransaction {
    sqlite3* db=nullptr;
    bool own=false;
    explicit ReadTransaction(sqlite3* handle):db(handle){
        if(!db)throw std::runtime_error("family audit has no database");
        own=sqlite3_get_autocommit(db)!=0;
        if(own && sqlite3_exec(db,"BEGIN DEFERRED",nullptr,nullptr,nullptr)!=SQLITE_OK)
            throw std::runtime_error("family audit cannot acquire consistent SQLite snapshot");
    }
    ~ReadTransaction(){
        if(own)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
    }
};
class Query {
public:
    Query(sqlite3* db,const char* sql){
        if(sqlite3_prepare_v2(db,sql,-1,&q_,nullptr)!=SQLITE_OK)
            throw std::runtime_error("family audit cannot prepare authoritative read");
    }
    ~Query(){sqlite3_finalize(q_);}
    void bind(int index,const std::string& str){
        if(sqlite3_bind_text(q_,index,str.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("family audit bind failed");
    }
    bool row(){
        const int c=sqlite3_step(q_);
        if(c==SQLITE_ROW)return true;
        if(c==SQLITE_DONE)return false;
        throw std::runtime_error("family audit authoritative read failed");
    }
    std::int64_t integer(int index) const{
        if(sqlite3_column_type(q_,index)!=SQLITE_INTEGER)
            throw std::runtime_error("family audit invalid numeric proof");
        return sqlite3_column_int64(q_,index);
    }
    std::string get(int index) const{
        if(sqlite3_column_type(q_,index)!=SQLITE_TEXT)
            throw std::runtime_error("family audit invalid identity representation");
        const unsigned char* p=sqlite3_column_text(q_,index);
        return reinterpret_cast<const char*>(p);
    }
private:
    sqlite3_stmt* q_=nullptr;
};
std::uint64_t parse(const std::string& s){
    if(s.empty() || s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("invalid family identity text");
    std::size_t n=0;
    const auto v=std::stoull(s,&n);
    if(n!=s.size() || !v)throw std::runtime_error("invalid or zero family identity");
    return v;
}
std::uint64_t floor_count(const Simulation& w,EntityId ship){
    std::uint64_t count=0;
    const auto* c=w.colony_ship(ship);
    if(!c || c->crew<0)throw std::runtime_error("missing ship or negative crew");
    count+=static_cast<std::uint64_t>(c->crew);
    for(const auto& [id,f]:w.farms())if(f.source_ship==ship){
        (void)id;
        if(f.workers<0)throw std::runtime_error("negative farm workforce");
        count+=static_cast<std::uint64_t>(f.workers);
    }
    for(const auto& [id,h]:w.habitats())if(h.source_ship==ship){
        (void)id;
        if(h.residents<0)throw std::runtime_error("negative habitat workforce");
        count+=static_cast<std::uint64_t>(h.residents);
    }
    return count;
}
bool same_sorted(const std::vector<std::uint64_t>& a,
                 const std::vector<std::uint64_t>& b){
    return std::is_sorted(a.begin(),a.end()) &&
           std::adjacent_find(a.begin(),a.end())==a.end() && a==b;
}
}
Audit assess(const Simulation& world,sqlite3* db,const DeparturePlan& request){
    Audit result;
    result.world_tick=world.tick();
    try {
        ReadTransaction snapshot(db);
        if(!db || !request.source || !request.receiver ||
           request.source==request.receiver || !request.family_seed ||
           request.family_scope.empty() || !world.passenger_needs_enabled())
            throw std::runtime_error("missing family/world authority or version");
        const auto* origin=world.find(request.source);
        const auto* dest=world.find(request.receiver);
        const auto* supplies=world.life_support(request.receiver);
        if(!origin || !dest || !supplies ||
           origin->type!=EntityType::Ship || dest->type!=EntityType::Ship ||
           origin->owner_id!=dest->owner_id || origin->system_id!=dest->system_id)
            throw std::runtime_error("source/receiver physical owner or ship mismatch");
        const auto near=[&](const Entity* place){
            if(!place || place->system_id!=dest->system_id)return false;
            const auto dx=place->position.x-dest->position.x;
            const auto dy=place->position.y-dest->position.y;
            const auto limit=Simulation::kDockingRangeMm;
            return dx>=-limit && dx<=limit && dy>=-limit && dy<=limit &&
                   dx*dx+dy*dy<=limit*limit;
        };
        if(!near(origin) || world.passengers_aboard(request.source) ||
           world.passengers_aboard(request.receiver))
            throw std::runtime_error("local rescue proximity or berth custody not reconciled");
        for(const auto& [site,h]:world.habitats())
            if(h.source_ship==request.source && h.residents && !near(world.find(site)))
                throw std::runtime_error("habitat residents cannot physically reach receiver");
        for(const auto& [site,fr]:world.farms())
            if(fr.source_ship==request.source && fr.workers && !near(world.find(site)))
                throw std::runtime_error("farm workers cannot physically reach receiver");
        std::vector<std::uint64_t> expected_adults;
        for(const auto& [id,n]:world.npc_records())if(n.home_ship==request.source){
            if(n.transport_id || n.location!=n.assigned_site)
                throw std::runtime_error("family adult already in unrelated physical transport");
            expected_adults.push_back(id);
        }
        result.named_adults=expected_adults.size();
        result.complete_adult_roster=floor_count(world,request.source)==expected_adults.size() &&
            same_sorted(request.adult_ids,expected_adults) &&
            !request.adult_ids.empty() && request.adult_ids.size()<=16;
        if(!result.complete_adult_roster)
            throw std::runtime_error("incomplete or duplicated living adult manifest");
        NpcAppearanceStore family(db,request.family_scope,request.family_seed,
            [&](std::uint64_t id){return world.npc_records().count(id)!=0;});
        // No adult may be in transit, deceased, or a phantom in the census.
        for(auto id:request.adult_ids)
            if(family.current_residence(id)!=request.source)
                throw std::runtime_error("guardian/adult family residence not at source");
        auto src=family.family_census(request.source);
        auto dst=family.family_census(request.receiver);
        if(src.current_population<request.adult_ids.size() ||
           dst.current_population<world.people_physically_aboard(request.receiver))
            throw std::runtime_error("family census disagrees with physical workforce");
        // Genuine child identities are ART02 births, NOT Simulation NPCs.
        // Require the independent world-birth bridge and family ledger to
        // agree for EVERY born child at the source (even unlisted children).
        Query travel_schema(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                               "AND name='sg_dependent_travel'");
        const bool has_child_travel=travel_schema.row();
        Query all(db,"SELECT child_id,parent_a,parent_b,event_id FROM npc_family_births "
                     "WHERE scope=? ORDER BY child_id");
        all.bind(1,request.family_scope);
        std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> births;
        std::set<std::uint64_t> source_children;
        while(all.row()){
            const auto child=parse(all.get(0));
            const auto a=parse(all.get(1)),b=parse(all.get(2));
            const auto event=parse(all.get(3));
            if(world.npc_records().count(child))
                throw std::runtime_error("dependent alias collides with living world NPC");
            if(family.current_residence(child)!=request.source)continue;
            Query deceased(db,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=? LIMIT 1");
            deceased.bind(1,request.family_scope);
            deceased.bind(2,std::to_string(child));
            if(deceased.row())
                throw std::runtime_error("deceased dependent cannot board as living child");
            if(has_child_travel){
                // Even if ART02 census still says 'source', an in-flight child
                // has custody on the actual ferry; no parent may silently
                // teleport that child out of the voyage. Check all scopes.
                Query traveling(db,"SELECT status,destination FROM sg_dependent_travel "
                    "WHERE child_id=? ORDER BY board_generation DESC,rowid DESC LIMIT 1");
                traveling.bind(1,std::to_string(child));
                if(traveling.row()){
                    if(traveling.integer(0)!=2 ||
                       parse(traveling.get(1))!=request.source)
                        throw std::runtime_error("dependent physically aboard or arrived elsewhere");
                }
            }
            Query proof(db,"SELECT parent_a,parent_b,home_ship,family_scope FROM sg_med_society_birth "
                            "WHERE event_id=? AND child_id=?");
            proof.bind(1,std::to_string(event));proof.bind(2,std::to_string(child));
            if(!proof.row() || parse(proof.get(0))!=a ||
               parse(proof.get(1))!=b ||
               parse(proof.get(2))!=request.source ||
               proof.get(3)!=request.family_scope || proof.row())
                throw std::runtime_error("missing or mismatched authoritative birth bridge");
            if(!births.emplace(child,std::make_pair(a,b)).second)
                throw std::runtime_error("duplicate child identity");
            source_children.insert(child);
        }
        // Examine ALL world-authoritative births at the source, not merely
        // those in the caller-selected family scope. A second family scope
        // must never become an invisible, abandoned dependent.
        Query sources(db,"SELECT child_id,family_scope FROM sg_med_society_birth "
                         "WHERE home_ship=?");
        sources.bind(1,std::to_string(request.source));
        while(sources.row()){
            const auto child=parse(sources.get(0));
            if(sources.get(1)!=request.family_scope ||
               !source_children.count(child))
                throw std::runtime_error("unlisted family scope or split child custody at source");
        }
        // INT-02: reconcile the original v77 physical-child registry with
        // the ART02 birth ledger BEFORE accepting a whole-source manifest.
        // Legacy read-only ART02 previews may have no registered native
        // children yet; once any child has physical custody here, a partial
        // registration can never authorize the departure of all siblings.
        std::size_t registered_children_at_source=0;
        for(const auto& [child_id,physical]:world.physical_dependents()) {
            if(physical.home_ship!=request.source)continue;
            if(physical.location!=request.source ||
               !physical.registration_command ||
               !source_children.count(child_id))
                throw std::runtime_error(
                    "native dependent custody lacks matching ART02 source birth");
            ++registered_children_at_source;
        }
        if(registered_children_at_source &&
           registered_children_at_source!=source_children.size())
            throw std::runtime_error(
                "source household has incomplete native physical registration");
        std::vector<std::uint64_t> requested_ids;
        std::uint64_t previous=0;
        for(const auto& ch:request.children){
            if(ch.child_id<(1ULL<<62) || ch.child_id<=previous ||
               !ch.guardian_id || !births.count(ch.child_id))
                throw std::runtime_error("incomplete, duplicate or forged dependent manifest");
            previous=ch.child_id;
            requested_ids.push_back(ch.child_id);
            const auto [a,b]=births.at(ch.child_id);
            // An already-registered physical child may have a different
            // lawful parent than the one listed as present escort. A draft
            // manifest must never silently substitute the escort during
            // preparation; legal AND existing physical guardian must agree.
            const auto physically_registered=world.physical_dependents().find(ch.child_id);
            if(physically_registered!=world.physical_dependents().end() &&
               (physically_registered->second.guardian_id!=ch.guardian_id ||
                physically_registered->second.home_ship!=request.source ||
                physically_registered->second.location!=request.source))
                throw std::runtime_error("evacuation manifest contradicts physical child guardian");
            const auto legal=family.current_nonparent_guardian(ch.child_id);
            if(legal){
                if(legal!=ch.guardian_id ||
                   !std::binary_search(expected_adults.begin(),expected_adults.end(),legal))
                    throw std::runtime_error("nonparent guardian not in complete live source roster");
            }else if(ch.guardian_id!=a && ch.guardian_id!=b)
                throw std::runtime_error("guardian is neither birth parent nor authorized nonparent");
            if(!std::binary_search(expected_adults.begin(),expected_adults.end(),a) ||
               !std::binary_search(expected_adults.begin(),expected_adults.end(),b))
                throw std::runtime_error("both recorded parents not accounted for in source roster");
        }
        std::vector<std::uint64_t> canonical(source_children.begin(),source_children.end());
        result.dependent_children=source_children.size();
        result.complete_child_roster=same_sorted(requested_ids,canonical) &&
            !request.children.empty() && request.children.size()<=32;
        if(!result.complete_child_roster)
            throw std::runtime_error("missing dependent child or forged extra child");
        result.birth_provenance=true;
        result.parent_or_guardian_custody=true;
        // The census is itself an independent population authority. It must
        // not hide unnamed children or claim an extra unlocated person.
        result.census_reconciled=src.current_population==
            expected_adults.size()+source_children.size() &&
            dst.current_population==floor_count(world,request.receiver);
        if(!result.census_reconciled)
            throw std::runtime_error("family census includes unaccounted source/receiver population");
        const auto receiver_count=world.people_physically_aboard(request.receiver);
        const auto incoming=expected_adults.size()+source_children.size();
        // One locally stationed rescue ship offers at most sixteen passenger
        // berths in the current native transport model.
        if(incoming>16 || receiver_count>1000-incoming)
            throw std::runtime_error("insufficient physical receiving population capacity");
        const auto total=static_cast<std::int64_t>(receiver_count+incoming);
        result.receiving_supplies=supplies->food_kg>=2*total &&
                                  supplies->water_kg>=2*total &&
                                  supplies->oxygen_kg>=2*total;
        if(!result.receiving_supplies)
            throw std::runtime_error("recipient does not have real food water and oxygen for children");
        result.ready_for_transaction=true;
        // This audit does NOT relocate children; cannot assert an evacuation
        // without the canonical world+ART02+regional commit and child needs.
        result.evacuated=false;
    }catch(const std::exception& e){result.refusal=e.what();}
    return result;
}
}
