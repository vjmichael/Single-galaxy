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
NpcAppearanceStore::~NpcAppearanceStore(){if(db_)sqlite3_close(db_);}
void NpcAppearanceStore::begin(){sql_exec(db_,"BEGIN IMMEDIATE");}
void NpcAppearanceStore::commit(){sql_exec(db_,"COMMIT");}
void NpcAppearanceStore::rollback() noexcept {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
void NpcAppearanceStore::require_existing_npc(std::uint64_t id) const {
    if(!id||!admit_(id))throw std::invalid_argument("NPC identity is not admitted by authoritative simulation");
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
