#include "galaxy/Persistence.hpp"
#include "galaxy/SplitRescueIntent.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy {
namespace {
struct Database {
    sqlite3* db=nullptr;
    explicit Database(const std::string& path,int flags){
        if(sqlite3_open_v2(path.c_str(),&db,flags|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const auto msg=db?sqlite3_errmsg(db):"open failed";
            const std::string error(msg);sqlite3_close(db);db=nullptr;
            throw std::runtime_error("split intent SQLite open: "+error);
        }
        sqlite3_busy_timeout(db,5000);
        if(sqlite3_exec(db,"PRAGMA foreign_keys=ON",nullptr,nullptr,nullptr)!=SQLITE_OK)
            throw std::runtime_error("split intent foreign keys unavailable");
    }
    ~Database(){sqlite3_close(db);}
    Database(const Database&)=delete;Database& operator=(const Database&)=delete;
};
void execute(sqlite3* db,const char* statement){
    char* err=nullptr;
    if(sqlite3_exec(db,statement,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string why=err?err:sqlite3_errmsg(db);sqlite3_free(err);
        throw std::runtime_error("split rescue SQLite: "+why);
    }
}
struct Query {
    sqlite3* db=nullptr;sqlite3_stmt* ptr=nullptr;
    Query(sqlite3* database,const char* text):db(database){
        if(sqlite3_prepare_v2(db,text,-1,&ptr,nullptr)!=SQLITE_OK)
            throw std::runtime_error("split rescue SQL prepare: "+std::string(sqlite3_errmsg(db)));
    }
    ~Query(){sqlite3_finalize(ptr);}
    Query(const Query&)=delete; Query& operator=(const Query&)=delete;
    void bind(int i,const std::string& s){
        if(sqlite3_bind_text(ptr,i,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("split SQL bind text");
    }
    void bind(int i,std::uint64_t n){
        if(n>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("split rescue number outside SQLite range");
        if(sqlite3_bind_int64(ptr,i,static_cast<sqlite3_int64>(n))!=SQLITE_OK)
            throw std::runtime_error("split SQL bind integer");
    }
    bool row(){
        const auto rc=sqlite3_step(ptr);
        if(rc==SQLITE_ROW)return true;
        if(rc==SQLITE_DONE)return false;
        throw std::runtime_error("split rescue SQL read: "+std::string(sqlite3_errmsg(db)));
    }
    void done(){if(sqlite3_step(ptr)!=SQLITE_DONE)
        throw std::runtime_error("split rescue SQL write: "+std::string(sqlite3_errmsg(db)));}
    std::string str(int col){
        const auto* p=sqlite3_column_text(ptr,col);
        if(!p||sqlite3_column_type(ptr,col)!=SQLITE_TEXT)
            throw std::runtime_error("null or non-text split rescue receipt");
        return reinterpret_cast<const char*>(p);
    }
    std::uint64_t number(int col){
        if(sqlite3_column_type(ptr,col)!=SQLITE_INTEGER||sqlite3_column_int64(ptr,col)<0)
            throw std::runtime_error("invalid unsigned split rescue receipt");
        return static_cast<std::uint64_t>(sqlite3_column_int64(ptr,col));
    }
};
std::uint64_t split_mix(std::uint64_t digest,const std::string& value){
    const auto token=std::to_string(value.size())+":"+value;
    for(const unsigned char byte:token){digest^=byte;digest*=1099511628211ULL;}
    return digest;
}
std::uint64_t split_header_digest(const std::string& id,std::uint64_t generation,
        std::uint64_t source_transport,EntityId source,EntityId origin,EntityId dest){
    std::uint64_t h=14695981039346656037ULL;
    for(const auto& v:{id,std::to_string(generation),std::to_string(source_transport),
                       std::to_string(source),std::to_string(origin),std::to_string(dest)})
        h=split_mix(h,v);
    return h;
}
std::uint64_t split_receiver_digest(std::uint64_t digest,EntityId receiver,
        std::uint64_t new_transport){
    return split_mix(split_mix(digest,std::to_string(receiver)),std::to_string(new_transport));
}
std::uint64_t split_family_digest(std::uint64_t digest,const std::string& scope,
        const std::string& region,std::uint64_t seed,std::uint64_t parent,
        std::uint64_t co,std::uint64_t owner){
    for(const auto& s:{scope,region,std::to_string(seed),std::to_string(parent),
                       std::to_string(co),std::to_string(owner)})
        digest=split_mix(digest,s);
    return digest;
}
std::uint64_t plan_fingerprint(const SplitRescueIntentPlan& plan){
    auto digest=split_header_digest(plan.intent_id,plan.expected_world_generation,
        plan.source_transport,plan.source_ferry,plan.origin,plan.destination);
    for(const auto& receiver:plan.receivers){
        digest=split_receiver_digest(digest,receiver.ferry,receiver.new_transport_id);
        for(const auto& family:receiver.families){
            digest=split_family_digest(digest,family.family_scope,family.region_scope,
                family.family_seed,family.guardian_id,family.co_guardian_id,
                family.expected_owner_generation);
            for(const auto child:family.child_ids)
                digest=split_mix(digest,std::to_string(child));
        }
    }
    return digest;
}
std::string string_id(std::uint64_t n){return std::to_string(n);}
std::uint64_t parse_id(const std::string& s){
    if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("malformed split rescue ID");
    std::size_t used=0;auto n=std::stoull(s,&used);
    if(!n||used!=s.size()||std::to_string(n)!=s)
        throw std::runtime_error("noncanonical split rescue ID");
    return n;
}
bool exists(sqlite3* db,const char* kind,const std::string& name){
    Query q(db,"SELECT 1 FROM sqlite_master WHERE type=? AND name=?");
    q.bind(1,std::string(kind));q.bind(2,name);return q.row();
}
bool meta_column(sqlite3* db){
    Query q(db,"PRAGMA table_info(world_meta)");
    while(q.row())if(q.str(1)=="split_rescue_plan_count")return true;
    return false;
}
void schema(sqlite3* db){
    if(!meta_column(db))execute(db,"ALTER TABLE world_meta ADD COLUMN "
       "split_rescue_plan_count INTEGER NOT NULL DEFAULT 0 CHECK(split_rescue_plan_count>=0)");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_rescue_intent ("
        "intent_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL CHECK(world_generation>0),"
        "source_transport TEXT NOT NULL,source_ferry TEXT NOT NULL,origin TEXT NOT NULL,"
        "destination TEXT NOT NULL,receiver_count INTEGER NOT NULL CHECK(receiver_count=2),"
        "family_count INTEGER NOT NULL CHECK(family_count BETWEEN 2 AND 8),"
        "adult_count INTEGER NOT NULL CHECK(adult_count>=2),"
        "child_count INTEGER NOT NULL CHECK(child_count>=2),"
        "fingerprint TEXT NOT NULL)");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_rescue_receiver ("
        "intent_id TEXT NOT NULL REFERENCES sg_split_rescue_intent(intent_id),"
        "ferry TEXT NOT NULL,new_transport TEXT NOT NULL,"
        "family_count INTEGER NOT NULL CHECK(family_count>=1),"
        "adult_count INTEGER NOT NULL CHECK(adult_count>=1),"
        "child_count INTEGER NOT NULL CHECK(child_count>=1),"
        "PRIMARY KEY(intent_id,ferry),UNIQUE(intent_id,new_transport))");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_rescue_family ("
        "intent_id TEXT NOT NULL REFERENCES sg_split_rescue_intent(intent_id),"
        "family_scope TEXT NOT NULL,region_scope TEXT NOT NULL,"
        "family_seed TEXT NOT NULL,guardian_id TEXT NOT NULL,co_guardian_id TEXT NOT NULL,"
        "owner_generation INTEGER NOT NULL CHECK(owner_generation>0),"
        "receiving_ferry TEXT NOT NULL,child_count INTEGER NOT NULL CHECK(child_count>=1),"
        "PRIMARY KEY(intent_id,family_scope),"
        "FOREIGN KEY(intent_id,receiving_ferry) REFERENCES sg_split_rescue_receiver(intent_id,ferry))");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_rescue_child ("
        "intent_id TEXT NOT NULL,family_scope TEXT NOT NULL,child_id TEXT NOT NULL,"
        "PRIMARY KEY(intent_id,child_id),"
        "FOREIGN KEY(intent_id,family_scope) REFERENCES sg_split_rescue_family(intent_id,family_scope))");
    for(const auto* table:{"sg_split_rescue_intent","sg_split_rescue_receiver",
                            "sg_split_rescue_family","sg_split_rescue_child"}){
        for(const auto* type:{"UPDATE","DELETE"}){
            const std::string name=std::string(table)+
                (std::string(type)=="UPDATE"?"_immutable_update":"_immutable_delete");
            const std::string s="CREATE TRIGGER IF NOT EXISTS "+name+" BEFORE "+type+
                " ON "+table+" BEGIN SELECT RAISE(ABORT,'split intent immutable'); END";
            execute(db,s.c_str());
        }
    }
}
std::set<std::pair<std::string,std::uint64_t>> active_children(
        sqlite3* db,std::uint64_t generation,EntityId ferry,std::uint64_t transport){
    Query q(db,"SELECT family_scope,child_id FROM sg_dependent_travel "
        "WHERE ferry=? AND transport_id=? AND board_generation<=? "
        "AND (arrival_generation IS NULL OR arrival_generation>?)");
    q.bind(1,string_id(ferry));q.bind(2,string_id(transport));
    q.bind(3,generation);q.bind(4,generation);
    std::set<std::pair<std::string,std::uint64_t>> children;
    while(q.row())if(!children.emplace(q.str(0),parse_id(q.str(1))).second)
        throw std::runtime_error("duplicate child on historical ferry");
    return children;
}
void prove_split_plan(sqlite3* db,const std::string& id,
                      std::uint64_t max_generation){
    Query h(db,"SELECT world_generation,source_transport,source_ferry,origin,destination,"
        "receiver_count,family_count,adult_count,child_count,fingerprint "
        "FROM sg_split_rescue_intent WHERE intent_id=?");
    h.bind(1,id);
    if(!h.row())throw std::runtime_error("split rescue plan not found");
    const auto gen=h.number(0),transport=parse_id(h.str(1)),source=parse_id(h.str(2));
    const auto origin=parse_id(h.str(3)),destination=parse_id(h.str(4));
    const auto nreceiver=h.number(5),nfamily=h.number(6),nadult=h.number(7),nchild=h.number(8);
    const auto expected_fingerprint=h.str(9);
    auto computed_fingerprint=split_header_digest(id,gen,transport,source,origin,destination);
    if(!gen||gen>max_generation||nreceiver!=2||nfamily<2||nfamily>8||nchild<2||h.row())
        throw std::runtime_error("split rescue invalid header");
    Query state(db,"SELECT state,checksum FROM snapshots WHERE generation=?");state.bind(1,gen);
    if(!state.row())throw std::runtime_error("split rescue pinned world snapshot missing");
    const auto* bytes=static_cast<const char*>(sqlite3_column_blob(state.ptr,0));
    const auto length=sqlite3_column_bytes(state.ptr,0);
    if(!bytes||length<=0||length>16*1024*1024)
        throw std::runtime_error("split rescue historic world snapshot corrupt");
    const auto historic=Simulation::from_snapshot(std::string(bytes,static_cast<std::size_t>(length)));
    if(std::to_string(historic.state_hash())!=state.str(1)||state.row())
        throw std::runtime_error("split rescue world snapshot checksum mismatch");
    if(historic.voyage_status(transport)!=3)
        throw std::runtime_error("split rescue plan not based on stranded voyage");
    const auto* first=historic.find(source);
    if(!first||first->type!=EntityType::Ship)throw std::runtime_error("split source ferry missing");
    std::set<std::uint64_t> actual_adults;
    for(const auto& [person,npc]:historic.npc_records())
        if(npc.location==source&&npc.transport_id==transport)actual_adults.insert(person);
    std::set<std::uint64_t> planned_adults;
    std::set<std::pair<std::string,std::uint64_t>> planned_children;
    std::set<EntityId> receiving_ferries;
    std::set<std::uint64_t> receiving_transports;
    std::uint64_t counted_family=0,counted_child=0;
    Query receivers(db,"SELECT ferry,new_transport,family_count,adult_count,child_count "
        "FROM sg_split_rescue_receiver WHERE intent_id=? ORDER BY ferry");receivers.bind(1,id);
    while(receivers.row()){
        const auto receiving=parse_id(receivers.str(0)),new_transport=parse_id(receivers.str(1));
        computed_fingerprint=split_receiver_digest(computed_fingerprint,receiving,new_transport);
        if(receiving==source||!receiving_ferries.insert(receiving).second||
           !receiving_transports.insert(new_transport).second||new_transport==transport||
           historic.voyage_status(new_transport)!=0)
            throw std::runtime_error("duplicate rescue ferry or new voyage ID");
        const auto* vessel=historic.find(receiving);
        if(!vessel||vessel->type!=EntityType::Ship||vessel->system_id!=first->system_id||
           vessel->dock_state!=DockState::InSpace||vessel->jump_state!=ShipJumpState::Ready)
            throw std::runtime_error("receiver not a co-located ready ferry");
        const auto dx=vessel->position.x-first->position.x;
        const auto dy=vessel->position.y-first->position.y;
        if(dx < -2500||dx>2500||dy < -2500||dy>2500||dx*dx+dy*dy>2500*2500)
            throw std::runtime_error("receiver too far from source for safe transfer");
        std::uint64_t local_families=0,local_adults=0,local_children=0;
        Query fam(db,"SELECT family_scope,region_scope,family_seed,guardian_id,"
            "co_guardian_id,owner_generation,child_count FROM sg_split_rescue_family "
            "WHERE intent_id=? AND receiving_ferry=? ORDER BY family_scope");
        fam.bind(1,id);fam.bind(2,string_id(receiving));
        while(fam.row()){
            ++local_families;
            const auto family=fam.str(0),region=fam.str(1);
            const auto seed=parse_id(fam.str(2));
            const auto parent=parse_id(fam.str(3));
            const auto co=fam.str(4)=="0"?0:parse_id(fam.str(4));
            computed_fingerprint=split_family_digest(computed_fingerprint,family,region,
                seed,parent,co,fam.number(5));
            if(family.empty()||region.empty()||!seed||!fam.number(5)||
               !parent||(co&&co==parent))throw std::runtime_error("split invalid family identity");
            Query region_binding(db,"SELECT region_scope FROM sg_dependent_region_binding "
                "WHERE family_scope=?");region_binding.bind(1,family);
            if(!region_binding.row()||region_binding.str(0)!=region||region_binding.row())
                throw std::runtime_error("split child regional provenance missing");
            for(const auto adult:{parent,co}){
                if(!adult)continue;
                const auto it=historic.npc_records().find(adult);
                if(!planned_adults.insert(adult).second||it==historic.npc_records().end()||
                   it->second.location!=source||it->second.transport_id!=transport||
                   it->second.home_ship!=origin)
                    throw std::runtime_error("split parent absent from original passenger roster");
                ++local_adults;
            }
            Query kids(db,"SELECT child_id FROM sg_split_rescue_child WHERE intent_id=? "
                "AND family_scope=? ORDER BY CAST(child_id AS INTEGER)");
            kids.bind(1,id);kids.bind(2,family);
            std::uint64_t family_children=0;
            while(kids.row()){
                ++family_children;const auto child=parse_id(kids.str(0));
                computed_fingerprint=split_mix(computed_fingerprint,string_id(child));
                if(!planned_children.emplace(family,child).second)
                    throw std::runtime_error("split duplicated child in manifest");
                Query birth(db,"SELECT parent_a,parent_b FROM npc_family_births "
                    "WHERE scope=? AND child_id=?");birth.bind(1,family);birth.bind(2,string_id(child));
                if(!birth.row())throw std::runtime_error("split dependent lacks birth certificate");
                const auto a=parse_id(birth.str(0)),b=parse_id(birth.str(1));
                if(!((a==parent&&b==co)||(a==co&&b==parent))||birth.row())
                    throw std::runtime_error("split dependent has mismatched birth parents");
                Query leg(db,"SELECT guardian_id,co_guardian_id,origin,destination "
                    "FROM sg_dependent_travel WHERE family_scope=? AND child_id=? AND "
                    "transport_id=? AND ferry=? AND board_generation<=? AND "
                    "(arrival_generation IS NULL OR arrival_generation>?)");
                leg.bind(1,family);leg.bind(2,string_id(child));leg.bind(3,string_id(transport));
                leg.bind(4,string_id(source));leg.bind(5,gen);leg.bind(6,gen);
                if(!leg.row()||parse_id(leg.str(0))!=parent||
                   leg.str(1)!=string_id(co)||parse_id(leg.str(2))!=origin||
                   parse_id(leg.str(3))!=destination||leg.row())
                    throw std::runtime_error("split dependent lacks historical physical berth");
            }
            if(family_children!=fam.number(6))
                throw std::runtime_error("split family manifest incomplete");
            local_children+=family_children;
        }
        if(local_families!=receivers.number(2)||local_adults!=receivers.number(3)||
           local_children!=receivers.number(4)||!local_families||!local_children||
           local_adults+local_children>16)
            throw std::runtime_error("split receiver manifest count or berth capacity violated");
        // Conservative, dedicated empty rescue ships are required for this
        // first split gate: no unrelated existing NPC or minor may be displaced.
        for(const auto& [person,npc]:historic.npc_records()){
            (void)person;
            if(npc.location==receiving&&npc.transport_id)
                throw std::runtime_error("split receiver already carries adult passengers");
        }
        Query occupied(db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? "
            "AND board_generation<=? AND (arrival_generation IS NULL OR arrival_generation>?)");
        occupied.bind(1,string_id(receiving));occupied.bind(2,gen);occupied.bind(3,gen);
        if(!occupied.row()||occupied.number(0)!=0)
            throw std::runtime_error("split receiver already has child berths");
        counted_family+=local_families;counted_child+=local_children;
    }
    if(string_id(computed_fingerprint)!=expected_fingerprint||
       receiving_ferries.size()!=nreceiver||counted_family!=nfamily||
       planned_adults.size()!=nadult||counted_child!=nchild||
       actual_adults!=planned_adults||
       active_children(db,gen,source,transport)!=planned_children)
        throw std::runtime_error("split rescue source ferry incomplete or surplus roster");
    Query orphan_f(db,"SELECT COUNT(*) FROM sg_split_rescue_family f LEFT JOIN "
        "sg_split_rescue_receiver r ON r.intent_id=f.intent_id AND r.ferry=f.receiving_ferry "
        "WHERE f.intent_id=? AND r.ferry IS NULL");orphan_f.bind(1,id);
    if(!orphan_f.row()||orphan_f.number(0))throw std::runtime_error("split orphan family allocation");
    Query orphan_c(db,"SELECT COUNT(*) FROM sg_split_rescue_child c LEFT JOIN "
        "sg_split_rescue_family f ON f.intent_id=c.intent_id AND "
        "f.family_scope=c.family_scope WHERE c.intent_id=? AND f.family_scope IS NULL");
    orphan_c.bind(1,id);
    if(!orphan_c.row()||orphan_c.number(0))throw std::runtime_error("split orphan child allocation");
}
void verify_all(sqlite3* db,std::uint64_t latest){
    const auto activated=meta_column(db);
    for(const auto* table:{"sg_split_rescue_intent","sg_split_rescue_receiver",
                            "sg_split_rescue_family","sg_split_rescue_child"}){
        if(exists(db,"table",table)!=activated)
            throw std::runtime_error("split rescue receipt activation/table mismatch");
        if(activated)for(const auto* suffix:{"_immutable_update","_immutable_delete"})
            if(!exists(db,"trigger",std::string(table)+suffix))
                throw std::runtime_error("split rescue immutable receipt trigger missing");
    }
    if(!activated)return;
    Query meta(db,"SELECT split_rescue_plan_count FROM world_meta WHERE singleton=1");
    if(!meta.row())throw std::runtime_error("split rescue count metadata missing");
    const auto expected=meta.number(0);
    Query intents(db,"SELECT intent_id FROM sg_split_rescue_intent ORDER BY intent_id");
    std::uint64_t seen=0;
    while(intents.row()){
        ++seen;const auto id=intents.str(0);
        if(id.empty()||id.size()>128)throw std::runtime_error("invalid split rescue intent ID");
        prove_split_plan(db,id,latest);
    }
    if(seen!=expected)throw std::runtime_error("split rescue anchor count was altered");
}
} // namespace

void verify_split_rescue_intents(const std::string& path,std::uint64_t latest){
    Database conn(path,SQLITE_OPEN_READONLY);
    verify_all(conn.db,latest);
}

void PersistentWorld::record_split_rescue_intent(const SplitRescueIntentPlan& plan,
                                                FailPoint fail){
    if(plan.intent_id.empty()||plan.intent_id.size()>128||
       plan.expected_world_generation!=generation_||!plan.source_transport||
       !plan.source_ferry||!plan.origin||!plan.destination||
       plan.origin==plan.destination||plan.receivers.size()!=2)
        throw std::invalid_argument("split rescue intent incomplete or stale");
    if(world_.voyage_status(plan.source_transport)!=3)
        throw std::runtime_error("split rescue requires a genuinely stranded original voyage");
    const auto voyage=world_.voyages_.find(plan.source_transport);
    if(voyage==world_.voyages_.end()||voyage->second.ferry!=plan.source_ferry||
       voyage->second.origin!=plan.origin)
        throw std::runtime_error("split rescue voyage identity mismatch");
    // Intent capture is only allowed with a quiet pending-command queue. It
    // cannot serve as an authorization for a competing actual rescue.
    if(!world_.commands_.empty())
        throw std::runtime_error("split rescue planning requires no pending host commands");
    Database conn(db_path_,SQLITE_OPEN_READWRITE);
    execute(conn.db,"BEGIN IMMEDIATE");
    try{
        Query generation(conn.db,"SELECT MAX(generation) FROM snapshots");
        if(!generation.row()||generation.number(0)!=generation_)
            throw std::runtime_error("split intent stale world generation at write lock");
        schema(conn.db);
        std::uint64_t estimated_families=0,estimated_adults=0,estimated_children=0;
        for(const auto& receiver:plan.receivers)
            for(const auto& family:receiver.families){
                ++estimated_families;
                estimated_adults+=family.co_guardian_id?2U:1U;
                estimated_children+=family.child_ids.size();
            }
        Query insert(conn.db,"INSERT INTO sg_split_rescue_intent VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        insert.bind(1,plan.intent_id);insert.bind(2,generation_);
        insert.bind(3,string_id(plan.source_transport));insert.bind(4,string_id(plan.source_ferry));
        insert.bind(5,string_id(plan.origin));insert.bind(6,string_id(plan.destination));
        insert.bind(7,static_cast<std::uint64_t>(2));
        insert.bind(8,estimated_families);insert.bind(9,estimated_adults);
        insert.bind(10,estimated_children);
        insert.bind(11,string_id(plan_fingerprint(plan)));insert.done();
        std::set<std::uint64_t> adults;
        std::set<std::pair<std::string,std::uint64_t>> children;
        std::set<std::string> scopes;
        EntityId prior=0;
        std::uint64_t all_families=0;
        for(const auto& receiver:plan.receivers){
            if(receiver.ferry<=prior||receiver.ferry==plan.source_ferry||
               !receiver.new_transport_id||receiver.new_transport_id==plan.source_transport||
               world_.voyages_.count(receiver.new_transport_id)||
               receiver.families.empty())
                throw std::invalid_argument("split allocation order or receiver invalid");
            prior=receiver.ferry;
            const auto* source=world_.find(plan.source_ferry);
            const auto* target=world_.find(receiver.ferry);
            if(!source||!target||source->type!=EntityType::Ship||
               target->type!=EntityType::Ship||source->system_id!=target->system_id||
               target->dock_state!=DockState::InSpace||target->jump_state!=ShipJumpState::Ready)
                throw std::runtime_error("split receiving vessel not safe and co-located");
            const auto dx=target->position.x-source->position.x;
            const auto dy=target->position.y-source->position.y;
            if(dx<-2500||dx>2500||dy<-2500||dy>2500||dx*dx+dy*dy>2500*2500)
                throw std::runtime_error("split receiving vessel is out of rescue range");
            for(const auto& [person,npc]:world_.npc_records()){
                (void)person;
                if(npc.location==receiver.ferry&&npc.transport_id)
                    throw std::runtime_error("split receiving ferry already carries passengers");
            }
            Query used(conn.db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? AND status=1");
            used.bind(1,string_id(receiver.ferry));
            if(!used.row()||used.number(0))
                throw std::runtime_error("split receiving ferry already has dependent berths");
            std::uint64_t num_adults=0,num_children=0;
            std::string prior_family;
            for(const auto& f:receiver.families){
                if(f.family_scope.empty()||f.family_scope<=prior_family||f.region_scope.empty()||
                   !f.family_seed||!f.guardian_id||!f.co_guardian_id||
                   f.guardian_id==f.co_guardian_id||!f.expected_owner_generation||
                   f.child_ids.empty()||f.child_ids.size()>15||
                   !std::is_sorted(f.child_ids.begin(),f.child_ids.end())||
                   std::adjacent_find(f.child_ids.begin(),f.child_ids.end())!=f.child_ids.end()||
                   !scopes.insert(f.family_scope).second)
                    throw std::invalid_argument("split family roster must be complete and canonical");
                prior_family=f.family_scope;++all_families;
                Query owner(conn.db,"SELECT owner,phase,generation FROM sg_regional_mode_authority "
                    "WHERE scope=?");owner.bind(1,f.region_scope);
                if(!owner.row()||owner.number(0)!=0||owner.number(1)!=0||
                   owner.number(2)!=f.expected_owner_generation||owner.row())
                    throw std::runtime_error("split family regional owner not stable cold");
                Query binding(conn.db,"SELECT region_scope FROM sg_dependent_region_binding "
                    "WHERE family_scope=?");binding.bind(1,f.family_scope);
                if(!binding.row()||binding.str(0)!=f.region_scope||binding.row())
                    throw std::runtime_error("split family region binding absent");
                NpcAppearanceStore family(conn.db,f.family_scope,f.family_seed,
                    [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
                for(const auto adult:{f.guardian_id,f.co_guardian_id}){
                    const auto npc=world_.npc_records().find(adult);
                    if(!adults.insert(adult).second||npc==world_.npc_records().end()||
                       npc->second.location!=plan.source_ferry||
                       npc->second.transport_id!=plan.source_transport||
                       npc->second.home_ship!=plan.origin||
                       family.current_residence(adult)!=plan.origin)
                        throw std::runtime_error("split guardian identity or custody mismatch");
                    ++num_adults;
                }
                Query births(conn.db,"SELECT child_id,parent_a,parent_b FROM npc_family_births "
                    "WHERE scope=? AND (parent_a=? OR parent_b=?) ORDER BY child_id");
                births.bind(1,f.family_scope);births.bind(2,string_id(f.guardian_id));
                births.bind(3,string_id(f.guardian_id));
                std::vector<std::uint64_t> expected;
                while(births.row()){
                    const auto child=parse_id(births.str(0));
                    const auto a=parse_id(births.str(1)),b=parse_id(births.str(2));
                    if(!((a==f.guardian_id&&b==f.co_guardian_id)||
                         (a==f.co_guardian_id&&b==f.guardian_id)))
                        throw std::runtime_error("split guardian not actual birth parent");
                    if(family.current_residence(child)==plan.origin)expected.push_back(child);
                }
                if(expected!=f.child_ids)throw std::runtime_error("split family omitted a dependent");
            }
            num_children=0;
            for(const auto& f:receiver.families)num_children+=f.child_ids.size();
            if(num_adults+num_children>16)
                throw std::runtime_error("split receiving ferry exceeds real berth capacity");
            Query radd(conn.db,"INSERT INTO sg_split_rescue_receiver VALUES(?,?,?,?,?,?)");
            radd.bind(1,plan.intent_id);radd.bind(2,string_id(receiver.ferry));
            radd.bind(3,string_id(receiver.new_transport_id));
            radd.bind(4,static_cast<std::uint64_t>(receiver.families.size()));
            radd.bind(5,num_adults);radd.bind(6,num_children);radd.done();
            for(const auto& f:receiver.families){
                Query fadd(conn.db,"INSERT INTO sg_split_rescue_family VALUES(?,?,?,?,?,?,?,?,?)");
                fadd.bind(1,plan.intent_id);fadd.bind(2,f.family_scope);fadd.bind(3,f.region_scope);
                fadd.bind(4,string_id(f.family_seed));fadd.bind(5,string_id(f.guardian_id));
                fadd.bind(6,string_id(f.co_guardian_id));fadd.bind(7,f.expected_owner_generation);
                fadd.bind(8,string_id(receiver.ferry));
                fadd.bind(9,static_cast<std::uint64_t>(f.child_ids.size()));fadd.done();
                for(const auto child:f.child_ids){
                    if(!children.emplace(f.family_scope,child).second)
                        throw std::runtime_error("split duplicate child");
                    Query leg(conn.db,"SELECT guardian_id,co_guardian_id FROM sg_dependent_travel "
                        "WHERE family_scope=? AND child_id=? AND transport_id=? AND ferry=? AND status=1");
                    leg.bind(1,f.family_scope);leg.bind(2,string_id(child));
                    leg.bind(3,string_id(plan.source_transport));leg.bind(4,string_id(plan.source_ferry));
                    if(!leg.row()||leg.str(0)!=string_id(f.guardian_id)||
                       leg.str(1)!=string_id(f.co_guardian_id)||leg.row())
                        throw std::runtime_error("split child physical berth mismatched");
                    Query child_add(conn.db,"INSERT INTO sg_split_rescue_child VALUES(?,?,?)");
                    child_add.bind(1,plan.intent_id);child_add.bind(2,f.family_scope);
                    child_add.bind(3,string_id(child));child_add.done();
                }
            }
        }
        std::set<std::uint64_t> source_adults;
        for(const auto& [person,npc]:world_.npc_records())
            if(npc.location==plan.source_ferry&&npc.transport_id==plan.source_transport)
                source_adults.insert(person);
        Query source_berths(conn.db,"SELECT family_scope,child_id FROM sg_dependent_travel "
            "WHERE ferry=? AND transport_id=? AND status=1");
        source_berths.bind(1,string_id(plan.source_ferry));
        source_berths.bind(2,string_id(plan.source_transport));
        std::set<std::pair<std::string,std::uint64_t>> source_children;
        while(source_berths.row())source_children.emplace(source_berths.str(0),parse_id(source_berths.str(1)));
        if(source_adults!=adults||source_children!=children||all_families<2||all_families>8)
            throw std::runtime_error("split rescue intent omitted source family or passenger");
        if(all_families!=estimated_families||adults.size()!=estimated_adults||
           children.size()!=estimated_children)
            throw std::runtime_error("split rescue proposed manifest counts disagree");
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected failure after split header/receipt write");
        execute(conn.db,"UPDATE world_meta SET split_rescue_plan_count="
            "split_rescue_plan_count+1 WHERE singleton=1");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected failure after split intent completion");
        verify_all(conn.db,generation_);
        execute(conn.db,"COMMIT");
    }catch(...){sqlite3_exec(conn.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}

// The split allocation is an IMMUTABLE authorization, not a movement record.
// Execution checks the original plan again under the very same world write lock.
void assert_split_execution_intent(sqlite3* db,const SplitRescueIntentPlan& plan,
                                   std::uint64_t current_generation){
    if(plan.intent_id.empty()||plan.receivers.size()!=2||
       plan.expected_world_generation>current_generation)
        throw std::invalid_argument("split rescue execution missing a current plan");
    prove_split_plan(db,plan.intent_id,current_generation);
    Query q(db,"SELECT world_generation,source_transport,source_ferry,origin,"
        "destination,fingerprint FROM sg_split_rescue_intent WHERE intent_id=?");
    q.bind(1,plan.intent_id);
    if(!q.row()||q.number(0)!=plan.expected_world_generation||
       q.str(1)!=string_id(plan.source_transport)||
       q.str(2)!=string_id(plan.source_ferry)||
       q.str(3)!=string_id(plan.origin)||q.str(4)!=string_id(plan.destination)||
       q.str(5)!=string_id(plan_fingerprint(plan))||q.row())
        throw std::runtime_error("split rescue command not authorized by recorded allocation");
}
void record_split_execution(sqlite3* db,const SplitRescueIntentPlan& plan,
                            std::uint64_t command_id,std::uint64_t generation,
                            std::uint64_t tick){
    assert_split_execution_intent(db,plan,generation);
    if(!command_id||generation<=plan.expected_world_generation||!tick)
        throw std::runtime_error("invalid split rescue completion generation");
    bool has_count=false;
    {Query cols(db,"PRAGMA table_info(world_meta)");
     while(cols.row())if(cols.str(1)=="split_rescue_execution_count")has_count=true;}
    if(!has_count)execute(db,"ALTER TABLE world_meta ADD COLUMN "
        "split_rescue_execution_count INTEGER NOT NULL DEFAULT 0 "
        "CHECK(split_rescue_execution_count>=0)");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_rescue_execution ("
        "intent_id TEXT PRIMARY KEY REFERENCES sg_split_rescue_intent(intent_id),"
        "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
        "command_id TEXT NOT NULL UNIQUE,tick INTEGER NOT NULL CHECK(tick>0),"
        "fingerprint TEXT NOT NULL)");
    for(const auto* op:{"UPDATE","DELETE"}){
        const auto name=std::string("sg_split_rescue_execution_immutable_")+
            (std::string(op)=="UPDATE"?"update":"delete");
        const auto sql="CREATE TRIGGER IF NOT EXISTS "+name+" BEFORE "+op+
            " ON sg_split_rescue_execution BEGIN SELECT RAISE(ABORT,'split execution immutable'); END";
        execute(db,sql.c_str());
    }
    Query add(db,"INSERT INTO sg_split_rescue_execution VALUES(?,?,?,?,?)");
    add.bind(1,plan.intent_id);add.bind(2,generation);
    add.bind(3,string_id(command_id));add.bind(4,tick);
    add.bind(5,string_id(plan_fingerprint(plan)));add.done();
    execute(db,"UPDATE world_meta SET split_rescue_execution_count="
        "split_rescue_execution_count+1 WHERE singleton=1");
}
void record_split_crew_execution(sqlite3* db,const SplitRescueIntentPlan& plan,
                std::uint64_t command_id,std::uint64_t generation,
                const std::vector<std::uint64_t>& crew,std::size_t primary_count){
    if(crew.size()<2||primary_count==0||primary_count>=crew.size()||
       !std::is_sorted(crew.begin(),crew.end())||
       std::adjacent_find(crew.begin(),crew.end())!=crew.end()||
       generation<=plan.expected_world_generation)
        throw std::runtime_error("crew rescue requires complete canonical operating crew");
    execute(db,"CREATE TABLE IF NOT EXISTS sg_split_crew_execution ("
        "intent_id TEXT NOT NULL REFERENCES sg_split_rescue_intent(intent_id),"
        "command_id TEXT NOT NULL,npc_id TEXT NOT NULL,"
        "source_ferry TEXT NOT NULL,receiver_ferry TEXT NOT NULL,"
        "transport_id TEXT NOT NULL,world_generation INTEGER NOT NULL CHECK(world_generation>0),"
        "PRIMARY KEY(command_id,npc_id))");
    for(const auto* op:{"UPDATE","DELETE"}){
        const std::string label=std::string(op)=="UPDATE"?"update":"delete";
        const auto sql="CREATE TRIGGER IF NOT EXISTS sg_split_crew_execution_immutable_"+label+
            " BEFORE "+std::string(op)+" ON sg_split_crew_execution BEGIN "
            "SELECT RAISE(ABORT,'crew rescue receipt immutable'); END";
        execute(db,sql.c_str());
    }
    bool anchor=false;
    {Query cols(db,"PRAGMA table_info(world_meta)");
     while(cols.row())if(cols.str(1)=="split_crew_execution_count")anchor=true;}
    if(!anchor)execute(db,"ALTER TABLE world_meta ADD COLUMN "
        "split_crew_execution_count INTEGER NOT NULL DEFAULT 0 "
        "CHECK(split_crew_execution_count>=0)");
    for(std::size_t i=0;i<crew.size();++i){
        const auto& receiver=plan.receivers[i<primary_count?0:1];
        Query receipt(db,"INSERT INTO sg_split_crew_execution VALUES(?,?,?,?,?,?,?)");
        receipt.bind(1,plan.intent_id);
        receipt.bind(2,string_id(command_id));
        receipt.bind(3,string_id(crew[i]));
        receipt.bind(4,string_id(plan.source_ferry));
        receipt.bind(5,string_id(receiver.ferry));
        receipt.bind(6,string_id(receiver.new_transport_id));
        receipt.bind(7,generation);receipt.done();
    }
    execute(db,"UPDATE world_meta SET split_crew_execution_count="
        "split_crew_execution_count+1 WHERE singleton=1");
}

void verify_split_executions(const std::string& path,std::uint64_t generation){
    Database conn(path,SQLITE_OPEN_READONLY);
    bool has_count=false;
    {Query cols(conn.db,"PRAGMA table_info(world_meta)");
     while(cols.row())if(cols.str(1)=="split_rescue_execution_count")has_count=true;}
    Query applied(conn.db,"SELECT COUNT(*) FROM historical_events WHERE action=? AND outcome=0");
    applied.bind(1,static_cast<std::uint64_t>(Action::HostSplitEvacuatePassengers));
    if(!applied.row())throw std::runtime_error("split applied event ledger inaccessible");
    const auto historical_count=applied.number(0);
    if(!has_count){
        if(exists(conn.db,"table","sg_split_rescue_execution")||historical_count)
            throw std::runtime_error("split execution table without activation anchor");
        return;
    }
    if(!exists(conn.db,"table","sg_split_rescue_execution")||
       !exists(conn.db,"trigger","sg_split_rescue_execution_immutable_update")||
       !exists(conn.db,"trigger","sg_split_rescue_execution_immutable_delete"))
        throw std::runtime_error("split rescue execution receipt/trigger missing");
    Query meta(conn.db,"SELECT split_rescue_execution_count FROM world_meta WHERE singleton=1");
    if(!meta.row())throw std::runtime_error("split rescue execution counter missing");
    const auto expected=meta.number(0);
    if(expected!=historical_count)
        throw std::runtime_error("split rescue applied world action lacks independent execution receipt");
    Query executions(conn.db,"SELECT intent_id,world_generation,command_id,tick,fingerprint "
        "FROM sg_split_rescue_execution ORDER BY intent_id");
    std::uint64_t count=0;
    while(executions.row()){
        ++count;
        const auto intent=executions.str(0);
        const auto committed=executions.number(1);
        const auto command=parse_id(executions.str(2)),tick=executions.number(3);
        if(committed>generation||!command||!tick)
            throw std::runtime_error("split execution beyond world generation");
        prove_split_plan(conn.db,intent,generation);
        Query header(conn.db,"SELECT fingerprint,world_generation FROM sg_split_rescue_intent "
            "WHERE intent_id=?");header.bind(1,intent);
        if(!header.row()||header.str(0)!=executions.str(4)||
           header.number(1)>=committed||header.row())
            throw std::runtime_error("split execution/intent proof detached");
        Query event(conn.db,"SELECT action,outcome,tick FROM historical_events "
            "WHERE generation=? AND command_id=?");
        event.bind(1,committed);event.bind(2,string_id(command));
        if(!event.row()||event.number(0)!=
              static_cast<std::uint64_t>(Action::HostSplitEvacuatePassengers)||
           event.number(1)!=static_cast<std::uint64_t>(Outcome::Applied)||
           event.number(2)!=tick||event.row())
            throw std::runtime_error("split rescue not backed by an applied host action");
        Query op(conn.db,"SELECT kind,tick FROM operation_log WHERE generation=?");
        op.bind(1,committed);
        if(!op.row()||op.number(0)!=2||op.number(1)!=tick||op.row())
            throw std::runtime_error("split rescue receipt not backed by committed world step");
        Query snap(conn.db,"SELECT state FROM snapshots WHERE generation=?");
        snap.bind(1,committed);
        if(!snap.row())throw std::runtime_error("split rescue historical snapshot not pinned");
        const auto* bytes=sqlite3_column_blob(snap.ptr,0);
        const auto length=sqlite3_column_bytes(snap.ptr,0);
        if(!bytes||length<=0||length>16*1024*1024)
            throw std::runtime_error("corrupt historical split rescue snapshot");
        const auto historic=Simulation::from_snapshot(
            std::string(static_cast<const char*>(bytes),static_cast<std::size_t>(length)));
        Query origin(conn.db,"SELECT source_transport,source_ferry FROM sg_split_rescue_intent "
            "WHERE intent_id=?");origin.bind(1,intent);
        if(!origin.row())throw std::runtime_error("split rescue original vanished");
        if(historic.voyage_status(parse_id(origin.str(0)))!=5)
            throw std::runtime_error("split rescue source voyage not closed");
        Query receivers(conn.db,"SELECT ferry,new_transport FROM sg_split_rescue_receiver "
            "WHERE intent_id=? ORDER BY ferry");receivers.bind(1,intent);
        std::uint64_t receiver_count=0;
        while(receivers.row()){
            ++receiver_count;
            const auto ferry=parse_id(receivers.str(0)),voyage=parse_id(receivers.str(1));
            if(historic.voyage_status(voyage)!=1)
                throw std::runtime_error("split rescue receiving voyage not active at commit");
            Query members(conn.db,"SELECT guardian_id,co_guardian_id FROM sg_split_rescue_family "
                "WHERE intent_id=? AND receiving_ferry=? ORDER BY family_scope");
            members.bind(1,intent);members.bind(2,string_id(ferry));
            while(members.row())for(const auto col:{0,1}){
                const auto adult=parse_id(members.str(col));
                const auto it=historic.npc_records().find(adult);
                if(it==historic.npc_records().end()||it->second.location!=ferry||
                   it->second.transport_id!=voyage)
                    throw std::runtime_error("split rescue adult assigned to wrong receiver");
            }
        }
        if(receiver_count!=2)throw std::runtime_error("split rescue missing receiving vessel");
    }
    if(count!=expected)throw std::runtime_error("split rescue execution anchor count corrupt");
    // v49: cross-check claimed native-crew movements against both pinned
    // adjacent world snapshots, independent crew sidecar receipts, and a
    // durable per-execution count. Missing evidence is corruption, not rescue.
    bool crew_anchor=false;
    {Query cols(conn.db,"PRAGMA table_info(world_meta)");
     while(cols.row())if(cols.str(1)=="split_crew_execution_count")crew_anchor=true;}
    std::uint64_t observed_crew_rescues=0;
    Query completed(conn.db,"SELECT intent_id,world_generation,command_id "
        "FROM sg_split_rescue_execution ORDER BY world_generation");
    while(completed.row()){
        const auto intent=completed.str(0);
        const auto gen=completed.number(1);
        const auto command=completed.str(2);
        Query source(conn.db,"SELECT source_ferry FROM sg_split_rescue_intent WHERE intent_id=?");
        source.bind(1,intent);
        if(!source.row())throw std::runtime_error("crew source rescue intent missing");
        const auto ship=parse_id(source.str(0));
        Query intent_gen(conn.db,"SELECT world_generation FROM sg_split_rescue_intent WHERE intent_id=?");
        intent_gen.bind(1,intent);
        if(!intent_gen.row())throw std::runtime_error("crew rescue intent generation unavailable");
        const auto predecessor=intent_gen.number(0);
        if(predecessor>=gen)throw std::runtime_error("crew rescue intent after execution");
        // The frozen allocation snapshot is pinned by the intent. The immediately
        // preceding generation may legitimately have been pruned after retries.
        Query snapshots(conn.db,"SELECT generation,state FROM snapshots WHERE generation IN (?,?) ORDER BY generation");
        snapshots.bind(1,predecessor);snapshots.bind(2,gen);
        if(!snapshots.row()||snapshots.number(0)!=predecessor)
            throw std::runtime_error("crew rescue allocation snapshot not pinned");
        auto read_snapshot=[&](Query& q)->Simulation{
            const auto* b=sqlite3_column_blob(q.ptr,1);
            const auto length=sqlite3_column_bytes(q.ptr,1);
            if(!b||length<=0||length>16*1024*1024)
                throw std::runtime_error("invalid crew historical snapshot");
            return Simulation::from_snapshot(std::string(static_cast<const char*>(b),
                static_cast<std::size_t>(length)));
        };
        const auto before=read_snapshot(snapshots);
        if(!snapshots.row()||snapshots.number(0)!=gen)
            throw std::runtime_error("crew rescue after snapshot absent");
        const auto after=read_snapshot(snapshots);
        std::vector<std::uint64_t> crew;
        for(const auto& [id,n]:before.npc_records())
            if(n.home_ship==ship&&n.assigned_site==ship&&n.role==NpcRole::Ship){
                const auto now=after.npc_records().find(id);
                if(now==after.npc_records().end()||
                   now->second.home_ship!=ship||now->second.assigned_site!=ship||
                   now->second.role!=NpcRole::Ship)
                    throw std::runtime_error("rescue changed/erased original crew employment");
                if(now->second.location!=n.location||now->second.transport_id!=n.transport_id)
                    crew.push_back(id);
            }
        if(crew.empty())continue; // legacy v48 passenger-only rescue
        ++observed_crew_rescues;
        if(!crew_anchor||!exists(conn.db,"table","sg_split_crew_execution")||
           !exists(conn.db,"trigger","sg_split_crew_execution_immutable_update")||
           !exists(conn.db,"trigger","sg_split_crew_execution_immutable_delete")||
           crew.size()!=static_cast<std::size_t>(before.colony_ship(ship)->crew))
            throw std::runtime_error("crew rescue receipts or complete roster missing");
        Query receivers(conn.db,"SELECT ferry,new_transport FROM sg_split_rescue_receiver "
            "WHERE intent_id=? ORDER BY ferry");receivers.bind(1,intent);
        std::map<std::uint64_t,std::uint64_t> mapped;
        while(receivers.row())mapped.emplace(parse_id(receivers.str(0)),
                                                parse_id(receivers.str(1)));
        if(mapped.size()!=2)throw std::runtime_error("crew receiving ferries missing");
        Query members(conn.db,"SELECT npc_id,source_ferry,receiver_ferry,transport_id,world_generation "
            "FROM sg_split_crew_execution WHERE intent_id=? AND command_id=? ORDER BY npc_id");
        members.bind(1,intent);members.bind(2,command);
        std::set<std::uint64_t> recovered;
        while(members.row()){
            const auto id=parse_id(members.str(0)),src=parse_id(members.str(1));
            const auto receiver=parse_id(members.str(2)),voyage=parse_id(members.str(3));
            const auto after_npc=after.npc_records().find(id);
            const auto old=before.npc_records().find(id);
            if(src!=ship||members.number(4)!=gen||!recovered.insert(id).second||
               !std::binary_search(crew.begin(),crew.end(),id)||
               !mapped.count(receiver)||mapped.at(receiver)!=voyage||
               old==before.npc_records().end()||old->second.location!=ship||
               old->second.transport_id||after_npc==after.npc_records().end()||
               after_npc->second.location!=receiver||after_npc->second.transport_id!=voyage)
                throw std::runtime_error("crew rescue sidecar disagrees with historical custody");
        }
        if(recovered.size()!=crew.size())
            throw std::runtime_error("crew rescue member receipt incomplete");
    }
    if(observed_crew_rescues>0 && !crew_anchor)
        throw std::runtime_error("split crew rescue counter deleted");
    if(crew_anchor){
        if(!exists(conn.db,"table","sg_split_crew_execution")||
           !exists(conn.db,"trigger","sg_split_crew_execution_immutable_update")||
           !exists(conn.db,"trigger","sg_split_crew_execution_immutable_delete"))
            throw std::runtime_error("crew rescue ledger deleted");
        Query q(conn.db,"SELECT split_crew_execution_count FROM world_meta WHERE singleton=1");
        if(!q.row()||q.number(0)!=observed_crew_rescues)
            throw std::runtime_error("crew rescue execution count mismatch");
        Query unexpected(conn.db,"SELECT COUNT(DISTINCT command_id) FROM sg_split_crew_execution");
        if(!unexpected.row()||unexpected.number(0)!=observed_crew_rescues)
            throw std::runtime_error("orphan crew rescue receipts present");
    }
}

VerifiedSplitRescueIntent PersistentWorld::verify_split_rescue_intent(
        const std::string& id) const {
    if(id.empty())throw std::invalid_argument("split rescue intent ID required");
    Database conn(db_path_,SQLITE_OPEN_READONLY);
    prove_split_plan(conn.db,id,generation_);
    Query h(conn.db,"SELECT world_generation,source_transport,family_count,adult_count,child_count "
        "FROM sg_split_rescue_intent WHERE intent_id=?");h.bind(1,id);
    if(!h.row())throw std::runtime_error("missing split rescue manifest");
    VerifiedSplitRescueIntent result{};
    result.intent_id=id;result.generation=h.number(0);
    result.source_transport=parse_id(h.str(1));
    result.families=static_cast<std::size_t>(h.number(2));
    result.adults=static_cast<std::size_t>(h.number(3));
    result.children=static_cast<std::size_t>(h.number(4));
    Query receivers(conn.db,"SELECT ferry FROM sg_split_rescue_receiver "
        "WHERE intent_id=? ORDER BY ferry");receivers.bind(1,id);
    while(receivers.row())result.receiving_ferries.push_back(parse_id(receivers.str(0)));
    return result;
}
} // namespace galaxy
