#include "galaxy/NpcAppearanceGenome.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy {
namespace {
constexpr std::uint64_t kFnvOffset=14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime=1099511628211ULL;

std::uint64_t stable_scope_hash(const std::string& scope) noexcept {
    auto h=kFnvOffset;
    for(unsigned char c:scope){h^=c;h*=kFnvPrime;}
    return h;
}
std::uint64_t splitmix(std::uint64_t& v) noexcept {
    v+=0x9E3779B97F4A7C15ULL;
    auto z=v;
    z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;
    z=(z^(z>>27))*0x94D049BB133111EBULL;
    return z^(z>>31);
}
std::uint64_t mix(std::uint64_t seed, std::uint64_t value) noexcept {
    auto state=seed^value;
    return splitmix(state);
}
void hash_u64(std::uint64_t& h, std::uint64_t val) noexcept {
    for(int i=0;i<8;++i){h^=(val>>(i*8))&0xffu;h*=kFnvPrime;}
}
std::uint64_t checksum(const NpcAppearanceProfile& p) noexcept {
    std::uint64_t h=kFnvOffset;
    hash_u64(h,kFaceGenomeVersion);hash_u64(h,p.npc_id);
    hash_u64(h,p.parent_a);hash_u64(h,p.parent_b);hash_u64(h,p.generation);
    for(const auto a:p.alleles){h^=a;h*=kFnvPrime;}
    return h;
}
std::uint64_t bounded(std::uint64_t& state,std::uint64_t upper) noexcept {
    // Uniform enough for visual variation; no dependency on STL RNG internals.
    return splitmix(state)%upper;
}
std::string decimal(std::uint64_t n){return std::to_string(n);}
std::uint64_t parse_u64(const unsigned char* s) {
    if(!s)throw std::runtime_error("null appearance metadata");
    const auto v=std::string(reinterpret_cast<const char*>(s));
    if(v.empty()||v.size()>20||v.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("invalid appearance numeric field");
    std::size_t pos=0;
    const auto n=std::stoull(v,&pos);
    if(pos!=v.size())throw std::runtime_error("invalid appearance numeric value");
    return n;
}
void sql_exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string msg=err?err:sqlite3_errmsg(db);sqlite3_free(err);
        throw std::runtime_error("appearance sqlite: "+msg);
    }
}
struct Statement {
    sqlite3_stmt* s=nullptr;
    Statement(sqlite3* db,const char* q){
        if(sqlite3_prepare_v2(db,q,-1,&s,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("appearance prepare: ")+sqlite3_errmsg(db));
    }
    ~Statement(){sqlite3_finalize(s);}
    void text(int col,const std::string& value){
        if(sqlite3_bind_text(s,col,value.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("appearance bind text failed");
    }
    void number(int col,int value){
        if(sqlite3_bind_int(s,col,value)!=SQLITE_OK)
            throw std::runtime_error("appearance bind number failed");
    }
    void blob(int col,const void* data,int bytes){
        if(sqlite3_bind_blob(s,col,data,bytes,SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("appearance bind blob failed");
    }
    int step(){const int r=sqlite3_step(s);
        if(r!=SQLITE_ROW && r!=SQLITE_DONE)
            throw std::runtime_error(std::string("appearance step: ")+sqlite3_errmsg(sqlite3_db_handle(s)));
        return r;
    }
};

NpcAppearanceProfile founder(std::uint64_t id,std::uint64_t seed){
    NpcAppearanceProfile p;p.npc_id=id;
    auto s=mix(seed, id^0x6e0d4e3e1b52a97fULL);
    for(std::size_t trait=0;trait<kFaceTraitCount;++trait){
        const int baseline=24+static_cast<int>(bounded(s,208));
        for(std::size_t locus=0;locus<kFaceLociPerTrait;++locus){
            for(std::size_t strand=0;strand<2;++strand){
                const int jitter=static_cast<int>(bounded(s,71))-35;
                const int allele=std::clamp(baseline+jitter,0,255);
                p.alleles[(trait*kFaceLociPerTrait+locus)*2+strand]=static_cast<std::uint8_t>(allele);
            }
        }
    }
    p.digest=checksum(p);return p;
}
NpcAppearanceProfile offspring(std::uint64_t id, const NpcAppearanceProfile& a,
                                const NpcAppearanceProfile& b, std::uint64_t seed){
    NpcAppearanceProfile p;
    p.npc_id=id;p.parent_a=a.npc_id;p.parent_b=b.npc_id;
    p.generation=std::max(a.generation,b.generation)+1;
    auto s=mix(mix(seed,id^0xd08c74fb52a1e937ULL),a.digest^b.digest);
    for(std::size_t locus=0;locus<kFaceAlleles/2;++locus){
        // Each of two children alleles is inherited from the corresponding
        // parent's two copies; occasional bounded variation.
        const auto a_pos=locus*2+bounded(s,2);
        const auto b_pos=locus*2+bounded(s,2);
        int aa=a.alleles[a_pos],bb=b.alleles[b_pos];
        if(bounded(s,1000)<8)aa=std::clamp(aa+static_cast<int>(bounded(s,17))-8,0,255);
        if(bounded(s,1000)<8)bb=std::clamp(bb+static_cast<int>(bounded(s,17))-8,0,255);
        p.alleles[locus*2]=static_cast<std::uint8_t>(aa);
        p.alleles[locus*2+1]=static_cast<std::uint8_t>(bb);
    }
    p.digest=checksum(p);return p;
}
} // namespace

std::array<float,kFaceTraitCount> NpcAppearanceProfile::facial_morphs() const noexcept {
    std::array<float,kFaceTraitCount> values{};
    for(std::size_t t=0;t<kFaceTraitCount;++t){
        unsigned total=0;
        for(std::size_t j=0;j<kFaceLociPerTrait*2;++j)
            total+=alleles[t*kFaceLociPerTrait*2+j];
        values[t]=static_cast<float>(total)/static_cast<float>(kFaceLociPerTrait*2*255);
    }
    return values;
}

NpcAppearanceStore::NpcAppearanceStore(const std::string& path,std::string scope,
                                        std::uint64_t seed,AdmitExistingNpc admit)
    :scope_(std::move(scope)),seed_(seed),admit_(std::move(admit)) {
    if(path.empty()||scope_.empty()||scope_.size()>128||!admit_)
        throw std::invalid_argument("appearance store needs path, world scope, and authoritative NPC admission");
    if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        const std::string err=db_?sqlite3_errmsg(db_):"cannot open sqlite";
        if(db_)sqlite3_close(db_);
        db_=nullptr;
        throw std::runtime_error("appearance sqlite open: "+err);
    }
    try {
        sqlite3_busy_timeout(db_,5000);
        sql_exec(db_,"PRAGMA foreign_keys=ON;");
        sql_exec(db_,"PRAGMA journal_mode=WAL;");
        sql_exec(db_,"PRAGMA synchronous=FULL;");
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_appearance_meta("
                     "scope TEXT PRIMARY KEY, schema_version INTEGER NOT NULL, world_seed TEXT NOT NULL)");
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_appearance_profiles("
                     "scope TEXT NOT NULL, npc_id TEXT NOT NULL,"
                     "parent_a TEXT NOT NULL, parent_b TEXT NOT NULL,"
                     "generation INTEGER NOT NULL CHECK(generation>=0),"
                     "alleles BLOB NOT NULL CHECK(length(alleles)=160),"
                     "digest TEXT NOT NULL, PRIMARY KEY(scope,npc_id))");
        // All family state is on the exact same SQLite connection as appearance
        // inserts; a committed birth cannot lack its inherited face or census.
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_family_census("
            "scope TEXT NOT NULL, settlement TEXT NOT NULL,"
            "initial_population INTEGER NOT NULL CHECK(initial_population>=1),"
            "current_population INTEGER NOT NULL CHECK(current_population>=0),"
            "revision INTEGER NOT NULL CHECK(revision>=0),"
            "last_tick INTEGER NOT NULL CHECK(last_tick>=0),"
            "PRIMARY KEY(scope,settlement))");
        // DEMOG-05q6b: a colony can lose its last living resident. Migrate
        // the legacy >=1 CHECK constraint transactionally without changing
        // any census identity, revision, population or birth/death receipt.
        {
            std::string schema;
            {
                Statement ddl(db_,"SELECT sql FROM sqlite_master WHERE type='table' AND name='npc_family_census'");
                if(ddl.step()!=SQLITE_ROW)throw std::runtime_error("family census table disappeared");
                const auto* raw=sqlite3_column_text(ddl.s,0);
                if(!raw)throw std::runtime_error("missing family census schema");
                schema=reinterpret_cast<const char*>(raw);
            } // finalize schema reader before ALTER TABLE
            if(schema.find("current_population>=1")!=std::string::npos){
                sql_exec(db_,"BEGIN IMMEDIATE");
                try {
                    sql_exec(db_,"ALTER TABLE npc_family_census RENAME TO npc_family_census_pre_zero");
                    sql_exec(db_,"CREATE TABLE npc_family_census("
                                 "scope TEXT NOT NULL, settlement TEXT NOT NULL,"
                                 "initial_population INTEGER NOT NULL CHECK(initial_population>=1),"
                                 "current_population INTEGER NOT NULL CHECK(current_population>=0),"
                                 "revision INTEGER NOT NULL CHECK(revision>=0),"
                                 "last_tick INTEGER NOT NULL CHECK(last_tick>=0),"
                                 "PRIMARY KEY(scope,settlement))");
                    sql_exec(db_,"INSERT INTO npc_family_census SELECT * FROM npc_family_census_pre_zero");
                    sql_exec(db_,"DROP TABLE npc_family_census_pre_zero");
                    sql_exec(db_,"COMMIT");
                }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
            }else if(schema.find("current_population>=0")==std::string::npos){
                throw std::runtime_error("unsupported existing family census constraint");
            }
        }
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_family_people("
            "scope TEXT NOT NULL,npc_id TEXT NOT NULL,settlement TEXT NOT NULL,"
            "born_tick INTEGER NOT NULL CHECK(born_tick>=0),"
            "parent_a TEXT NOT NULL,parent_b TEXT NOT NULL,"
            "PRIMARY KEY(scope,npc_id))");
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_family_births("
            "scope TEXT NOT NULL,event_id TEXT NOT NULL,child_id TEXT NOT NULL,"
            "settlement TEXT NOT NULL,parent_a TEXT NOT NULL,parent_b TEXT NOT NULL,"
            "tick INTEGER NOT NULL CHECK(tick>0),census_revision INTEGER NOT NULL,"
            "appearance_digest TEXT NOT NULL,"
            "PRIMARY KEY(scope,event_id), UNIQUE(scope,child_id))");
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_family_deaths("
            "scope TEXT NOT NULL,event_id TEXT NOT NULL,npc_id TEXT NOT NULL,"
            "settlement TEXT NOT NULL,tick INTEGER NOT NULL CHECK(tick>0),"
            "census_revision INTEGER NOT NULL,PRIMARY KEY(scope,event_id),"
            "UNIQUE(scope,npc_id))");
        sql_exec(db_,"CREATE TABLE IF NOT EXISTS npc_family_moves("
            "scope TEXT NOT NULL,event_id TEXT NOT NULL,npc_id TEXT NOT NULL,"
            "origin TEXT NOT NULL,destination TEXT NOT NULL,tick INTEGER NOT NULL CHECK(tick>0),"
            "origin_revision INTEGER NOT NULL,destination_revision INTEGER NOT NULL,"
            "PRIMARY KEY(scope,event_id))");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_move_no_update "
            "BEFORE UPDATE ON npc_family_moves BEGIN SELECT RAISE(ABORT,'move immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_move_no_delete "
            "BEFORE DELETE ON npc_family_moves BEGIN SELECT RAISE(ABORT,'move immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_death_no_update "
            "BEFORE UPDATE ON npc_family_deaths BEGIN SELECT RAISE(ABORT,'death immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_death_no_delete "
            "BEFORE DELETE ON npc_family_deaths BEGIN SELECT RAISE(ABORT,'death immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_birth_no_update "
            "BEFORE UPDATE ON npc_family_births BEGIN SELECT RAISE(ABORT,'birth immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_birth_no_delete "
            "BEFORE DELETE ON npc_family_births BEGIN SELECT RAISE(ABORT,'birth immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_people_no_update "
            "BEFORE UPDATE ON npc_family_people BEGIN SELECT RAISE(ABORT,'family person immutable'); END");
        sql_exec(db_,"CREATE TRIGGER IF NOT EXISTS npc_family_people_no_delete "
            "BEFORE DELETE ON npc_family_people BEGIN SELECT RAISE(ABORT,'family person immutable'); END");
        begin();
        try {
            Statement q(db_,"SELECT schema_version,world_seed FROM npc_appearance_meta WHERE scope=?");q.text(1,scope_);
            if(q.step()==SQLITE_ROW){
                if(sqlite3_column_int(q.s,0)!=static_cast<int>(kFaceGenomeVersion) ||
                   parse_u64(sqlite3_column_text(q.s,1))!=seed_)
                    throw std::runtime_error("appearance world seed or schema mismatch");
            }else{
                Statement ins(db_,"INSERT INTO npc_appearance_meta(scope,schema_version,world_seed) VALUES(?,?,?)");
                ins.text(1,scope_);ins.number(2,kFaceGenomeVersion);ins.text(3,decimal(seed_));
                if(ins.step()!=SQLITE_DONE)throw std::runtime_error("appearance metadata insert failed");
            }
            commit();
        }catch(...){rollback();throw;}
    }catch(...){sqlite3_close(db_);db_=nullptr;throw;}
}
NpcAppearanceStore::NpcAppearanceStore(sqlite3* db,std::string scope,std::uint64_t seed,
                                      AdmitExistingNpc admit)
    :db_(db),borrowed_transaction_(true),scope_(std::move(scope)),seed_(seed),admit_(std::move(admit)) {
    if(!db_||sqlite3_get_autocommit(db_)!=0||scope_.empty()||!admit_)
        throw std::invalid_argument("appearance borrowed connection requires active world transaction");
    Statement meta(db_,"SELECT schema_version,world_seed FROM npc_appearance_meta WHERE scope=?");
    meta.text(1,scope_);
    if(meta.step()!=SQLITE_ROW || sqlite3_column_int(meta.s,0)!=static_cast<int>(kFaceGenomeVersion) ||
       parse_u64(sqlite3_column_text(meta.s,1))!=seed_)
        throw std::runtime_error("appearance world scope/seed not initialized");
}
NpcAppearanceStore::~NpcAppearanceStore(){if(db_ && !borrowed_transaction_)sqlite3_close(db_);}
void NpcAppearanceStore::begin(){if(!borrowed_transaction_)sql_exec(db_,"BEGIN IMMEDIATE");}
void NpcAppearanceStore::commit(){if(!borrowed_transaction_)sql_exec(db_,"COMMIT");}
void NpcAppearanceStore::rollback() noexcept {if(!borrowed_transaction_)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
bool NpcAppearanceStore::is_born_npc(std::uint64_t id) const {
    Statement q(db_,"SELECT born_tick FROM npc_family_people WHERE scope=? AND npc_id=?");
    q.text(1,scope_);q.text(2,decimal(id));
    return q.step()==SQLITE_ROW && sqlite3_column_int64(q.s,0)>0;
}
void NpcAppearanceStore::require_existing_npc(std::uint64_t id) const {
    if(!id||(!admit_(id)&&!is_born_npc(id)))
        throw std::invalid_argument("NPC identity lacks authoritative founder or journaled family birth");
}
NpcAppearanceProfile NpcAppearanceStore::lookup(std::uint64_t id,bool& found) const {
    Statement q(db_,"SELECT parent_a,parent_b,generation,alleles,digest FROM npc_appearance_profiles WHERE scope=? AND npc_id=?");
    q.text(1,scope_);q.text(2,decimal(id));
    NpcAppearanceProfile p;p.npc_id=id;
    if(q.step()==SQLITE_DONE){found=false;return p;}
    found=true;
    p.parent_a=parse_u64(sqlite3_column_text(q.s,0));
    p.parent_b=parse_u64(sqlite3_column_text(q.s,1));
    const auto gen=sqlite3_column_int64(q.s,2);
    if(gen<0||gen>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("invalid appearance generation");
    p.generation=static_cast<std::uint32_t>(gen);
    if(sqlite3_column_bytes(q.s,3)!=static_cast<int>(kFaceAlleles))
        throw std::runtime_error("appearance genotype length corrupt");
    std::memcpy(p.alleles.data(),sqlite3_column_blob(q.s,3),p.alleles.size());
    p.digest=parse_u64(sqlite3_column_text(q.s,4));
    if(checksum(p)!=p.digest)throw std::runtime_error("appearance checksum failure");
    if((p.parent_a==0)!=(p.parent_b==0))throw std::runtime_error("incomplete parent record");
    if((p.parent_a==0)!=(p.generation==0))throw std::runtime_error("appearance lineage generation invalid");
    return p;
}
void NpcAppearanceStore::insert(const NpcAppearanceProfile& p){
    Statement q(db_,"INSERT INTO npc_appearance_profiles(scope,npc_id,parent_a,parent_b,generation,alleles,digest) VALUES(?,?,?,?,?,?,?)");
    q.text(1,scope_);q.text(2,decimal(p.npc_id));q.text(3,decimal(p.parent_a));q.text(4,decimal(p.parent_b));
    q.number(5,static_cast<int>(p.generation));q.blob(6,p.alleles.data(),static_cast<int>(p.alleles.size()));
    q.text(7,decimal(p.digest));q.step();
}
NpcAppearanceProfile NpcAppearanceStore::ensure_founder(std::uint64_t id){
    require_existing_npc(id);begin();
    try{
        bool found=false;auto p=lookup(id,found);
        if(found){if(p.parent_a||p.parent_b)throw std::invalid_argument("existing NPC is not a founder");}
        else {p=founder(id,mix(seed_,stable_scope_hash(scope_)));insert(p);}
        commit();return p;
    }catch(...){rollback();throw;}
}
NpcAppearanceProfile NpcAppearanceStore::ensure_child(std::uint64_t id,std::uint64_t aa,std::uint64_t bb){
    require_existing_npc(id);
    if(!aa||!bb||aa==bb||id==aa||id==bb)throw std::invalid_argument("invalid appearance parent IDs");
    if(aa>bb)std::swap(aa,bb); // stable parent order
    begin();
    try {
        bool found=false;auto existing=lookup(id,found);
        if(found){
            if(existing.parent_a!=aa||existing.parent_b!=bb)
                throw std::invalid_argument("cannot replace immutable NPC parentage");
            commit();return existing;
        }
        bool found_a=false,found_b=false;
        const auto a=lookup(aa,found_a),b=lookup(bb,found_b);
        if(!found_a||!found_b||!admit_(aa)||!admit_(bb))
            throw std::invalid_argument("parents must be existing admitted NPC profiles");
        if(std::max(a.generation,b.generation)>=static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            throw std::overflow_error("appearance family generation overflow");
        auto child=offspring(id,a,b,mix(seed_,stable_scope_hash(scope_)));
        insert(child);commit();return child;
    }catch(...){rollback();throw;}
}
bool NpcAppearanceStore::exists(std::uint64_t id) const {
    if(!id)return false;
    bool found=false;lookup(id,found);return found;
}
NpcAppearanceProfile NpcAppearanceStore::get(std::uint64_t id) const {
    require_existing_npc(id);bool found=false;auto p=lookup(id,found);
    if(!found)throw std::out_of_range("NPC appearance not materialized");
    return p;
}
std::uint64_t NpcAppearanceStore::named_profile_count() const {
    Statement q(db_,"SELECT COUNT(*) FROM npc_appearance_profiles WHERE scope=?");q.text(1,scope_);
    if(q.step()!=SQLITE_ROW)throw std::runtime_error("appearance count failed");
    return static_cast<std::uint64_t>(sqlite3_column_int64(q.s,0));
}

} // namespace galaxy

namespace galaxy {
namespace {
constexpr std::uint64_t kMinFamilyChildId = 1ULL<<62; // separate from playable NPC IDs
constexpr std::uint64_t kMaxCensus = 1000000000000ULL;
NpcFamilyCensus read_census(sqlite3* db, const std::string& scope,std::uint64_t settlement){
    Statement q(db,"SELECT initial_population,current_population,revision,last_tick "
        "FROM npc_family_census WHERE scope=? AND settlement=?");
    q.text(1,scope);q.text(2,decimal(settlement));
    if(q.step()!=SQLITE_ROW)throw std::out_of_range("family settlement not enrolled");
    NpcFamilyCensus r;r.settlement=settlement;
    r.initial_population=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,0));
    r.current_population=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,1));
    r.revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,2));
    r.last_tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,3));
    if(!r.initial_population||r.initial_population>kMaxCensus||
       r.current_population>kMaxCensus)
        throw std::runtime_error("family census corrupt");
    return r;
}
} // namespace

void NpcAppearanceStore::register_settlement(std::uint64_t settlement, std::uint64_t population){
    if(!settlement||!population||population>kMaxCensus)
        throw std::invalid_argument("invalid family settlement baseline");
    begin();
    try {
        Statement q(db_,"SELECT initial_population FROM npc_family_census WHERE scope=? AND settlement=?");
        q.text(1,scope_);q.text(2,decimal(settlement));
        if(q.step()==SQLITE_ROW){
            if(static_cast<std::uint64_t>(sqlite3_column_int64(q.s,0))!=population)
                throw std::invalid_argument("immutable settlement census baseline changed");
        }else{
            Statement ins(db_,"INSERT INTO npc_family_census "
                "(scope,settlement,initial_population,current_population,revision,last_tick) "
                "VALUES(?,?,?,?,0,0)");
            ins.text(1,scope_);ins.text(2,decimal(settlement));
            ins.number(3,static_cast<int>(0)); // overwritten below by safe i64 bind
            // Population may exceed the signed 32-bit integer range.
            if(sqlite3_bind_int64(ins.s,3,static_cast<sqlite3_int64>(population))!=SQLITE_OK ||
               sqlite3_bind_int64(ins.s,4,static_cast<sqlite3_int64>(population))!=SQLITE_OK)
                throw std::runtime_error("family census bind failed");
            ins.step();
        }
        commit();
    }catch(...){rollback();throw;}
}

NpcFamilyCensus NpcAppearanceStore::family_census(std::uint64_t settlement) const {
    if(!settlement)throw std::invalid_argument("settlement ID required");
    return read_census(db_,scope_,settlement);
}

NpcAppearanceProfile NpcAppearanceStore::enroll_family_founder(std::uint64_t npc_id,
                                                                 std::uint64_t settlement){
    // A founder must already be a named live NPC in the authoritative world.
    if(!npc_id||npc_id>=kMinFamilyChildId||!admit_(npc_id))
        throw std::invalid_argument("founder identity is not admitted by live world");
    begin();
    try {
        const auto census=read_census(db_,scope_,settlement);
        Statement q(db_,"SELECT settlement,born_tick FROM npc_family_people WHERE scope=? AND npc_id=?");
        q.text(1,scope_);q.text(2,decimal(npc_id));
        const bool existing=q.step()==SQLITE_ROW;
        if(existing && (parse_u64(sqlite3_column_text(q.s,0))!=settlement ||
                         sqlite3_column_int64(q.s,1)!=0))
            throw std::invalid_argument("founder already attached to different family settlement");
        bool found=false;auto face=lookup(npc_id,found);
        if(found && (face.parent_a||face.parent_b))
            throw std::invalid_argument("child cannot be enrolled as a founder");
        if(!found){face=founder(npc_id,mix(seed_,stable_scope_hash(scope_)));insert(face);}
        if(!existing){
            Statement n(db_,"SELECT COUNT(*) FROM npc_family_people WHERE scope=? AND settlement=?");
            n.text(1,scope_);n.text(2,decimal(settlement));
            if(n.step()!=SQLITE_ROW||sqlite3_column_int64(n.s,0)>=static_cast<sqlite3_int64>(census.current_population))
                throw std::invalid_argument("named family records exceed settlement population");
            Statement ins(db_,"INSERT INTO npc_family_people "
                "(scope,npc_id,settlement,born_tick,parent_a,parent_b) VALUES(?,?,?,0,'0','0')");
            ins.text(1,scope_);ins.text(2,decimal(npc_id));ins.text(3,decimal(settlement));ins.step();
        }
        commit();return face;
    }catch(...){rollback();throw;}
}

NpcBirthRecord NpcAppearanceStore::get_birth(std::uint64_t event_id) const {
    Statement q(db_,"SELECT child_id,settlement,parent_a,parent_b,tick,census_revision,appearance_digest "
        "FROM npc_family_births WHERE scope=? AND event_id=?");
    q.text(1,scope_);q.text(2,decimal(event_id));
    if(q.step()!=SQLITE_ROW)throw std::out_of_range("birth event not found");
    NpcBirthRecord r;r.event_id=event_id;
    r.child_id=parse_u64(sqlite3_column_text(q.s,0));
    r.settlement=parse_u64(sqlite3_column_text(q.s,1));
    r.parent_a=parse_u64(sqlite3_column_text(q.s,2));
    r.parent_b=parse_u64(sqlite3_column_text(q.s,3));
    r.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,4));
    r.census_revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,5));
    r.appearance_digest=parse_u64(sqlite3_column_text(q.s,6));
    bool found=false;const auto child=lookup(r.child_id,found);
    if(!found||child.digest!=r.appearance_digest||child.parent_a!=r.parent_a||
       child.parent_b!=r.parent_b)throw std::runtime_error("family birth/face contradiction");
    return r;
}

NpcBirthRecord NpcAppearanceStore::record_birth(
    std::uint64_t event_id,std::uint64_t child_id,std::uint64_t pa,std::uint64_t pb,
    std::uint64_t settlement,std::uint64_t tick,std::uint64_t expected_revision,
    AuthorizeBirth authorize){
    return record_birth_impl(event_id,child_id,pa,pb,settlement,tick,expected_revision,
                             std::move(authorize),false);
}
NpcBirthRecord NpcAppearanceStore::record_live_birth(
    std::uint64_t event_id,std::uint64_t child_id,std::uint64_t pa,std::uint64_t pb,
    std::uint64_t settlement,std::uint64_t tick,std::uint64_t expected_revision,
    AuthorizeBirth authorize){
    if(!borrowed_transaction_)throw std::logic_error("live birth requires world transaction");
    return record_birth_impl(event_id,child_id,pa,pb,settlement,tick,expected_revision,
                             std::move(authorize),true);
}
NpcBirthRecord NpcAppearanceStore::record_birth_impl(
    std::uint64_t event_id,std::uint64_t child_id,std::uint64_t pa,std::uint64_t pb,
    std::uint64_t settlement,std::uint64_t tick,std::uint64_t expected_revision,
    AuthorizeBirth authorize,bool live){
    if(!event_id||(!live && child_id<kMinFamilyChildId)||
       (live && (child_id>=kMinFamilyChildId || !admit_(child_id)))||
       !settlement||!tick||
       !pa||!pb||pa==pb||child_id==pa||child_id==pb||!authorize)
        throw std::invalid_argument("invalid named family birth parameters");
    if(pa>pb)std::swap(pa,pb);
    // Do not allocate an identity already present in the playable simulation.
    if(!live && admit_(child_id))throw std::invalid_argument("family child ID collides with live NPC");
    begin();
    try {
        Statement prior(db_,"SELECT 1 FROM npc_family_births WHERE scope=? AND event_id=?");
        prior.text(1,scope_);prior.text(2,decimal(event_id));
        if(prior.step()==SQLITE_ROW){
            const auto r=get_birth(event_id);
            if(r.child_id!=child_id||r.settlement!=settlement||r.parent_a!=pa||
               r.parent_b!=pb||r.tick!=tick)
                throw std::invalid_argument("birth event replay parameters changed");
            commit();return r;
        }
        auto census=read_census(db_,scope_,settlement);
        if(census.revision!=expected_revision||tick<census.last_tick)
            throw std::invalid_argument("stale family census revision or time");
        if(census.current_population>=kMaxCensus || census.revision>=kMaxCensus)
            throw std::overflow_error("family population/revision capacity exhausted");
        if(!authorize(settlement,pa,pb,tick))
            throw std::invalid_argument("host denied birth (housing, food, population policy)");
        // Both parents must be real persistent members of the same settlement.
        for(auto parent:{pa,pb}){
            Statement member(db_,"SELECT settlement,born_tick FROM npc_family_people WHERE scope=? AND npc_id=?");
            member.text(1,scope_);member.text(2,decimal(parent));
            if(member.step()!=SQLITE_ROW || current_residence(parent)!=settlement)
                throw std::invalid_argument("birth parent not in settlement family registry");
            const auto parent_born_tick=static_cast<std::uint64_t>(sqlite3_column_int64(member.s,1));
            if(parent_born_tick>=tick)
                throw std::invalid_argument("a newborn cannot immediately become a parent");
            Statement dead(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=?");
            dead.text(1,scope_);dead.text(2,decimal(parent));
            if(dead.step()==SQLITE_ROW)
                throw std::invalid_argument("a deceased NPC cannot parent a new birth");
        }
        bool af=false,bf=false,cf=false;
        auto a=lookup(pa,af),b=lookup(pb,bf);
        lookup(child_id,cf);
        if(!af||!bf||cf || std::max(a.generation,b.generation)>=static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            throw std::invalid_argument("invalid family lineage or occupied child ID");
        auto child=offspring(child_id,a,b,mix(seed_,stable_scope_hash(scope_)));
        // All three writes are one SQLite transaction: physical population
        // census, child identity/lineage and appearance profile.
        insert(child);
        Statement person(db_,"INSERT INTO npc_family_people "
            "(scope,npc_id,settlement,born_tick,parent_a,parent_b) VALUES(?,?,?,?,?,?)");
        person.text(1,scope_);person.text(2,decimal(child_id));
        person.text(3,decimal(settlement));
        if(sqlite3_bind_int64(person.s,4,static_cast<sqlite3_int64>(tick))!=SQLITE_OK)
            throw std::runtime_error("birth tick bind failed");
        person.text(5,decimal(pa));person.text(6,decimal(pb));person.step();
        Statement ledger(db_,"INSERT INTO npc_family_births "
            "(scope,event_id,child_id,settlement,parent_a,parent_b,tick,census_revision,appearance_digest) "
            "VALUES(?,?,?,?,?,?,?,?,?)");
        ledger.text(1,scope_);ledger.text(2,decimal(event_id));ledger.text(3,decimal(child_id));
        ledger.text(4,decimal(settlement));ledger.text(5,decimal(pa));ledger.text(6,decimal(pb));
        if(sqlite3_bind_int64(ledger.s,7,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(ledger.s,8,static_cast<sqlite3_int64>(census.revision+1))!=SQLITE_OK)
            throw std::runtime_error("birth ledger bind failed");
        ledger.text(9,decimal(child.digest));ledger.step();
        Statement count(db_,"UPDATE npc_family_census SET current_population=current_population+1,"
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=?");
        if(sqlite3_bind_int64(count.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(count.s,4,static_cast<sqlite3_int64>(expected_revision))!=SQLITE_OK)
            throw std::runtime_error("birth revision bind failed");
        count.text(2,scope_);count.text(3,decimal(settlement));count.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("concurrent birth census revision");
        commit();
        return {event_id,child_id,pa,pb,settlement,tick,census.revision+1,child.digest};
    }catch(...){rollback();throw;}
}
} // namespace galaxy

namespace galaxy {
NpcDeathRecord NpcAppearanceStore::get_death(std::uint64_t event_id) const {
    Statement q(db_,"SELECT npc_id,settlement,tick,census_revision "
        "FROM npc_family_deaths WHERE scope=? AND event_id=?");
    q.text(1,scope_);q.text(2,decimal(event_id));
    if(q.step()!=SQLITE_ROW)throw std::out_of_range("death event not found");
    NpcDeathRecord r;r.event_id=event_id;
    r.npc_id=parse_u64(sqlite3_column_text(q.s,0));
    r.settlement=parse_u64(sqlite3_column_text(q.s,1));
    r.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,2));
    r.census_revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,3));
    return r;
}
NpcDeathRecord NpcAppearanceStore::record_death(
    std::uint64_t event_id,std::uint64_t npc_id,std::uint64_t settlement,
    std::uint64_t tick,std::uint64_t expected_revision,AuthorizeDeath authorize){
    if(!event_id||!npc_id||!settlement||!tick||!authorize)
        throw std::invalid_argument("invalid family death request");
    begin();
    try {
        Statement prior(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND event_id=?");
        prior.text(1,scope_);prior.text(2,decimal(event_id));
        if(prior.step()==SQLITE_ROW){
            const auto r=get_death(event_id);
            if(r.npc_id!=npc_id||r.settlement!=settlement||r.tick!=tick)
                throw std::invalid_argument("death replay parameters changed");
            commit();return r;
        }
        auto census=read_census(db_,scope_,settlement);
        if(census.revision!=expected_revision||tick<census.last_tick||!census.current_population)
            throw std::invalid_argument("stale family death revision, time, or population");
        if(!authorize(settlement,npc_id,tick))
            throw std::invalid_argument("host denied family death event");
        Statement member(db_,"SELECT settlement,born_tick FROM npc_family_people WHERE scope=? AND npc_id=?");
        member.text(1,scope_);member.text(2,decimal(npc_id));
        if(member.step()!=SQLITE_ROW||current_residence(npc_id)!=settlement ||
           static_cast<std::uint64_t>(sqlite3_column_int64(member.s,1))>=tick)
            throw std::invalid_argument("family death requires living member at later tick");
        Statement dead(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=?");
        dead.text(1,scope_);dead.text(2,decimal(npc_id));
        if(dead.step()==SQLITE_ROW)throw std::invalid_argument("NPC already deceased");
        Statement ins(db_,"INSERT INTO npc_family_deaths "
            "(scope,event_id,npc_id,settlement,tick,census_revision) VALUES(?,?,?,?,?,?)");
        ins.text(1,scope_);ins.text(2,decimal(event_id));ins.text(3,decimal(npc_id));
        ins.text(4,decimal(settlement));
        if(sqlite3_bind_int64(ins.s,5,static_cast<sqlite3_int64>(tick))!=SQLITE_OK||
           sqlite3_bind_int64(ins.s,6,static_cast<sqlite3_int64>(census.revision+1))!=SQLITE_OK)
            throw std::runtime_error("family death bind failed");
        ins.step();
        Statement upd(db_,"UPDATE npc_family_census SET current_population=current_population-1,"
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=?");
        if(sqlite3_bind_int64(upd.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK||
           sqlite3_bind_int64(upd.s,4,static_cast<sqlite3_int64>(expected_revision))!=SQLITE_OK)
            throw std::runtime_error("family death revision bind failed");
        upd.text(2,scope_);upd.text(3,decimal(settlement));upd.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("concurrent family death revision");
        commit();return {event_id,npc_id,settlement,tick,census.revision+1};
    }catch(...){rollback();throw;}
}
} // namespace galaxy

namespace galaxy {
std::uint64_t NpcAppearanceStore::current_nonparent_guardian(std::uint64_t child_id) const {
    if(!child_id)throw std::invalid_argument("guardianship query needs child identity");
    Statement schema(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                          "AND name='npc_family_guardianship'");
    if(schema.step()!=SQLITE_ROW)return 0;
    Statement row(db_,"SELECT guardian_id FROM npc_family_guardianship "
                      "WHERE scope=? AND child_id=? ORDER BY generation DESC,event_id DESC LIMIT 1");
    row.text(1,scope_);row.text(2,decimal(child_id));
    if(row.step()!=SQLITE_ROW)return 0;
    return parse_u64(sqlite3_column_text(row.s,0));
}

std::uint64_t NpcAppearanceStore::current_residence(std::uint64_t npc_id) const {
    if(!npc_id)throw std::invalid_argument("family current residence requires NPC ID");
    Statement q(db_,"SELECT destination FROM npc_family_moves WHERE scope=? AND npc_id=? "
                   "ORDER BY tick DESC,rowid DESC LIMIT 1");
    q.text(1,scope_);q.text(2,decimal(npc_id));
    if(q.step()==SQLITE_ROW)return parse_u64(sqlite3_column_text(q.s,0));
    Statement origin(db_,"SELECT settlement FROM npc_family_people WHERE scope=? AND npc_id=?");
    origin.text(1,scope_);origin.text(2,decimal(npc_id));
    if(origin.step()!=SQLITE_ROW)throw std::out_of_range("NPC not enrolled in family registry");
    return parse_u64(sqlite3_column_text(origin.s,0));
}
NpcMoveRecord NpcAppearanceStore::get_move(std::uint64_t event_id) const {
    Statement q(db_,"SELECT npc_id,origin,destination,tick,origin_revision,destination_revision "
                   "FROM npc_family_moves WHERE scope=? AND event_id=?");
    q.text(1,scope_);q.text(2,decimal(event_id));
    if(q.step()!=SQLITE_ROW)throw std::out_of_range("family migration missing");
    NpcMoveRecord r{};r.event_id=event_id;
    r.npc_id=parse_u64(sqlite3_column_text(q.s,0));
    r.origin=parse_u64(sqlite3_column_text(q.s,1));
    r.destination=parse_u64(sqlite3_column_text(q.s,2));
    r.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,3));
    r.origin_revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,4));
    r.destination_revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.s,5));
    return r;
}
NpcMoveRecord NpcAppearanceStore::record_move(std::uint64_t event_id,std::uint64_t npc,
    std::uint64_t origin,std::uint64_t destination,std::uint64_t tick,
    std::uint64_t src_revision,std::uint64_t dst_revision,
    const std::function<bool(std::uint64_t,std::uint64_t,std::uint64_t)>& authorize) {
    if(!borrowed_transaction_ || !event_id || !npc || !origin || !destination ||
       origin==destination || !tick || !authorize)
        throw std::invalid_argument("family migration needs world transaction and valid identity");
    begin();
    try {
        Statement prior(db_,"SELECT 1 FROM npc_family_moves WHERE scope=? AND event_id=?");
        prior.text(1,scope_);prior.text(2,decimal(event_id));
        if(prior.step()==SQLITE_ROW)throw std::invalid_argument("duplicate migration event");
        if(current_residence(npc)!=origin || !authorize(npc,origin,destination))
            throw std::runtime_error("family migration identity or host authority mismatch");
        Statement dead(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=?");
        dead.text(1,scope_);dead.text(2,decimal(npc));
        if(dead.step()==SQLITE_ROW)throw std::runtime_error("dead cannot migrate");
        Statement deps(db_,"SELECT 1 FROM npc_family_births WHERE scope=? "
                          "AND (parent_a=? OR parent_b=?)");
        deps.text(1,scope_);deps.text(2,decimal(npc));deps.text(3,decimal(npc));
        if(deps.step()==SQLITE_ROW)
            throw std::runtime_error("family dependents require later household migration gate");
        const auto a=read_census(db_,scope_,origin),b=read_census(db_,scope_,destination);
        if(a.revision!=src_revision || b.revision!=dst_revision ||
           a.current_population==0 || b.current_population>=kMaxCensus ||
           tick<a.last_tick || tick<b.last_tick)
            throw std::runtime_error("family move census CAS/time mismatch");
        Statement insert(db_,"INSERT INTO npc_family_moves VALUES (?,?,?,?,?,?,?,?)");
        insert.text(1,scope_);insert.text(2,decimal(event_id));insert.text(3,decimal(npc));
        insert.text(4,decimal(origin));insert.text(5,decimal(destination));
        if(sqlite3_bind_int64(insert.s,6,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(insert.s,7,static_cast<sqlite3_int64>(src_revision+1))!=SQLITE_OK ||
           sqlite3_bind_int64(insert.s,8,static_cast<sqlite3_int64>(dst_revision+1))!=SQLITE_OK)
            throw std::runtime_error("family move bind failure");
        insert.step();
        Statement src(db_,"UPDATE npc_family_census SET current_population=current_population-1,"
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=? "
            "AND current_population>0");
        if(sqlite3_bind_int64(src.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(src.s,4,static_cast<sqlite3_int64>(src_revision))!=SQLITE_OK)
            throw std::runtime_error("family source migration bind failure");
        src.text(2,scope_);src.text(3,decimal(origin));src.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("family source CAS failed");
        Statement dst(db_,"UPDATE npc_family_census SET current_population=current_population+1,"
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=?");
        if(sqlite3_bind_int64(dst.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(dst.s,4,static_cast<sqlite3_int64>(dst_revision))!=SQLITE_OK)
            throw std::runtime_error("family destination migration bind failure");
        dst.text(2,scope_);dst.text(3,decimal(destination));dst.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("family destination CAS failed");
        commit();
        return {event_id,npc,origin,destination,tick,src_revision+1,dst_revision+1};
    }catch(...){rollback();throw;}
}
} // namespace galaxy

namespace galaxy {
std::vector<NpcMoveRecord> NpcAppearanceStore::record_guardian_household_moves(
    std::uint64_t guardian_event,std::uint64_t guardian_id,
    const std::vector<std::pair<std::uint64_t,std::uint64_t>>& children,
    std::uint64_t origin,std::uint64_t destination,std::uint64_t tick,
    std::uint64_t src_revision,std::uint64_t dst_revision,
    const std::function<bool(std::uint64_t,std::uint64_t,std::uint64_t)>& authorize,
    std::uint64_t co_parent_id,std::uint64_t co_parent_event_id){
    if((co_parent_id==0)!=(co_parent_event_id==0) ||
       (co_parent_id && (co_parent_id==guardian_id ||
         co_parent_event_id==guardian_event ||
         current_residence(co_parent_id)!=origin ||
         !authorize(co_parent_id,origin,destination))))
        throw std::runtime_error("unbound or incorrect second-parent family move");
    if(!borrowed_transaction_ || !guardian_event || !guardian_id || !origin ||
       !destination || origin==destination || !tick || !authorize ||
       children.empty() || children.size()>32 ||
       !authorize(guardian_id,origin,destination) ||
       current_residence(guardian_id)!=origin)
        throw std::invalid_argument("household requires authenticated guardian and bounded dependents");
    // A dependent is a reserved high-range child identity and must NEVER be
    // confused with a living NPC or counted as a qualified worker.
    std::vector<std::pair<std::uint64_t,std::uint64_t>> actual;
    {
        Statement q(db_,"SELECT child_id FROM npc_family_births WHERE scope=? "
                        "AND (parent_a=? OR parent_b=?) ORDER BY child_id");
        q.text(1,scope_);q.text(2,decimal(guardian_id));q.text(3,decimal(guardian_id));
        while(q.step()==SQLITE_ROW){
            const auto child=parse_u64(sqlite3_column_text(q.s,0));
            if(current_residence(child)!=origin)
                throw std::runtime_error("guardian has a child at another residence; split custody not supported");
            actual.emplace_back(child,0);
        }
    }
    std::sort(actual.begin(),actual.end(),[](const auto& a,const auto& b){
        return a.first<b.first;
    });
    if(actual.size()!=children.size())
        throw std::runtime_error("incomplete child manifest for guardian household");
    std::uint64_t previous=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> entries;
    entries.emplace_back(guardian_id,guardian_event);
    if(co_parent_id)entries.emplace_back(co_parent_id,co_parent_event_id);
    for(std::size_t i=0;i<children.size();++i){
        const auto [child,event]=children[i];
        if(child<(1ULL<<62) || !event || child<=previous || event==guardian_event ||
           actual[i].first!=child ||
           std::any_of(entries.begin(),entries.end(),[&](const auto& e){return e.second==event;}))
            throw std::runtime_error("incomplete, unsorted or duplicate dependent household manifest");
        previous=child;entries.emplace_back(child,event);
    }
    const auto src=read_census(db_,scope_,origin),dst=read_census(db_,scope_,destination);
    const auto count=static_cast<std::uint64_t>(entries.size());
    if(src.revision!=src_revision || dst.revision!=dst_revision ||
       src.current_population<count || dst.current_population>kMaxCensus-count ||
       tick<src.last_tick || tick<dst.last_tick)
        throw std::runtime_error("household census revision or admission capacity stale");
    std::vector<NpcMoveRecord> moved;
    for(std::size_t i=0;i<entries.size();++i){
        const auto [npc,event]=entries[i];
        if(current_residence(npc)!=origin)
            throw std::runtime_error("dependent residence changed during world transaction");
        Statement dead(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? AND npc_id=?");
        dead.text(1,scope_);dead.text(2,decimal(npc));
        if(dead.step()==SQLITE_ROW)throw std::runtime_error("dead household member cannot migrate");
        const auto a=src_revision+i+1,b=dst_revision+i+1;
        Statement rec(db_,"INSERT INTO npc_family_moves "
            "(scope,event_id,npc_id,origin,destination,tick,origin_revision,destination_revision) "
            "VALUES(?,?,?,?,?,?,?,?)");
        rec.text(1,scope_);rec.text(2,decimal(event));rec.text(3,decimal(npc));
        rec.text(4,decimal(origin));rec.text(5,decimal(destination));
        if(sqlite3_bind_int64(rec.s,6,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(rec.s,7,static_cast<sqlite3_int64>(a))!=SQLITE_OK ||
           sqlite3_bind_int64(rec.s,8,static_cast<sqlite3_int64>(b))!=SQLITE_OK)
            throw std::runtime_error("household record bind failed");
        rec.step();
        Statement source(db_,"UPDATE npc_family_census SET current_population=current_population-1, "
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=? "
            "AND current_population>0");
        if(sqlite3_bind_int64(source.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(source.s,4,static_cast<sqlite3_int64>(a-1))!=SQLITE_OK)
            throw std::runtime_error("household source bind failed");
        source.text(2,scope_);source.text(3,decimal(origin));source.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("household source revision conflict");
        Statement target(db_,"UPDATE npc_family_census SET current_population=current_population+1, "
            "revision=revision+1,last_tick=? WHERE scope=? AND settlement=? AND revision=?");
        if(sqlite3_bind_int64(target.s,1,static_cast<sqlite3_int64>(tick))!=SQLITE_OK ||
           sqlite3_bind_int64(target.s,4,static_cast<sqlite3_int64>(b-1))!=SQLITE_OK)
            throw std::runtime_error("household target bind failed");
        target.text(2,scope_);target.text(3,decimal(destination));target.step();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("household target revision conflict");
        moved.push_back({event,npc,origin,destination,tick,a,b});
    }
    return moved;
}
} // namespace galaxy
