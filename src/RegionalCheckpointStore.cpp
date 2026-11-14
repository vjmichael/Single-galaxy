#include "galaxy/RegionalCheckpointStore.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy::regional_events {
namespace {
constexpr std::uint64_t max_records=2'000'000;
constexpr std::size_t max_bytes=128 * 1024 * 1024;
constexpr std::size_t max_label=4096;
constexpr std::uint64_t max_sqlint=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
struct Binary {
    std::string bytes;
    std::size_t at=0;
    void u8(std::uint8_t n) { bytes.push_back(static_cast<char>(n)); }
    void u64(std::uint64_t n) { for(int i=0;i<8;++i){u8(static_cast<std::uint8_t>(n));n>>=8;} }
    void i64(std::int64_t n){
        std::uint64_t bits=0;
        static_assert(sizeof(bits)==sizeof(n),"64-bit archival integers required");
        std::memcpy(&bits,&n,sizeof(bits));u64(bits);
    }
    void str(const std::string& s){
        if(s.size()>max_label)throw std::invalid_argument("regional label too long");
        u64(s.size());bytes.append(s);
    }
    std::uint8_t get8(){
        if(at>=bytes.size())throw std::runtime_error("truncated regional checkpoint");
        return static_cast<std::uint8_t>(bytes[at++]);
    }
    std::uint64_t get64(){
        if(bytes.size()-at<8)throw std::runtime_error("truncated regional integer");
        std::uint64_t n=0;
        for(int i=0;i<8;++i)n|=static_cast<std::uint64_t>(get8())<<(8*i);
        return n;
    }
    std::int64_t geti64(){
        const auto bits=get64();std::int64_t n=0;
        std::memcpy(&n,&bits,sizeof(n));return n;
    }
    std::string getstr(){
        const auto n=get64();
        if(n>max_label || n>bytes.size()-at)throw std::runtime_error("invalid regional label length");
        auto s=bytes.substr(at,static_cast<std::size_t>(n));
        at+=static_cast<std::size_t>(n);
        return s;
    }
    std::uint64_t count(){
        const auto n=get64();
        if(n>max_records)throw std::runtime_error("too many regional records");
        return n;
    }
};
std::uint64_t hash_bytes(const std::string& bytes, std::size_t end){
    std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<end;++i){h^=static_cast<unsigned char>(bytes[i]);h*=1099511628211ULL;}
    return h;
}
void must(int code,sqlite3* db,const char* context){
    if(code!=SQLITE_OK)throw std::runtime_error(std::string(context)+": "+sqlite3_errmsg(db));
}
void exec(sqlite3* db,const char* sql){
    char* err=nullptr;
    const int code=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(code!=SQLITE_OK){
        std::string message=err?err:sqlite3_errmsg(db);
        sqlite3_free(err);
        throw std::runtime_error("regional SQLite: "+message);
    }
}
class Statement {
public:
    sqlite3_stmt* stmt=nullptr;
    sqlite3* db=nullptr;
    Statement(sqlite3* database,const char* sql):db(database){must(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr),db,"prepare regional SQL");}
    ~Statement(){sqlite3_finalize(stmt);}
    void bind_text(int n,const std::string& s){must(sqlite3_bind_text(stmt,n,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT),db,"bind scope");}
    void bind_int(int n,std::uint64_t v){
        if(v>max_sqlint)throw std::overflow_error("SQLite revision exhausted");
        must(sqlite3_bind_int64(stmt,n,static_cast<sqlite3_int64>(v)),db,"bind revision");
    }
    void bind_blob(int n,const std::string& s){must(sqlite3_bind_blob(stmt,n,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT),db,"bind checkpoint");}
};
// v50: a region hosting an actual material-backed factory may not accept an
// unrelated cold command or society tick that leaves the factory behind.
// The single-factory adapter commits both effects in its own authorized path.
bool has_active_factory(sqlite3* db,const std::string& scope){
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v50_factory'");
    const int found=sqlite3_step(table.stmt);
    if(found==SQLITE_DONE)return false;
    if(found!=SQLITE_ROW)throw std::runtime_error("factory presence check failed");
    Statement row(db,"SELECT 1 FROM sg_v50_factory WHERE scope=?1");
    row.bind_text(1,scope);
    const int rc=sqlite3_step(row.stmt);
    if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)
        throw std::runtime_error("factory region binding check failed");
    return rc==SQLITE_ROW;
}
bool has_active_network(sqlite3* db,const std::string& scope){
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='sg_v51_network'");
    if(sqlite3_step(table.stmt)!=SQLITE_ROW)return false;
    Statement row(db,"SELECT 1 FROM sg_v51_network WHERE scope=?1");row.bind_text(1,scope);
    const auto result=sqlite3_step(row.stmt);
    if(result!=SQLITE_ROW && result!=SQLITE_DONE)throw std::runtime_error("network authority lookup failed");
    return result==SQLITE_ROW;
}
void reject_uncoordinated_factory(sqlite3* db,const std::string& scope){
    if(has_active_factory(db,scope) || has_active_network(db,scope))
        throw std::runtime_error("factory region requires coordinated industry+population effects");
}
void put_life(Binary& b,const ColonyLifeSupport& c){
    b.i64(c.food_kg);b.i64(c.water_kg);b.i64(c.oxygen_kg);
    b.i64(c.nutrients_kg);b.i64(c.co2_kg);b.i64(c.crops_kg);
    b.i64(c.compost_kg);b.i64(c.wastewater_kg);b.i64(c.organic_waste_kg);
    b.i64(c.sludge_kg);b.i64(c.hydrogen_kg);
    b.u64(c.crops_ready_tick);b.u64(c.cycles);
    b.u64(c.consecutive_supplied_cycles);b.u64(c.shortages);
    b.i64(c.harvested_food_kg);b.i64(c.recovered_water_kg);
}
ColonyLifeSupport get_life(Binary& b){
    ColonyLifeSupport c;
    c.food_kg=b.geti64();c.water_kg=b.geti64();c.oxygen_kg=b.geti64();
    c.nutrients_kg=b.geti64();c.co2_kg=b.geti64();c.crops_kg=b.geti64();
    c.compost_kg=b.geti64();c.wastewater_kg=b.geti64();c.organic_waste_kg=b.geti64();
    c.sludge_kg=b.geti64();c.hydrogen_kg=b.geti64();
    c.crops_ready_tick=b.get64();c.cycles=b.get64();
    c.consecutive_supplied_cycles=b.get64();c.shortages=b.get64();
    c.harvested_food_kg=b.geti64();c.recovered_water_kg=b.geti64();
    return c;
}
void put_cargo(Binary& b,const Cargo& c){b.i64(c.food_kg);b.i64(c.water_kg);b.i64(c.oxygen_kg);}
Cargo get_cargo(Binary& b){return {b.geti64(),b.geti64(),b.geti64()};}
void validate_cargo(const Cargo& c){if(c.food_kg<0 || c.water_kg<0 || c.oxygen_kg<0)throw std::runtime_error("negative regional cargo");}
}
CheckpointStore::CheckpointStore(const std::string& file,const std::string& scope):scope_(scope){
    if(file.empty() || scope.empty() || scope.size()>max_label)throw std::invalid_argument("regional checkpoint requires file and scope");
    const int rc=sqlite3_open_v2(file.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr);
    if(rc!=SQLITE_OK){std::string msg=db_?sqlite3_errmsg(db_):"database open failed";sqlite3_close(db_);db_=nullptr;throw std::runtime_error(msg);}
    try{
        sqlite3_busy_timeout(db_,3000);
        exec(db_,"PRAGMA journal_mode=WAL");
        exec(db_,"PRAGMA synchronous=FULL");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_regional_checkpoint ("
                 "scope TEXT PRIMARY KEY NOT NULL, revision INTEGER NOT NULL, payload BLOB NOT NULL)");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_regional_command_receipt ("
                 "scope TEXT NOT NULL, command_id TEXT NOT NULL, actor TEXT NOT NULL, "
                 "request TEXT NOT NULL, revision INTEGER NOT NULL, outcome INTEGER NOT NULL, "
                 "PRIMARY KEY(scope,command_id))");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_regional_custody_scope ("
                 "scope TEXT PRIMARY KEY NOT NULL)");
        exec(db_,"CREATE TABLE IF NOT EXISTS sg_regional_shipment_custody ("
                 "scope TEXT NOT NULL, shipment_id INTEGER NOT NULL, "
                 "handoff_id TEXT NOT NULL, origin TEXT NOT NULL, destination TEXT NOT NULL, "
                 "depart_tick INTEGER NOT NULL, arrive_tick INTEGER NOT NULL, "
                 "food INTEGER NOT NULL CHECK(food>=0),water INTEGER NOT NULL CHECK(water>=0),"
                 "oxygen INTEGER NOT NULL CHECK(oxygen>=0),"
                 "stage INTEGER NOT NULL CHECK(stage BETWEEN 0 AND 2),"
                 "PRIMARY KEY(scope,shipment_id), UNIQUE(scope,handoff_id))");
        exec(db_,"CREATE INDEX IF NOT EXISTS sg_regional_custody_due "
                 "ON sg_regional_shipment_custody(scope,stage,depart_tick,arrive_tick)");
    }catch(...){sqlite3_close(db_);db_=nullptr;throw;}
}
CheckpointStore::~CheckpointStore(){if(db_)sqlite3_close(db_);}
std::string CheckpointStore::encode(const Scheduler& s) const {
    Binary b;
    b.bytes="SGREVT01";
    b.u64(s.tick_);b.u64(s.cooldown_);b.u64(s.next_id_);b.u64(s.next_sequence_);
    b.u64(s.ration_.food_kg);b.u64(s.ration_.water_kg);b.u64(s.ration_.oxygen_kg);
    b.u64(s.colonies_.size());
    for(const auto& [id,c]:s.colonies_){
        b.str(id);b.u64(c.processed_tick);b.u64(c.population);
        put_life(b,c.habitat);put_life(b,c.depot);
        b.u8(s.shortage_open_.at(id)?1:0);
    }
    b.u64(s.shipments_.size());
    for(const auto& [id,ship]:s.shipments_){
        b.u64(id);b.str(ship.origin);b.str(ship.destination);
        b.u64(ship.departure_tick);b.u64(ship.arrival_tick);
        put_cargo(b,ship.cargo);b.u8(static_cast<std::uint8_t>(ship.stage));
    }
    b.u64(s.events_.size());
    for(const auto& e:s.events_){
        b.u64(e.tick);b.u64(e.sequence);b.u64(e.reference);
        b.u8(static_cast<std::uint8_t>(e.kind));b.str(e.colony);
        put_cargo(b,e.cargo);b.u64(e.population);
    }
    b.u64(s.notices_.size());
    for(const auto& n:s.notices_){
        b.u64(n.tick);b.u64(n.sequence);b.u8(static_cast<std::uint8_t>(n.kind));
        b.str(n.colony);b.u64(n.reference);b.u64(n.shortage_periods);
    }
    if(b.bytes.size()>max_bytes-8)throw std::runtime_error("regional checkpoint exceeds storage safety limit");
    b.u64(hash_bytes(b.bytes,b.bytes.size()));
    return std::move(b.bytes);
}
Scheduler CheckpointStore::decode(const std::string& bytes) const {
    if(bytes.size()<16 || bytes.size()>max_bytes || bytes.compare(0,8,"SGREVT01")!=0)
        throw std::runtime_error("unknown regional checkpoint format");
    Binary b;b.bytes=bytes;b.at=8;
    // Check checksum before trusting any record counts or allocation sizes.
    const auto payload_end=bytes.size()-8;
    std::uint64_t stored=0;
    for(int i=0;i<8;++i)stored|=static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[payload_end+i]))<<(8*i);
    if(stored!=hash_bytes(bytes,payload_end))throw std::runtime_error("regional checkpoint integrity failure");
    const auto tick=b.get64(), cooldown=b.get64(), next_id=b.get64(), next_sequence=b.get64();
    auto ration=macro_needs::Ration{b.get64(),b.get64(),b.get64()};
    if(!cooldown || !next_id || !next_sequence)throw std::runtime_error("invalid regional checkpoint metadata");
    Scheduler s(cooldown,ration);
    s.tick_=tick;s.next_id_=next_id;s.next_sequence_=next_sequence;
    for(std::uint64_t i=0,n=b.count();i<n;++i){
        auto id=b.getstr();cold_colony::State c;
        c.processed_tick=b.get64();c.population=b.get64();
        c.habitat=get_life(b);c.depot=get_life(b);
        const auto flag=b.get8();
        if(id.empty() || c.processed_tick!=tick || flag>1 || !s.colonies_.emplace(id,c).second)
            throw std::runtime_error("invalid regional colony checkpoint");
        s.shortage_open_[id]=(flag==1);
    }
    for(std::uint64_t i=0,n=b.count();i<n;++i){
        Shipment sh;sh.id=b.get64();sh.origin=b.getstr();sh.destination=b.getstr();
        sh.departure_tick=b.get64();sh.arrival_tick=b.get64();sh.cargo=get_cargo(b);
        const auto stage=b.get8();
        validate_cargo(sh.cargo);
        if(!sh.id || stage>2 || sh.departure_tick>=sh.arrival_tick ||
           !s.colonies_.count(sh.origin) || !s.colonies_.count(sh.destination) ||
           !s.shipments_.emplace(sh.id,sh).second)
            throw std::runtime_error("invalid regional shipment checkpoint");
        s.shipments_.at(sh.id).stage=static_cast<ShipmentStage>(stage);
    }
    for(std::uint64_t i=0,n=b.count();i<n;++i){
        Scheduler::Scheduled e;
        e.tick=b.get64();e.sequence=b.get64();e.reference=b.get64();
        const auto kind=b.get8();e.colony=b.getstr();
        e.cargo=get_cargo(b);e.population=b.get64();
        validate_cargo(e.cargo);
        if(e.tick<=tick || !e.sequence || kind>3 || !s.colonies_.count(e.colony))
            throw std::runtime_error("invalid regional queued event checkpoint");
        e.kind=static_cast<Scheduler::EventKind>(kind);
        s.events_.push_back(std::move(e));
    }
    for(std::uint64_t i=0,n=b.count();i<n;++i){
        Notice no;
        no.tick=b.get64();no.sequence=b.get64();
        const auto kind=b.get8();no.colony=b.getstr();
        no.reference=b.get64();no.shortage_periods=b.get64();
        if(kind>6 || !no.sequence || no.tick>tick || !s.colonies_.count(no.colony))
            throw std::runtime_error("invalid regional notice checkpoint");
        no.kind=static_cast<NoticeKind>(kind);
        s.notices_.push_back(std::move(no));
    }
    if(b.at!=payload_end)throw std::runtime_error("regional checkpoint has trailing data");
    return s;
}
std::optional<VersionedCheckpoint> CheckpointStore::load() const {
    Statement q(db_,"SELECT revision,payload FROM sg_regional_checkpoint WHERE scope=?1");
    q.bind_text(1,scope_);
    const int rc=sqlite3_step(q.stmt);
    if(rc==SQLITE_DONE)return std::nullopt;
    if(rc!=SQLITE_ROW)throw std::runtime_error("regional checkpoint query failed");
    const auto rev=sqlite3_column_int64(q.stmt,0);
    const int length=sqlite3_column_bytes(q.stmt,1);
    const void* ptr=sqlite3_column_blob(q.stmt,1);
    if(rev<1 || length<16 || static_cast<std::size_t>(length)>max_bytes || !ptr)
        throw std::runtime_error("invalid regional checkpoint row");
    std::string bytes(static_cast<const char*>(ptr),static_cast<std::size_t>(length));
    auto state=decode(bytes);
    return VersionedCheckpoint{std::move(state),static_cast<std::uint64_t>(rev)};
}
std::uint64_t CheckpointStore::commit_casualties_in_world_transaction(
    sqlite3* active,std::uint64_t expected_revision,std::uint64_t expected_owner_generation,
    std::uint64_t tick,const std::map<std::string,std::uint64_t>& reductions,
    const std::map<std::string,std::uint64_t>& initial_populations,
    const std::string& command_id) const {
    if(!active || sqlite3_get_autocommit(active)!=0 || active==db_ || reductions.empty() ||
       !expected_revision || expected_revision>=max_sqlint ||
       !expected_owner_generation || expected_owner_generation>max_sqlint ||
       command_id.empty() || command_id.size()>max_label || tick>max_sqlint)
        throw std::invalid_argument("regional society transition requires live world transaction");
    const auto* world_path=sqlite3_db_filename(active,"main");
    const auto* region_path=sqlite3_db_filename(db_,"main");
    if(!world_path||!region_path||std::string(world_path)!=region_path)
        throw std::runtime_error("regional society transition used detached SQLite world");
    reject_uncoordinated_factory(active,scope_);
    { // A prepared handoff or ownership change always freezes the writer.
        Statement owner(active,"SELECT owner,phase,generation,checkpoint_revision FROM "
                               "sg_regional_mode_authority WHERE scope=?1");
        owner.bind_text(1,scope_);
        if(sqlite3_step(owner.stmt)!=SQLITE_ROW ||
           sqlite3_column_int(owner.stmt,0)!=0 || sqlite3_column_int(owner.stmt,1)!=0 ||
           sqlite3_column_int64(owner.stmt,2)!=static_cast<sqlite3_int64>(expected_owner_generation) ||
           sqlite3_column_int64(owner.stmt,3)!=static_cast<sqlite3_int64>(expected_revision))
            throw std::runtime_error("regional society owner generation/phase/revision fence rejected");
    }
    Statement old(active,"SELECT revision,payload FROM sg_regional_checkpoint WHERE scope=?1");
    old.bind_text(1,scope_);
    if(sqlite3_step(old.stmt)!=SQLITE_ROW ||
       sqlite3_column_int64(old.stmt,0)!=static_cast<sqlite3_int64>(expected_revision))
        throw std::runtime_error("regional society CAS revision mismatch");
    const auto* blob=sqlite3_column_blob(old.stmt,1);
    const int len=sqlite3_column_bytes(old.stmt,1);
    if(!blob || len<16 || static_cast<std::size_t>(len)>max_bytes)
        throw std::runtime_error("regional checkpoint payload missing");
    Scheduler updated=decode(std::string(static_cast<const char*>(blob),static_cast<std::size_t>(len)));
    if(updated.pending_events()!=0 || updated.tick()>=tick)
        throw std::runtime_error("regional society pending events or invalid tick");
    if(initial_populations.size()!=reductions.size())
        throw std::runtime_error("regional colony population bindings incomplete");
    for(const auto& [name,n]:reductions){
        const auto prior=initial_populations.find(name);
        if(prior==initial_populations.end() ||
           updated.colony(name).population!=prior->second)
            throw std::runtime_error("regional colony population preflight stale");
        if(name.empty() || n==0 || updated.colony(name).population<n)
            throw std::runtime_error("regional society invalid colony casualty reduction");
        updated.schedule_population(name,updated.colony(name).population-n,tick);
    }
    updated.advance_to(tick);  // same deterministic ECON03 scheduler, not an ad hoc blob edit
    if(updated.pending_events()!=0)throw std::runtime_error("regional society update left events pending");
    const auto bytes=encode(updated);
    Statement update(active,"UPDATE sg_regional_checkpoint SET revision=?2,payload=?3 "
                            "WHERE scope=?1 AND revision=?4");
    update.bind_text(1,scope_);update.bind_int(2,expected_revision+1);
    update.bind_blob(3,bytes);update.bind_int(4,expected_revision);
    if(sqlite3_step(update.stmt)!=SQLITE_DONE||sqlite3_changes(active)!=1)
        throw std::runtime_error("regional society concurrent checkpoint write");
    Statement fence(active,"UPDATE sg_regional_mode_authority SET checkpoint_revision=?2,"
                           "boundary_tick=?3 WHERE scope=?1 AND generation=?4 AND "
                           "owner=0 AND phase=0 AND checkpoint_revision=?5");
    fence.bind_text(1,scope_);fence.bind_int(2,expected_revision+1);
    fence.bind_int(3,tick);fence.bind_int(4,expected_owner_generation);
    fence.bind_int(5,expected_revision);
    if(sqlite3_step(fence.stmt)!=SQLITE_DONE||sqlite3_changes(active)!=1)
        throw std::runtime_error("regional society authority changed during update");
    Statement receipt(active,"INSERT INTO sg_regional_command_receipt "
                             "(scope,command_id,actor,request,revision,outcome) "
                             "VALUES (?1,?2,'society-host',?3,?4,?5)");
    receipt.bind_text(1,scope_);receipt.bind_text(2,command_id);
    receipt.bind_text(3,"society-deaths-generation:"+std::to_string(tick));
    receipt.bind_int(4,expected_revision+1);receipt.bind_int(5,reductions.size());
    if(sqlite3_step(receipt.stmt)!=SQLITE_DONE)
        throw std::runtime_error("regional society duplicate or corrupt effect receipt");
    return expected_revision+1;
}

std::uint64_t CheckpointStore::commit_birth_in_world_transaction(
    sqlite3* active,std::uint64_t expected_revision,std::uint64_t expected_owner_generation,
    std::uint64_t tick,const std::string& colony,std::uint64_t initial_population,
    const std::string& birth_event_id) const {
    if(!active || active==db_ || sqlite3_get_autocommit(active)!=0 ||
       !expected_revision || expected_revision>=max_sqlint ||
       !expected_owner_generation || expected_owner_generation>max_sqlint ||
       colony.empty() || colony.size()>max_label || birth_event_id.empty() ||
       birth_event_id.size()>max_label || !tick || tick>max_sqlint ||
       initial_population>=max_sqlint)
        throw std::invalid_argument("birth requires a bounded active world transaction");
    const auto* w=sqlite3_db_filename(active,"main");
    const auto* reg=sqlite3_db_filename(db_,"main");
    if(!w || !reg || std::string(w)!=reg)
        throw std::runtime_error("birth linked to detached region database");
    reject_uncoordinated_factory(active,scope_);
    {
        Statement owner(active,"SELECT owner,phase,generation,checkpoint_revision "
            "FROM sg_regional_mode_authority WHERE scope=?1");
        owner.bind_text(1,scope_);
        if(sqlite3_step(owner.stmt)!=SQLITE_ROW ||
           sqlite3_column_int(owner.stmt,0)!=0 || sqlite3_column_int(owner.stmt,1)!=0 ||
           sqlite3_column_int64(owner.stmt,2)!=static_cast<sqlite3_int64>(expected_owner_generation) ||
           sqlite3_column_int64(owner.stmt,3)!=static_cast<sqlite3_int64>(expected_revision))
            throw std::runtime_error("birth cold-owner generation/phase fence refused");
    }
    Statement old(active,"SELECT revision,payload FROM sg_regional_checkpoint WHERE scope=?1");
    old.bind_text(1,scope_);
    if(sqlite3_step(old.stmt)!=SQLITE_ROW ||
       sqlite3_column_int64(old.stmt,0)!=static_cast<sqlite3_int64>(expected_revision))
        throw std::runtime_error("birth regional checkpoint revision mismatch");
    const auto* blob=sqlite3_column_blob(old.stmt,1);
    const auto len=sqlite3_column_bytes(old.stmt,1);
    if(!blob || len<16 || static_cast<std::size_t>(len)>max_bytes)
        throw std::runtime_error("birth regional payload absent");
    auto updated=decode(std::string(static_cast<const char*>(blob),static_cast<std::size_t>(len)));
    if(updated.pending_events()!=0 || updated.tick()>=tick ||
       updated.colony(colony).population!=initial_population)
        throw std::runtime_error("birth regional cohort not at certified pre-state");
    updated.schedule_population(colony,initial_population+1,tick);
    updated.advance_to(tick);
    if(updated.pending_events()!=0 || updated.colony(colony).population!=initial_population+1)
        throw std::runtime_error("birth regional scheduler failed population increase");
    const auto bytes=encode(updated);
    Statement cp(active,"UPDATE sg_regional_checkpoint SET revision=?2,payload=?3 "
                        "WHERE scope=?1 AND revision=?4");
    cp.bind_text(1,scope_);cp.bind_int(2,expected_revision+1);
    cp.bind_blob(3,bytes);cp.bind_int(4,expected_revision);
    if(sqlite3_step(cp.stmt)!=SQLITE_DONE || sqlite3_changes(active)!=1)
        throw std::runtime_error("birth checkpoint CAS failed");
    Statement fence(active,"UPDATE sg_regional_mode_authority SET checkpoint_revision=?2,"
        "boundary_tick=?3 WHERE scope=?1 AND generation=?4 "
        "AND owner=0 AND phase=0 AND checkpoint_revision=?5");
    fence.bind_text(1,scope_);fence.bind_int(2,expected_revision+1);
    fence.bind_int(3,tick);fence.bind_int(4,expected_owner_generation);
    fence.bind_int(5,expected_revision);
    if(sqlite3_step(fence.stmt)!=SQLITE_DONE || sqlite3_changes(active)!=1)
        throw std::runtime_error("birth owner changed during commit");
    Statement receipt(active,"INSERT INTO sg_regional_command_receipt "
        "(scope,command_id,actor,request,revision,outcome) "
        "VALUES (?1,?2,'society-birth-host',?3,?4,1)");
    receipt.bind_text(1,scope_);receipt.bind_text(2,birth_event_id);
    receipt.bind_text(3,"society-birth-generation:"+std::to_string(tick)+":"+colony);
    receipt.bind_int(4,expected_revision+1);
    if(sqlite3_step(receipt.stmt)!=SQLITE_DONE)
        throw std::runtime_error("duplicate regional birth receipt");
    return expected_revision+1;
}

std::uint64_t CheckpointStore::commit_migration_in_world_transaction(
    sqlite3* active,std::uint64_t expected_revision,std::uint64_t expected_owner_generation,
    std::uint64_t tick,const std::string& origin,const std::string& destination,
    std::uint64_t origin_pop,std::uint64_t dest_pop,const std::string& command_id,
    std::uint64_t person_count,const std::string& receipt_actor) const {
    if(!active || sqlite3_get_autocommit(active)!=0 || active==db_ || !expected_revision ||
       expected_revision>=max_sqlint || !expected_owner_generation ||
       expected_owner_generation>max_sqlint || !tick || tick>max_sqlint ||
       origin.empty() || destination.empty() || origin==destination ||
       person_count==0 || origin_pop<person_count || origin_pop>max_records ||
       person_count>max_records-dest_pop ||
       command_id.empty() || command_id.size()>max_label ||
       (receipt_actor!="society-migration-host" &&
        receipt_actor!="colony-family-evacuation-host"))
        throw std::invalid_argument("migration requires bounded world transaction");
    const auto* a=sqlite3_db_filename(active,"main"),*b=sqlite3_db_filename(db_,"main");
    if(!a || !b || std::string(a)!=b)
        throw std::runtime_error("migration cannot use a detached region database");
    reject_uncoordinated_factory(active,scope_);
    Statement owner(active,"SELECT owner,phase,generation,checkpoint_revision FROM "
        "sg_regional_mode_authority WHERE scope=?1");
    owner.bind_text(1,scope_);
    if(sqlite3_step(owner.stmt)!=SQLITE_ROW || sqlite3_column_int(owner.stmt,0)!=0 ||
       sqlite3_column_int(owner.stmt,1)!=0 ||
       sqlite3_column_int64(owner.stmt,2)!=static_cast<sqlite3_int64>(expected_owner_generation) ||
       sqlite3_column_int64(owner.stmt,3)!=static_cast<sqlite3_int64>(expected_revision))
        throw std::runtime_error("migration cold owner generation or handoff fence refused");
    Statement old(active,"SELECT revision,payload FROM sg_regional_checkpoint WHERE scope=?1");
    old.bind_text(1,scope_);
    if(sqlite3_step(old.stmt)!=SQLITE_ROW ||
       sqlite3_column_int64(old.stmt,0)!=static_cast<sqlite3_int64>(expected_revision))
        throw std::runtime_error("migration stale regional revision");
    const auto* blob=sqlite3_column_blob(old.stmt,1);
    const int len=sqlite3_column_bytes(old.stmt,1);
    if(!blob || len<16 || static_cast<std::size_t>(len)>max_bytes)
        throw std::runtime_error("migration missing regional checkpoint payload");
    auto updated=decode(std::string(static_cast<const char*>(blob),static_cast<std::size_t>(len)));
    if(updated.pending_events()!=0 || updated.tick()>=tick ||
       updated.colony(origin).population!=origin_pop ||
       updated.colony(destination).population!=dest_pop)
        throw std::runtime_error("migration regional populations or tick stale");
    updated.schedule_population(origin,origin_pop-person_count,tick);
    updated.schedule_population(destination,dest_pop+person_count,tick);
    updated.advance_to(tick);
    if(updated.pending_events()!=0 || updated.colony(origin).population!=origin_pop-person_count ||
       updated.colony(destination).population!=dest_pop+person_count)
        throw std::runtime_error("migration scheduler population conservation failed");
    const auto bytes=encode(updated);
    Statement cp(active,"UPDATE sg_regional_checkpoint SET revision=?2,payload=?3 "
                        "WHERE scope=?1 AND revision=?4");
    cp.bind_text(1,scope_);cp.bind_int(2,expected_revision+1);
    cp.bind_blob(3,bytes);cp.bind_int(4,expected_revision);
    if(sqlite3_step(cp.stmt)!=SQLITE_DONE || sqlite3_changes(active)!=1)
        throw std::runtime_error("migration regional checkpoint CAS failed");
    Statement fence(active,"UPDATE sg_regional_mode_authority SET checkpoint_revision=?2,"
        "boundary_tick=?3 WHERE scope=?1 AND generation=?4 AND owner=0 AND phase=0 "
        "AND checkpoint_revision=?5");
    fence.bind_text(1,scope_);fence.bind_int(2,expected_revision+1);
    fence.bind_int(3,tick);fence.bind_int(4,expected_owner_generation);
    fence.bind_int(5,expected_revision);
    if(sqlite3_step(fence.stmt)!=SQLITE_DONE || sqlite3_changes(active)!=1)
        throw std::runtime_error("migration authority generation changed during world step");
    Statement receipt(active,"INSERT INTO sg_regional_command_receipt "
        "(scope,command_id,actor,request,revision,outcome) "
        "VALUES (?1,?2,?5,?3,?4,1)");
    receipt.bind_text(1,scope_);receipt.bind_text(2,command_id);
    receipt.bind_text(3,"society-migration-generation:"+std::to_string(tick)+":"+origin+":"+destination+
        (person_count==1 ? "" : ":household:"+std::to_string(person_count)));
    receipt.bind_int(4,expected_revision+1);
    receipt.bind_text(5,receipt_actor);
    if(sqlite3_step(receipt.stmt)!=SQLITE_DONE)
        throw std::runtime_error("duplicate migration regional receipt");
    return expected_revision+1;
}

std::uint64_t CheckpointStore::save(const Scheduler& scheduler,std::uint64_t expected_revision){
    if(expected_revision>=max_sqlint)throw std::overflow_error("regional revision overflow");
    const auto payload=encode(scheduler);
    exec(db_,"BEGIN IMMEDIATE");
    try{
        // Once mode ownership is enrolled, no unreceipted legacy save may
        // bypass the fidelity/ownership journal.
        {
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                 "AND name='sg_regional_mode_authority'");
            const auto found=sqlite3_step(table.stmt);
            if(found==SQLITE_ROW){
                Statement owner(db_,"SELECT 1 FROM sg_regional_mode_authority WHERE scope=?1");
                owner.bind_text(1,scope_);
                const auto rc=sqlite3_step(owner.stmt);
                if(rc==SQLITE_ROW)throw std::runtime_error("region authority prohibits legacy checkpoint writes");
                if(rc!=SQLITE_DONE)throw std::runtime_error("regional authority fence query failed");
            }else if(found!=SQLITE_DONE)throw std::runtime_error("regional authority schema query failed");
        }
        // Once a region has switched to receipted commands, older callers may
        // not silently mutate it through the unaudited compatibility API.
        {
            Statement receipts(db_,"SELECT 1 FROM sg_regional_command_receipt WHERE scope=?1 LIMIT 1");
            receipts.bind_text(1,scope_);
            const auto step=sqlite3_step(receipts.stmt);
            if(step==SQLITE_ROW)throw std::runtime_error("regional checkpoint requires receipted command");
            if(step!=SQLITE_DONE)throw std::runtime_error("regional command fence lookup failed");
        }
        // Opting into physical custody ownership also fences legacy snapshot
        // writes, even before the first receipted command is committed.
        {
            Statement scope(db_,"SELECT 1 FROM sg_regional_custody_scope WHERE scope=?1 LIMIT 1");
            scope.bind_text(1,scope_);
            const int rc=sqlite3_step(scope.stmt);
            if(rc==SQLITE_ROW)throw std::runtime_error("custody-enabled region requires host commands");
            if(rc!=SQLITE_DONE)throw std::runtime_error("custody scope fence lookup failed");
        }
        if(expected_revision==0){
            Statement q(db_,"INSERT OR IGNORE INTO sg_regional_checkpoint(scope,revision,payload) VALUES (?1,1,?2)");
            q.bind_text(1,scope_);q.bind_blob(2,payload);
            if(sqlite3_step(q.stmt)!=SQLITE_DONE)throw std::runtime_error("regional initial checkpoint insert failed");
        }else{
            Statement q(db_,"UPDATE sg_regional_checkpoint SET revision=?3,payload=?4 WHERE scope=?1 AND revision=?2");
            q.bind_text(1,scope_);q.bind_int(2,expected_revision);
            q.bind_int(3,expected_revision+1);q.bind_blob(4,payload);
            if(sqlite3_step(q.stmt)!=SQLITE_DONE)throw std::runtime_error("regional checkpoint update failed");
        }
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("stale regional checkpoint revision");
        exec(db_,"COMMIT");
    }catch(...){try{exec(db_,"ROLLBACK");}catch(...){}throw;}
    return expected_revision+1;
}
CommandReceipt CheckpointStore::commit_command(
    const std::string& command_id,const std::string& actor,
    const std::string& canonical_request,std::uint64_t expected_revision,
    const std::function<std::uint64_t(Scheduler&)>& mutate,
    const SqliteMutationEffect& atomic_effect,bool custody_enforced,
    std::uint64_t expected_owner_generation,int required_owner_mode){
    if(required_owner_mode!=0 && required_owner_mode!=1)
        throw std::invalid_argument("invalid required region owner");
    if(command_id.empty() || command_id.size()>max_label || actor.empty() ||
       actor.size()>max_label || canonical_request.empty() ||
       canonical_request.size()>max_label)
        throw std::invalid_argument("regional command requires bounded identity and canonical request");
    if(!mutate)throw std::invalid_argument("regional mutation callback required");
    if(expected_revision==0 || expected_revision>=max_sqlint)
        throw std::invalid_argument("regional command requires existing checkpoint revision");
    exec(db_,"BEGIN IMMEDIATE");
    try{
        {
            Statement custody(db_,"SELECT 1 FROM sg_regional_custody_scope WHERE scope=?1");
            custody.bind_text(1,scope_);
            const int rc=sqlite3_step(custody.stmt);
            if(rc==SQLITE_ROW && (!custody_enforced || !atomic_effect))
                throw std::runtime_error("custody-enabled scope requires atomic physical receipt effect");
            if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)
                throw std::runtime_error("custody gate query failed");
        }
        // An enrolled mode owns the exclusive right to mutate the regional
        // scheduler. Prepared transitions freeze commands; detailed ownership
        // always excludes cold scheduler writes, including retries.
        {
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                 "AND name='sg_regional_mode_authority'");
            const auto found=sqlite3_step(table.stmt);
            if(found==SQLITE_ROW){
                Statement q(db_,"SELECT owner,phase,generation FROM sg_regional_mode_authority WHERE scope=?1");
                q.bind_text(1,scope_);
                const auto rc=sqlite3_step(q.stmt);
                if(rc==SQLITE_ROW){
                    const auto actual=sqlite3_column_int64(q.stmt,2);
                    if(sqlite3_column_int(q.stmt,0)!=required_owner_mode || sqlite3_column_int(q.stmt,1)!=0)
                        throw std::runtime_error("regional owner/phase fence rejected command");
                    if(expected_owner_generation==0 || expected_owner_generation>max_sqlint ||
                       actual<1 || actual!=static_cast<sqlite3_int64>(expected_owner_generation))
                        throw std::runtime_error("regional cold-owner generation fence rejected writer");
                }else if(rc==SQLITE_DONE){
                    if(required_owner_mode!=0)throw std::runtime_error("detailed command without enrolled region");
                }else throw std::runtime_error("regional mode fence query failed");
            }else if(found==SQLITE_DONE){
                if(required_owner_mode!=0)throw std::runtime_error("detailed command without authority table");
            }else throw std::runtime_error("regional mode schema query failed");
        }
        const bool single=has_active_factory(db_,scope_);
        const bool network=has_active_network(db_,scope_);
        if(single&&network)throw std::runtime_error("two industrial checkpoint owners registered");
        if((single||network) && (!atomic_effect || !custody_enforced ||
           (single && (actor!="factory-host" ||
             canonical_request.rfind("industrial-cold-v50:",0)!=0)) ||
           (network && (actor!="network-host" ||
             canonical_request.rfind("network-cold-v51:",0)!=0))))
            throw std::runtime_error("uncoordinated regional command would bypass industry checkpoint");
        // Detailed-mode calls additionally require a durable active-entity
        // registration for this exact authority generation. Merely supplying
        // detailed_mode=true from a host path cannot create a warm owner.
        if(required_owner_mode==1){
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                 "AND name='sg_warm_active'");
            if(sqlite3_step(table.stmt)!=SQLITE_ROW)
                throw std::runtime_error("detailed operation without active materialization table");
            Statement scene(db_,"SELECT 1 FROM sg_warm_active WHERE scope=?1 AND generation=?2");
            scene.bind_text(1,scope_);scene.bind_int(2,expected_owner_generation);
            if(sqlite3_step(scene.stmt)!=SQLITE_ROW)
                throw std::runtime_error("detailed operation without active generation manifest");
        }
        // Check deduplication BEFORE CAS, so delayed repeats after subsequent
        // successful commands still return their original receipt.
        {
            Statement q(db_,"SELECT actor,request,revision,outcome FROM sg_regional_command_receipt "
                            "WHERE scope=?1 AND command_id=?2");
            q.bind_text(1,scope_);q.bind_text(2,command_id);
            const int rc=sqlite3_step(q.stmt);
            if(rc==SQLITE_ROW){
                const auto* a=sqlite3_column_text(q.stmt,0);
                const auto* p=sqlite3_column_text(q.stmt,1);
                const auto rev=sqlite3_column_int64(q.stmt,2);
                const auto out=sqlite3_column_int64(q.stmt,3);
                const int alen=sqlite3_column_bytes(q.stmt,0);
                const int plen=sqlite3_column_bytes(q.stmt,1);
                if(!a || !p || alen<0 || plen<0 ||
                   actor!=std::string(reinterpret_cast<const char*>(a),static_cast<std::size_t>(alen)) ||
                   canonical_request!=std::string(reinterpret_cast<const char*>(p),static_cast<std::size_t>(plen)))
                    throw std::runtime_error("regional command-id collision");
                if(rev<1 || out<0)throw std::runtime_error("invalid regional command receipt");
                // Reject a torn/corrupt current checkpoint even on dedupe.
                if(!load())throw std::runtime_error("regional receipt without checkpoint");
                exec(db_,"COMMIT");
                return {static_cast<std::uint64_t>(rev),static_cast<std::uint64_t>(out),true};
            }
            if(rc!=SQLITE_DONE)throw std::runtime_error("regional command lookup failed");
        }
        auto latest=load();
        if(!latest || latest->revision!=expected_revision)
            throw std::runtime_error("stale regional command revision");
        auto candidate=latest->scheduler; // No changes to live state until commit.
        const std::uint64_t outcome=mutate(candidate);
        if(outcome>max_sqlint)throw std::overflow_error("regional outcome exceeds SQLite range");
        const auto payload=encode(candidate); // May reject invalid/oversized state.
        // The physical manifest and scheduler revision commit together. The effect
        // must use this SAME sqlite connection and must not make external calls.
        if(atomic_effect)atomic_effect(db_,latest->scheduler,candidate,outcome);
        {
            Statement u(db_,"UPDATE sg_regional_checkpoint SET revision=?3,payload=?4 "
                            "WHERE scope=?1 AND revision=?2");
            u.bind_text(1,scope_);u.bind_int(2,expected_revision);
            u.bind_int(3,expected_revision+1);u.bind_blob(4,payload);
            if(sqlite3_step(u.stmt)!=SQLITE_DONE || sqlite3_changes(db_)!=1)
                throw std::runtime_error("regional command checkpoint conflict");
        }
        // Keep the authority checkpoint boundary in sync with every cold
        // command, inside this same transaction; otherwise a later handoff
        // might accept a stale boundary despite the CAS checkpoint update.
        {
            Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                            "AND name='sg_regional_mode_authority'");
            const auto found=sqlite3_step(t.stmt);
            if(found==SQLITE_ROW){
                Statement u(db_,"UPDATE sg_regional_mode_authority SET "
                    "checkpoint_revision=?2,boundary_tick=?3 WHERE scope=?1 "
                    "AND owner=?4 AND phase=0");
                u.bind_text(1,scope_);u.bind_int(2,expected_revision+1);
                u.bind_int(3,candidate.tick());u.bind_int(4,static_cast<std::uint64_t>(required_owner_mode));
                if(sqlite3_step(u.stmt)!=SQLITE_DONE || sqlite3_changes(db_)!=1)
                    throw std::runtime_error("regional authority checkpoint sync failed");
            }else if(found!=SQLITE_DONE)
                throw std::runtime_error("regional authority schema query failed");
        }
        {
            Statement q(db_,"INSERT INTO sg_regional_command_receipt "
                            "(scope,command_id,actor,request,revision,outcome) "
                            "VALUES (?1,?2,?3,?4,?5,?6)");
            q.bind_text(1,scope_);q.bind_text(2,command_id);q.bind_text(3,actor);
            q.bind_text(4,canonical_request);q.bind_int(5,expected_revision+1);
            q.bind_int(6,outcome);
            if(sqlite3_step(q.stmt)!=SQLITE_DONE)
                throw std::runtime_error("regional command receipt insert failed");
        }
        exec(db_,"COMMIT");
        return {expected_revision+1,outcome,false};
    }catch(...){try{exec(db_,"ROLLBACK");}catch(...){}throw;}
}
} // namespace galaxy::regional_events
