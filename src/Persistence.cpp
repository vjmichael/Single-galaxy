#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <sqlite3.h>

#include <filesystem>
#include <cerrno>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <limits>
#include <sstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy {
namespace {
[[noreturn]] void db_error(sqlite3* db, const std::string& why) {
    throw std::runtime_error("SQLite " + why + ": " + sqlite3_errmsg(db));
}
void run(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int code = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (code != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("SQLite statement failed: " + message);
    }
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) db_error(db, "prepare");
    }
    ~Statement() { sqlite3_finalize(s_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void i64(int index, std::int64_t value) {
        if (sqlite3_bind_int64(s_, index, value) != SQLITE_OK) db_error(db_, "bind integer");
    }
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(s_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            db_error(db_, "bind text");
    }
    void null(int index) {
        if (sqlite3_bind_null(s_,index) != SQLITE_OK) db_error(db_, "bind null");
    }
    void blob(int index, const std::string& value) {
        if (sqlite3_bind_blob(s_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            db_error(db_, "bind blob");
    }
    void execute() {
        if (sqlite3_step(s_) != SQLITE_DONE) db_error(db_, "execute");
    }
    bool row() {
        auto code = sqlite3_step(s_);
        if (code == SQLITE_ROW) return true;
        if (code == SQLITE_DONE) return false;
        db_error(db_, "read");
    }
    sqlite3_stmt* get() { return s_; }
private:
    sqlite3* db_;
    sqlite3_stmt* s_ = nullptr;
};

std::uint64_t parse_u64(const unsigned char* p) {
    if (!p) throw std::runtime_error("SQLite: null u64 value");
    const std::string s(reinterpret_cast<const char*>(p));
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("SQLite: invalid u64 text");
    std::size_t used = 0;
    const auto n = std::stoull(s, &used);
    if (used != s.size()) throw std::runtime_error("SQLite: invalid u64 suffix");
    return n;
}
// Deterministic text form for operations committed after the v2 migration.
std::string command_record(const Command& c) {
    std::ostringstream out;
    out << c.id << ' ' << c.execute_tick << ' ' << c.sequence << ' ' << c.issuer << ' '
        << c.actor << ' ' << c.target << ' ' << static_cast<int>(c.action) << ' '
        << c.thrust_x << ' ' << c.thrust_y << ' ' << c.amount;
    return out.str();
}
bool is_life_action(Action a) {
    return a>=Action::PlantCrops && a<=Action::LoadLifeWater;
}
std::int64_t life_transfer_amount(const Command& c){
    switch(c.action){
      case Action::PlantCrops:return 4*c.amount;
      case Action::HarvestCrops:return 2*c.amount;
      case Action::RecycleWater:return 9*c.amount/10;
      case Action::RecycleCompost:return c.amount;
      case Action::ProduceOxygen:return 8*c.amount;
      case Action::LoadLifeWater:return 1000*c.amount;
      default:return -1;
    }
}
std::string life_ledger_source(const Command& c){
    return "life:"+std::to_string(c.actor)+":"+std::to_string(static_cast<int>(c.action));
}
std::string life_ledger_destination(const Command& c){
    return "life:"+std::to_string(c.actor)+":products";
}
Command decode_command(const std::string& record) {
    Command c{};
    int action = -1;
    std::istringstream in(record);
    if (!(in >> c.id >> c.execute_tick >> c.sequence >> c.issuer >> c.actor
          >> c.target >> action >> c.thrust_x >> c.thrust_y >> c.amount))
        throw std::runtime_error("recovery: invalid command journal record");
    std::string extra;
    if (in >> extra || action < 0 || action > static_cast<int>(Action::TransferWorkshopFinalOutput) || c.id == 0 || c.issuer == 0)
        throw std::runtime_error("recovery: invalid command journal values");
    c.action = static_cast<Action>(action);
    if (command_record(c) != record) throw std::runtime_error("recovery: noncanonical command journal");
    return c;
}
void quick_integrity(sqlite3* db) {
    Statement q(db, "PRAGMA integrity_check");
    if (!q.row() || sqlite3_column_type(q.get(), 0) != SQLITE_TEXT ||
        std::string(reinterpret_cast<const char*>(sqlite3_column_text(q.get(), 0))) != "ok" || q.row())
        throw std::runtime_error("SQLite integrity_check failed; source must be preserved");
}
// Deterministic, non-cryptographic change detector, complemented by strict
// cross-checks against the independent command journal and world snapshot.
std::string historical_digest(const std::string& id, std::uint64_t generation,
                              const Event& ev, const std::string& command) {
    const std::string record = id + "|" + std::to_string(generation) + "|" +
        std::to_string(ev.tick) + "|" + command + "|" +
        std::to_string(static_cast<int>(ev.result)) + "|" + std::to_string(ev.transferred);
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : record) { value ^= byte; value *= 1099511628211ULL; }
    return std::to_string(value);
}
std::string physical_digest(const PhysicalIncident& e) {
    // Non-cryptographic corruption detector; strict reconciliation with
    // independent resource/jump ledgers and simulation activity is mandatory.
    const auto value=e.incident_id+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick)+"|"+std::to_string(e.command_id)+"|"+
        std::to_string(static_cast<int>(e.kind))+"|"+e.source+"|"+
        e.destination+"|"+std::to_string(e.amount)+"|"+e.detail;
    std::uint64_t digest=14695981039346656037ULL;
    for(const unsigned char byte:value){digest^=byte;digest*=1099511628211ULL;}
    return std::to_string(digest);
}
std::string sqlite_str(sqlite3_stmt* q,int column){
    const auto* text=sqlite3_column_text(q,column);
    if(!text || sqlite3_column_type(q,column)!=SQLITE_TEXT)
        throw std::runtime_error("SQLite: missing/invalid physical field");
    return reinterpret_cast<const char*>(text);
}
std::string knowledge_digest(const std::string& previous, const KnowledgeRecord& r) {
    // Accidental-corruption detector, not a cryptographic signature. A chain
    // also detects leaf deletion without depending on another descendant.
    const std::string text=previous+"|"+std::to_string(r.id)+"|"+r.incident_id+"|"+
        std::to_string(r.parent_id)+"|"+std::to_string(r.speaker_npc)+"|"+
        std::to_string(r.recipient_npc)+"|"+std::to_string(static_cast<int>(r.fidelity))+"|"+
        std::to_string(r.valence)+"|"+std::to_string(r.confidence)+"|"+
        std::to_string(r.generation)+"|"+std::to_string(r.tick)+"|"+r.account;
    std::uint64_t value=14695981039346656037ULL;
    for(const unsigned char byte:text){value^=byte;value*=1099511628211ULL;}
    return std::to_string(value);
}
std::string social_digest(const std::string& previous,const std::string& contents){
    std::uint64_t v=14695981039346656037ULL;
    for(const unsigned char b:previous+"|"+contents){v^=b;v*=1099511628211ULL;}
    return std::to_string(v); // Corruption detector; NOT adversarial authentication.
}
std::string social_members(const std::vector<std::uint64_t>& members){
    std::string s;
    for(auto id:members){if(!s.empty())s+=",";s+=std::to_string(id);}
    return s;
}
std::vector<std::uint64_t> parse_social_members(const std::string& record){
    std::vector<std::uint64_t> out;std::stringstream ss(record);std::string piece;
    while(std::getline(ss,piece,',')){
        if(piece.empty()||piece.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("invalid social group membership encoding");
        const auto id=std::stoull(piece);
        if(id==0 || (!out.empty() && id<=out.back()))
            throw std::runtime_error("noncanonical social group membership");
        out.push_back(id);
    }
    if(out.size()<2||out.size()>24||social_members(out)!=record)
        throw std::runtime_error("social group membership out of bounds");
    return out;
}
SocialGroup read_group(sqlite3_stmt* q){
    const auto id=sqlite3_column_int64(q,0),g=sqlite3_column_int64(q,2);
    if(id<=0||g<=0)throw std::runtime_error("invalid social group identity");
    SocialGroup r;r.id=static_cast<std::uint64_t>(id);r.name=sqlite_str(q,1);
    r.generation=static_cast<std::uint64_t>(g);
    r.members=parse_social_members(sqlite_str(q,3));return r;
}
GroupView read_view(sqlite3_stmt* q){
    const auto id=sqlite3_column_int64(q,0),gid=sqlite3_column_int64(q,1),kid=sqlite3_column_int64(q,2),
        g=sqlite3_column_int64(q,6),tick=sqlite3_column_int64(q,7);
    if(id<=0||gid<=0||kid<=0||g<=0||tick<0)
        throw std::runtime_error("invalid social group view identity");
    GroupView r;r.id=id;r.group_id=gid;r.knowledge_id=kid;
    r.stance=static_cast<GroupStance>(sqlite3_column_int(q,3));
    r.confidence=sqlite3_column_int(q,4);r.account=sqlite_str(q,5);
    r.generation=g;r.tick=tick;return r;
}
GroupMembershipEvent read_membership(sqlite3_stmt* q){
    const auto id=sqlite3_column_int64(q,0),group=sqlite3_column_int64(q,1);
    const auto epoch=sqlite3_column_int64(q,5),prior=sqlite3_column_int64(q,6);
    const auto gen=sqlite3_column_int64(q,7),tick=sqlite3_column_int64(q,8);
    if(id<=0||group<=0||epoch<=0||prior<0||gen<=0||tick<0)
        throw std::runtime_error("SQLite: invalid membership event identity");
    GroupMembershipEvent r;
    r.id=id;r.group_id=group;r.npc_id=parse_u64(sqlite3_column_text(q,2));
    r.sponsor_npc=parse_u64(sqlite3_column_text(q,3));
    r.kind=static_cast<GroupMembershipKind>(sqlite3_column_int(q,4));
    r.epoch=epoch;r.prior_id=prior;r.generation=gen;r.tick=tick;
    return r;
}
std::string membership_body(const GroupMembershipEvent& r){
    return std::to_string(r.id)+"|"+std::to_string(r.group_id)+"|"+
        std::to_string(r.npc_id)+"|"+std::to_string(r.sponsor_npc)+"|"+
        std::to_string(static_cast<int>(r.kind))+"|"+std::to_string(r.epoch)+"|"+
        std::to_string(r.prior_id)+"|"+std::to_string(r.generation)+"|"+std::to_string(r.tick);
}
std::string group_body(const SocialGroup& r){
    return std::to_string(r.id)+"|"+r.name+"|"+std::to_string(r.generation)+"|"+social_members(r.members);
}
std::string view_body(const GroupView& r){
    return std::to_string(r.id)+"|"+std::to_string(r.group_id)+"|"+
        std::to_string(r.knowledge_id)+"|"+std::to_string(static_cast<int>(r.stance))+"|"+
        std::to_string(r.confidence)+"|"+r.account+"|"+
        std::to_string(r.generation)+"|"+std::to_string(r.tick);
}
SocialTieEvent read_tie(sqlite3_stmt* q){
    const auto id=sqlite3_column_int64(q,0),prior=sqlite3_column_int64(q,3),
        claim=sqlite3_column_int64(q,4),gen=sqlite3_column_int64(q,8),tick=sqlite3_column_int64(q,9);
    if(id<=0||prior<0||claim<0||gen<=0||tick<0)
        throw std::runtime_error("SQLite: invalid social tie identity");
    SocialTieEvent r;r.id=static_cast<std::uint64_t>(id);
    r.observer_npc=parse_u64(sqlite3_column_text(q,1));
    r.source_npc=parse_u64(sqlite3_column_text(q,2));
    r.prior_id=static_cast<std::uint64_t>(prior);
    r.knowledge_id=static_cast<std::uint64_t>(claim);
    r.kind=static_cast<SocialTieKind>(sqlite3_column_int(q,5));
    r.delta=sqlite3_column_int(q,6);r.trust=sqlite3_column_int(q,7);
    r.generation=static_cast<std::uint64_t>(gen);r.tick=static_cast<std::uint64_t>(tick);
    return r;
}
SocialEncounter read_encounter(sqlite3_stmt* q){
    const auto id=sqlite3_column_int64(q,0),cycle=sqlite3_column_int64(q,1);
    const auto gen=sqlite3_column_int64(q,4),tick=sqlite3_column_int64(q,5);
    const auto slot=sqlite3_column_int64(q,6),prior=sqlite3_column_int64(q,7),ord=sqlite3_column_int64(q,8);
    if(id<=0||cycle<=0||gen<=0||tick<0||slot<0||prior<0||ord<=0)
        throw std::runtime_error("SQLite: malformed encounter record");
    SocialEncounter e;
    e.id=id;e.cycle_id=cycle;
    e.npc_low=parse_u64(sqlite3_column_text(q,2));
    e.npc_high=parse_u64(sqlite3_column_text(q,3));
    e.generation=gen;e.tick=tick;e.slot=slot;e.prior_id=prior;e.ordinal=ord;
    return e;
}
std::string encounter_body(const SocialEncounter& e){
    return std::to_string(e.id)+"|"+std::to_string(e.cycle_id)+"|"+
        std::to_string(e.npc_low)+"|"+std::to_string(e.npc_high)+"|"+
        std::to_string(e.generation)+"|"+std::to_string(e.tick)+"|"+
        std::to_string(e.slot)+"|"+std::to_string(e.prior_id)+"|"+
        std::to_string(e.ordinal);
}
SocialInteractionEvent read_interaction(sqlite3_stmt* q) {
    SocialInteractionEvent e;
    const auto id=sqlite3_column_int64(q,0),encounter=sqlite3_column_int64(q,1),
        prior=sqlite3_column_int64(q,7),gen=sqlite3_column_int64(q,8),tick=sqlite3_column_int64(q,9);
    if(id<=0||encounter<=0||prior<0||gen<=0||tick<0)
        throw std::runtime_error("SQLite: invalid social interaction row");
    e.id=id;e.encounter_id=encounter;
    e.observer_npc=parse_u64(sqlite3_column_text(q,2));
    e.actor_npc=parse_u64(sqlite3_column_text(q,3));
    e.kind=static_cast<SocialInteractionKind>(sqlite3_column_int(q,4));
    e.delta=sqlite3_column_int(q,5);e.rapport=sqlite3_column_int(q,6);
    e.prior_id=prior;e.generation=gen;e.tick=tick;
    return e;
}
std::string interaction_body(const SocialInteractionEvent& e){
    return std::to_string(e.id)+"|"+std::to_string(e.encounter_id)+"|"+
        std::to_string(e.observer_npc)+"|"+std::to_string(e.actor_npc)+"|"+
        std::to_string(static_cast<int>(e.kind))+"|"+std::to_string(e.delta)+"|"+
        std::to_string(e.rapport)+"|"+std::to_string(e.prior_id)+"|"+
        std::to_string(e.generation)+"|"+std::to_string(e.tick);
}
GroupAttributionEvent read_attribution(sqlite3_stmt* q){
    GroupAttributionEvent e;
    const auto positive=[&](int i)->std::uint64_t{
        auto n=sqlite3_column_int64(q,i);
        if(n<=0)throw std::runtime_error("SQLite: invalid group attribution identity");
        return static_cast<std::uint64_t>(n);
    };
    e.id=positive(0);e.interaction_id=positive(1);e.observer_group=positive(2);
    e.actor_group=positive(3);
    e.observer_npc=parse_u64(sqlite3_column_text(q,4));
    e.actor_npc=parse_u64(sqlite3_column_text(q,5));
    const auto seq=sqlite3_column_int64(q,6),prior=sqlite3_column_int64(q,10),
        tick=sqlite3_column_int64(q,12);
    if(seq<0||prior<0||tick<0)throw std::runtime_error("SQLite: negative attribution epoch");
    e.membership_seq=static_cast<std::uint64_t>(seq);
    e.stance=static_cast<GroupAttributionStance>(sqlite3_column_int(q,7));
    e.delta=sqlite3_column_int(q,8);e.feeling=sqlite3_column_int(q,9);
    e.prior_id=static_cast<std::uint64_t>(prior);
    e.generation=positive(11);e.tick=static_cast<std::uint64_t>(tick);
    return e;
}
std::string attribution_body(const GroupAttributionEvent& e){
    return std::to_string(e.id)+"|"+std::to_string(e.interaction_id)+"|"+
        std::to_string(e.observer_group)+"|"+std::to_string(e.actor_group)+"|"+
        std::to_string(e.observer_npc)+"|"+std::to_string(e.actor_npc)+"|"+
        std::to_string(e.membership_seq)+"|"+std::to_string(static_cast<int>(e.stance))+"|"+
        std::to_string(e.delta)+"|"+std::to_string(e.feeling)+"|"+
        std::to_string(e.prior_id)+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick);
}
int attribution_delta(GroupAttributionStance stance){
    if(stance==GroupAttributionStance::Appreciative)return 8;
    if(stance==GroupAttributionStance::Resentful)return -12;
    return 0;
}

PhysicalAppraisalEvent read_physical_appraisal(sqlite3_stmt* q){
    PhysicalAppraisalEvent e;
    const auto id=sqlite3_column_int64(q,0),observation=sqlite3_column_int64(q,1),
        grp=sqlite3_column_int64(q,4),epoch=sqlite3_column_int64(q,5),
        gen=sqlite3_column_int64(q,8),tick=sqlite3_column_int64(q,9);
    if(id<=0||observation<=0||grp<0||epoch<0||gen<=0||tick<0)
        throw std::runtime_error("SQLite: invalid physical appraisal row");
    e.id=id;e.observation_id=observation;e.incident_id=sqlite_str(q,2);
    e.observer_npc=parse_u64(sqlite3_column_text(q,3));e.context_group=grp;
    e.membership_seq=epoch;e.stance=static_cast<PhysicalAppraisalStance>(sqlite3_column_int(q,6));
    e.confidence=sqlite3_column_int(q,7);e.generation=gen;e.tick=tick;
    return e;
}
std::string physical_appraisal_body(const PhysicalAppraisalEvent& e){
    return std::to_string(e.id)+"|"+std::to_string(e.observation_id)+"|"+
        e.incident_id+"|"+std::to_string(e.observer_npc)+"|"+
        std::to_string(e.context_group)+"|"+std::to_string(e.membership_seq)+"|"+
        std::to_string(static_cast<int>(e.stance))+"|"+std::to_string(e.confidence)+"|"+
        std::to_string(e.generation)+"|"+std::to_string(e.tick);
}

IncidentRoleReceipt read_incident_role(sqlite3_stmt* q){
    IncidentRoleReceipt e;
    const auto id=sqlite3_column_int64(q,0),npc=sqlite3_column_int64(q,2),
      observation=sqlite3_column_int64(q,4),gen=sqlite3_column_int64(q,5),
      tick=sqlite3_column_int64(q,6);
    if(id<=0||npc<=0||observation<0||gen<=0||tick<0)
        throw std::runtime_error("SQLite: invalid NPC incident role receipt");
    e.id=id;e.incident_id=sqlite_str(q,1);e.npc_id=npc;
    e.role=static_cast<VerifiedNpcRole>(sqlite3_column_int(q,3));
    e.observation_id=observation;e.generation=gen;e.tick=tick;
    return e;
}
std::string incident_role_body(const IncidentRoleReceipt& e){
    return std::to_string(e.id)+"|"+e.incident_id+"|"+
      std::to_string(e.npc_id)+"|"+std::to_string(static_cast<int>(e.role))+"|"+
      std::to_string(e.observation_id)+"|"+std::to_string(e.generation)+"|"+
      std::to_string(e.tick);
}

NpcMiningAssignment read_npc_mining_assignment(sqlite3_stmt* q){
    NpcMiningAssignment e;
    const auto v=[&](int i)->std::uint64_t {
        const auto n=sqlite3_column_int64(q,i);
        if(n<0)throw std::runtime_error("SQLite: negative NPC mining assignment field");
        return static_cast<std::uint64_t>(n);
    };
    e.id=v(0);e.command_id=v(1);e.npc_id=v(2);e.ship_id=v(3);
    e.rock_id=v(4);e.generation=v(5);e.tick=v(6);
    if(!e.id||!e.command_id||!e.npc_id||!e.ship_id||!e.rock_id||!e.generation)
        throw std::runtime_error("SQLite: invalid NPC mining assignment");
    return e;
}
std::string npc_mining_assignment_body(const NpcMiningAssignment& e){
    return std::to_string(e.id)+"|"+std::to_string(e.command_id)+"|"+
        std::to_string(e.npc_id)+"|"+std::to_string(e.ship_id)+"|"+
        std::to_string(e.rock_id)+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick);
}
NpcMiningExecution read_npc_mining_execution(sqlite3_stmt* q){
    NpcMiningExecution e;
    const auto v=[&](int i)->std::uint64_t {
        const auto n=sqlite3_column_int64(q,i);
        if(n<0)throw std::runtime_error("SQLite: negative NPC mining execution field");
        return static_cast<std::uint64_t>(n);
    };
    e.id=v(0);e.assignment_id=v(1);e.command_id=v(2);e.npc_id=v(3);
    e.incident_id=sqlite_str(q,4);e.generation=v(5);e.tick=v(6);
    e.amount=sqlite3_column_int64(q,7);
    if(!e.id||!e.assignment_id||!e.command_id||!e.npc_id||
       e.incident_id.empty()||!e.generation||e.amount<=0)
        throw std::runtime_error("SQLite: invalid NPC mining execution");
    return e;
}
std::string npc_mining_execution_body(const NpcMiningExecution& e){
    return std::to_string(e.id)+"|"+std::to_string(e.assignment_id)+"|"+
        std::to_string(e.command_id)+"|"+std::to_string(e.npc_id)+"|"+
        e.incident_id+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick)+"|"+std::to_string(e.amount);
}

NpcFabricationAssignment read_npc_fabrication_assignment(sqlite3_stmt* q){
    NpcFabricationAssignment e;
    const auto v=[&](int i)->std::uint64_t {
        const auto n=sqlite3_column_int64(q,i);
        if(n<0)throw std::runtime_error("SQLite: negative fabrication assignment field");
        return static_cast<std::uint64_t>(n);
    };
    e.id=v(0);e.command_id=v(1);e.npc_id=v(2);e.ship_id=v(3);e.issuer=v(4);
    e.action=static_cast<Action>(sqlite3_column_int(q,5));e.material_code=v(6);
    e.quantity=sqlite3_column_int64(q,7);e.generation=v(8);e.tick=v(9);
    if(!e.id||!e.command_id||!e.npc_id||!e.ship_id||!e.issuer||
       (e.action!=Action::FabricateModule&&e.action!=Action::FabricateMaterialModule)||
       ((e.action==Action::FabricateModule&&e.material_code!=0)||
        (e.action==Action::FabricateMaterialModule&&e.material_code!=1&&e.material_code!=2))||
       e.quantity<1||e.quantity>1000||!e.generation)
        throw std::runtime_error("SQLite: invalid fabrication assignment");
    return e;
}
std::string npc_fabrication_assignment_body(const NpcFabricationAssignment& e){
    return std::to_string(e.id)+"|"+std::to_string(e.command_id)+"|"+
        std::to_string(e.npc_id)+"|"+std::to_string(e.ship_id)+"|"+
        std::to_string(e.issuer)+"|"+std::to_string(static_cast<int>(e.action))+"|"+
        std::to_string(e.material_code)+"|"+std::to_string(e.quantity)+"|"+
        std::to_string(e.generation)+"|"+std::to_string(e.tick);
}
NpcFabricationExecution read_npc_fabrication_execution(sqlite3_stmt* q){
    NpcFabricationExecution e;
    const auto v=[&](int i)->std::uint64_t {
        const auto n=sqlite3_column_int64(q,i);
        if(n<0)throw std::runtime_error("SQLite: negative fabrication execution field");
        return static_cast<std::uint64_t>(n);
    };
    e.id=v(0);e.assignment_id=v(1);e.command_id=v(2);e.npc_id=v(3);
    e.incident_id=sqlite_str(q,4);e.generation=v(5);e.tick=v(6);
    e.transferred=sqlite3_column_int64(q,7);
    if(!e.id||!e.assignment_id||!e.command_id||!e.npc_id||
       e.incident_id.empty()||!e.generation||e.transferred<=0)
        throw std::runtime_error("SQLite: invalid fabrication execution");
    return e;
}
std::string npc_fabrication_execution_body(const NpcFabricationExecution& e){
    return std::to_string(e.id)+"|"+std::to_string(e.assignment_id)+"|"+
        std::to_string(e.command_id)+"|"+std::to_string(e.npc_id)+"|"+
        e.incident_id+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick)+"|"+std::to_string(e.transferred);
}

struct EncounterCycle {
    std::uint64_t id=0,generation=0,tick=0,population=0,pairs=0,start=0,
                  scanned=0,end=0,contacts=0;
};
EncounterCycle read_encounter_cycle(sqlite3_stmt* q){
    EncounterCycle e;
    const auto value=[&](int i){
        const auto n=sqlite3_column_int64(q,i);
        if(n<0)throw std::runtime_error("SQLite: negative encounter cycle field");
        return static_cast<std::uint64_t>(n);
    };
    e.id=value(0);e.generation=value(1);e.tick=value(2);e.population=value(3);
    e.pairs=value(4);e.start=value(5);e.scanned=value(6);e.end=value(7);
    e.contacts=value(8);return e;
}
std::string encounter_cycle_body(const EncounterCycle& c){
    return std::to_string(c.id)+"|"+std::to_string(c.generation)+"|"+
        std::to_string(c.tick)+"|"+std::to_string(c.population)+"|"+
        std::to_string(c.pairs)+"|"+std::to_string(c.start)+"|"+
        std::to_string(c.scanned)+"|"+std::to_string(c.end)+"|"+
        std::to_string(c.contacts);
}
// Decode one unordered NPC pair without expanding all O(N^2) possibilities.
std::pair<std::uint64_t,std::uint64_t> encounter_pair_at(
        const std::vector<std::uint64_t>& npcs,std::uint64_t slot){
    for(std::size_t i=0;i+1<npcs.size();++i){
        const auto width=static_cast<std::uint64_t>(npcs.size()-i-1);
        if(slot<width)return {npcs[i],npcs[i+1+static_cast<std::size_t>(slot)]};
        slot-=width;
    }
    throw std::runtime_error("SQLite: encounter scheduler slot overflow");
}
std::string tie_body(const SocialTieEvent& r){
    return std::to_string(r.id)+"|"+std::to_string(r.observer_npc)+"|"+
        std::to_string(r.source_npc)+"|"+std::to_string(r.prior_id)+"|"+
        std::to_string(r.knowledge_id)+"|"+std::to_string(static_cast<int>(r.kind))+"|"+
        std::to_string(r.delta)+"|"+std::to_string(r.trust)+"|"+
        std::to_string(r.generation)+"|"+std::to_string(r.tick);
}
KnowledgeRecord read_knowledge(sqlite3_stmt* s) {
    KnowledgeRecord r;
    const auto ident=sqlite3_column_int64(s,0),parent=sqlite3_column_int64(s,2),
               generation=sqlite3_column_int64(s,8),tick=sqlite3_column_int64(s,9);
    if(ident<=0 || parent<0 || generation<=0 || tick<0)
        throw std::runtime_error("SQLite: invalid knowledge number");
    r.id=static_cast<std::uint64_t>(ident);
    r.incident_id=sqlite_str(s,1);
    r.parent_id=static_cast<std::uint64_t>(parent);
    r.speaker_npc=parse_u64(sqlite3_column_text(s,3));
    r.recipient_npc=parse_u64(sqlite3_column_text(s,4));
    r.fidelity=static_cast<ClaimFidelity>(sqlite3_column_int(s,5));
    r.valence=sqlite3_column_int(s,6);r.confidence=sqlite3_column_int(s,7);
    r.generation=static_cast<std::uint64_t>(generation);
    r.tick=static_cast<std::uint64_t>(tick);
    r.account=sqlite_str(s,10);
    return r;
}
bool valid_knowledge(const KnowledgeRecord& r) {
    return r.speaker_npc>0 && r.recipient_npc>0 && r.generation>0 &&
        r.valence>=-100 && r.valence<=100 && r.confidence>=0 && r.confidence<=100 &&
        static_cast<int>(r.fidelity)>=0 && static_cast<int>(r.fidelity)<=3 &&
        r.account.size()>0 && r.account.size()<=240 &&
        r.account.find('\0')==std::string::npos &&
        ((r.parent_id==0 && r.fidelity==ClaimFidelity::LocalObservation &&
          r.speaker_npc==r.recipient_npc) ||
         (r.parent_id>0 && r.fidelity!=ClaimFidelity::LocalObservation &&
          r.speaker_npc!=r.recipient_npc));
}
std::uint64_t latest_generation(sqlite3* db) {
    Statement q(db, "SELECT COALESCE(MAX(generation),0) FROM snapshots");
    if (!q.row()) throw std::runtime_error("SQLite: missing generation result");
    return static_cast<std::uint64_t>(sqlite3_column_int64(q.get(), 0));
}
}

class PersistentWorld::Store {
public:
    explicit Store(const std::string& path) {
        if (path.empty()) throw std::invalid_argument("SQLite filename required");
        if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            const std::string error = db_ ? sqlite3_errmsg(db_) : "open failed";
            sqlite3_close(db_); db_ = nullptr;
            throw std::runtime_error("SQLite open: " + error);
        }
        try {
        sqlite3_busy_timeout(db_, 5000);
        run(db_, "PRAGMA foreign_keys=ON");
        run(db_, "PRAGMA journal_mode=WAL");
        run(db_, "PRAGMA synchronous=FULL");
        Statement version(db_, "PRAGMA user_version");
        if (!version.row()) throw std::runtime_error("SQLite: no schema version");
        const int v = sqlite3_column_int(version.get(), 0);
        if (v < 0 || v > 17) throw std::runtime_error("SQLite: incompatible schema version");
        if (v == 0) {
            run(db_, "BEGIN IMMEDIATE");
            try {
                run(db_, "CREATE TABLE IF NOT EXISTS snapshots ("
                         "generation INTEGER PRIMARY KEY CHECK(generation>0),"
                         "format INTEGER NOT NULL CHECK(format=1),"
                         "state BLOB NOT NULL, checksum TEXT NOT NULL, ore_total INTEGER NOT NULL CHECK(ore_total>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS world_meta ("
                         "singleton INTEGER PRIMARY KEY CHECK(singleton=1), resource_origin INTEGER NOT NULL CHECK(resource_origin>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS receipts ("
                         "id TEXT PRIMARY KEY, accepted_generation INTEGER NOT NULL,"
                         "execution_tick INTEGER, result INTEGER, transferred INTEGER,"
                         "CHECK(transferred IS NULL OR transferred>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS resource_ledger ("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT, generation INTEGER NOT NULL,"
                         "tick INTEGER NOT NULL, command_id TEXT UNIQUE, source_id TEXT, destination_id TEXT,"
                         "amount INTEGER NOT NULL CHECK(amount>=0), reason TEXT NOT NULL)");
                run(db_, "PRAGMA user_version=1");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 1) {
            // One atomic, non-destructive v1 -> v2 upgrade. Pre-upgrade operations
            // cannot be reconstructed; only later v2 operations receive a journal.
            run(db_, "BEGIN IMMEDIATE");
            try {
                run(db_, "CREATE TABLE operation_log ("
                         "generation INTEGER PRIMARY KEY CHECK(generation>0),"
                         "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),"
                         "command TEXT, state_hash TEXT NOT NULL, tick INTEGER NOT NULL,"
                         "CHECK((kind=1 AND command IS NOT NULL) OR (kind!=1 AND command IS NULL)))");
                run(db_, "PRAGMA user_version=2");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 2) {
            const auto has_column = [&](const char* table,const char* name) {
                Statement info(db_, (std::string("PRAGMA table_info(")+table+")").c_str());
                while (info.row()) if (std::string(reinterpret_cast<const char*>(sqlite3_column_text(info.get(),1)))==name) return true;
                return false;
            };
            run(db_, "BEGIN IMMEDIATE");
            try {
                if (!has_column("world_meta","credit_origin"))
                    run(db_, "ALTER TABLE world_meta ADD COLUMN credit_origin INTEGER NOT NULL DEFAULT 0 CHECK(credit_origin>=0)");
                if (!has_column("snapshots","credit_total"))
                    run(db_, "ALTER TABLE snapshots ADD COLUMN credit_total INTEGER NOT NULL DEFAULT 0 CHECK(credit_total>=0)");
                run(db_, "CREATE TABLE IF NOT EXISTS credit_ledger (command_id TEXT PRIMARY KEY, tick INTEGER NOT NULL, station_id TEXT NOT NULL, owner_id TEXT NOT NULL, ore_units INTEGER NOT NULL CHECK(ore_units>0), unit_price INTEGER NOT NULL CHECK(unit_price>0), credit_amount INTEGER NOT NULL CHECK(credit_amount>0))");
                run(db_, "PRAGMA user_version=3");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 3) {
            run(db_, "BEGIN IMMEDIATE");
            try {
                // A jump's departure and arrival are accountable independent
                // records, committed in the same SQLite transaction as the ship.
                run(db_, "CREATE TABLE IF NOT EXISTS jump_ledger ("
                         "command_id TEXT PRIMARY KEY, ship_id TEXT NOT NULL,"
                         "gate_id TEXT NOT NULL, source_system INTEGER NOT NULL CHECK(source_system>0),"
                         "dest_system INTEGER NOT NULL CHECK(dest_system>0),"
                         "cargo_at_departure INTEGER NOT NULL CHECK(cargo_at_departure>=0),"
                         "accepted_tick INTEGER NOT NULL CHECK(accepted_tick>0),"
                         "completed_tick INTEGER CHECK(completed_tick>accepted_tick))");
                run(db_, "CREATE UNIQUE INDEX IF NOT EXISTS one_pending_jump_per_ship "
                         "ON jump_ledger(ship_id) WHERE completed_tick IS NULL");
                run(db_, "PRAGMA user_version=4");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 4) {
            // Existing v4 worlds retain all their snapshots/receipts. Earlier
            // events are not silently invented; new events start at this boundary.
            run(db_, "BEGIN IMMEDIATE");
            try {
                const auto baseline = latest_generation(db_);
                run(db_, "CREATE TABLE IF NOT EXISTS historical_meta ("
                         "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                         "start_generation INTEGER NOT NULL CHECK(start_generation>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS historical_events ("
                         "event_id TEXT PRIMARY KEY, command_id TEXT NOT NULL UNIQUE,"
                         "generation INTEGER NOT NULL CHECK(generation>0),"
                         "tick INTEGER NOT NULL CHECK(tick>=0),"
                         "actor_id TEXT NOT NULL, target_id TEXT NOT NULL,"
                         "action INTEGER NOT NULL CHECK(action>=0),"
                         "outcome INTEGER NOT NULL CHECK(outcome>=0),"
                         "transferred INTEGER NOT NULL CHECK(transferred>=0),"
                         "command TEXT NOT NULL, digest TEXT NOT NULL)");
                run(db_, "CREATE INDEX IF NOT EXISTS historical_by_generation ON historical_events(generation)");
                run(db_, "CREATE TRIGGER IF NOT EXISTS historical_no_update BEFORE UPDATE ON historical_events "
                         "BEGIN SELECT RAISE(ABORT, 'historical events are immutable'); END");
                run(db_, "CREATE TRIGGER IF NOT EXISTS historical_no_delete BEFORE DELETE ON historical_events "
                         "BEGIN SELECT RAISE(ABORT, 'historical events are immutable'); END");
                Statement boundary(db_, "INSERT INTO historical_meta(singleton,start_generation) VALUES(1,?)");
                boundary.i64(1, static_cast<std::int64_t>(baseline)); boundary.execute();
                run(db_, "PRAGMA user_version=5");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 5) {
            // Preserve old journals/snapshots. Never backfill legacy physical
            // events from incomplete evidence or claim old casualties.
            run(db_, "BEGIN IMMEDIATE");
            try {
                const auto baseline=latest_generation(db_);
                run(db_, "CREATE TABLE physical_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                         "start_generation INTEGER NOT NULL CHECK(start_generation>=0))");
                run(db_, "CREATE TABLE physical_incidents("
                         "incident_id TEXT PRIMARY KEY, generation INTEGER NOT NULL CHECK(generation>0),"
                         "tick INTEGER NOT NULL CHECK(tick>0),command_id TEXT NOT NULL,"
                         "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 5),"
                         "source TEXT NOT NULL,destination TEXT NOT NULL,"
                         "amount INTEGER NOT NULL CHECK(amount>=0),detail TEXT NOT NULL,digest TEXT NOT NULL)");
                run(db_, "CREATE INDEX physical_by_generation ON physical_incidents(generation)");
                run(db_, "CREATE TRIGGER physical_no_update BEFORE UPDATE ON physical_incidents "
                         "BEGIN SELECT RAISE(ABORT,'physical facts are immutable'); END");
                run(db_, "CREATE TRIGGER physical_no_delete BEFORE DELETE ON physical_incidents "
                         "BEGIN SELECT RAISE(ABORT,'physical facts are immutable'); END");
                Statement m(db_,"INSERT INTO physical_meta(singleton,start_generation) VALUES(1,?)");
                m.i64(1,static_cast<std::int64_t>(baseline));m.execute();
                run(db_,"PRAGMA user_version=6");run(db_,"COMMIT");
            } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 6) {
            // GRSM-02: knowledge starts empty on migration. It would be false
            // to invent witness stories for events from previous saves.
            run(db_, "BEGIN IMMEDIATE");
            try {
                const auto baseline=latest_generation(db_);
                run(db_, "CREATE TABLE knowledge_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                         "start_generation INTEGER NOT NULL CHECK(start_generation>=0),"
                         "row_count INTEGER NOT NULL CHECK(row_count>=0),"
                         "tip_digest TEXT NOT NULL)");
                run(db_, "CREATE TABLE npc_knowledge("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "incident_id TEXT NOT NULL REFERENCES physical_incidents(incident_id),"
                         "parent_id INTEGER REFERENCES npc_knowledge(id),"
                         "speaker_id TEXT NOT NULL,recipient_id TEXT NOT NULL,"
                         "fidelity INTEGER NOT NULL CHECK(fidelity BETWEEN 0 AND 3),"
                         "valence INTEGER NOT NULL CHECK(valence BETWEEN -100 AND 100),"
                         "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 100),"
                         "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                         "account TEXT NOT NULL CHECK(length(account) BETWEEN 1 AND 240),"
                         "digest TEXT NOT NULL)");
                run(db_, "CREATE INDEX knowledge_by_recipient ON npc_knowledge(recipient_id,id)");
                run(db_, "CREATE UNIQUE INDEX unique_local_observation "
                         "ON npc_knowledge(incident_id,recipient_id) WHERE parent_id IS NULL");
                run(db_, "CREATE TRIGGER knowledge_no_update BEFORE UPDATE ON npc_knowledge "
                         "BEGIN SELECT RAISE(ABORT,'knowledge accounts are immutable'); END");
                run(db_, "CREATE TRIGGER knowledge_no_delete BEFORE DELETE ON npc_knowledge "
                         "BEGIN SELECT RAISE(ABORT,'knowledge accounts are immutable'); END");
                Statement meta(db_, "INSERT INTO knowledge_meta(singleton,start_generation,row_count,tip_digest) "
                                    "VALUES(1,?,0,'origin')");
                meta.i64(1,static_cast<std::int64_t>(baseline));meta.execute();
                run(db_, "PRAGMA user_version=7");run(db_, "COMMIT");
            } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 7) {
            // GRSM-03: no retrospective groups, memberships, or opinions.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE social_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "groups_count INTEGER NOT NULL DEFAULT 0,groups_tip TEXT NOT NULL,"
                    "relays_count INTEGER NOT NULL DEFAULT 0,relays_tip TEXT NOT NULL,"
                    "views_count INTEGER NOT NULL DEFAULT 0,views_tip TEXT NOT NULL)");
                run(db_,"INSERT INTO social_meta VALUES(1,0,'origin',0,'origin',0,'origin')");
                run(db_,"CREATE TABLE social_groups(id INTEGER PRIMARY KEY,name TEXT NOT NULL,"
                    "generation INTEGER NOT NULL CHECK(generation>0),members TEXT NOT NULL,digest TEXT NOT NULL)");
                run(db_,"CREATE TABLE group_relays(id INTEGER PRIMARY KEY,"
                    "group_id INTEGER NOT NULL REFERENCES social_groups(id),"
                    "knowledge_id INTEGER NOT NULL UNIQUE REFERENCES npc_knowledge(id),digest TEXT NOT NULL)");
                run(db_,"CREATE TABLE group_views(id INTEGER PRIMARY KEY,"
                    "group_id INTEGER NOT NULL REFERENCES social_groups(id),"
                    "knowledge_id INTEGER NOT NULL REFERENCES npc_knowledge(id),"
                    "stance INTEGER NOT NULL CHECK(stance BETWEEN 1 AND 3),"
                    "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 100),"
                    "account TEXT NOT NULL CHECK(length(account) BETWEEN 1 AND 240),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "digest TEXT NOT NULL)");
                for(const auto* table:{"social_groups","group_relays","group_views"}){
                    const auto name=std::string(table);
                    run(db_,("CREATE TRIGGER "+name+"_no_update BEFORE UPDATE ON "+name+
                             " BEGIN SELECT RAISE(ABORT,'social history immutable'); END").c_str());
                    run(db_,("CREATE TRIGGER "+name+"_no_delete BEFORE DELETE ON "+name+
                             " BEGIN SELECT RAISE(ABORT,'social history immutable'); END").c_str());
                }
                run(db_,"PRAGMA user_version=8");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 8) {
            // GRSM-03b: pre-existing games have no invented contacts or trust.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE social_tie_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL DEFAULT 0 CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO social_tie_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE social_tie_events(id INTEGER PRIMARY KEY,"
                    "observer TEXT NOT NULL,source TEXT NOT NULL,prior_id INTEGER NOT NULL DEFAULT 0,"
                    "knowledge_id INTEGER NOT NULL DEFAULT 0,"
                    "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 2),"
                    "delta INTEGER NOT NULL CHECK(delta BETWEEN -25 AND 25),"
                    "trust INTEGER NOT NULL CHECK(trust BETWEEN 0 AND 100),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "digest TEXT NOT NULL)");
                run(db_,"CREATE INDEX social_ties_pair ON social_tie_events(observer,source,id)");
                run(db_,"CREATE UNIQUE INDEX social_tie_one_reassessment ON social_tie_events(knowledge_id) WHERE knowledge_id>0");
                run(db_,"CREATE TRIGGER social_tie_no_update BEFORE UPDATE ON social_tie_events "
                    "BEGIN SELECT RAISE(ABORT,'social tie history immutable'); END");
                run(db_,"CREATE TRIGGER social_tie_no_delete BEFORE DELETE ON social_tie_events "
                    "BEGIN SELECT RAISE(ABORT,'social tie history immutable'); END");
                run(db_,"PRAGMA user_version=9");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 9) {
            // GRSM-03c: never synthesize memberships for old stories.
            // Prior relays have epoch marker 0: founding members only.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE membership_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL DEFAULT 0 CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO membership_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE group_membership_events(id INTEGER PRIMARY KEY,"
                    "group_id INTEGER NOT NULL REFERENCES social_groups(id),"
                    "npc_id TEXT NOT NULL,sponsor_id TEXT NOT NULL,"
                    "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 2),"
                    "epoch INTEGER NOT NULL CHECK(epoch>0),prior_id INTEGER NOT NULL CHECK(prior_id>=0),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "digest TEXT NOT NULL)");
                run(db_,"CREATE INDEX membership_group_history ON group_membership_events(group_id,id)");
                run(db_,"CREATE TRIGGER membership_no_update BEFORE UPDATE ON group_membership_events "
                    "BEGIN SELECT RAISE(ABORT,'membership history immutable'); END");
                run(db_,"CREATE TRIGGER membership_no_delete BEFORE DELETE ON group_membership_events "
                    "BEGIN SELECT RAISE(ABORT,'membership history immutable'); END");
                run(db_,"ALTER TABLE group_relays ADD COLUMN membership_seq INTEGER NOT NULL DEFAULT 0 "
                    "CHECK(membership_seq>=0)");
                run(db_,"PRAGMA user_version=10");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 10) {
            // GRSM-03c.2: no historical contacts may be synthesized on upgrade.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE encounter_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "scan_count INTEGER NOT NULL CHECK(scan_count>=0),scan_tip TEXT NOT NULL,"
                    "contact_count INTEGER NOT NULL CHECK(contact_count>=0),contact_tip TEXT NOT NULL,"
                    "cursor INTEGER NOT NULL CHECK(cursor>=0))");
                run(db_,"INSERT INTO encounter_meta VALUES(1,0,'origin',0,'origin',0)");
                run(db_,"CREATE TABLE encounter_scans(id INTEGER PRIMARY KEY,"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "population INTEGER NOT NULL CHECK(population BETWEEN 2 AND 1024),"
                    "pair_count INTEGER NOT NULL CHECK(pair_count>0),"
                    "start_slot INTEGER NOT NULL CHECK(start_slot>=0),"
                    "scanned INTEGER NOT NULL CHECK(scanned BETWEEN 1 AND 64),"
                    "end_slot INTEGER NOT NULL CHECK(end_slot>=0),"
                    "contacts INTEGER NOT NULL CHECK(contacts BETWEEN 0 AND 8),digest TEXT NOT NULL)");
                run(db_,"CREATE TABLE social_encounters(id INTEGER PRIMARY KEY,"
                    "cycle_id INTEGER NOT NULL REFERENCES encounter_scans(id),"
                    "npc_low TEXT NOT NULL,npc_high TEXT NOT NULL,"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "slot INTEGER NOT NULL CHECK(slot>=0),prior_id INTEGER NOT NULL CHECK(prior_id>=0),"
                    "ordinal INTEGER NOT NULL CHECK(ordinal>0),digest TEXT NOT NULL)");
                run(db_,"CREATE UNIQUE INDEX social_encounter_cycle_slot ON social_encounters(cycle_id,slot)");
                run(db_,"CREATE INDEX social_encounter_pair ON social_encounters(npc_low,npc_high,id)");
                for(const auto* name:{"encounter_scans","social_encounters"}) {
                    run(db_,(std::string("CREATE TRIGGER ")+name+"_no_update BEFORE UPDATE ON "+name+
                        " BEGIN SELECT RAISE(ABORT,'encounter history immutable'); END").c_str());
                    run(db_,(std::string("CREATE TRIGGER ")+name+"_no_delete BEFORE DELETE ON "+name+
                        " BEGIN SELECT RAISE(ABORT,'encounter history immutable'); END").c_str());
                }
                run(db_,"PRAGMA user_version=11");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if (v <= 11) {
            // GRSM-03c.3: no fabricated historical cooperation/conflict on
            // migration from older worlds or their empty encounter logs.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE interaction_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO interaction_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE social_interactions(id INTEGER PRIMARY KEY,"
                    "encounter_id INTEGER NOT NULL REFERENCES social_encounters(id),"
                    "observer TEXT NOT NULL,actor TEXT NOT NULL,"
                    "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 2),"
                    "delta INTEGER NOT NULL CHECK(delta IN (8,-12)),"
                    "rapport INTEGER NOT NULL CHECK(rapport BETWEEN -100 AND 100),"
                    "prior_id INTEGER NOT NULL CHECK(prior_id>=0),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "digest TEXT NOT NULL)");
                run(db_,"CREATE UNIQUE INDEX social_interaction_once ON "
                    "social_interactions(encounter_id,observer,actor)");
                run(db_,"CREATE INDEX social_interaction_pair ON "
                    "social_interactions(observer,actor,id)");
                run(db_,"CREATE TRIGGER social_interactions_no_update BEFORE UPDATE ON social_interactions "
                    "BEGIN SELECT RAISE(ABORT,'interaction history immutable'); END");
                run(db_,"CREATE TRIGGER social_interactions_no_delete BEFORE DELETE ON social_interactions "
                    "BEGIN SELECT RAISE(ABORT,'interaction history immutable'); END");
                run(db_,"PRAGMA user_version=12");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if(v<=12){
            // GRSM-03d: old worlds have no invented between-group sentiment.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE attribution_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO attribution_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE group_attributions(id INTEGER PRIMARY KEY,"
                    "interaction_id INTEGER NOT NULL REFERENCES social_interactions(id),"
                    "observer_group INTEGER NOT NULL REFERENCES social_groups(id),"
                    "actor_group INTEGER NOT NULL REFERENCES social_groups(id),"
                    "observer TEXT NOT NULL,actor TEXT NOT NULL,"
                    "membership_seq INTEGER NOT NULL CHECK(membership_seq>=0),"
                    "stance INTEGER NOT NULL CHECK(stance BETWEEN 1 AND 3),"
                    "delta INTEGER NOT NULL CHECK(delta IN (8,-12,0)),"
                    "feeling INTEGER NOT NULL CHECK(feeling BETWEEN -100 AND 100),"
                    "prior_id INTEGER NOT NULL CHECK(prior_id>=0),"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "tick INTEGER NOT NULL CHECK(tick>=0),digest TEXT NOT NULL,"
                    "CHECK(observer_group<>actor_group))");
                run(db_,"CREATE UNIQUE INDEX group_attribution_source_once ON "
                    "group_attributions(interaction_id,observer_group,actor_group)");
                run(db_,"CREATE INDEX group_attribution_member_history ON "
                    "group_attributions(observer,observer_group,actor_group,id)");
                run(db_,"CREATE TRIGGER group_attributions_no_update BEFORE UPDATE ON group_attributions "
                    "BEGIN SELECT RAISE(ABORT,'group attributions immutable'); END");
                run(db_,"CREATE TRIGGER group_attributions_no_delete BEFORE DELETE ON group_attributions "
                    "BEGIN SELECT RAISE(ABORT,'group attributions immutable'); END");
                run(db_,"PRAGMA user_version=13");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if(v<=13){
            // GRSM-04a: old worlds cannot be granted retrospective eyewitness
            // opinions. New appraisals require a real firsthand claim on tick.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE physical_appraisal_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO physical_appraisal_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE physical_appraisals(id INTEGER PRIMARY KEY,"
                    "observation_id INTEGER NOT NULL REFERENCES npc_knowledge(id),"
                    "incident_id TEXT NOT NULL REFERENCES physical_incidents(incident_id),"
                    "observer TEXT NOT NULL,context_group INTEGER NOT NULL CHECK(context_group>=0),"
                    "membership_seq INTEGER NOT NULL CHECK(membership_seq>=0),"
                    "stance INTEGER NOT NULL CHECK(stance BETWEEN 1 AND 3),"
                    "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 100),"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "tick INTEGER NOT NULL CHECK(tick>=0),digest TEXT NOT NULL,"
                    "UNIQUE(observation_id,context_group))");
                run(db_,"CREATE INDEX physical_appraisal_observer ON physical_appraisals(observer,context_group,id)");
                run(db_,"CREATE TRIGGER physical_appraisals_no_update BEFORE UPDATE ON physical_appraisals "
                    "BEGIN SELECT RAISE(ABORT,'physical appraisal immutable'); END");
                run(db_,"CREATE TRIGGER physical_appraisals_no_delete BEFORE DELETE ON physical_appraisals "
                    "BEGIN SELECT RAISE(ABORT,'physical appraisal immutable'); END");
                run(db_,"PRAGMA user_version=14");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if(v<=14){
            // GRSM-04b.A: an auditable distinction between a *known* victim,
            // firsthand observer, and an unproven NPC actor or beneficiary.
            // Migrations never manufacture historical participation receipts.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE incident_role_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO incident_role_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE incident_role_receipts(id INTEGER PRIMARY KEY,"
                    "incident_id TEXT NOT NULL REFERENCES physical_incidents(incident_id),"
                    "npc_id TEXT NOT NULL,role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 2),"
                    "observation_id INTEGER NOT NULL CHECK(observation_id>=0),"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "tick INTEGER NOT NULL CHECK(tick>=0),digest TEXT NOT NULL,"
                    "UNIQUE(incident_id,npc_id,role),"
                    "CHECK((role=1 AND observation_id=0) OR (role=2 AND observation_id>0)))");
                run(db_,"CREATE INDEX incident_role_by_npc ON incident_role_receipts(npc_id,role,id)");
                run(db_,"CREATE TRIGGER incident_role_no_update BEFORE UPDATE ON incident_role_receipts "
                    "BEGIN SELECT RAISE(ABORT,'incident role immutable'); END");
                run(db_,"CREATE TRIGGER incident_role_no_delete BEFORE DELETE ON incident_role_receipts "
                    "BEGIN SELECT RAISE(ABORT,'incident role immutable'); END");
                run(db_,"PRAGMA user_version=15");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if(v<=15){
            // GRSM-04b.B1: pending assignment and independently measured
            // execution are separate immutable rows. Neither exists for
            // historical commands upgraded from v15.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE npc_mining_assignment_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO npc_mining_assignment_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE npc_mining_assignments(id INTEGER PRIMARY KEY,"
                    "command_id TEXT NOT NULL UNIQUE REFERENCES receipts(id),"
                    "npc_id TEXT NOT NULL,ship_id TEXT NOT NULL,rock_id TEXT NOT NULL,"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "tick INTEGER NOT NULL CHECK(tick>=0),digest TEXT NOT NULL)");
                run(db_,"CREATE TRIGGER npc_mining_assignment_no_update BEFORE UPDATE ON npc_mining_assignments "
                    "BEGIN SELECT RAISE(ABORT,'NPC mining assignment immutable'); END");
                run(db_,"CREATE TRIGGER npc_mining_assignment_no_delete BEFORE DELETE ON npc_mining_assignments "
                    "BEGIN SELECT RAISE(ABORT,'NPC mining assignment immutable'); END");
                run(db_,"CREATE TABLE npc_mining_execution_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO npc_mining_execution_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE npc_mining_executions(id INTEGER PRIMARY KEY,"
                    "assignment_id INTEGER NOT NULL UNIQUE REFERENCES npc_mining_assignments(id),"
                    "command_id TEXT NOT NULL UNIQUE REFERENCES receipts(id),"
                    "npc_id TEXT NOT NULL,"
                    "incident_id TEXT NOT NULL UNIQUE REFERENCES physical_incidents(incident_id),"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "tick INTEGER NOT NULL CHECK(tick>=0),"
                    "amount INTEGER NOT NULL CHECK(amount>0),digest TEXT NOT NULL)");
                run(db_,"CREATE TRIGGER npc_mining_execution_no_update BEFORE UPDATE ON npc_mining_executions "
                    "BEGIN SELECT RAISE(ABORT,'NPC mining execution immutable'); END");
                run(db_,"CREATE TRIGGER npc_mining_execution_no_delete BEFORE DELETE ON npc_mining_executions "
                    "BEGIN SELECT RAISE(ABORT,'NPC mining execution immutable'); END");
                run(db_,"PRAGMA user_version=16");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        if(v<=16){
            // B2a extends named execution without touching the meaning of old
            // mining receipts. Migration never assigns a worker retroactively.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE npc_fabrication_assignment_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO npc_fabrication_assignment_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE npc_fabrication_assignments(id INTEGER PRIMARY KEY,"
                    "command_id TEXT NOT NULL UNIQUE REFERENCES receipts(id),"
                    "npc_id TEXT NOT NULL,ship_id TEXT NOT NULL,issuer TEXT NOT NULL,"
                    "action INTEGER NOT NULL,material_code INTEGER NOT NULL,quantity INTEGER NOT NULL CHECK(quantity>0),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),digest TEXT NOT NULL)");
                run(db_,"CREATE TRIGGER npc_fabrication_assignment_no_update BEFORE UPDATE ON npc_fabrication_assignments "
                    "BEGIN SELECT RAISE(ABORT,'fabrication assignment immutable'); END");
                run(db_,"CREATE TRIGGER npc_fabrication_assignment_no_delete BEFORE DELETE ON npc_fabrication_assignments "
                    "BEGIN SELECT RAISE(ABORT,'fabrication assignment immutable'); END");
                run(db_,"CREATE TABLE npc_fabrication_execution_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                    "event_count INTEGER NOT NULL CHECK(event_count>=0),tip TEXT NOT NULL)");
                run(db_,"INSERT INTO npc_fabrication_execution_meta VALUES(1,0,'origin')");
                run(db_,"CREATE TABLE npc_fabrication_executions(id INTEGER PRIMARY KEY,"
                    "assignment_id INTEGER NOT NULL UNIQUE REFERENCES npc_fabrication_assignments(id),"
                    "command_id TEXT NOT NULL UNIQUE REFERENCES receipts(id),"
                    "npc_id TEXT NOT NULL,"
                    "incident_id TEXT NOT NULL UNIQUE REFERENCES physical_incidents(incident_id),"
                    "generation INTEGER NOT NULL CHECK(generation>0),tick INTEGER NOT NULL CHECK(tick>=0),"
                    "transferred INTEGER NOT NULL CHECK(transferred>0),digest TEXT NOT NULL)");
                run(db_,"CREATE TRIGGER npc_fabrication_execution_no_update BEFORE UPDATE ON npc_fabrication_executions "
                    "BEGIN SELECT RAISE(ABORT,'fabrication execution immutable'); END");
                run(db_,"CREATE TRIGGER npc_fabrication_execution_no_delete BEFORE DELETE ON npc_fabrication_executions "
                    "BEGIN SELECT RAISE(ABORT,'fabrication execution immutable'); END");
                run(db_,"PRAGMA user_version=17");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        } catch (...) { sqlite3_close_v2(db_); db_ = nullptr; throw; }
    }
    ~Store() { sqlite3_close(db_); }
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool restore(Simulation& out, std::uint64_t& generation, std::int64_t& origin, std::int64_t& credit_origin) {
        Statement q(db_, "SELECT generation,format,state,checksum,ore_total,credit_total FROM snapshots ORDER BY generation DESC LIMIT 1");
        if (!q.row()) {
            Statement check(db_, "SELECT COUNT(*) FROM receipts");
            if (!check.row() || sqlite3_column_int64(check.get(), 0) != 0)
                throw std::runtime_error("SQLite: receipts exist without a snapshot; failing closed");
            return false;
        }
        generation = static_cast<std::uint64_t>(sqlite3_column_int64(q.get(), 0));
        if (generation == 0 || sqlite3_column_int(q.get(), 1) != 1)
            throw std::runtime_error("SQLite: unsupported snapshot generation or format");
        const auto* data = static_cast<const char*>(sqlite3_column_blob(q.get(), 2));
        const int size = sqlite3_column_bytes(q.get(), 2);
        if (size <= 0 || !data || size > 16 * 1024 * 1024)
            throw std::runtime_error("SQLite: corrupt current snapshot body; refusing rollback");
        const std::string bytes(data, static_cast<std::size_t>(size));
        const auto expected_hash = parse_u64(sqlite3_column_text(q.get(), 3));
        const auto expected_ore = sqlite3_column_int64(q.get(), 4);
        auto restored = Simulation::from_snapshot(bytes);
        if (restored.state_hash() != expected_hash || restored.total_ore() != expected_ore ||
            restored.total_credits() != sqlite3_column_int64(q.get(), 5))
            throw std::runtime_error("SQLite: corrupt current snapshot checksum/ore; failing closed");
        Statement meta(db_, "SELECT resource_origin,credit_origin FROM world_meta WHERE singleton=1");
        if (!meta.row()) throw std::runtime_error("SQLite: missing world origin");
        origin = sqlite3_column_int64(meta.get(), 0);
        credit_origin = sqlite3_column_int64(meta.get(),1);
        if (origin != expected_ore || credit_origin != restored.total_credits())
            throw std::runtime_error("SQLite: resource/credit conservation failure");
        Statement receipts(db_, "SELECT id,execution_tick,result,transferred FROM receipts");
        std::map<std::uint64_t, Event> event_by_id;
        for (const auto& ev : restored.events()) event_by_id.emplace(ev.command_id, ev);
        std::size_t receipt_count = 0;
        while (receipts.row()) {
            ++receipt_count;
            const auto* raw_id = sqlite3_column_text(receipts.get(), 0);
            const auto id = parse_u64(raw_id);
            if (std::to_string(id) != reinterpret_cast<const char*>(raw_id) || !restored.has_command_id(id))
                throw std::runtime_error("SQLite: receipt ID not in snapshot; failing closed");
            const auto event = event_by_id.find(id);
            if (event == event_by_id.end()) {
                if (sqlite3_column_type(receipts.get(), 1) != SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 2) != SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 3) != SQLITE_NULL)
                    throw std::runtime_error("SQLite: pending receipt incorrectly marked executed");
            } else {
                if (sqlite3_column_type(receipts.get(), 1) == SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 2) == SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 3) == SQLITE_NULL ||
                    sqlite3_column_int64(receipts.get(), 1) != static_cast<sqlite3_int64>(event->second.tick) ||
                    sqlite3_column_int(receipts.get(), 2) != static_cast<int>(event->second.result) ||
                    sqlite3_column_int64(receipts.get(), 3) != event->second.transferred)
                    throw std::runtime_error("SQLite: execution receipt contradicts snapshot");
            }
        }
        if (receipt_count != restored.accepted_command_count())
            throw std::runtime_error("SQLite: receipt count mismatch; failing closed");
        // Index the command journal once. Source/recipient checks for hangar
        // transfers must use the authenticated issuer and destination station,
        // not merely an amount that happens to conserve ore.
        std::map<std::uint64_t, Command> journal_commands;
        {
            Statement journal(db_, "SELECT command FROM operation_log WHERE kind=1 ORDER BY generation");
            while (journal.row()) {
                const auto* raw = sqlite3_column_text(journal.get(), 0);
                if (!raw) throw std::runtime_error("SQLite: missing command journal entry");
                const auto c = decode_command(reinterpret_cast<const char*>(raw));
                if (!journal_commands.emplace(c.id, c).second || !restored.has_command_id(c.id))
                    throw std::runtime_error("SQLite: duplicate/unknown command in operation journal");
            }
        }
        // GRSM-01 facts must agree with the independent transaction journal,
        // executed receipts and snapshot. A missing record is a hard error,
        // not a story the world is allowed to silently forget.
        std::uint64_t history_start = 0;
        {
            Statement m(db_, "SELECT start_generation FROM historical_meta WHERE singleton=1");
            if (!m.row()) throw std::runtime_error("SQLite: missing history boundary");
            const auto n = sqlite3_column_int64(m.get(), 0);
            if (n < 0 || static_cast<std::uint64_t>(n) > generation || m.row())
                throw std::runtime_error("SQLite: invalid historical migration boundary");
            history_start = static_cast<std::uint64_t>(n);
        }
        std::map<std::uint64_t, std::uint64_t> expected_event_generation;
        {
            Statement steps(db_, "SELECT generation,tick FROM operation_log WHERE kind=2 AND generation>? ORDER BY generation");
            steps.i64(1, static_cast<std::int64_t>(history_start));
            std::map<std::uint64_t,std::uint64_t> tick_generation;
            while (steps.row()) {
                const auto g = sqlite3_column_int64(steps.get(), 0);
                const auto tick = sqlite3_column_int64(steps.get(), 1);
                if (g <= 0 || tick <= 0 || !tick_generation.emplace(static_cast<std::uint64_t>(tick),
                    static_cast<std::uint64_t>(g)).second)
                    throw std::runtime_error("SQLite: duplicate historical step generation");
            }
            for (const auto& [id,ev] : event_by_id) {
                const auto it=tick_generation.find(ev.tick);
                if (it != tick_generation.end()) expected_event_generation.emplace(id,it->second);
            }
        }
        std::size_t historical_rows=0;
        {
            Statement history(db_, "SELECT event_id,command_id,generation,tick,actor_id,target_id,action,"
                         "outcome,transferred,command,digest FROM historical_events ORDER BY generation,command_id");
            while(history.row()) {
                ++historical_rows;
                const auto read_text=[&](int n)->std::string {
                    if (sqlite3_column_type(history.get(), n)!=SQLITE_TEXT)
                        throw std::runtime_error("SQLite: nontext historical field");
                    const auto raw=sqlite3_column_text(history.get(),n);
                    if(!raw) throw std::runtime_error("SQLite: missing historical field");
                    return reinterpret_cast<const char*>(raw);
                };
                const auto id=parse_u64(sqlite3_column_text(history.get(),1));
                const auto g=sqlite3_column_int64(history.get(),2);
                const auto tick=sqlite3_column_int64(history.get(),3);
                const auto actor=parse_u64(sqlite3_column_text(history.get(),4));
                const auto target=parse_u64(sqlite3_column_text(history.get(),5));
                const auto action=sqlite3_column_int(history.get(),6);
                const auto outcome=sqlite3_column_int(history.get(),7);
                const auto transferred=sqlite3_column_int64(history.get(),8);
                const auto serialized=read_text(9);
                const auto entry=expected_event_generation.find(id);
                const auto ev=event_by_id.find(id);
                const auto cmd=journal_commands.find(id);
                if (entry==expected_event_generation.end() || ev==event_by_id.end() ||
                    cmd==journal_commands.end() || g<=0 || static_cast<std::uint64_t>(g)!=entry->second ||
                    tick<0 || static_cast<std::uint64_t>(tick)!=ev->second.tick ||
                    actor!=cmd->second.actor || target!=cmd->second.target ||
                    action!=static_cast<int>(cmd->second.action) ||
                    outcome!=static_cast<int>(ev->second.result) ||
                    transferred!=ev->second.transferred ||
                    read_text(0)!="command:"+std::to_string(id) ||
                    serialized!=command_record(cmd->second) ||
                    read_text(10)!=historical_digest(read_text(0),static_cast<std::uint64_t>(g),ev->second,serialized))
                    throw std::runtime_error("SQLite: historical fact contradicts world/journal");
            }
        }
        if (historical_rows!=expected_event_generation.size())
            throw std::runtime_error("SQLite: missing committed historical facts");
        // The ledger is a second durable accounting record, not a second source
        // of authority. Detect discrepancies before exposing a restored world.
        Statement ledger(db_, "SELECT command_id,tick,amount,reason,source_id,destination_id FROM resource_ledger ORDER BY id");
        bool saw_genesis = false;
        std::size_t transfers = 0;
        std::size_t hangar_transfers = 0;
        std::size_t load_transfers = 0;
        std::size_t sale_transfers = 0;
        std::size_t refine_transfers=0, fabricate_transfers=0, build_transfers=0;
        std::size_t mineral_refine_transfers=0,material_fabricate_transfers=0;
        std::size_t life_transfers=0, farm_builds=0, farm_stock_transfers=0, habitat_builds=0, habitat_transfers=0,utility_builds=0,utility_repairs=0,utility_spare_transfers=0,community_builds=0,community_transfers=0,clinic_builds=0,clinic_transfers=0,reclaimer_builds=0,waterworks_builds=0,waterworks_transfers=0;
        std::size_t workshop_builds=0,workshop_feeds=0,workshop_outputs=0,workshop_finisher_builds=0,workshop_final_outputs=0;
        std::size_t auto_transfers = 0;
        std::vector<ActivityEvent> recorded_auto;
        for (const auto& a : restored.activity_events()) {
            if (a.kind == ActivityKind::AutoMiningTransfer) recorded_auto.push_back(a);
        }
        while (ledger.row()) {
            const auto reason_raw = sqlite3_column_text(ledger.get(), 3);
            if (!reason_raw) throw std::runtime_error("SQLite: null ledger reason");
            const std::string reason(reinterpret_cast<const char*>(reason_raw));
            const auto amount = sqlite3_column_int64(ledger.get(), 2);
            if (reason == "initial_world_allocation") {
                if (saw_genesis || sqlite3_column_type(ledger.get(), 0) != SQLITE_NULL ||
                    amount != origin || sqlite3_column_int64(ledger.get(), 1) != 0)
                    throw std::runtime_error("SQLite: malformed genesis ledger entry");
                saw_genesis = true;
            } else if (reason == "mining_transfer") {
                ++transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(), 0));
                const auto it = event_by_id.find(id);
                if (it == event_by_id.end() || it->second.result != Outcome::Applied ||
                    it->second.transferred <= 0 || it->second.transferred != amount ||
                    it->second.tick != static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(), 1)))
                    throw std::runtime_error("SQLite: ledger transfer contradicts executed event");
                const auto cmd = journal_commands.find(id);
                if (cmd != journal_commands.end() &&
                    (cmd->second.action != Action::Mine ||
                     std::to_string(cmd->second.target) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 4)) ||
                     std::to_string(cmd->second.actor) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 5))))
                    throw std::runtime_error("SQLite: mining ledger source or destination mismatch");
            } else if (reason == "hangar_transfer") {
                ++hangar_transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(), 0));
                const auto it = event_by_id.find(id);
                const auto cmd = journal_commands.find(id);
                if (it == event_by_id.end() || it->second.result != Outcome::Applied ||
                    it->second.transferred <= 0 || it->second.transferred != amount ||
                    it->second.tick != static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(), 1)) ||
                    sqlite3_column_type(ledger.get(), 4) != SQLITE_TEXT ||
                    sqlite3_column_type(ledger.get(), 5) != SQLITE_TEXT ||
                    cmd == journal_commands.end() || cmd->second.action != Action::UnloadOre ||
                    std::to_string(cmd->second.actor) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 4)) ||
                    ("hangar:" + std::to_string(cmd->second.target) + ":owner:" +
                        std::to_string(cmd->second.issuer)) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 5)))
                    throw std::runtime_error("SQLite: hangar ledger source/destination or receipt mismatch");
            } else if (reason == "load_transfer" || reason == "sale_transfer") {
                if (reason == "load_transfer") ++load_transfers;
                else ++sale_transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto it=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(it==event_by_id.end() || cmd==journal_commands.end() ||
                   it->second.result!=Outcome::Applied || it->second.transferred<=0 ||
                   it->second.transferred!=amount ||
                   it->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   cmd->second.action!=(reason=="load_transfer" ? Action::LoadOre : Action::SellOre))
                    throw std::runtime_error("SQLite: logistic transfer receipt mismatch");
                const auto hangar_id="hangar:"+std::to_string(cmd->second.target)+":owner:"+std::to_string(cmd->second.issuer);
                const auto market_id="market:"+std::to_string(cmd->second.target);
                const auto src=reason=="load_transfer" ? hangar_id : hangar_id;
                const auto dst=reason=="load_transfer" ? std::to_string(cmd->second.actor) : market_id;
                if(sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   src!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dst!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: logistic transfer destination mismatch");
            } else if(reason=="refining_transfer" || reason=="fabrication_transfer" ||
                      reason=="construction_embodiment"){
                const auto action=reason=="refining_transfer" ? Action::RefineOre :
                                  reason=="fabrication_transfer" ? Action::FabricateModule :
                                  Action::ConstructOutpost;
                if(action==Action::RefineOre)++refine_transfers;
                else if(action==Action::FabricateModule)++fabricate_transfers;
                else ++build_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=action || ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=amount || amount<=0 ||
                   (action==Action::RefineOre && amount!=cmd->second.amount) ||
                   (action==Action::FabricateModule && amount!=2*cmd->second.amount) ||
                   (action==Action::ConstructOutpost && amount!=8) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   std::to_string(cmd->second.actor)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   (reason=="refining_transfer" ? "refined:" :
                    reason=="fabrication_transfer" ? "modules:" : "outpost:")+std::to_string(cmd->second.actor)!=
                      reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: colony transformation ledger mismatch");
            } else if (reason=="mineral_refining_kg" || reason=="material_fabrication_t"){
                const bool refine=reason=="mineral_refining_kg";
                if(refine)++mineral_refine_transfers;else ++material_fabricate_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(refine?Action::RefineMineral:Action::FabricateMaterialModule) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount || amount<=0 ||
                   amount!=(refine?cmd->second.amount:cmd->second.amount*2) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: mineral/material transformation receipt mismatch");
                const auto src=(refine?"mineral:":"good:")+std::to_string(cmd->second.actor)+":"+std::to_string(cmd->second.target);
                const auto dst=(refine?"goods:":"modules:")+std::to_string(cmd->second.actor);
                if(src!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dst!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: mineral/material ledger endpoints mismatch");
            } else if (reason=="life_support_transform_kg") {
                ++life_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   !is_life_action(cmd->second.action) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount<=0 || amount!=life_transfer_amount(cmd->second) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   life_ledger_source(cmd->second)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   life_ledger_destination(cmd->second)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: life-support operation ledger mismatch");
            } else if(reason=="farm_construction_t" || reason=="farm_stock_transfer_kg"){
                const bool build=reason=="farm_construction_t";
                if(build)++farm_builds;else ++farm_stock_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructFarm:Action::TransferFarmStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: farm resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"farm:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"farm:":cmd->second.amount>0?"farm:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: farm resource ledger endpoints mismatch");
            } else if (reason=="habitat_construction_t" || reason=="habitat_stock_transfer_kg"){
                const bool build=reason=="habitat_construction_t";
                if(build)++habitat_builds;else ++habitat_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=(build?Action::ConstructHabitat:Action::TransferHabitatStock)||
                   ev->second.result!=Outcome::Applied||ev->second.transferred!=amount||
                   amount!=(build?4:std::abs(cmd->second.amount))||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: habitat resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"habitat:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"habitat:":cmd->second.amount>0?"habitat:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4))||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: habitat ledger endpoints mismatch");
            } else if(reason=="utility_construction_t" || reason=="utility_repair_t"){
                const bool build=reason=="utility_construction_t";
                if(build)++utility_builds;else ++utility_repairs;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=(build?Action::ConstructUtility:Action::RepairUtility)||
                   ev->second.result!=Outcome::Applied||ev->second.transferred!=amount||
                   amount!=(build?4:2)||cmd->second.amount!=1||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: utility operation receipt mismatch");
                const auto source="modules:"+std::to_string(cmd->second.actor);
                const auto dest="utility:"+std::to_string(cmd->second.target);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4))||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: utility resource ledger endpoints mismatch");
            } else if(reason=="utility_spare_transfer_t"){
                ++utility_spare_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::TransferUtilitySpares ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   amount!=2*std::abs(cmd->second.amount) || ev->second.transferred!=amount ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: utility spare receipt mismatch");
                const auto source=(cmd->second.amount>0?"modules:":"utility-spares:")+
                    std::to_string(cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(cmd->second.amount>0?"utility-spares:":"modules:")+
                    std::to_string(cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: utility spare endpoints mismatch");
            } else if(reason=="community_construction_t" || reason=="community_stock_transfer_kg"){
                const bool build=reason=="community_construction_t";
                if(build)++community_builds;else ++community_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructCommunity:Action::TransferCommunityStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: community resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"community:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"community:":cmd->second.amount>0?"community:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: community ledger endpoints mismatch");
            } else if(reason=="clinic_construction_t" || reason=="clinic_stock_transfer_kg"){
                const bool build=reason=="clinic_construction_t";
                if(build)++clinic_builds;else ++clinic_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructClinic:Action::TransferClinicStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: clinic resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"clinic:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"clinic:":cmd->second.amount>0?"clinic:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: clinic ledger endpoints mismatch");
            } else if(reason=="waterworks_construction_t" || reason=="waterworks_feed_transfer_kg"){
                const bool build=reason=="waterworks_construction_t";
                if(build)++waterworks_builds;else ++waterworks_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructWaterworks:Action::TransferWaterworksFeed) ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: waterworks receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"good-water:":"waterworks-feed:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"waterworks:":cmd->second.amount>0?"waterworks-feed:":"good-water:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: waterworks receipt endpoints mismatch");
            } else if(reason=="workshop_construction_t" ||
                      reason=="workshop_feed_transfer_kg" ||
                      reason=="workshop_output_transfer_kg"){
                const bool build=reason=="workshop_construction_t";
                const bool feed=reason=="workshop_feed_transfer_kg";
                if(build)++workshop_builds;else if(feed)++workshop_feeds;else ++workshop_outputs;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructWorkshop:
                     feed?Action::TransferWorkshopFeed:Action::TransferWorkshopOutput) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?2:feed?std::abs(cmd->second.amount):20*cmd->second.amount) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: workshop resource receipt mismatch");
                const auto& c=cmd->second;
                const auto source=build?"modules:"+std::to_string(c.actor):
                    feed?(c.amount>0?"good-aluminum:"+std::to_string(c.actor):
                        "workshop-feed:"+std::to_string(c.target)):
                        "workshop-output:"+std::to_string(c.target);
                const auto dest=build?"workshop:"+std::to_string(c.target):
                    feed?(c.amount>0?"workshop-feed:"+std::to_string(c.target):
                        "good-aluminum:"+std::to_string(c.actor)):
                        "goods:"+std::to_string(c.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: workshop endpoints mismatch");
            } else if(reason=="workshop_finisher_construction_t" ||
                      reason=="workshop_final_output_transfer_kg"){
                const bool build=reason=="workshop_finisher_construction_t";
                if(build)++workshop_finisher_builds;else ++workshop_final_outputs;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructWorkshopFinisher:
                      Action::TransferWorkshopFinalOutput) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?2:20*cmd->second.amount) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: workshop finisher receipt mismatch");
                const auto& c=cmd->second;
                const auto source=build?"modules:"+std::to_string(c.actor):
                    "workshop-final:"+std::to_string(c.target);
                const auto dest=build?"workshop-finisher:"+std::to_string(c.target):
                    "goods:"+std::to_string(c.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: workshop finisher endpoints mismatch");
            } else if(reason=="reclaimer_construction_t"){
                ++reclaimer_builds;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::ConstructReclaimer ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=4 ||
                   amount!=4 ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   std::string(reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)))!=
                     "modules:"+std::to_string(cmd->second.actor) ||
                   std::string(reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))!=
                     "reclaimer:"+std::to_string(cmd->second.target))
                    throw std::runtime_error("SQLite: reclaimer resource receipt mismatch");
            } else if (reason == "automated_mining_transfer") {
                if (auto_transfers >= recorded_auto.size() ||
                    sqlite3_column_type(ledger.get(), 0) != SQLITE_NULL)
                    throw std::runtime_error("SQLite: extra or noncanonical auto transfer");
                const auto& ev = recorded_auto[auto_transfers++];
                if (parse_u64(sqlite3_column_text(ledger.get(), 4)) != ev.target ||
                    parse_u64(sqlite3_column_text(ledger.get(), 5)) != ev.actor ||
                    amount != ev.transferred ||
                    sqlite3_column_int64(ledger.get(), 1) != static_cast<sqlite3_int64>(ev.tick))
                    throw std::runtime_error("SQLite: auto mining amount/tick mismatch");
            } else throw std::runtime_error("SQLite: unrecognized ledger reason");
        }
        if (auto_transfers != recorded_auto.size())
            throw std::runtime_error("SQLite: missing auto mining resource ledger rows");
        std::size_t expected_transfers = 0;
        std::size_t expected_hangar_transfers = 0;
        std::size_t expected_load_transfers = 0;
        std::size_t expected_sale_transfers = 0;
        std::size_t expected_refine=0,expected_fabricate=0,expected_build=0;
        std::size_t expected_mineral_refine=0,expected_material_fabricate=0;
        std::size_t expected_life_transfers=0,expected_farm_builds=0,expected_farm_stock_transfers=0;
        std::size_t expected_habitat_builds=0, expected_habitat_transfers=0;
        std::size_t expected_utility_builds=0,expected_utility_repairs=0,expected_utility_spares=0,expected_community_builds=0,expected_community_transfers=0,expected_clinic_builds=0,expected_clinic_transfers=0,expected_reclaimer_builds=0,expected_waterworks_builds=0,expected_waterworks_transfers=0,expected_workshop_builds=0,expected_workshop_feeds=0,expected_workshop_outputs=0,expected_workshop_finisher_builds=0,expected_workshop_final_outputs=0;

        for (const auto& ev : restored.events()) {
            if (ev.result != Outcome::Applied || ev.transferred <= 0) continue;
            const auto cmd = journal_commands.find(ev.command_id);
            if (cmd == journal_commands.end()) {
                // Pre-v2 snapshots can have valid mining receipts predating the
                // operation journal. No hangar transfer existed before B5.
                ++expected_transfers;
            } else if (cmd->second.action == Action::Mine) ++expected_transfers;
            else if (cmd->second.action == Action::UnloadOre) ++expected_hangar_transfers;
            else if (cmd->second.action == Action::LoadOre) ++expected_load_transfers;
            else if (cmd->second.action == Action::SellOre) ++expected_sale_transfers;
            else if (cmd->second.action == Action::RefineOre) ++expected_refine;
            else if (cmd->second.action == Action::FabricateModule) ++expected_fabricate;
            else if (cmd->second.action == Action::ConstructOutpost) ++expected_build;
            else if (cmd->second.action == Action::RefineMineral) ++expected_mineral_refine;
            else if (cmd->second.action == Action::FabricateMaterialModule) ++expected_material_fabricate;
            else if (is_life_action(cmd->second.action)) ++expected_life_transfers;
            else if (cmd->second.action==Action::ConstructFarm) ++expected_farm_builds;
            else if (cmd->second.action==Action::TransferFarmStock) ++expected_farm_stock_transfers;
            else if (cmd->second.action==Action::ConstructHabitat) ++expected_habitat_builds;
            else if (cmd->second.action==Action::TransferHabitatStock) ++expected_habitat_transfers;
            else if (cmd->second.action==Action::ConstructUtility) ++expected_utility_builds;
            else if (cmd->second.action==Action::RepairUtility) ++expected_utility_repairs;
            else if (cmd->second.action==Action::TransferUtilitySpares) ++expected_utility_spares;
            else if (cmd->second.action==Action::ConstructCommunity) ++expected_community_builds;
            else if (cmd->second.action==Action::TransferCommunityStock) ++expected_community_transfers;
            else if (cmd->second.action==Action::ConstructReclaimer) ++expected_reclaimer_builds;
            else if (cmd->second.action==Action::ConstructWaterworks) ++expected_waterworks_builds;
            else if (cmd->second.action==Action::TransferWaterworksFeed) ++expected_waterworks_transfers;
            else if (cmd->second.action==Action::ConstructWorkshop) ++expected_workshop_builds;
            else if (cmd->second.action==Action::TransferWorkshopFeed) ++expected_workshop_feeds;
            else if (cmd->second.action==Action::TransferWorkshopOutput) ++expected_workshop_outputs;
            else if (cmd->second.action==Action::ConstructWorkshopFinisher) ++expected_workshop_finisher_builds;
            else if (cmd->second.action==Action::TransferWorkshopFinalOutput) ++expected_workshop_final_outputs;
            else if (cmd->second.action==Action::ConstructClinic) ++expected_clinic_builds;
            else if (cmd->second.action==Action::TransferClinicStock) ++expected_clinic_transfers;
            else throw std::runtime_error("SQLite: resource transfer tied to non-transfer command");
        }
        if (!saw_genesis || transfers != expected_transfers || hangar_transfers != expected_hangar_transfers ||
            load_transfers != expected_load_transfers || sale_transfers != expected_sale_transfers ||
            refine_transfers!=expected_refine || fabricate_transfers!=expected_fabricate ||
            build_transfers!=expected_build ||
            mineral_refine_transfers!=expected_mineral_refine ||
            material_fabricate_transfers!=expected_material_fabricate ||
            life_transfers!=expected_life_transfers ||
            farm_builds!=expected_farm_builds ||
            farm_stock_transfers!=expected_farm_stock_transfers ||
            habitat_builds!=expected_habitat_builds ||
            habitat_transfers!=expected_habitat_transfers ||
            utility_builds!=expected_utility_builds || utility_repairs!=expected_utility_repairs ||
            utility_spare_transfers!=expected_utility_spares ||
            community_builds!=expected_community_builds || community_transfers!=expected_community_transfers ||
            clinic_builds!=expected_clinic_builds || clinic_transfers!=expected_clinic_transfers ||
            reclaimer_builds!=expected_reclaimer_builds ||
            waterworks_builds!=expected_waterworks_builds ||
            waterworks_transfers!=expected_waterworks_transfers ||
            workshop_builds!=expected_workshop_builds ||
            workshop_feeds!=expected_workshop_feeds ||
            workshop_outputs!=expected_workshop_outputs ||
            workshop_finisher_builds!=expected_workshop_finisher_builds ||
            workshop_final_outputs!=expected_workshop_final_outputs)
            throw std::runtime_error("SQLite: ledger event count mismatch");
        std::size_t credit_rows=0;
        Statement cash(db_, "SELECT command_id,tick,station_id,owner_id,ore_units,unit_price,credit_amount FROM credit_ledger");
        while (cash.row()) {
            ++credit_rows;
            const auto id=parse_u64(sqlite3_column_text(cash.get(),0));
            const auto cmd=journal_commands.find(id); const auto it=event_by_id.find(id);
            if (cmd==journal_commands.end() || cmd->second.action!=Action::SellOre ||
                it==event_by_id.end() || it->second.result!=Outcome::Applied ||
                it->second.transferred!=sqlite3_column_int64(cash.get(),4) ||
                it->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(cash.get(),1)) ||
                std::to_string(cmd->second.target)!=reinterpret_cast<const char*>(sqlite3_column_text(cash.get(),2)) ||
                std::to_string(cmd->second.issuer)!=reinterpret_cast<const char*>(sqlite3_column_text(cash.get(),3)))
                throw std::runtime_error("SQLite: sale credit journal contradiction");
            const auto market=restored.market(cmd->second.target);
            const auto unit=sqlite3_column_int64(cash.get(),5);
            const auto amount=sqlite3_column_int64(cash.get(),6);
            if (!market || unit!=market->price_per_ore || unit<=0 ||
                it->second.transferred<=0 ||
                it->second.transferred>std::numeric_limits<std::int64_t>::max()/unit ||
                amount!=it->second.transferred*unit)
                throw std::runtime_error("SQLite: sale credit amount or price mismatch");
        }
        if (credit_rows!=expected_sale_transfers)
            throw std::runtime_error("SQLite: missing sale credit journal");
        // B6.2b: independently verify both sides of each inter-system transfer.
        std::size_t successful_jump_events=0,completed_jump_events=0,jump_rows=0;
        for (const auto& ev : restored.events()) {
            const auto cmd=journal_commands.find(ev.command_id);
            if(cmd!=journal_commands.end() && cmd->second.action==Action::RequestJump &&
               ev.result==Outcome::Applied) ++successful_jump_events;
        }
        for(const auto& a:restored.activity_events())if(a.kind==ActivityKind::JumpCompleted)
            ++completed_jump_events;
        Statement jumps(db_,"SELECT command_id,ship_id,gate_id,source_system,dest_system,"
                           "cargo_at_departure,accepted_tick,completed_tick FROM jump_ledger");
        std::size_t completed_rows=0;
        while(jumps.row()){
            ++jump_rows;
            const auto id=parse_u64(sqlite3_column_text(jumps.get(),0));
            const auto ship_id=parse_u64(sqlite3_column_text(jumps.get(),1));
            const auto gate_id=parse_u64(sqlite3_column_text(jumps.get(),2));
            const auto source=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),3));
            const auto destination=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),4));
            const auto cargo=sqlite3_column_int64(jumps.get(),5);
            const auto accepted_tick=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),6));
            const auto completed=sqlite3_column_type(jumps.get(),7)!=SQLITE_NULL;
            const auto completed_tick=completed?static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),7)):0;
            const auto cmd=journal_commands.find(id); const auto event=event_by_id.find(id);
            const auto* gate=restored.find(gate_id);
            const auto* ship=restored.find(ship_id);
            if(cmd==journal_commands.end() || cmd->second.action!=Action::RequestJump ||
               cmd->second.actor!=ship_id || cmd->second.target!=gate_id ||
               event==event_by_id.end() || event->second.result!=Outcome::Applied ||
               event->second.tick!=accepted_tick || event->second.transferred!=0 ||
               !gate || gate->type!=EntityType::Stargate || !ship ||
               (ship->type!=EntityType::Ship && (ship->type!=EntityType::Wreck || !completed)) ||
               gate->system_id!=source || gate->gate_destination!=destination ||
               cargo<0 || cargo>ship->cargo_capacity)
                throw std::runtime_error("SQLite: jump departure ledger inconsistent");
            if(completed){
                ++completed_rows;
                if(completed_tick!=accepted_tick+Simulation::kJumpTransitTicks)
                    throw std::runtime_error("SQLite: invalid jump completion time");
                const auto it=std::find_if(restored.activity_events().begin(),restored.activity_events().end(),
                    [&](const ActivityEvent& a){return a.kind==ActivityKind::JumpCompleted &&
                        a.tick==completed_tick && a.actor==ship_id && a.target==gate_id;});
                if(it==restored.activity_events().end())
                    throw std::runtime_error("SQLite: completed jump lacks arrival journal");
            }else if(ship->system_id!=source || ship->jump_state!=ShipJumpState::Jumping ||
                     ship->pending_gate!=gate_id || ship->cargo!=cargo)
                throw std::runtime_error("SQLite: uncompleted jump has no matching vessel");
        }
        if(jump_rows!=successful_jump_events || completed_rows!=completed_jump_events)
            throw std::runtime_error("SQLite: missing or duplicate jump journal entry");
        // GRSM-01b: compare every immutable incident after migration against
        // independent transactional resource/jump journals and activity facts.
        std::uint64_t physical_start=0;
        {
            Statement m(db_,"SELECT start_generation FROM physical_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("SQLite: missing physical boundary");
            const auto n=sqlite3_column_int64(m.get(),0);
            if(n<0 || static_cast<std::uint64_t>(n)>generation || m.row())
                throw std::runtime_error("SQLite: invalid physical boundary");
            physical_start=static_cast<std::uint64_t>(n);
        }
        std::map<std::string,PhysicalIncident> expected_physical;
        const auto expect=[&](const PhysicalIncident& p){
            if(p.generation<=physical_start || p.generation>generation || p.tick==0 ||
               !expected_physical.emplace(p.incident_id,p).second)
                throw std::runtime_error("SQLite: duplicate/invalid expected physical fact");
        };
        {
            Statement r(db_,"SELECT id,generation,tick,command_id,source_id,destination_id,amount,reason "
                            "FROM resource_ledger WHERE generation>? AND source_id IS NOT NULL ORDER BY id");
            r.i64(1,static_cast<std::int64_t>(physical_start));
            while(r.row()){
                PhysicalIncident e;
                e.incident_id="cargo:"+std::to_string(sqlite3_column_int64(r.get(),0));
                e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),1));
                e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),2));
                if(sqlite3_column_type(r.get(),3)!=SQLITE_NULL)e.command_id=parse_u64(sqlite3_column_text(r.get(),3));
                e.kind=PhysicalKind::CargoMovement;
                e.source=sqlite_str(r.get(),4);e.destination=sqlite_str(r.get(),5);
                e.amount=sqlite3_column_int64(r.get(),6);e.detail=sqlite_str(r.get(),7);
                if(e.amount<=0 || e.source.empty() || e.destination.empty())
                    throw std::runtime_error("SQLite: invalid physical cargo reference");
                expect(e);
            }
        }
        // Jumps retain exact ship ID, origin/destination, and departure cargo.
        // Completed journeys are separate facts, never overwritten start facts.
        {
            Statement j(db_,"SELECT j.command_id,j.ship_id,j.gate_id,j.source_system,j.dest_system,"
                "j.cargo_at_departure,j.accepted_tick,j.completed_tick,h.generation "
                "FROM jump_ledger j JOIN historical_events h ON h.command_id=j.command_id "
                "WHERE h.generation>? ORDER BY j.command_id");
            j.i64(1,static_cast<std::int64_t>(physical_start));
            while(j.row()){
                const auto id=parse_u64(sqlite3_column_text(j.get(),0));
                const auto ship=sqlite_str(j.get(),1),gate=sqlite_str(j.get(),2);
                const auto source=std::to_string(sqlite3_column_int64(j.get(),3));
                const auto dest=std::to_string(sqlite3_column_int64(j.get(),4));
                const auto cargo=sqlite3_column_int64(j.get(),5);
                const auto departed=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),6));
                const auto startgen=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),8));
                expect(PhysicalIncident{"journey:start:"+std::to_string(id),startgen,departed,id,
                    PhysicalKind::JourneyStarted,source,dest,cargo,ship+":"+gate});
                if(sqlite3_column_type(j.get(),7)!=SQLITE_NULL){
                    const auto arrival=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),7));
                    Statement step(db_,"SELECT generation FROM operation_log WHERE kind=2 AND tick=?");
                    step.i64(1,static_cast<std::int64_t>(arrival));
                    if(!step.row())throw std::runtime_error("SQLite: arrival missing committed tick");
                    const auto arrgen=static_cast<std::uint64_t>(sqlite3_column_int64(step.get(),0));
                    expect(PhysicalIncident{"journey:end:"+std::to_string(id),arrgen,arrival,id,
                        PhysicalKind::JourneyCompleted,source,dest,cargo,ship+":"+gate});
                }
            }
        }
        for(const auto& a:restored.activity_events()){
            if(a.kind!=ActivityKind::ShipDestroyed && a.kind!=ActivityKind::NpcLost)continue;
            const auto h=expected_event_generation.find(a.start_command_id);
            const auto cmd=journal_commands.find(a.start_command_id);
            if(h==expected_event_generation.end() || cmd==journal_commands.end() ||
               ((a.kind==ActivityKind::ShipDestroyed && cmd->second.action!=Action::HostDestroyShip) ||
                (a.kind==ActivityKind::NpcLost && cmd->second.action!=Action::HostCrewLoss)) ||
               a.tick!=event_by_id.at(a.start_command_id).tick ||
               event_by_id.at(a.start_command_id).result!=Outcome::Applied)
                throw std::runtime_error("SQLite: physical loss lacks executed host action");
            if(h->second<=physical_start)continue;
            expect(PhysicalIncident{(a.kind==ActivityKind::ShipDestroyed?"wreck:":"death:")+
                    std::to_string(a.start_command_id)+(a.kind==ActivityKind::NpcLost?":"+std::to_string(a.target):""),
                h->second,a.tick,a.start_command_id,
                a.kind==ActivityKind::ShipDestroyed?PhysicalKind::ShipDestroyed:PhysicalKind::NpcLost,
                std::to_string(a.actor),std::to_string(a.target),1,
                a.kind==ActivityKind::ShipDestroyed?"wreck_persists":"individual_identity_lost"});
        }
        {
            Statement facts(db_,"SELECT incident_id,generation,tick,command_id,kind,source,destination,amount,detail,digest "
                                "FROM physical_incidents");
            std::size_t count=0;
            while(facts.row()){
                ++count;
                PhysicalIncident e;
                e.incident_id=sqlite_str(facts.get(),0);
                const auto gen=sqlite3_column_int64(facts.get(),1),tick=sqlite3_column_int64(facts.get(),2);
                if(gen<=0 || tick<=0)throw std::runtime_error("SQLite: invalid physical timestamp");
                e.generation=static_cast<std::uint64_t>(gen);e.tick=static_cast<std::uint64_t>(tick);
                e.command_id=parse_u64(sqlite3_column_text(facts.get(),3));
                const auto kind=sqlite3_column_int(facts.get(),4);
                if(kind<1 || kind>5)throw std::runtime_error("SQLite: invalid physical kind");
                e.kind=static_cast<PhysicalKind>(kind);
                e.source=sqlite_str(facts.get(),5);e.destination=sqlite_str(facts.get(),6);
                e.amount=sqlite3_column_int64(facts.get(),7);e.detail=sqlite_str(facts.get(),8);
                const auto it=expected_physical.find(e.incident_id);
                if(it==expected_physical.end() || e.generation!=it->second.generation ||
                   e.tick!=it->second.tick || e.command_id!=it->second.command_id ||
                   e.kind!=it->second.kind || e.source!=it->second.source ||
                   e.destination!=it->second.destination || e.amount!=it->second.amount ||
                   e.detail!=it->second.detail || sqlite_str(facts.get(),9)!=physical_digest(e))
                    throw std::runtime_error("SQLite: physical incident contradicts authoritative source");
            }
            if(count!=expected_physical.size())
                throw std::runtime_error("SQLite: physical incident missing or duplicated");
        }
        // GRSM-02: verify the bounded-per-query *API* against a full ordered
        // integrity scan. Preserve contradictory reports, but reject missing
        // provenance, false source chains and changed records.
        {
            Statement m(db_,"SELECT start_generation,row_count,tip_digest FROM knowledge_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("SQLite: missing knowledge migration metadata");
            const auto start=sqlite3_column_int64(m.get(),0),count=sqlite3_column_int64(m.get(),1);
            const auto tip=sqlite_str(m.get(),2);
            if(start<0 || static_cast<std::uint64_t>(start)>generation || count<0 || m.row())
                throw std::runtime_error("SQLite: invalid knowledge migration metadata");
            Statement entries(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
                 "fidelity,valence,confidence,generation,tick,account,digest FROM npc_knowledge ORDER BY id");
            std::map<std::uint64_t,KnowledgeRecord> prior;
            std::uint64_t expected_id=0;
            std::string chain="origin";
            while(entries.row()){
                const auto e=read_knowledge(entries.get());
                if(e.id!=++expected_id || !valid_knowledge(e) || e.generation<=static_cast<std::uint64_t>(start) ||
                   e.generation>generation || e.tick>restored.tick())
                    throw std::runtime_error("SQLite: invalid knowledge record");
                Statement incident(db_,"SELECT generation,tick FROM physical_incidents WHERE incident_id=?");
                incident.text(1,e.incident_id);
                if(!incident.row() || sqlite3_column_int64(incident.get(),0)>static_cast<sqlite3_int64>(e.generation) ||
                   sqlite3_column_int64(incident.get(),1)>static_cast<sqlite3_int64>(e.tick))
                    throw std::runtime_error("SQLite: knowledge references unavailable incident");
                if(e.parent_id){
                    const auto p=prior.find(e.parent_id);
                    if(p==prior.end() || p->second.incident_id!=e.incident_id ||
                       p->second.recipient_npc!=e.speaker_npc ||
                       p->second.generation>e.generation || p->second.tick>e.tick)
                        throw std::runtime_error("SQLite: NPC claim lacks personal source");
                } else if(e.account!="local_proximity_observation") {
                    throw std::runtime_error("SQLite: ungrounded direct observation");
                }
                chain=knowledge_digest(chain,e);
                if(sqlite_str(entries.get(),11)!=chain)
                    throw std::runtime_error("SQLite: knowledge chain mismatch");
                prior.emplace(e.id,e);
            }
            if(expected_id!=static_cast<std::uint64_t>(count) || chain!=tip)
                throw std::runtime_error("SQLite: missing or changed knowledge entries");
        }
        // GRSM-03: validate social history independently of physical facts.
        // A group records its REAL founders at a historical snapshot; its
        // relays record contact between two members, not omniscient news.
        {
            Statement meta(db_,"SELECT groups_count,groups_tip,relays_count,relays_tip,"
                               "views_count,views_tip FROM social_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing social history metadata");
            const auto gc=sqlite3_column_int64(meta.get(),0),rc=sqlite3_column_int64(meta.get(),2),
                       vc=sqlite3_column_int64(meta.get(),4);
            if(gc<0||rc<0||vc<0)throw std::runtime_error("SQLite: social history negative count");
            const auto gt=sqlite_str(meta.get(),1),rt=sqlite_str(meta.get(),3),
                       vt=sqlite_str(meta.get(),5);
            if(meta.row())throw std::runtime_error("SQLite: extra social metadata");
            std::map<std::uint64_t,SocialGroup> groups;
            std::map<std::uint64_t,std::pair<std::uint64_t,KnowledgeRecord>> relays;
            std::map<std::uint64_t,Simulation> snapshots;
            // Only three recent full snapshots are retained by the existing
            // checkpoint policy. Older contacts are proven by durable append-
            // only host receipts and chained checksums, not by retained world
            // snapshots. Never reject a legitimate aged conversation merely
            // because its historical full snapshot was compacted away.
            auto historic_world=[&](std::uint64_t g)->const Simulation*{
                auto it=snapshots.find(g);
                if(it==snapshots.end()){
                    Statement snap(db_,"SELECT state FROM snapshots WHERE generation=?");
                    snap.i64(1,static_cast<sqlite3_int64>(g));
                    if(!snap.row())return nullptr;
                    const auto bytes=sqlite3_column_bytes(snap.get(),0);
                    const auto* data=static_cast<const char*>(sqlite3_column_blob(snap.get(),0));
                    if(bytes<=0||bytes>16*1024*1024||!data)
                        throw std::runtime_error("SQLite: social history malformed checkpoint");
                    auto world=Simulation::from_snapshot(std::string(data,static_cast<std::size_t>(bytes)));
                    it=snapshots.emplace(g,std::move(world)).first;
                }
                return &it->second;
            };
            std::uint64_t group_count=0,relay_count=0,view_count=0;
            std::string group_tip="origin",relay_tip="origin",view_tip="origin";
            Statement group(db_,"SELECT id,name,generation,members,digest FROM social_groups ORDER BY id");
            while(group.row()){
                const auto r=read_group(group.get());
                if(r.id!=++group_count||r.generation>generation||r.name.empty()||
                   r.name.size()>64||r.name.find('\0')!=std::string::npos)
                    throw std::runtime_error("SQLite: invalid social group");
                if(const auto* past=historic_world(r.generation)){
                    EntityId home=0,location=0;
                    for(const auto member:r.members){
                        const auto it=past->npc_records().find(member);
                        if(it==past->npc_records().end())
                            throw std::runtime_error("SQLite: group invented an NPC");
                        if(!home){home=it->second.home_ship;location=it->second.location;}
                        if(it->second.home_ship!=home||it->second.location!=location)
                            throw std::runtime_error("SQLite: group membership impossible at founding");
                    }
                }
                group_tip=social_digest(group_tip,group_body(r));
                if(sqlite_str(group.get(),4)!=group_tip)
                    throw std::runtime_error("SQLite: group history digest mismatch");
                groups.emplace(r.id,r);
            }
            if(group_count!=static_cast<std::uint64_t>(gc)||group_tip!=gt)
                throw std::runtime_error("SQLite: group history count or tip mismatch");
            // Rebuild the group member states from the founder list and the
            // complete append-only membership stream BEFORE validating relays.
            std::map<std::uint64_t,std::vector<std::uint64_t>> current_members;
            std::map<std::uint64_t,std::uint64_t> prior_event,epochs;
            for(const auto& entry:groups)current_members.emplace(entry.first,entry.second.members);
            std::uint64_t member_seq=0;
            std::string member_tip="origin";
            Statement member_meta(db_,"SELECT event_count,tip FROM membership_meta WHERE singleton=1");
            if(!member_meta.row())throw std::runtime_error("SQLite: missing membership metadata");
            const auto member_count=sqlite3_column_int64(member_meta.get(),0);
            if(member_count<0)throw std::runtime_error("SQLite: negative membership count");
            const auto expected_member_tip=sqlite_str(member_meta.get(),1);
            if(member_meta.row())throw std::runtime_error("SQLite: duplicate membership metadata");
            std::map<std::uint64_t,std::vector<std::uint64_t>> members_by_seq;
            // Every membership event is verified in order. Relay epochs below
            // refer to this globally monotonic sequence, not wall-clock time.
            Statement member(db_,"SELECT id,group_id,npc_id,sponsor_id,kind,epoch,prior_id,"
                "generation,tick,digest FROM group_membership_events ORDER BY id");
            std::vector<GroupMembershipEvent> member_events;
            while(member.row()){
                const auto e=read_membership(member.get());
                const auto group=groups.find(e.group_id);
                if(e.id!=++member_seq||group==groups.end()||e.npc_id==0||
                   e.generation<group->second.generation||e.generation>generation||
                   (e.kind!=GroupMembershipKind::Join&&e.kind!=GroupMembershipKind::Leave)||
                   e.epoch!=epochs[e.group_id]+1||e.prior_id!=prior_event[e.group_id])
                    throw std::runtime_error("SQLite: invalid group membership epoch");
                if(!member_events.empty() && (e.generation<member_events.back().generation||
                   e.tick<member_events.back().tick))
                    throw std::runtime_error("SQLite: membership time reversed");
                auto& active=current_members[e.group_id];
                const auto found=std::lower_bound(active.begin(),active.end(),e.npc_id);
                if(e.kind==GroupMembershipKind::Join){
                    if((found!=active.end()&&*found==e.npc_id)||!e.sponsor_npc||
                       e.sponsor_npc==e.npc_id||active.size()>=24||
                       !std::binary_search(active.begin(),active.end(),e.sponsor_npc))
                        throw std::runtime_error("SQLite: invalid membership sponsor");
                    if(const auto* past=historic_world(e.generation)){
                        const auto a=past->npc_records().find(e.npc_id),
                                   b=past->npc_records().find(e.sponsor_npc);
                        if(a==past->npc_records().end()||b==past->npc_records().end()||
                           a->second.location!=b->second.location)
                            throw std::runtime_error("SQLite: membership join without contact");
                    }
                    active.insert(found,e.npc_id);
                }else{
                    if(e.sponsor_npc||found==active.end()||*found!=e.npc_id)
                        throw std::runtime_error("SQLite: invalid membership exit");
                    active.erase(found);
                }
                member_tip=social_digest(member_tip,membership_body(e));
                if(sqlite_str(member.get(),9)!=member_tip)
                    throw std::runtime_error("SQLite: membership chain damaged");
                epochs[e.group_id]=e.epoch;prior_event[e.group_id]=e.id;
                member_events.push_back(e);
            }
            if(member_seq!=static_cast<std::uint64_t>(member_count)||member_tip!=expected_member_tip)
                throw std::runtime_error("SQLite: membership history missing or changed");
            auto members_at=[&](std::uint64_t gid,std::uint64_t upto){
                auto active=groups.at(gid).members;
                for(const auto& e:member_events){
                    if(e.id>upto)break;
                    if(e.group_id!=gid)continue;
                    const auto pos=std::lower_bound(active.begin(),active.end(),e.npc_id);
                    if(e.kind==GroupMembershipKind::Join)active.insert(pos,e.npc_id);
                    else active.erase(pos);
                }
                return active;
            };
            Statement relay(db_,"SELECT id,group_id,knowledge_id,digest,membership_seq FROM group_relays ORDER BY id");
            while(relay.row()){
                const auto id=sqlite3_column_int64(relay.get(),0),
                           gid=sqlite3_column_int64(relay.get(),1),
                           kid=sqlite3_column_int64(relay.get(),2);
                if(id!=static_cast<sqlite3_int64>(++relay_count)||gid<=0||kid<=0||
                   !groups.count(static_cast<std::uint64_t>(gid)))
                    throw std::runtime_error("SQLite: invalid group relay");
                Statement claim(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
                    "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge WHERE id=?");
                claim.i64(1,kid);
                if(!claim.row())throw std::runtime_error("SQLite: group relay has no actual claim");
                const auto k=read_knowledge(claim.get());
                const auto seq=sqlite3_column_int64(relay.get(),4);
                if(seq<0||static_cast<std::uint64_t>(seq)>member_seq)
                    throw std::runtime_error("SQLite: group relay references impossible membership epoch");
                const auto group_members=members_at(static_cast<std::uint64_t>(gid),static_cast<std::uint64_t>(seq));
                if(seq && (member_events[static_cast<std::size_t>(seq-1)].generation>k.generation||
                    member_events[static_cast<std::size_t>(seq-1)].tick>k.tick))
                    throw std::runtime_error("SQLite: group relay predates membership receipt");
                if(!k.parent_id||k.generation<groups.at(static_cast<std::uint64_t>(gid)).generation||
                   !std::binary_search(group_members.begin(),group_members.end(),k.speaker_npc)||
                   !std::binary_search(group_members.begin(),group_members.end(),k.recipient_npc))
                    throw std::runtime_error("SQLite: group relay was not member-to-member");
                if(const auto* past=historic_world(k.generation)){
                    const auto a=past->npc_records().find(k.speaker_npc),
                               b=past->npc_records().find(k.recipient_npc);
                    if(a==past->npc_records().end()||b==past->npc_records().end()||
                       a->second.location!=b->second.location)
                        throw std::runtime_error("SQLite: group relay had no real physical contact");
                }
                relay_tip=social_digest(relay_tip,std::to_string(id)+"|"+
                    std::to_string(gid)+"|"+std::to_string(kid)+
                    (seq?"|epoch:"+std::to_string(seq):""));
                if(sqlite_str(relay.get(),3)!=relay_tip)
                    throw std::runtime_error("SQLite: group relay digest mismatch");
                if(!relays.emplace(static_cast<std::uint64_t>(kid),
                        std::make_pair(static_cast<std::uint64_t>(gid),k)).second)
                    throw std::runtime_error("SQLite: group relay duplicated knowledge");
            }
            if(relay_count!=static_cast<std::uint64_t>(rc)||relay_tip!=rt)
                throw std::runtime_error("SQLite: group relays missing or changed");
            Statement view(db_,"SELECT id,group_id,knowledge_id,stance,confidence,account,"
                               "generation,tick,digest FROM group_views ORDER BY id");
            while(view.row()){
                const auto r=read_view(view.get());
                const auto k=relays.find(r.knowledge_id);
                if(r.id!=++view_count||k==relays.end()||k->second.first!=r.group_id||
                   r.generation!=k->second.second.generation ||
                   r.tick!=k->second.second.tick||r.generation>generation||
                   static_cast<int>(r.stance)<1||static_cast<int>(r.stance)>3||
                   r.confidence<0||r.confidence>100||r.account.empty()||
                   r.account.size()>240||r.account.find('\0')!=std::string::npos)
                    throw std::runtime_error("SQLite: group view has no eligible witnessed relay");
                view_tip=social_digest(view_tip,view_body(r));
                if(sqlite_str(view.get(),8)!=view_tip)
                    throw std::runtime_error("SQLite: group view digest mismatch");
            }
            if(view_count!=static_cast<std::uint64_t>(vc)||view_tip!=vt)
                throw std::runtime_error("SQLite: social views missing or changed");
        }
        // GRSM-03b: replay event-sourced directed trust, with cross-references
        // into immutable received claims and retained physical snapshots.
        {
            Statement meta(db_,"SELECT event_count,tip FROM social_tie_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing tie metadata");
            const auto count=sqlite3_column_int64(meta.get(),0);
            if(count<0)throw std::runtime_error("SQLite: negative social tie count");
            const auto final_tip=sqlite_str(meta.get(),1);
            if(meta.row())throw std::runtime_error("SQLite: duplicate tie metadata");
            std::uint64_t seq=0;std::string tip="origin";
            std::map<std::pair<std::uint64_t,std::uint64_t>,SocialTieEvent> last;
            std::map<std::uint64_t,bool> feedback_claims;
            Statement events(db_,"SELECT id,observer,source,prior_id,knowledge_id,kind,delta,trust,"
                "generation,tick,digest FROM social_tie_events ORDER BY id");
            while(events.row()){
                const auto e=read_tie(events.get());
                if(e.id!=++seq||e.observer_npc==0||e.source_npc==0||
                   e.observer_npc==e.source_npc||e.generation>generation||
                   static_cast<int>(e.kind)<1||static_cast<int>(e.kind)>2)
                    throw std::runtime_error("SQLite: invalid directed social tie event");
                const auto pair=std::make_pair(e.observer_npc,e.source_npc);
                const auto previous=last.find(pair);
                if(e.kind==SocialTieKind::Contact){
                    if(previous!=last.end()||e.prior_id||e.knowledge_id||e.delta||e.trust!=50)
                        throw std::runtime_error("SQLite: fabricated/repeated first social contact");
                }else{
                    if(previous==last.end()||e.prior_id!=previous->second.id||
                       !e.knowledge_id||e.delta==0||e.delta<-25||e.delta>25||
                       e.trust!=std::clamp(previous->second.trust+e.delta,0,100)||
                       !feedback_claims.emplace(e.knowledge_id,true).second)
                        throw std::runtime_error("SQLite: invalid trust change receipt");
                    Statement claim(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
                        "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge WHERE id=?");
                    claim.i64(1,static_cast<sqlite3_int64>(e.knowledge_id));
                    if(!claim.row())throw std::runtime_error("SQLite: trust has no source testimony");
                    const auto k=read_knowledge(claim.get());
                    if(k.speaker_npc!=e.source_npc||k.recipient_npc!=e.observer_npc||
                       k.parent_id==0||k.generation>e.generation||k.tick>e.tick)
                        throw std::runtime_error("SQLite: trust does not reference received testimony");
                }
                if(previous!=last.end() && (e.generation<previous->second.generation||
                     e.tick<previous->second.tick))
                    throw std::runtime_error("SQLite: social trust time reversed");
                // Contact is verified against the actual checkpoint whenever
                // one survives, not invented on migration from old saves.
                Statement snap(db_,"SELECT state FROM snapshots WHERE generation=?");
                snap.i64(1,static_cast<sqlite3_int64>(e.generation));
                if(snap.row()){
                    const auto bytes=sqlite3_column_bytes(snap.get(),0);
                    const auto* data=static_cast<const char*>(sqlite3_column_blob(snap.get(),0));
                    if(bytes<=0||bytes>16*1024*1024||!data)
                        throw std::runtime_error("SQLite: malformed social tie checkpoint");
                    const auto past=Simulation::from_snapshot(std::string(data,static_cast<std::size_t>(bytes)));
                    const auto a=past.npc_records().find(e.observer_npc);
                    const auto b=past.npc_records().find(e.source_npc);
                    if(a==past.npc_records().end()||b==past.npc_records().end()||
                       a->second.location!=b->second.location)
                        throw std::runtime_error("SQLite: social tie without physical contact");
                }
                tip=social_digest(tip,tie_body(e));
                if(sqlite_str(events.get(),10)!=tip)
                    throw std::runtime_error("SQLite: social trust chain mismatch");
                last[pair]=e;
            }
            if(seq!=static_cast<std::uint64_t>(count)||tip!=final_tip)
                throw std::runtime_error("SQLite: deleted or altered social trust event");
        }
        // GRSM-03c.2: check the complete bounded encounter schedule ledger.
        // Migrated v10 games have no invented meetings. Previously attested
        // contacts survive rolling snapshot compaction via immutable receipts.
        {
            Statement meta(db_,"SELECT scan_count,scan_tip,contact_count,contact_tip,cursor "
                "FROM encounter_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing encounter metadata");
            const auto scans_meta=sqlite3_column_int64(meta.get(),0),
                contacts_meta=sqlite3_column_int64(meta.get(),2),cursor_meta=sqlite3_column_int64(meta.get(),4);
            if(scans_meta<0||contacts_meta<0||cursor_meta<0)
                throw std::runtime_error("SQLite: invalid encounter metadata");
            const auto scan_tip_meta=sqlite_str(meta.get(),1),contact_tip_meta=sqlite_str(meta.get(),3);
            if(meta.row())throw std::runtime_error("SQLite: duplicate encounter metadata");
            std::uint64_t scan_count=0,contact_count=0,cursor=0;
            std::uint64_t prev_generation=0,prev_tick=0;
            std::string scan_tip="origin",contact_tip="origin";
            std::map<std::pair<std::uint64_t,std::uint64_t>,SocialEncounter> previous;
            Statement scans(db_,"SELECT id,generation,tick,population,pair_count,start_slot,scanned,"
                "end_slot,contacts,digest FROM encounter_scans ORDER BY id");
            while(scans.row()){
                const auto c=read_encounter_cycle(scans.get());
                if(c.id!=++scan_count||c.generation>generation||c.tick>restored.tick()||
                   c.generation<prev_generation||c.tick<prev_tick||
                   c.population<2||c.population>1024||
                   c.pairs!=c.population*(c.population-1)/2||
                   !c.scanned||c.scanned>64||c.scanned>c.pairs||c.contacts>8||
                   c.contacts>c.scanned||c.start!=cursor%c.pairs||
                   c.end!=(c.start+c.scanned)%c.pairs)
                    throw std::runtime_error("SQLite: invalid encounter scan receipt");
                scan_tip=social_digest(scan_tip,encounter_cycle_body(c));
                if(sqlite_str(scans.get(),9)!=scan_tip)
                    throw std::runtime_error("SQLite: damaged encounter scan chain");
                // When the precise physical snapshot still exists, re-attest
                // participant identity and the pair's location/slot mapping.
                std::vector<std::uint64_t> ids;
                std::map<std::uint64_t,EntityId> locations;
                Statement snap(db_,"SELECT state FROM snapshots WHERE generation=?");
                snap.i64(1,static_cast<sqlite3_int64>(c.generation));
                if(snap.row()){
                    const auto sz=sqlite3_column_bytes(snap.get(),0);
                    const auto* raw=static_cast<const char*>(sqlite3_column_blob(snap.get(),0));
                    if(sz<=0||sz>16*1024*1024||!raw)
                        throw std::runtime_error("SQLite: invalid encounter snapshot");
                    const auto past=Simulation::from_snapshot(std::string(raw,static_cast<std::size_t>(sz)));
                    if(past.tick()!=c.tick||past.npc_records().size()!=c.population)
                        throw std::runtime_error("SQLite: encounter scan population mismatch");
                    for(const auto& npc:past.npc_records()){
                        ids.push_back(npc.first);
                        locations.emplace(npc.first,npc.second.location);
                    }
                }
                std::uint64_t observed=0;
                Statement contacts(db_,"SELECT id,cycle_id,npc_low,npc_high,generation,tick,slot,"
                    "prior_id,ordinal,digest FROM social_encounters WHERE cycle_id=? ORDER BY id");
                contacts.i64(1,static_cast<sqlite3_int64>(c.id));
                while(contacts.row()){
                    const auto e=read_encounter(contacts.get());
                    const auto key=std::make_pair(e.npc_low,e.npc_high);
                    const auto prior=previous.find(key);
                    if(e.id!=++contact_count||e.cycle_id!=c.id||e.npc_low==0||
                       e.npc_low>=e.npc_high||e.generation!=c.generation||e.tick!=c.tick||
                       e.slot>=c.pairs||((e.slot+c.pairs-c.start)%c.pairs)>=c.scanned||
                       e.prior_id!=(prior==previous.end()?0:prior->second.id)||
                       e.ordinal!=(prior==previous.end()?1:prior->second.ordinal+1)||
                       (prior!=previous.end()&&(e.tick<prior->second.tick||
                        e.tick-prior->second.tick<8)))
                        throw std::runtime_error("SQLite: impossible encounter provenance");
                    if(!ids.empty()){
                        if(encounter_pair_at(ids,e.slot)!=key)
                            throw std::runtime_error("SQLite: false encounter slot identity");
                        if(locations.at(e.npc_low)!=locations.at(e.npc_high))
                            throw std::runtime_error("SQLite: encounter had no physical proximity");
                    }
                    contact_tip=social_digest(contact_tip,encounter_body(e));
                    if(sqlite_str(contacts.get(),9)!=contact_tip)
                        throw std::runtime_error("SQLite: corrupted encounter receipt");
                    previous[key]=e;++observed;
                }
                if(observed!=c.contacts)
                    throw std::runtime_error("SQLite: scan contact tally mismatch");
                cursor=c.end;prev_generation=c.generation;prev_tick=c.tick;
            }
            if(scan_count!=static_cast<std::uint64_t>(scans_meta)||
               contact_count!=static_cast<std::uint64_t>(contacts_meta)||
               cursor!=static_cast<std::uint64_t>(cursor_meta)||
               scan_tip!=scan_tip_meta||contact_tip!=contact_tip_meta)
                throw std::runtime_error("SQLite: missing encounter cycle/contact or invalid cursor");
        }
        // GRSM-03c.3: replay directed rapport as a *separate dimension* from
        // testimony credibility. Every change must be anchored in a real,
        // same-tick encountered pair. A proximity receipt alone changes nothing.
        {
            Statement meta(db_,"SELECT event_count,tip FROM interaction_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing interaction metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto expected_tip=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid interaction metadata");
            std::uint64_t count=0;std::string tip="origin";
            std::map<std::pair<std::uint64_t,std::uint64_t>,SocialInteractionEvent> last;
            std::set<std::pair<std::uint64_t,std::uint64_t>> used;
            Statement events(db_,"SELECT id,encounter_id,observer,actor,kind,delta,rapport,"
                "prior_id,generation,tick,digest FROM social_interactions ORDER BY id");
            while(events.row()){
                const auto e=read_interaction(events.get());
                if(e.id!=++count||e.observer_npc==0||e.actor_npc==0||
                   e.observer_npc==e.actor_npc||e.generation>generation||
                   e.tick>restored.tick()||
                   (e.kind!=SocialInteractionKind::Assistance&&e.kind!=SocialInteractionKind::Harm)||
                   e.delta!=(e.kind==SocialInteractionKind::Assistance?8:-12)||
                   !used.emplace(e.encounter_id,e.observer_npc).second)
                    throw std::runtime_error("SQLite: invalid social reaction provenance");
                Statement contact(db_,"SELECT npc_low,npc_high,generation,tick "
                    "FROM social_encounters WHERE id=?");
                contact.i64(1,static_cast<sqlite3_int64>(e.encounter_id));
                if(!contact.row())throw std::runtime_error("SQLite: reaction lacks encounter evidence");
                const auto low=parse_u64(sqlite3_column_text(contact.get(),0)),
                    high=parse_u64(sqlite3_column_text(contact.get(),1));
                if(std::min(e.observer_npc,e.actor_npc)!=low||
                   std::max(e.observer_npc,e.actor_npc)!=high||
                   e.generation<static_cast<std::uint64_t>(sqlite3_column_int64(contact.get(),2))||
                   e.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(contact.get(),3)))
                    throw std::runtime_error("SQLite: reaction has impossible meeting timestamp or actors");
                const auto key=std::make_pair(e.observer_npc,e.actor_npc);
                const auto prev=last.find(key);
                if(e.prior_id!=(prev==last.end()?0:prev->second.id)||
                   e.rapport!=std::clamp((prev==last.end()?0:prev->second.rapport)+e.delta,-100,100)||
                   (prev!=last.end()&&(e.generation<prev->second.generation||e.tick<prev->second.tick)))
                    throw std::runtime_error("SQLite: invalid directional rapport sequence");
                tip=social_digest(tip,interaction_body(e));
                if(sqlite_str(events.get(),10)!=tip)
                    throw std::runtime_error("SQLite: social interaction chain altered");
                last[key]=e;
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=expected_tip)
                throw std::runtime_error("SQLite: missing or corrupted social interactions");
        }
        // GRSM-03d: individually interpreted group reputation, NOT group
        // ratification. Reconstruct each directed score from source receipts
        // and membership as of the frozen epoch, not today's roster.
        {
            Statement meta(db_,"SELECT event_count,tip FROM attribution_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: absent attribution metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto expected_tip=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid attribution metadata");
            Statement membership(db_,"SELECT event_count FROM membership_meta WHERE singleton=1");
            if(!membership.row())throw std::runtime_error("SQLite: missing membership epoch");
            const auto latest_epoch=sqlite3_column_int64(membership.get(),0);
            std::uint64_t count=0;std::string tip="origin";
            using PerceptionKey=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
            std::map<PerceptionKey,GroupAttributionEvent> last;
            Statement rows(db_,"SELECT id,interaction_id,observer_group,actor_group,observer,actor,"
                "membership_seq,stance,delta,feeling,prior_id,generation,tick,digest "
                "FROM group_attributions ORDER BY id");
            while(rows.row()){
                auto e=read_attribution(rows.get());
                if(e.id!=++count||e.observer_group==e.actor_group||
                   e.observer_npc==e.actor_npc||e.generation>generation||
                   e.tick>restored.tick()||e.membership_seq>static_cast<std::uint64_t>(latest_epoch)||
                   static_cast<int>(e.stance)<1||static_cast<int>(e.stance)>3||
                   e.delta!=attribution_delta(e.stance))
                    throw std::runtime_error("SQLite: invalid group viewpoint evidence");
                Statement source(db_,"SELECT encounter_id,observer,actor,kind,delta,rapport,"
                    "prior_id,generation,tick FROM social_interactions WHERE id=?");
                source.i64(1,static_cast<sqlite3_int64>(e.interaction_id));
                if(!source.row())throw std::runtime_error("SQLite: group viewpoint has no reaction");
                if(parse_u64(sqlite3_column_text(source.get(),1))!=e.observer_npc||
                   parse_u64(sqlite3_column_text(source.get(),2))!=e.actor_npc||
                   static_cast<std::uint64_t>(sqlite3_column_int64(source.get(),7))!=e.generation||
                   static_cast<std::uint64_t>(sqlite3_column_int64(source.get(),8))!=e.tick)
                    throw std::runtime_error("SQLite: group viewpoint mismatches source reaction");
                Statement groups(db_,"SELECT id,generation FROM social_groups WHERE id IN (?,?)");
                groups.i64(1,static_cast<sqlite3_int64>(e.observer_group));
                groups.i64(2,static_cast<sqlite3_int64>(e.actor_group));
                int found=0;
                while(groups.row()){
                    if(sqlite3_column_int64(groups.get(),1)>static_cast<sqlite3_int64>(e.generation))
                        throw std::runtime_error("SQLite: group attribution predates group");
                    ++found;
                }
                if(found!=2)throw std::runtime_error("SQLite: group viewpoint lacks group identity");
                const auto observers=group_members_at(e.observer_group,e.membership_seq);
                const auto actors=group_members_at(e.actor_group,e.membership_seq);
                if(!std::binary_search(observers.begin(),observers.end(),e.observer_npc)||
                   !std::binary_search(actors.begin(),actors.end(),e.actor_npc))
                    throw std::runtime_error("SQLite: group viewpoint fabricated membership");
                const PerceptionKey key={e.observer_npc,e.observer_group,e.actor_group};
                const auto prev=last.find(key);
                if(e.prior_id!=(prev==last.end()?0:prev->second.id)||
                   e.feeling!=std::clamp((prev==last.end()?0:prev->second.feeling)+e.delta,-100,100)||
                   (prev!=last.end()&&(e.generation<prev->second.generation||e.tick<prev->second.tick)))
                    throw std::runtime_error("SQLite: group viewpoint sequence altered");
                tip=social_digest(tip,attribution_body(e));
                if(tip!=sqlite_str(rows.get(),13))
                    throw std::runtime_error("SQLite: group viewpoint digest mismatch");
                last[key]=e;
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=expected_tip)
                throw std::runtime_error("SQLite: missing or corrupted group viewpoints");
        }
        // GRSM-04a: appraisals are subjective, but their evidence is not.
        // Verify the source is a genuine *firsthand* claim of the same physical
        // incident, not hearsay or invented past contact. Group membership is
        // checked at the original epoch even if the person subsequently quits.
        {
            Statement meta(db_,"SELECT event_count,tip FROM physical_appraisal_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing physical appraisal metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto end_tip=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid physical appraisal metadata");
            const auto latest_epoch=last_membership_seq();
            std::uint64_t count=0;std::string tip="origin";
            Statement records(db_,"SELECT id,observation_id,incident_id,observer,context_group,"
                "membership_seq,stance,confidence,generation,tick,digest FROM physical_appraisals ORDER BY id");
            while(records.row()){
                const auto e=read_physical_appraisal(records.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick()||
                   e.membership_seq>latest_epoch||
                   static_cast<int>(e.stance)<1||static_cast<int>(e.stance)>3||
                   e.confidence<0||e.confidence>100||
                   (e.context_group==0&&e.membership_seq!=0))
                    throw std::runtime_error("SQLite: invalid appraisal identity or epoch");
                Statement claim(db_,"SELECT incident_id,COALESCE(parent_id,0),recipient_id,"
                    "fidelity,generation,tick FROM npc_knowledge WHERE id=?");
                claim.i64(1,static_cast<sqlite3_int64>(e.observation_id));
                if(!claim.row()||sqlite_str(claim.get(),0)!=e.incident_id||
                   sqlite3_column_int64(claim.get(),1)!=0||
                   parse_u64(sqlite3_column_text(claim.get(),2))!=e.observer_npc||
                   sqlite3_column_int(claim.get(),3)!=static_cast<int>(ClaimFidelity::LocalObservation)||
                   sqlite3_column_int64(claim.get(),4)!=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(claim.get(),5)!=static_cast<sqlite3_int64>(e.tick))
                    throw std::runtime_error("SQLite: appraisal lacks firsthand evidence");
                Statement fact(db_,"SELECT generation,tick FROM physical_incidents WHERE incident_id=?");
                fact.text(1,e.incident_id);
                if(!fact.row()||
                   sqlite3_column_int64(fact.get(),0)!=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(fact.get(),1)!=static_cast<sqlite3_int64>(e.tick))
                    throw std::runtime_error("SQLite: appraisal lacks committed physical source");
                if(e.context_group){
                    Statement g(db_,"SELECT generation FROM social_groups WHERE id=?");
                    g.i64(1,static_cast<sqlite3_int64>(e.context_group));
                    if(!g.row()||sqlite3_column_int64(g.get(),0)>static_cast<sqlite3_int64>(e.generation))
                        throw std::runtime_error("SQLite: appraisal predates its group");
                    const auto members=group_members_at(e.context_group,e.membership_seq);
                    if(!std::binary_search(members.begin(),members.end(),e.observer_npc))
                        throw std::runtime_error("SQLite: appraiser lacks original group membership");
                }
                tip=social_digest(tip,physical_appraisal_body(e));
                if(tip!=sqlite_str(records.get(),10))
                    throw std::runtime_error("SQLite: altered physical appraisal digest");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=end_tip)
                throw std::runtime_error("SQLite: deleted or altered appraisal history");
        }
        // GRSM-04b.A: every named role is reconciled to an independently
        // committed physical fact and (when needed) a true firsthand receipt.
        // Neither a ship actor nor a host command implies an NPC culprit.
        {
            Statement meta(db_,"SELECT event_count,tip FROM incident_role_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing incident role metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto expected_tip=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid incident role metadata");
            std::uint64_t count=0;std::string tip="origin";
            Statement rows(db_,"SELECT id,incident_id,npc_id,role,observation_id,generation,tick,digest "
                "FROM incident_role_receipts ORDER BY id");
            while(rows.row()){
                const auto e=read_incident_role(rows.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick()||
                   (e.role!=VerifiedNpcRole::Victim&&e.role!=VerifiedNpcRole::FirsthandObserver))
                    throw std::runtime_error("SQLite: invalid NPC responsibility role");
                Statement fact(db_,"SELECT generation,tick,kind,destination FROM physical_incidents WHERE incident_id=?");
                fact.text(1,e.incident_id);
                if(!fact.row()||sqlite3_column_int64(fact.get(),0)!=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(fact.get(),1)!=static_cast<sqlite3_int64>(e.tick))
                    throw std::runtime_error("SQLite: role lacks authentic physical incident");
                if(e.role==VerifiedNpcRole::Victim){
                    if(e.observation_id!=0||sqlite3_column_int(fact.get(),2)!=static_cast<int>(PhysicalKind::NpcLost)||
                       sqlite_str(fact.get(),3)!=std::to_string(e.npc_id))
                        throw std::runtime_error("SQLite: victim is not named by real casualty");
                }else{
                    if(e.observation_id==0)throw std::runtime_error("SQLite: missing firsthand source");
                    Statement claim(db_,"SELECT incident_id,COALESCE(parent_id,0),recipient_id,fidelity,"
                        "generation,tick FROM npc_knowledge WHERE id=?");
                    claim.i64(1,e.observation_id);
                    if(!claim.row()||sqlite_str(claim.get(),0)!=e.incident_id||
                       sqlite3_column_int64(claim.get(),1)!=0||
                       parse_u64(sqlite3_column_text(claim.get(),2))!=e.npc_id||
                       sqlite3_column_int(claim.get(),3)!=static_cast<int>(ClaimFidelity::LocalObservation)||
                       sqlite3_column_int64(claim.get(),4)!=static_cast<sqlite3_int64>(e.generation)||
                       sqlite3_column_int64(claim.get(),5)!=static_cast<sqlite3_int64>(e.tick))
                        throw std::runtime_error("SQLite: role lacks true firsthand observation");
                }
                tip=social_digest(tip,incident_role_body(e));
                if(tip!=sqlite_str(rows.get(),7))
                    throw std::runtime_error("SQLite: tampered NPC role digest");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=expected_tip)
                throw std::runtime_error("SQLite: missing or altered NPC role receipts");
        }
        // GRSM-04b.B1: do not treat a ship/crew association as executor proof.
        // Only the explicit assigned work path, followed by actual committed
        // mining effects and the original command journal, can yield a named
        // execution. Both immutable chains and their tips must survive restore.
        {
            Statement meta(db_,"SELECT event_count,tip FROM npc_mining_assignment_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing mining assignment metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto tip_end=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid mining assignment metadata");
            std::uint64_t count=0;std::string tip="origin";
            Statement q(db_,"SELECT id,command_id,npc_id,ship_id,rock_id,generation,tick,digest "
                           "FROM npc_mining_assignments ORDER BY id");
            while(q.row()){
                const auto e=read_npc_mining_assignment(q.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick())
                    throw std::runtime_error("SQLite: invalid mining assignment sequence");
                Statement cmd(db_,"SELECT command FROM operation_log WHERE kind=1 AND generation=?");
                cmd.i64(1,e.generation);
                if(!cmd.row())throw std::runtime_error("SQLite: orphaned mining assignment");
                const auto parsed=decode_command(sqlite_str(cmd.get(),0));
                if(parsed.id!=e.command_id||parsed.action!=Action::Mine||
                   parsed.actor!=e.ship_id||parsed.target!=e.rock_id||
                   parsed.execute_tick!=e.tick+1||cmd.row())
                    throw std::runtime_error("SQLite: mining assignment contradicts command");
                Statement receipt(db_,"SELECT accepted_generation FROM receipts WHERE id=?");
                receipt.text(1,std::to_string(e.command_id));
                if(!receipt.row()||sqlite3_column_int64(receipt.get(),0)!=static_cast<sqlite3_int64>(e.generation))
                    throw std::runtime_error("SQLite: mining assignment lacks accepted receipt");
                tip=social_digest(tip,npc_mining_assignment_body(e));
                if(tip!=sqlite_str(q.get(),7))throw std::runtime_error("SQLite: altered mining assignment history");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=tip_end)
                throw std::runtime_error("SQLite: deleted mining assignment history");
        }
        {
            Statement meta(db_,"SELECT event_count,tip FROM npc_mining_execution_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing mining execution metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto tip_end=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid mining execution metadata");
            std::uint64_t count=0;std::string tip="origin";
            Statement q(db_,"SELECT id,assignment_id,command_id,npc_id,incident_id,generation,tick,amount,digest "
                           "FROM npc_mining_executions ORDER BY id");
            while(q.row()){
                const auto e=read_npc_mining_execution(q.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick())
                    throw std::runtime_error("SQLite: invalid mining executor sequence");
                Statement assignment(db_,"SELECT command_id,npc_id,ship_id,rock_id,generation,tick "
                    "FROM npc_mining_assignments WHERE id=?");
                assignment.i64(1,e.assignment_id);
                if(!assignment.row()||
                   parse_u64(sqlite3_column_text(assignment.get(),0))!=e.command_id||
                   parse_u64(sqlite3_column_text(assignment.get(),1))!=e.npc_id||
                   sqlite3_column_int64(assignment.get(),4)>=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(assignment.get(),5)+1!=static_cast<sqlite3_int64>(e.tick))
                    throw std::runtime_error("SQLite: executor lacks original named assignment");
                const auto ship=sqlite_str(assignment.get(),2),rock=sqlite_str(assignment.get(),3);
                Statement source(db_,"SELECT kind,source,destination,generation,tick,amount,command_id "
                    "FROM physical_incidents WHERE incident_id=?");
                source.text(1,e.incident_id);
                if(!source.row()||sqlite3_column_int(source.get(),0)!=static_cast<int>(PhysicalKind::CargoMovement)||
                   sqlite_str(source.get(),1)!=rock||sqlite_str(source.get(),2)!=ship||
                   sqlite3_column_int64(source.get(),3)!=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(source.get(),4)!=static_cast<sqlite3_int64>(e.tick)||
                   sqlite3_column_int64(source.get(),5)!=e.amount||
                   parse_u64(sqlite3_column_text(source.get(),6))!=e.command_id)
                    throw std::runtime_error("SQLite: NPC executor lacks real cargo movement");
                Statement result(db_,"SELECT execution_tick,result,transferred FROM receipts WHERE id=?");
                result.text(1,std::to_string(e.command_id));
                if(!result.row()||sqlite3_column_int64(result.get(),0)!=static_cast<sqlite3_int64>(e.tick)||
                   sqlite3_column_int(result.get(),1)!=static_cast<int>(Outcome::Applied)||
                   sqlite3_column_int64(result.get(),2)!=e.amount)
                    throw std::runtime_error("SQLite: NPC execution contradicts command result");
                tip=social_digest(tip,npc_mining_execution_body(e));
                if(tip!=sqlite_str(q.get(),8))throw std::runtime_error("SQLite: altered NPC execution history");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=tip_end)
                throw std::runtime_error("SQLite: deleted NPC execution history");
        }
        // Migration does not manufacture executor identities for older work.
        // Validate both append-only fabrication chains against the original
        // accepted command, applied receipt, and committed cargo movement.
        {
            Statement meta(db_,"SELECT event_count,tip FROM npc_fabrication_assignment_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing fabrication assignment metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto tip_end=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid fabrication assignment metadata");
            std::uint64_t count=0;std::string tip="origin";
            Statement q(db_,"SELECT id,command_id,npc_id,ship_id,issuer,action,material_code,quantity,generation,tick,digest "
                "FROM npc_fabrication_assignments ORDER BY id");
            while(q.row()){
                const auto e=read_npc_fabrication_assignment(q.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick())
                    throw std::runtime_error("SQLite: invalid fabrication assignment sequence");
                Statement cmd(db_,"SELECT command FROM operation_log WHERE kind=1 AND generation=?");
                cmd.i64(1,e.generation);
                if(!cmd.row())throw std::runtime_error("SQLite: orphaned fabrication assignment");
                const auto parsed=decode_command(sqlite_str(cmd.get(),0));
                if(parsed.id!=e.command_id||parsed.action!=e.action||
                   parsed.actor!=e.ship_id||parsed.target!=e.material_code||
                   parsed.issuer!=e.issuer||parsed.amount!=e.quantity||
                   parsed.execute_tick!=e.tick+1||cmd.row())
                    throw std::runtime_error("SQLite: fabrication assignment contradicts command");
                Statement receipt(db_,"SELECT accepted_generation FROM receipts WHERE id=?");
                receipt.text(1,std::to_string(e.command_id));
                if(!receipt.row()||sqlite3_column_int64(receipt.get(),0)!=static_cast<sqlite3_int64>(e.generation))
                    throw std::runtime_error("SQLite: fabrication assignment lacks accepted receipt");
                tip=social_digest(tip,npc_fabrication_assignment_body(e));
                if(tip!=sqlite_str(q.get(),10))throw std::runtime_error("SQLite: altered fabrication assignment");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=tip_end)
                throw std::runtime_error("SQLite: deleted fabrication assignment");
        }
        {
            Statement meta(db_,"SELECT event_count,tip FROM npc_fabrication_execution_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing fabrication execution metadata");
            const auto expected=sqlite3_column_int64(meta.get(),0);
            const auto tip_end=sqlite_str(meta.get(),1);
            if(expected<0||meta.row())throw std::runtime_error("SQLite: invalid fabrication execution metadata");
            std::uint64_t count=0;std::string tip="origin";
            Statement q(db_,"SELECT id,assignment_id,command_id,npc_id,incident_id,generation,tick,transferred,digest "
                "FROM npc_fabrication_executions ORDER BY id");
            while(q.row()){
                const auto e=read_npc_fabrication_execution(q.get());
                if(e.id!=++count||e.generation>generation||e.tick>restored.tick())
                    throw std::runtime_error("SQLite: invalid fabrication executor sequence");
                Statement assignment(db_,"SELECT command_id,npc_id,ship_id,issuer,action,material_code,quantity,generation,tick "
                    "FROM npc_fabrication_assignments WHERE id=?");
                assignment.i64(1,e.assignment_id);
                if(!assignment.row()||
                   parse_u64(sqlite3_column_text(assignment.get(),0))!=e.command_id||
                   parse_u64(sqlite3_column_text(assignment.get(),1))!=e.npc_id||
                   sqlite3_column_int64(assignment.get(),7)>=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(assignment.get(),8)+1!=static_cast<sqlite3_int64>(e.tick)||
                   2*sqlite3_column_int64(assignment.get(),6)!=e.transferred)
                    throw std::runtime_error("SQLite: fabrication execution lacks named assignment");
                const auto ship=sqlite_str(assignment.get(),2);
                const auto action=static_cast<Action>(sqlite3_column_int(assignment.get(),4));
                const auto material=sqlite3_column_int64(assignment.get(),5);
                const auto source=action==Action::FabricateModule?ship:
                    "good:"+ship+":"+std::to_string(material);
                const auto destination="modules:"+ship;
                Statement physical(db_,"SELECT kind,source,destination,generation,tick,amount,command_id "
                    "FROM physical_incidents WHERE incident_id=?");
                physical.text(1,e.incident_id);
                if(!physical.row()||sqlite3_column_int(physical.get(),0)!=static_cast<int>(PhysicalKind::CargoMovement)||
                   sqlite_str(physical.get(),1)!=source||sqlite_str(physical.get(),2)!=destination||
                   sqlite3_column_int64(physical.get(),3)!=static_cast<sqlite3_int64>(e.generation)||
                   sqlite3_column_int64(physical.get(),4)!=static_cast<sqlite3_int64>(e.tick)||
                   sqlite3_column_int64(physical.get(),5)!=e.transferred||
                   parse_u64(sqlite3_column_text(physical.get(),6))!=e.command_id)
                    throw std::runtime_error("SQLite: fabrication executor missing real cargo movement");
                Statement result(db_,"SELECT execution_tick,result,transferred FROM receipts WHERE id=?");
                result.text(1,std::to_string(e.command_id));
                if(!result.row()||sqlite3_column_int64(result.get(),0)!=static_cast<sqlite3_int64>(e.tick)||
                   sqlite3_column_int(result.get(),1)!=static_cast<int>(Outcome::Applied)||
                   sqlite3_column_int64(result.get(),2)!=e.transferred)
                    throw std::runtime_error("SQLite: fabrication executor contradicts command result");
                tip=social_digest(tip,npc_fabrication_execution_body(e));
                if(tip!=sqlite_str(q.get(),8))throw std::runtime_error("SQLite: altered fabrication execution");
            }
            if(count!=static_cast<std::uint64_t>(expected)||tip!=tip_end)
                throw std::runtime_error("SQLite: deleted fabrication execution");
        }
        out.swap(restored);
        return true;
    }

    void commit(const Simulation& state, std::uint64_t expected_generation, std::int64_t origin, std::int64_t credit_origin,
                const Command* accepted, const std::vector<Event>& new_events,
                const std::map<std::uint64_t, Command>& due_commands,
                const std::vector<ActivityEvent>& activity_delta, FailPoint fail,
                const NpcMiningAssignment* npc_mining_assignment=nullptr,
                const NpcFabricationAssignment* npc_fabrication_assignment=nullptr) {
        if (state.total_ore() != origin || state.total_credits() != credit_origin)
            throw std::runtime_error("resource or credit conservation violation");
        const auto bytes = state.canonical_state();
        if (bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("snapshot limit exceeded; checkpoint compaction needed");
        run(db_, "BEGIN IMMEDIATE");
        try {
            const auto current = latest_generation(db_);
            if (current != expected_generation)
                throw std::runtime_error("stale world generation; reload before writing");
            if (expected_generation >= static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
                throw std::runtime_error("generation exhausted");
            const auto next = expected_generation + 1;
            if (expected_generation == 0) {
                Statement meta(db_, "INSERT INTO world_meta(singleton,resource_origin,credit_origin) VALUES(1,?,?)");
                meta.i64(1, origin); meta.i64(2,credit_origin); meta.execute();
                Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,amount,reason) VALUES(?,?,?,?)");
                ledger.i64(1, static_cast<std::int64_t>(next));
                ledger.i64(2, 0); ledger.i64(3, origin); ledger.text(4, "initial_world_allocation"); ledger.execute();
            }
            if (accepted) {
                Statement receipt(db_, "INSERT INTO receipts(id,accepted_generation) VALUES(?,?)");
                receipt.text(1, std::to_string(accepted->id));
                receipt.i64(2, static_cast<std::int64_t>(next));
                receipt.execute();
            }
            if(npc_mining_assignment){
                const auto& e=*npc_mining_assignment;
                if(!accepted||e.command_id!=accepted->id||e.ship_id!=accepted->actor||
                   e.rock_id!=accepted->target||accepted->action!=Action::Mine||
                   e.generation!=next||e.tick+1!=accepted->execute_tick||!e.npc_id)
                    throw std::runtime_error("invalid accepted named NPC mining job");
                Statement meta(db_,"SELECT event_count,tip FROM npc_mining_assignment_meta WHERE singleton=1");
                if(!meta.row())throw std::runtime_error("missing NPC assignment sequence");
                const auto count=sqlite3_column_int64(meta.get(),0);
                if(count<0||count==std::numeric_limits<sqlite3_int64>::max()||
                   e.id!=static_cast<std::uint64_t>(count+1))
                    throw std::runtime_error("NPC mining assignment sequence mismatch");
                const auto tip=social_digest(sqlite_str(meta.get(),1),npc_mining_assignment_body(e));
                Statement add(db_,"INSERT INTO npc_mining_assignments VALUES(?,?,?,?,?,?,?,?)");
                add.i64(1,e.id);add.text(2,std::to_string(e.command_id));
                add.text(3,std::to_string(e.npc_id));add.text(4,std::to_string(e.ship_id));
                add.text(5,std::to_string(e.rock_id));add.i64(6,e.generation);
                add.i64(7,e.tick);add.text(8,tip);add.execute();
                Statement update(db_,"UPDATE npc_mining_assignment_meta SET event_count=?,tip=? WHERE singleton=1");
                update.i64(1,e.id);update.text(2,tip);update.execute();
                if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing NPC assignment metadata update");
            }
            if(npc_fabrication_assignment){
                const auto& e=*npc_fabrication_assignment;
                if(!accepted||e.command_id!=accepted->id||e.ship_id!=accepted->actor||
                   e.issuer!=accepted->issuer||e.action!=accepted->action||
                   e.material_code!=accepted->target||e.quantity!=accepted->amount||
                   e.generation!=next||e.tick+1!=accepted->execute_tick||!e.npc_id||
                   (e.action!=Action::FabricateModule && e.action!=Action::FabricateMaterialModule))
                    throw std::runtime_error("invalid accepted NPC fabrication job");
                Statement meta(db_,"SELECT event_count,tip FROM npc_fabrication_assignment_meta WHERE singleton=1");
                if(!meta.row())throw std::runtime_error("missing fabrication assignment sequence");
                const auto count=sqlite3_column_int64(meta.get(),0);
                if(count<0||count==std::numeric_limits<sqlite3_int64>::max()||
                   e.id!=static_cast<std::uint64_t>(count+1))
                    throw std::runtime_error("fabrication assignment sequence mismatch");
                const auto tip=social_digest(sqlite_str(meta.get(),1),npc_fabrication_assignment_body(e));
                Statement add(db_,"INSERT INTO npc_fabrication_assignments VALUES(?,?,?,?,?,?,?,?,?,?,?)");
                add.i64(1,e.id);add.text(2,std::to_string(e.command_id));
                add.text(3,std::to_string(e.npc_id));add.text(4,std::to_string(e.ship_id));
                add.text(5,std::to_string(e.issuer));add.i64(6,static_cast<int>(e.action));
                add.i64(7,e.material_code);add.i64(8,e.quantity);add.i64(9,e.generation);
                add.i64(10,e.tick);add.text(11,tip);add.execute();
                Statement update(db_,"UPDATE npc_fabrication_assignment_meta SET event_count=?,tip=? WHERE singleton=1");
                update.i64(1,e.id);update.text(2,tip);update.execute();
                if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing fabrication assignment metadata update");
            }
            for (const auto& ev : new_events) {
                Statement update(db_, "UPDATE receipts SET execution_tick=?,result=?,transferred=? WHERE id=? AND execution_tick IS NULL");
                update.i64(1, static_cast<std::int64_t>(ev.tick));
                update.i64(2, static_cast<int>(ev.result));
                update.i64(3, ev.transferred);
                update.text(4, std::to_string(ev.command_id));
                update.execute();
                if (sqlite3_changes(db_) != 1)
                    throw std::runtime_error("missing/double execution receipt");
                // Same transaction as receipts, physical resource ledgers and
                // snapshot. A rejected action is recorded as rejected, never
                // inferred to have harmed people, ships or cargo.
                const auto due = due_commands.find(ev.command_id);
                if (due==due_commands.end() || due->second.execute_tick!=ev.tick ||
                    ev.transferred<0 || ev.tick!=state.tick())
                    throw std::runtime_error("history: unmatched executed command");
                const auto record=command_record(due->second);
                const auto historical_id="command:"+std::to_string(ev.command_id);
                Statement history(db_, "INSERT INTO historical_events(event_id,command_id,generation,tick,"
                    "actor_id,target_id,action,outcome,transferred,command,digest) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
                history.text(1,historical_id);
                history.text(2,std::to_string(ev.command_id));
                history.i64(3,static_cast<std::int64_t>(next));
                history.i64(4,static_cast<std::int64_t>(ev.tick));
                history.text(5,std::to_string(due->second.actor));
                history.text(6,std::to_string(due->second.target));
                history.i64(7,static_cast<int>(due->second.action));
                history.i64(8,static_cast<int>(ev.result));
                history.i64(9,ev.transferred);
                history.text(10,record);
                history.text(11,historical_digest(historical_id,next,ev,record));
                history.execute();
                if (ev.result == Outcome::Applied && ev.transferred > 0) {
                    const auto cmd = due_commands.find(ev.command_id);
                    if (cmd == due_commands.end() ||
                        (cmd->second.action != Action::Mine && cmd->second.action != Action::UnloadOre &&
                         cmd->second.action != Action::LoadOre && cmd->second.action != Action::SellOre &&
                         cmd->second.action != Action::RefineOre &&
                         cmd->second.action != Action::FabricateModule &&
                         cmd->second.action != Action::ConstructOutpost &&
                         cmd->second.action != Action::RefineMineral &&
                         cmd->second.action != Action::FabricateMaterialModule &&
                         !is_life_action(cmd->second.action) &&
                         cmd->second.action!=Action::ConstructFarm &&
                         cmd->second.action!=Action::TransferFarmStock &&
                         cmd->second.action!=Action::ConstructHabitat &&
                         cmd->second.action!=Action::TransferHabitatStock &&
                         cmd->second.action!=Action::ConstructUtility &&
                         cmd->second.action!=Action::RepairUtility &&
                         cmd->second.action!=Action::TransferUtilitySpares &&
                         cmd->second.action!=Action::ConstructCommunity &&
                         cmd->second.action!=Action::TransferCommunityStock &&
                         cmd->second.action!=Action::ConstructWaterworks &&
                         cmd->second.action!=Action::TransferWaterworksFeed &&
                         cmd->second.action!=Action::ConstructReclaimer &&
                         cmd->second.action!=Action::ConstructClinic &&
                         cmd->second.action!=Action::TransferClinicStock &&
                         cmd->second.action!=Action::ConstructWorkshop &&
                         cmd->second.action!=Action::TransferWorkshopFeed &&
                         cmd->second.action!=Action::TransferWorkshopOutput &&
                         cmd->second.action!=Action::ConstructWorkshopFinisher &&
                         cmd->second.action!=Action::TransferWorkshopFinalOutput))
                        throw std::runtime_error("unmatched resource transfer event");
                    Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,command_id,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?,?)");
                    ledger.i64(1, static_cast<std::int64_t>(next));
                    ledger.i64(2, static_cast<std::int64_t>(ev.tick));
                    ledger.text(3, std::to_string(ev.command_id));
                    if (cmd->second.action == Action::Mine) {
                        ledger.text(4, std::to_string(cmd->second.target));
                        ledger.text(5, std::to_string(cmd->second.actor));
                        ledger.i64(6, ev.transferred); ledger.text(7, "mining_transfer");
                    } else if(cmd->second.action==Action::RefineMineral ||
                              cmd->second.action==Action::FabricateMaterialModule){
                        const auto refine=cmd->second.action==Action::RefineMineral;
                        ledger.text(4,(refine?"mineral:":"good:")+std::to_string(cmd->second.actor)+":"+std::to_string(cmd->second.target));
                        ledger.text(5,(refine?"goods:":"modules:")+std::to_string(cmd->second.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,refine?"mineral_refining_kg":"material_fabrication_t");
                    } else if(cmd->second.action==Action::ConstructWorkshop ||
                              cmd->second.action==Action::TransferWorkshopFeed ||
                              cmd->second.action==Action::TransferWorkshopOutput){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructWorkshop;
                        const bool feed=c.action==Action::TransferWorkshopFeed;
                        if(ev.transferred!=(build?2:feed?std::abs(c.amount):20*c.amount))
                            throw std::runtime_error("workshop transfer amount mismatch");
                        ledger.text(4,build?"modules:"+std::to_string(c.actor):
                            feed?(c.amount>0?"good-aluminum:"+std::to_string(c.actor):
                                "workshop-feed:"+std::to_string(c.target)):
                                "workshop-output:"+std::to_string(c.target));
                        ledger.text(5,build?"workshop:"+std::to_string(c.target):
                            feed?(c.amount>0?"workshop-feed:"+std::to_string(c.target):
                                "good-aluminum:"+std::to_string(c.actor)):
                                "goods:"+std::to_string(c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"workshop_construction_t":
                            feed?"workshop_feed_transfer_kg":"workshop_output_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructWorkshopFinisher ||
                              cmd->second.action==Action::TransferWorkshopFinalOutput){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructWorkshopFinisher;
                        if(ev.transferred!=(build?2:20*c.amount))
                            throw std::runtime_error("workshop finisher transfer amount mismatch");
                        ledger.text(4,build?"modules:"+std::to_string(c.actor):
                            "workshop-final:"+std::to_string(c.target));
                        ledger.text(5,build?"workshop-finisher:"+std::to_string(c.target):
                            "goods:"+std::to_string(c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"workshop_finisher_construction_t":
                            "workshop_final_output_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructFarm ||
                              cmd->second.action==Action::TransferFarmStock){
                        const bool build=cmd->second.action==Action::ConstructFarm;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("farm transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"farm:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"farm:":c.amount>0?"farm:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"farm_construction_t":"farm_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructHabitat ||
                              cmd->second.action==Action::TransferHabitatStock){
                        const bool build=cmd->second.action==Action::ConstructHabitat;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("habitat transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"habitat:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"habitat:":c.amount>0?"habitat:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"habitat_construction_t":"habitat_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructUtility ||
                              cmd->second.action==Action::RepairUtility){
                        const bool build=cmd->second.action==Action::ConstructUtility;
                        if(ev.transferred!=(build?4:2))
                            throw std::runtime_error("utility transfer receipt mismatch");
                        ledger.text(4,"modules:"+std::to_string(cmd->second.actor));
                        ledger.text(5,"utility:"+std::to_string(cmd->second.target));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"utility_construction_t":"utility_repair_t");
                    } else if(cmd->second.action==Action::TransferUtilitySpares){
                        const auto& c=cmd->second;
                        if(ev.transferred!=2*std::abs(c.amount))
                            throw std::runtime_error("utility spare transfer receipt mismatch");
                        ledger.text(4,(c.amount>0?"modules:":"utility-spares:")+
                            std::to_string(c.amount>0?c.actor:c.target));
                        ledger.text(5,(c.amount>0?"utility-spares:":"modules:")+
                            std::to_string(c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,"utility_spare_transfer_t");
                    } else if(cmd->second.action==Action::ConstructCommunity ||
                              cmd->second.action==Action::TransferCommunityStock){
                        const bool build=cmd->second.action==Action::ConstructCommunity;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("community transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"community:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"community:":c.amount>0?"community:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"community_construction_t":"community_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructWaterworks ||
                              cmd->second.action==Action::TransferWaterworksFeed){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructWaterworks;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("waterworks transfer amount mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"good-water:":"waterworks-feed:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"waterworks:":c.amount>0?"waterworks-feed:":"good-water:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"waterworks_construction_t":"waterworks_feed_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructReclaimer){
                        if(ev.transferred!=4)throw std::runtime_error("reclaimer resource receipt mismatch");
                        ledger.text(4,"modules:"+std::to_string(cmd->second.actor));
                        ledger.text(5,"reclaimer:"+std::to_string(cmd->second.target));
                        ledger.i64(6,4);ledger.text(7,"reclaimer_construction_t");
                    } else if(cmd->second.action==Action::ConstructClinic ||
                              cmd->second.action==Action::TransferClinicStock){
                        const bool build=cmd->second.action==Action::ConstructClinic;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("clinic transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"clinic:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"clinic:":c.amount>0?"clinic:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"clinic_construction_t":"clinic_stock_transfer_kg");
                    } else if(is_life_action(cmd->second.action)){
                        if(ev.transferred != life_transfer_amount(cmd->second))
                            throw std::runtime_error("life-support transfer receipt mismatch");
                        ledger.text(4,life_ledger_source(cmd->second));
                        ledger.text(5,life_ledger_destination(cmd->second));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,"life_support_transform_kg");
                    } else if(cmd->second.action==Action::RefineOre ||
                              cmd->second.action==Action::FabricateModule ||
                              cmd->second.action==Action::ConstructOutpost){
                        // Store physical ore-equivalent movements, not module counts.
                        // State hash + invariant prove transformations conserve mass.
                        const auto source=std::to_string(cmd->second.actor);
                        const auto label=cmd->second.action==Action::RefineOre ? "refined" :
                                         cmd->second.action==Action::FabricateModule ? "modules" : "outpost";
                        ledger.text(4,source);
                        ledger.text(5,label+std::string(":")+source);
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,cmd->second.action==Action::RefineOre ? "refining_transfer" :
                                      cmd->second.action==Action::FabricateModule ? "fabrication_transfer" :
                                      "construction_embodiment");
                    } else {
                        const auto hangar="hangar:"+std::to_string(cmd->second.target)+
                            ":owner:"+std::to_string(cmd->second.issuer);
                        if (cmd->second.action == Action::UnloadOre) {
                            ledger.text(4,std::to_string(cmd->second.actor));
                            ledger.text(5,hangar);
                            ledger.text(7,"hangar_transfer");
                        } else if (cmd->second.action == Action::LoadOre) {
                            ledger.text(4,hangar);
                            ledger.text(5,std::to_string(cmd->second.actor));
                            ledger.text(7,"load_transfer");
                        } else {
                            ledger.text(4,hangar);
                            ledger.text(5,"market:"+std::to_string(cmd->second.target));
                            ledger.text(7,"sale_transfer");
                            const auto m=state.market(cmd->second.target);
                            if (!m || m->price_per_ore<=0 ||
                                ev.transferred>std::numeric_limits<std::int64_t>::max()/m->price_per_ore)
                                throw std::runtime_error("invalid sale credit calculation");
                            Statement credit(db_,"INSERT INTO credit_ledger(command_id,tick,station_id,owner_id,ore_units,unit_price,credit_amount) VALUES(?,?,?,?,?,?,?)");
                            credit.text(1,std::to_string(ev.command_id));
                            credit.i64(2,static_cast<std::int64_t>(ev.tick));
                            credit.text(3,std::to_string(cmd->second.target));
                            credit.text(4,std::to_string(cmd->second.issuer));
                            credit.i64(5,ev.transferred);
                            credit.i64(6,m->price_per_ore);
                            credit.i64(7,ev.transferred*m->price_per_ore);
                            credit.execute();
                        }
                        ledger.i64(6, ev.transferred);
                    }
                    ledger.execute();
                }
            }
            for(const auto& ev:new_events){
                if(ev.result!=Outcome::Applied)continue;
                const auto cmd=due_commands.find(ev.command_id);
                if(cmd==due_commands.end() || cmd->second.action!=Action::RequestJump)continue;
                const auto* ship=state.find(cmd->second.actor);
                const auto* gate=state.find(cmd->second.target);
                if(!ship || !gate || ship->type!=EntityType::Ship || gate->type!=EntityType::Stargate ||
                   ship->system_id!=gate->system_id || ship->pending_gate!=gate->id ||
                   ship->jump_state!=ShipJumpState::Jumping || ev.transferred!=0)
                    throw std::runtime_error("invalid jump departure state");
                Statement row(db_,"INSERT INTO jump_ledger(command_id,ship_id,gate_id,"
                                  "source_system,dest_system,cargo_at_departure,accepted_tick)"
                                  " VALUES(?,?,?,?,?,?,?)");
                row.text(1,std::to_string(cmd->second.id));
                row.text(2,std::to_string(ship->id));
                row.text(3,std::to_string(gate->id));
                row.i64(4,ship->system_id);row.i64(5,gate->gate_destination);
                row.i64(6,ship->cargo);row.i64(7,ev.tick);row.execute();
            }
            for (const auto& a : activity_delta) {
                if(a.kind==ActivityKind::JumpCompleted){
                    const auto* ship=state.find(a.actor);
                    const auto* gate=state.find(a.target);
                    if(!ship || !gate || ship->type!=EntityType::Ship || gate->type!=EntityType::Stargate ||
                        ship->system_id!=gate->gate_destination || ship->jump_state!=ShipJumpState::ArrivalCloak)
                        throw std::runtime_error("invalid jump arrival state");
                    Statement complete(db_,"UPDATE jump_ledger SET completed_tick=? "
                                           "WHERE ship_id=? AND gate_id=? AND completed_tick IS NULL "
                                           "AND dest_system=? AND cargo_at_departure=?");
                    complete.i64(1,a.tick);complete.text(2,std::to_string(a.actor));
                    complete.text(3,std::to_string(a.target));
                    complete.i64(4,ship->system_id);complete.i64(5,ship->cargo);
                    complete.execute();
                    if(sqlite3_changes(db_)!=1)
                        throw std::runtime_error("missing or duplicate jump completion");
                }
                if (a.kind != ActivityKind::AutoMiningTransfer) continue;
                if (a.result != Outcome::Applied || a.transferred <= 0 ||
                    a.tick != state.tick() || !state.has_command_id(a.start_command_id))
                    throw std::runtime_error("invalid automated mining transfer");
                Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?)");
                ledger.i64(1, static_cast<std::int64_t>(next));
                ledger.i64(2, static_cast<std::int64_t>(a.tick));
                ledger.text(3, std::to_string(a.target));
                ledger.text(4, std::to_string(a.actor));
                ledger.i64(5, a.transferred);
                ledger.text(6, "automated_mining_transfer");
                ledger.execute();
            }
            // Write physical incident rows in the SAME transaction as all
            // sources, receipts, state snapshot and operation journal.
            auto add_physical=[&](const PhysicalIncident& e){
                Statement add(db_,"INSERT INTO physical_incidents(incident_id,generation,tick,command_id,"
                    "kind,source,destination,amount,detail,digest) VALUES(?,?,?,?,?,?,?,?,?,?)");
                add.text(1,e.incident_id);add.i64(2,static_cast<std::int64_t>(e.generation));
                add.i64(3,static_cast<std::int64_t>(e.tick));add.text(4,std::to_string(e.command_id));
                add.i64(5,static_cast<int>(e.kind));add.text(6,e.source);add.text(7,e.destination);
                add.i64(8,e.amount);add.text(9,e.detail);add.text(10,physical_digest(e));add.execute();
            };
            {
                Statement r(db_,"SELECT id,tick,command_id,source_id,destination_id,amount,reason "
                                "FROM resource_ledger WHERE generation=? AND source_id IS NOT NULL ORDER BY id");
                r.i64(1,static_cast<std::int64_t>(next));
                while(r.row()){
                    PhysicalIncident e;
                    e.incident_id="cargo:"+std::to_string(sqlite3_column_int64(r.get(),0));
                    e.generation=next;e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),1));
                    if(sqlite3_column_type(r.get(),2)!=SQLITE_NULL)e.command_id=parse_u64(sqlite3_column_text(r.get(),2));
                    e.kind=PhysicalKind::CargoMovement;
                    e.source=sqlite_str(r.get(),3);e.destination=sqlite_str(r.get(),4);
                    e.amount=sqlite3_column_int64(r.get(),5);e.detail=sqlite_str(r.get(),6);
                    add_physical(e);
                }
            }
            for(const auto& ev:new_events){
                const auto cmd=due_commands.find(ev.command_id);
                if(ev.result!=Outcome::Applied || cmd==due_commands.end() ||
                   cmd->second.action!=Action::RequestJump)continue;
                Statement j(db_,"SELECT ship_id,gate_id,source_system,dest_system,cargo_at_departure "
                                "FROM jump_ledger WHERE command_id=?");
                j.text(1,std::to_string(ev.command_id));
                if(!j.row())throw std::runtime_error("history: missing jump start source");
                const auto ship=sqlite_str(j.get(),0),gate=sqlite_str(j.get(),1);
                add_physical(PhysicalIncident{"journey:start:"+std::to_string(ev.command_id),
                    next,ev.tick,ev.command_id,PhysicalKind::JourneyStarted,
                    std::to_string(sqlite3_column_int64(j.get(),2)),
                    std::to_string(sqlite3_column_int64(j.get(),3)),
                    sqlite3_column_int64(j.get(),4),ship+":"+gate});
            }
            for(const auto& a:activity_delta){
                if(a.kind==ActivityKind::JumpCompleted){
                    Statement j(db_,"SELECT command_id,ship_id,gate_id,source_system,dest_system,cargo_at_departure "
                                    "FROM jump_ledger WHERE ship_id=? AND gate_id=? AND completed_tick=?");
                    j.text(1,std::to_string(a.actor));j.text(2,std::to_string(a.target));
                    j.i64(3,static_cast<std::int64_t>(a.tick));
                    if(!j.row())throw std::runtime_error("history: missing jump completion source");
                    const auto id=parse_u64(sqlite3_column_text(j.get(),0));
                    Statement start(db_,"SELECT generation FROM historical_events WHERE command_id=?");
                    start.text(1,std::to_string(id));
                    Statement boundary(db_,"SELECT start_generation FROM physical_meta WHERE singleton=1");
                    if(!boundary.row())throw std::runtime_error("history: missing physical boundary");
                    const auto physical_start=sqlite3_column_int64(boundary.get(),0);
                    if(start.row() && sqlite3_column_int64(start.get(),0)>physical_start){
                        add_physical(PhysicalIncident{"journey:end:"+std::to_string(id),next,a.tick,id,
                            PhysicalKind::JourneyCompleted,std::to_string(sqlite3_column_int64(j.get(),3)),
                            std::to_string(sqlite3_column_int64(j.get(),4)),sqlite3_column_int64(j.get(),5),
                            sqlite_str(j.get(),1)+":"+sqlite_str(j.get(),2)});
                    }
                } else if(a.kind==ActivityKind::ShipDestroyed || a.kind==ActivityKind::NpcLost){
                    add_physical(PhysicalIncident{(a.kind==ActivityKind::ShipDestroyed?"wreck:":"death:")+
                            std::to_string(a.start_command_id)+
                            (a.kind==ActivityKind::NpcLost?":"+std::to_string(a.target):""),
                        next,a.tick,a.start_command_id,
                        a.kind==ActivityKind::ShipDestroyed?PhysicalKind::ShipDestroyed:PhysicalKind::NpcLost,
                        std::to_string(a.actor),std::to_string(a.target),1,
                        a.kind==ActivityKind::ShipDestroyed?"wreck_persists":"individual_identity_lost"});
                }
            }
            // Named crew execution can only be recorded in the same COMMIT
            // as a genuinely applied mining result and its cargo movement.
            // A valid assignment without a real effect remains *only* an
            // assignment; a ship's automatic work is never evidence by itself.
            for(const auto& ev:new_events){
                Statement assigned(db_,"SELECT id,npc_id,ship_id,rock_id FROM npc_mining_assignments WHERE command_id=?");
                assigned.text(1,std::to_string(ev.command_id));
                if(!assigned.row())continue;
                if(ev.result!=Outcome::Applied||ev.transferred<=0)continue;
                const auto npc=parse_u64(sqlite3_column_text(assigned.get(),1));
                const auto ship=parse_u64(sqlite3_column_text(assigned.get(),2));
                const auto rock=parse_u64(sqlite3_column_text(assigned.get(),3));
                const auto n=state.npc_records().find(npc);
                if(n==state.npc_records().end()||n->second.home_ship!=ship||
                   n->second.location!=ship||n->second.role!=NpcRole::Ship)continue;
                const auto cmd=due_commands.find(ev.command_id);
                if(cmd==due_commands.end()||cmd->second.action!=Action::Mine||
                   cmd->second.actor!=ship||cmd->second.target!=rock)
                    throw std::runtime_error("named mining command provenance invalid");
                Statement physical(db_,"SELECT incident_id,amount FROM physical_incidents "
                    "WHERE command_id=? AND generation=? AND tick=? AND kind=? AND source=? AND destination=?");
                physical.text(1,std::to_string(ev.command_id));physical.i64(2,next);
                physical.i64(3,ev.tick);physical.i64(4,static_cast<int>(PhysicalKind::CargoMovement));
                physical.text(5,std::to_string(rock));physical.text(6,std::to_string(ship));
                if(!physical.row()||sqlite3_column_int64(physical.get(),1)!=ev.transferred)
                    throw std::runtime_error("named mining result missing physical movement");
                const auto incident=sqlite_str(physical.get(),0);
                if(physical.row())throw std::runtime_error("multiple physical movements for one NPC work command");
                Statement meta(db_,"SELECT event_count,tip FROM npc_mining_execution_meta WHERE singleton=1");
                if(!meta.row())throw std::runtime_error("missing NPC execution metadata");
                const auto count=sqlite3_column_int64(meta.get(),0);
                if(count<0||count==std::numeric_limits<sqlite3_int64>::max())
                    throw std::runtime_error("NPC execution sequence exhausted");
                NpcMiningExecution e;
                e.id=count+1;e.assignment_id=sqlite3_column_int64(assigned.get(),0);
                e.command_id=ev.command_id;e.npc_id=npc;e.incident_id=incident;
                e.generation=next;e.tick=ev.tick;e.amount=ev.transferred;
                const auto tip=social_digest(sqlite_str(meta.get(),1),npc_mining_execution_body(e));
                Statement ins(db_,"INSERT INTO npc_mining_executions VALUES(?,?,?,?,?,?,?,?,?)");
                ins.i64(1,e.id);ins.i64(2,e.assignment_id);ins.text(3,std::to_string(e.command_id));
                ins.text(4,std::to_string(e.npc_id));ins.text(5,incident);
                ins.i64(6,e.generation);ins.i64(7,e.tick);ins.i64(8,e.amount);ins.text(9,tip);
                ins.execute();
                Statement update(db_,"UPDATE npc_mining_execution_meta SET event_count=?,tip=? WHERE singleton=1");
                update.i64(1,e.id);update.text(2,tip);update.execute();
                if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing NPC execution metadata update");
            }
            // B2a: named fabrication uses the already-applied ship command,
            // committed mass transfer, and physical incident as source facts.
            // There is no inferred credit from a submitted but failed job.
            for(const auto& ev:new_events){
                Statement assigned(db_,"SELECT id,npc_id,ship_id,issuer,action,material_code,quantity "
                    "FROM npc_fabrication_assignments WHERE command_id=?");
                assigned.text(1,std::to_string(ev.command_id));
                if(!assigned.row())continue;
                if(ev.result!=Outcome::Applied||ev.transferred<=0)continue;
                const auto npc=parse_u64(sqlite3_column_text(assigned.get(),1));
                const auto ship=parse_u64(sqlite3_column_text(assigned.get(),2));
                const auto issuer=parse_u64(sqlite3_column_text(assigned.get(),3));
                const auto action=static_cast<Action>(sqlite3_column_int(assigned.get(),4));
                const auto material=static_cast<EntityId>(sqlite3_column_int64(assigned.get(),5));
                const auto amount=sqlite3_column_int64(assigned.get(),6);
                const auto n=state.npc_records().find(npc);
                if(n==state.npc_records().end()||n->second.home_ship!=ship||
                   n->second.location!=ship||n->second.role!=NpcRole::Ship)continue;
                const auto cmd=due_commands.find(ev.command_id);
                if(cmd==due_commands.end()||cmd->second.action!=action||
                   cmd->second.actor!=ship||cmd->second.target!=material||
                   cmd->second.issuer!=issuer||cmd->second.amount!=amount||
                   ev.transferred!=2*amount)
                    throw std::runtime_error("named fabrication command provenance invalid");
                const auto source=action==Action::FabricateModule ? std::to_string(ship) :
                    "good:"+std::to_string(ship)+":"+std::to_string(material);
                const auto destination="modules:"+std::to_string(ship);
                Statement physical(db_,"SELECT incident_id,amount FROM physical_incidents "
                    "WHERE command_id=? AND generation=? AND tick=? AND kind=? AND source=? AND destination=?");
                physical.text(1,std::to_string(ev.command_id));physical.i64(2,next);
                physical.i64(3,ev.tick);physical.i64(4,static_cast<int>(PhysicalKind::CargoMovement));
                physical.text(5,source);physical.text(6,destination);
                if(!physical.row()||sqlite3_column_int64(physical.get(),1)!=ev.transferred)
                    throw std::runtime_error("named fabrication missing real movement");
                const auto incident=sqlite_str(physical.get(),0);
                if(physical.row())throw std::runtime_error("multiple movements for fabrication command");
                Statement meta(db_,"SELECT event_count,tip FROM npc_fabrication_execution_meta WHERE singleton=1");
                if(!meta.row())throw std::runtime_error("missing fabrication execution metadata");
                const auto count=sqlite3_column_int64(meta.get(),0);
                if(count<0||count==std::numeric_limits<sqlite3_int64>::max())
                    throw std::runtime_error("fabrication executor sequence exhausted");
                NpcFabricationExecution e;
                e.id=count+1;e.assignment_id=sqlite3_column_int64(assigned.get(),0);
                e.command_id=ev.command_id;e.npc_id=npc;e.incident_id=incident;
                e.generation=next;e.tick=ev.tick;e.transferred=ev.transferred;
                const auto tip=social_digest(sqlite_str(meta.get(),1),npc_fabrication_execution_body(e));
                Statement ins(db_,"INSERT INTO npc_fabrication_executions VALUES(?,?,?,?,?,?,?,?,?)");
                ins.i64(1,e.id);ins.i64(2,e.assignment_id);ins.text(3,std::to_string(e.command_id));
                ins.text(4,std::to_string(e.npc_id));ins.text(5,e.incident_id);
                ins.i64(6,e.generation);ins.i64(7,e.tick);ins.i64(8,e.transferred);
                ins.text(9,tip);ins.execute();
                Statement update(db_,"UPDATE npc_fabrication_execution_meta SET event_count=?,tip=? WHERE singleton=1");
                update.i64(1,e.id);update.text(2,tip);update.execute();
                if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing fabrication executor metadata update");
            }
            if (fail == FailPoint::AfterReceiptWrite)
                throw std::runtime_error("injected failure after receipt changes");
            Statement snapshot(db_, "INSERT INTO snapshots(generation,format,state,checksum,ore_total,credit_total) VALUES(?,?,?,?,?,?)");
            snapshot.i64(1, static_cast<std::int64_t>(next));
            snapshot.i64(2, 1); snapshot.blob(3, bytes);
            snapshot.text(4, std::to_string(state.state_hash()));
            snapshot.i64(5, origin); snapshot.i64(6, credit_origin); snapshot.execute();
            Statement op(db_, "INSERT INTO operation_log(generation,kind,command,state_hash,tick) VALUES(?,?,?,?,?)");
            op.i64(1, static_cast<std::int64_t>(next));
            op.i64(2, expected_generation == 0 ? 0 : accepted ? 1 : 2);
            if (accepted) op.text(3, command_record(*accepted));
            op.text(4, std::to_string(state.state_hash()));
            op.i64(5, static_cast<std::int64_t>(state.tick()));
            op.execute();
            // Keep three last immutable checkpoints for offline inspection; never silently recover
            // from a corrupt latest snapshot because its effects/receipts may be newer.
            Statement prune(db_, "DELETE FROM snapshots WHERE generation < ?");
            prune.i64(1, static_cast<std::int64_t>(next > 2 ? next - 2 : 1));
            prune.execute();
            if (fail == FailPoint::AfterSnapshotWrite)
                throw std::runtime_error("injected failure after snapshot write");
            run(db_, "COMMIT");
        } catch (...) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
    std::vector<HistoricalEvent> recent_history(std::size_t limit) const {
        if(limit==0 || limit>256) throw std::invalid_argument("history limit must be 1..256");
        Statement q(db_, "SELECT event_id,command_id,generation,tick,actor_id,target_id,action,"
                         "outcome,transferred FROM historical_events ORDER BY generation DESC, event_id DESC LIMIT ?");
        q.i64(1,static_cast<std::int64_t>(limit));
        std::vector<HistoricalEvent> entries;
        while(q.row()) {
            HistoricalEvent e;
            const auto str=[&](int col)->std::string {
                const auto* p=sqlite3_column_text(q.get(),col);
                if(!p)throw std::runtime_error("invalid history row");
                return reinterpret_cast<const char*>(p);
            };
            e.event_id=str(0);e.command_id=parse_u64(sqlite3_column_text(q.get(),1));
            e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),2));
            e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),3));
            e.actor=parse_u64(sqlite3_column_text(q.get(),4));
            e.target=parse_u64(sqlite3_column_text(q.get(),5));
            e.action=static_cast<Action>(sqlite3_column_int(q.get(),6));
            e.result=static_cast<Outcome>(sqlite3_column_int(q.get(),7));
            e.transferred=sqlite3_column_int64(q.get(),8);
            entries.push_back(e);
        }
        return entries;
    }
    std::vector<PhysicalIncident> recent_physical_incidents(std::size_t limit) const {
        if(limit==0 || limit>256)throw std::invalid_argument("physical history limit must be 1..256");
        Statement q(db_,"SELECT incident_id,generation,tick,command_id,kind,source,destination,amount,detail "
                        "FROM physical_incidents ORDER BY generation DESC,incident_id DESC LIMIT ?");
        q.i64(1,static_cast<std::int64_t>(limit));
        std::vector<PhysicalIncident> out;
        while(q.row()){
            PhysicalIncident e;
            e.incident_id=sqlite_str(q.get(),0);
            e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),1));
            e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),2));
            e.command_id=parse_u64(sqlite3_column_text(q.get(),3));
            e.kind=static_cast<PhysicalKind>(sqlite3_column_int(q.get(),4));
            e.source=sqlite_str(q.get(),5);e.destination=sqlite_str(q.get(),6);
            e.amount=sqlite3_column_int64(q.get(),7);e.detail=sqlite_str(q.get(),8);
            out.push_back(std::move(e));
        }
        return out;
    }
    bool record_local_observation(const Simulation& world,std::uint64_t gen,
                                  const std::string& incident_id,std::uint64_t npc_id) {
        if(incident_id.empty() || incident_id.size()>120 || !npc_id)return false;
        const auto it=world.npc_records().find(npc_id);
        if(it==world.npc_records().end())return false;
        const auto* location=world.find(it->second.location);
        if(!location)return false;
        Statement fact(db_,"SELECT generation,tick,kind,source,destination FROM physical_incidents WHERE incident_id=?");
        fact.text(1,incident_id);
        if(!fact.row() || sqlite3_column_int64(fact.get(),0)!=static_cast<sqlite3_int64>(gen) ||
           sqlite3_column_int64(fact.get(),1)!=static_cast<sqlite3_int64>(world.tick()))return false;
        Statement boundary(db_,"SELECT start_generation FROM knowledge_meta WHERE singleton=1");
        if(!boundary.row() || sqlite3_column_int64(boundary.get(),0)>=static_cast<sqlite3_int64>(gen))
            return false; // legacy incidents cannot create retrospective eyewitnesses
        const auto kind=static_cast<PhysicalKind>(sqlite3_column_int(fact.get(),2));
        const auto source=sqlite_str(fact.get(),3),destination=sqlite_str(fact.get(),4);
        bool eligible=false;
        if(kind==PhysicalKind::NpcLost) {
            eligible=(source==std::to_string(it->second.location));
        } else if(kind==PhysicalKind::CargoMovement) {
            eligible=(source==std::to_string(it->second.location) ||
                      destination==std::to_string(it->second.location));
        } else if(kind==PhysicalKind::ShipDestroyed) {
            // Nearby witness to an extant wreck, not its destroyed occupant.
            const auto* wreck=world.find(static_cast<EntityId>(std::stoull(destination)));
            if(wreck && wreck->type==EntityType::Wreck && wreck->system_id==location->system_id) {
                const auto dx=static_cast<long double>(wreck->position.x)-location->position.x;
                const auto dy=static_cast<long double>(wreck->position.y)-location->position.y;
                eligible=(dx*dx+dy*dy<=5000.0L*5000.0L);
            }
        }
        if(!eligible)return false;
        Statement duplicate(db_,"SELECT 1 FROM npc_knowledge WHERE incident_id=? AND recipient_id=? AND parent_id IS NULL");
        duplicate.text(1,incident_id);duplicate.text(2,std::to_string(npc_id));
        if(duplicate.row())return false;
        KnowledgeRecord e;
        e.incident_id=incident_id;e.speaker_npc=npc_id;e.recipient_npc=npc_id;
        e.fidelity=ClaimFidelity::LocalObservation;e.valence=0;e.confidence=100;
        e.generation=gen;e.tick=world.tick();e.account="local_proximity_observation";
        return append_knowledge(e,gen);
    }
    bool transmit_claim(const Simulation& world,std::uint64_t gen,
                        std::uint64_t parent_id,std::uint64_t listener,
                        ClaimFidelity fidelity,int valence,int confidence,const std::string& account) {
        if(!parent_id || !listener || fidelity==ClaimFidelity::LocalObservation)return false;
        Statement source(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
            "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge WHERE id=?");
        source.i64(1,static_cast<sqlite3_int64>(parent_id));
        if(!source.row())return false;
        const auto from=read_knowledge(source.get());
        const auto a=world.npc_records().find(from.recipient_npc),b=world.npc_records().find(listener);
        if(a==world.npc_records().end() || b==world.npc_records().end() ||
           from.recipient_npc==listener || a->second.location!=b->second.location)return false;
        KnowledgeRecord e;
        e.parent_id=parent_id;e.incident_id=from.incident_id;
        e.speaker_npc=from.recipient_npc;e.recipient_npc=listener;
        e.fidelity=fidelity;e.valence=valence;e.confidence=confidence;
        e.generation=gen;e.tick=world.tick();e.account=account;
        if(!valid_knowledge(e))return false;
        return append_knowledge(e,gen);
    }
    std::vector<KnowledgeRecord> recent_knowledge(std::size_t limit) const {
        if(limit==0 || limit>256)throw std::invalid_argument("knowledge limit must be 1..256");
        Statement entries(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
            "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge ORDER BY id DESC LIMIT ?");
        entries.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<KnowledgeRecord> out;
        while(entries.row())out.push_back(read_knowledge(entries.get()));
        return out;
    }
    std::vector<std::uint64_t> group_members_at(std::uint64_t group_id,std::uint64_t upto) const {
        Statement q(db_,"SELECT id,name,generation,members FROM social_groups WHERE id=?");
        q.i64(1,static_cast<sqlite3_int64>(group_id));
        if(!q.row())return {};
        auto active=read_group(q.get()).members;
        Statement changes(db_,"SELECT id,group_id,npc_id,sponsor_id,kind,epoch,prior_id,"
            "generation,tick FROM group_membership_events WHERE group_id=? AND id<=? ORDER BY id");
        changes.i64(1,static_cast<sqlite3_int64>(group_id));
        changes.i64(2,static_cast<sqlite3_int64>(upto));
        while(changes.row()){
            const auto e=read_membership(changes.get());
            const auto pos=std::lower_bound(active.begin(),active.end(),e.npc_id);
            if(e.kind==GroupMembershipKind::Join)active.insert(pos,e.npc_id);
            else if(pos!=active.end()&&*pos==e.npc_id)active.erase(pos);
        }
        return active;
    }
    std::uint64_t last_membership_seq() const {
        Statement meta(db_,"SELECT event_count FROM membership_meta WHERE singleton=1");
        if(!meta.row())throw std::runtime_error("missing membership metadata");
        return static_cast<std::uint64_t>(sqlite3_column_int64(meta.get(),0));
    }
    SocialGroup load_social_group(std::uint64_t group_id) const {
        Statement q(db_,"SELECT id,name,generation,members FROM social_groups WHERE id=?");
        q.i64(1,static_cast<sqlite3_int64>(group_id));
        if(!q.row())return {};
        auto g=read_group(q.get());g.members=group_members_at(group_id,last_membership_seq());return g;
    }
    std::vector<SocialGroup> recent_social_groups(std::size_t limit) const {
        if(limit==0||limit>128)throw std::invalid_argument("group limit must be 1..128");
        Statement q(db_,"SELECT id,name,generation,members FROM social_groups ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<SocialGroup> out;
        while(q.row()){
            auto g=read_group(q.get());g.members=group_members_at(g.id,last_membership_seq());
            out.push_back(std::move(g));
        }
        return out;
    }
    std::vector<std::uint64_t> active_social_group_members(std::uint64_t gid) const {
        if(!load_social_group(gid).id)return {};
        return group_members_at(gid,last_membership_seq());
    }
    std::vector<GroupMembershipEvent> recent_group_membership(std::size_t limit) const {
        if(!limit||limit>128)throw std::invalid_argument("membership limit must be 1..128");
        Statement q(db_,"SELECT id,group_id,npc_id,sponsor_id,kind,epoch,prior_id,"
            "generation,tick FROM group_membership_events ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<GroupMembershipEvent> out;while(q.row())out.push_back(read_membership(q.get()));
        return out;
    }
    bool change_group_membership(const Simulation& world,std::uint64_t gen,std::uint64_t gid,
                                 std::uint64_t npc,GroupMembershipKind kind,std::uint64_t sponsor){
        if(!gid||!npc||(kind!=GroupMembershipKind::Join&&kind!=GroupMembershipKind::Leave)||
           (kind==GroupMembershipKind::Leave&&sponsor))return false;
        if(world.npc_records().find(npc)==world.npc_records().end())return false;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale group membership generation");
            const auto g=load_social_group(gid);
            if(!g.id){run(db_,"ROLLBACK");return false;}
            const auto found=std::binary_search(g.members.begin(),g.members.end(),npc);
            if(kind==GroupMembershipKind::Join){
                if(found||g.members.size()>=24||!sponsor||sponsor==npc||
                   !std::binary_search(g.members.begin(),g.members.end(),sponsor)||
                   !co_located(world,npc,sponsor)){
                    run(db_,"ROLLBACK");return false;
                }
            }else if(!found){run(db_,"ROLLBACK");return false;}
            Statement prior(db_,"SELECT id,epoch FROM group_membership_events WHERE group_id=? ORDER BY id DESC LIMIT 1");
            prior.i64(1,static_cast<sqlite3_int64>(gid));
            const bool existed=prior.row();
            const auto previous_id=existed?sqlite3_column_int64(prior.get(),0):0;
            const auto previous_epoch=existed?sqlite3_column_int64(prior.get(),1):0;
            Statement meta(db_,"SELECT event_count,tip FROM membership_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing membership metadata");
            const auto count=sqlite3_column_int64(meta.get(),0);
            if(count<0||count==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("membership sequence exhausted");
            GroupMembershipEvent e;
            e.id=count+1;e.group_id=gid;e.npc_id=npc;e.sponsor_npc=sponsor;
            e.kind=kind;e.epoch=previous_epoch+1;e.prior_id=previous_id;
            e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(meta.get(),1),membership_body(e));
            Statement insert(db_,"INSERT INTO group_membership_events VALUES(?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,e.id);insert.i64(2,e.group_id);insert.text(3,std::to_string(e.npc_id));
            insert.text(4,std::to_string(e.sponsor_npc));insert.i64(5,static_cast<int>(e.kind));
            insert.i64(6,e.epoch);insert.i64(7,e.prior_id);insert.i64(8,e.generation);
            insert.i64(9,e.tick);insert.text(10,tip);insert.execute();
            Statement update(db_,"UPDATE membership_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,e.id);update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing membership metadata");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    std::vector<GroupView> recent_group_views(std::size_t limit) const {
        if(limit==0||limit>128)throw std::invalid_argument("view limit must be 1..128");
        Statement q(db_,"SELECT id,group_id,knowledge_id,stance,confidence,account,generation,tick "
                        "FROM group_views ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<GroupView> out;while(q.row())out.push_back(read_view(q.get()));return out;
    }
    std::uint64_t found_social_group(const Simulation& world,std::uint64_t gen,
                                     const std::string& name,std::vector<std::uint64_t> members){
        if(name.empty()||name.size()>64||name.find('\0')!=std::string::npos||
           members.size()<2||members.size()>24)return 0;
        std::sort(members.begin(),members.end());
        if(std::adjacent_find(members.begin(),members.end())!=members.end() || !members.front())return 0;
        EntityId home=0,location=0;
        for(const auto id:members){
            const auto it=world.npc_records().find(id);
            if(it==world.npc_records().end())return 0;
            if(home==0){home=it->second.home_ship;location=it->second.location;}
            if(it->second.home_ship!=home||it->second.location!=location)return 0;
        }
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=gen)throw std::runtime_error("stale social group generation");
            Statement m(db_,"SELECT groups_count,groups_tip FROM social_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("missing social metadata");
            const auto n=sqlite3_column_int64(m.get(),0);
            if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("social group sequence exhausted");
            SocialGroup g;g.id=static_cast<std::uint64_t>(n+1);g.generation=gen;
            g.name=name;g.members=std::move(members);
            const auto tip=social_digest(sqlite_str(m.get(),1),group_body(g));
            Statement add(db_,"INSERT INTO social_groups VALUES(?,?,?,?,?)");
            add.i64(1,static_cast<sqlite3_int64>(g.id));add.text(2,g.name);
            add.i64(3,static_cast<sqlite3_int64>(g.generation));
            add.text(4,social_members(g.members));add.text(5,tip);add.execute();
            Statement update(db_,"UPDATE social_meta SET groups_count=?,groups_tip=? WHERE singleton=1");
            update.i64(1,static_cast<sqlite3_int64>(g.id));update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("social metadata missing");
            run(db_,"COMMIT");return g.id;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    bool relay_group_claim(const Simulation& world,std::uint64_t gen,std::uint64_t group_id,
                           std::uint64_t parent_id,std::uint64_t listener,ClaimFidelity fidelity,
                           int valence,int confidence,const std::string& account){
        if(!group_id||!parent_id||!listener||fidelity==ClaimFidelity::LocalObservation)return false;
        const auto g=load_social_group(group_id);if(!g.id)return false;
        Statement source(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
            "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge WHERE id=?");
        source.i64(1,static_cast<sqlite3_int64>(parent_id));
        if(!source.row())return false;
        const auto from=read_knowledge(source.get());
        if(!std::binary_search(g.members.begin(),g.members.end(),from.recipient_npc)||
           !std::binary_search(g.members.begin(),g.members.end(),listener)||
           from.recipient_npc==listener)return false;
        const auto a=world.npc_records().find(from.recipient_npc);
        const auto b=world.npc_records().find(listener);
        if(a==world.npc_records().end()||b==world.npc_records().end()||
           a->second.location!=b->second.location)return false;
        KnowledgeRecord e;
        e.parent_id=parent_id;e.incident_id=from.incident_id;
        e.speaker_npc=from.recipient_npc;e.recipient_npc=listener;
        e.fidelity=fidelity;e.valence=valence;e.confidence=confidence;
        e.generation=gen;e.tick=world.tick();e.account=account;
        return append_knowledge(e,gen,group_id);
    }
    bool record_group_view(const Simulation& world,std::uint64_t gen,std::uint64_t group_id,
                           std::uint64_t knowledge_id,GroupStance stance,int confidence,
                           const std::string& account){
        if(!group_id||!knowledge_id||static_cast<int>(stance)<1||static_cast<int>(stance)>3||
           confidence<0||confidence>100||account.empty()||account.size()>240||
           account.find('\0')!=std::string::npos)return false;
        const auto g=load_social_group(group_id);if(!g.id)return false;
        Statement relay(db_,"SELECT k.generation,k.tick,k.speaker_id,k.recipient_id FROM group_relays r "
            "JOIN npc_knowledge k ON k.id=r.knowledge_id "
            "WHERE r.group_id=? AND r.knowledge_id=?");
        relay.i64(1,static_cast<sqlite3_int64>(group_id));
        relay.i64(2,static_cast<sqlite3_int64>(knowledge_id));
        if(!relay.row()||sqlite3_column_int64(relay.get(),0)!=static_cast<sqlite3_int64>(gen)||
           sqlite3_column_int64(relay.get(),1)!=static_cast<sqlite3_int64>(world.tick()))return false;
        const auto speaker=parse_u64(sqlite3_column_text(relay.get(),2));
        const auto recipient=parse_u64(sqlite3_column_text(relay.get(),3));
        if(!std::binary_search(g.members.begin(),g.members.end(),speaker)||
           !std::binary_search(g.members.begin(),g.members.end(),recipient))return false;
        const auto a=world.npc_records().find(speaker),b=world.npc_records().find(recipient);
        if(a==world.npc_records().end()||b==world.npc_records().end()||
           a->second.location!=b->second.location)return false;
        // A view is formed by an actual member meeting; the already-recorded
        // witnessed relay is immutable. Later physical separation is fine.
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=gen)throw std::runtime_error("stale group view generation");
            // Membership is mutable. Recheck under the write lock to prevent
            // a view from being attributed to speakers who just left.
            const auto active=group_members_at(group_id,last_membership_seq());
            if(!std::binary_search(active.begin(),active.end(),speaker)||
               !std::binary_search(active.begin(),active.end(),recipient)){
                run(db_,"ROLLBACK");return false;
            }
            Statement m(db_,"SELECT views_count,views_tip FROM social_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("missing social view metadata");
            const auto n=sqlite3_column_int64(m.get(),0);
            if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("social view sequence exhausted");
            GroupView r;r.id=static_cast<std::uint64_t>(n+1);r.group_id=group_id;
            r.knowledge_id=knowledge_id;r.stance=stance;r.confidence=confidence;
            r.account=account;r.generation=gen;r.tick=world.tick();
            const auto tip=social_digest(sqlite_str(m.get(),1),view_body(r));
            Statement add(db_,"INSERT INTO group_views VALUES(?,?,?,?,?,?,?,?,?)");
            add.i64(1,static_cast<sqlite3_int64>(r.id));add.i64(2,static_cast<sqlite3_int64>(r.group_id));
            add.i64(3,static_cast<sqlite3_int64>(r.knowledge_id));add.i64(4,static_cast<int>(r.stance));
            add.i64(5,r.confidence);add.text(6,r.account);
            add.i64(7,static_cast<sqlite3_int64>(r.generation));add.i64(8,static_cast<sqlite3_int64>(r.tick));
            add.text(9,tip);add.execute();
            Statement up(db_,"UPDATE social_meta SET views_count=?,views_tip=? WHERE singleton=1");
            up.i64(1,static_cast<sqlite3_int64>(r.id));up.text(2,tip);up.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("social view metadata missing");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    std::vector<SocialTieEvent> recent_social_ties(std::size_t limit) const {
        if(limit==0||limit>128)throw std::invalid_argument("tie limit must be 1..128");
        Statement q(db_,"SELECT id,observer,source,prior_id,knowledge_id,kind,delta,trust,"
            "generation,tick FROM social_tie_events ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<SocialTieEvent> out;
        while(q.row())out.push_back(read_tie(q.get()));
        return out;
    }
    std::vector<SocialEncounter> recent_social_encounters(std::size_t limit) const {
        if(!limit||limit>128)throw std::invalid_argument("encounter limit must be 1..128");
        Statement q(db_,"SELECT id,cycle_id,npc_low,npc_high,generation,tick,slot,"
            "prior_id,ordinal FROM social_encounters ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<SocialEncounter> out;
        while(q.row())out.push_back(read_encounter(q.get()));
        return out;
    }
    std::vector<SocialInteractionEvent> recent_social_interactions(std::size_t limit) const {
        if(!limit||limit>128)throw std::invalid_argument("interaction limit must be 1..128");
        Statement q(db_,"SELECT id,encounter_id,observer,actor,kind,delta,rapport,"
            "prior_id,generation,tick FROM social_interactions ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<SocialInteractionEvent> result;
        while(q.row())result.push_back(read_interaction(q.get()));
        return result;
    }
    int directed_rapport(std::uint64_t observer,std::uint64_t actor) const {
        if(!observer||!actor||observer==actor)return 0;
        Statement q(db_,"SELECT rapport FROM social_interactions WHERE observer=? AND actor=? "
            "ORDER BY id DESC LIMIT 1");
        q.text(1,std::to_string(observer));q.text(2,std::to_string(actor));
        return q.row()?sqlite3_column_int(q.get(),0):0;
    }
    std::vector<GroupAttributionEvent> recent_group_attributions(std::size_t limit) const{
        if(!limit||limit>128)throw std::invalid_argument("group attribution limit must be 1..128");
        Statement q(db_,"SELECT id,interaction_id,observer_group,actor_group,observer,actor,"
            "membership_seq,stance,delta,feeling,prior_id,generation,tick "
            "FROM group_attributions ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<GroupAttributionEvent> out;
        while(q.row())out.push_back(read_attribution(q.get()));
        return out;
    }
    int individual_group_feeling(std::uint64_t observer,std::uint64_t observer_group,
                                 std::uint64_t actor_group) const{
        if(!observer||!observer_group||!actor_group||observer_group==actor_group)return 0;
        Statement q(db_,"SELECT feeling FROM group_attributions WHERE observer=? "
            "AND observer_group=? AND actor_group=? ORDER BY id DESC LIMIT 1");
        q.text(1,std::to_string(observer));q.i64(2,static_cast<sqlite3_int64>(observer_group));
        q.i64(3,static_cast<sqlite3_int64>(actor_group));
        return q.row()?sqlite3_column_int(q.get(),0):0;
    }
    GroupRelationSummary group_relation_summary(std::uint64_t from,std::uint64_t toward) const{
        GroupRelationSummary r;r.observer_group=from;r.actor_group=toward;
        if(!from||!toward||from==toward||!load_social_group(from).id||
           !load_social_group(toward).id)return r;
        const auto active=group_members_at(from,last_membership_seq());
        r.current_members=active.size();
        int total=0;
        Statement q(db_,"SELECT feeling FROM group_attributions WHERE observer=? "
            "AND observer_group=? AND actor_group=? ORDER BY id DESC LIMIT 1");
        for(auto npc:active){
            sqlite3_reset(q.get());sqlite3_clear_bindings(q.get());
            q.text(1,std::to_string(npc));q.i64(2,static_cast<sqlite3_int64>(from));
            q.i64(3,static_cast<sqlite3_int64>(toward));
            if(q.row()){
                const int opinion=sqlite3_column_int(q.get(),0);
                ++r.informed_members;total+=opinion;
                if(opinion>0)++r.favorable;
                else if(opinion<0)++r.unfavorable;
                else ++r.neutral;
            }
        }
        if(r.informed_members)r.mean_feeling=total/static_cast<int>(r.informed_members);
        return r;
    }
    bool attribute_interaction_to_group(const Simulation& world,std::uint64_t gen,
             std::uint64_t interaction_id,std::uint64_t from_group,
             std::uint64_t toward_group,GroupAttributionStance stance){
        if(!interaction_id||!from_group||!toward_group||from_group==toward_group||
           (stance!=GroupAttributionStance::Appreciative&&
            stance!=GroupAttributionStance::Resentful&&
            stance!=GroupAttributionStance::Ambivalent))return false;
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale group attribution generation");
            Statement source(db_,"SELECT encounter_id,observer,actor,kind,delta,rapport,"
                "prior_id,generation,tick FROM social_interactions WHERE id=?");
            source.i64(1,static_cast<sqlite3_int64>(interaction_id));
            if(!source.row()||
               sqlite3_column_int64(source.get(),7)!=static_cast<sqlite3_int64>(gen)||
               sqlite3_column_int64(source.get(),8)!=static_cast<sqlite3_int64>(world.tick())){
                run(db_,"ROLLBACK");return false;
            }
            const auto observer=parse_u64(sqlite3_column_text(source.get(),1));
            const auto actor=parse_u64(sqlite3_column_text(source.get(),2));
            const auto observers=load_social_group(from_group);
            const auto actors=load_social_group(toward_group);
            const auto epoch=last_membership_seq();
            if(!observers.id||!actors.id||
               !std::binary_search(observers.members.begin(),observers.members.end(),observer)||
               !std::binary_search(actors.members.begin(),actors.members.end(),actor)||
               !co_located(world,observer,actor)){
                run(db_,"ROLLBACK");return false;
            }
            Statement used(db_,"SELECT id FROM group_attributions WHERE interaction_id=? "
                "AND observer_group=? AND actor_group=?");
            used.i64(1,static_cast<sqlite3_int64>(interaction_id));
            used.i64(2,static_cast<sqlite3_int64>(from_group));
            used.i64(3,static_cast<sqlite3_int64>(toward_group));
            if(used.row()){run(db_,"ROLLBACK");return false;}
            Statement previous(db_,"SELECT id,feeling FROM group_attributions WHERE observer=? "
                "AND observer_group=? AND actor_group=? ORDER BY id DESC LIMIT 1");
            previous.text(1,std::to_string(observer));
            previous.i64(2,static_cast<sqlite3_int64>(from_group));
            previous.i64(3,static_cast<sqlite3_int64>(toward_group));
            const bool existed=previous.row();
            const auto prior=existed?sqlite3_column_int64(previous.get(),0):0;
            const int prior_feeling=existed?sqlite3_column_int(previous.get(),1):0;
            Statement m(db_,"SELECT event_count,tip FROM attribution_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("missing group attribution metadata");
            const auto count=sqlite3_column_int64(m.get(),0);
            if(count<0||count==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("group attribution sequence exhausted");
            GroupAttributionEvent e;
            e.id=count+1;e.interaction_id=interaction_id;
            e.observer_group=from_group;e.actor_group=toward_group;
            e.observer_npc=observer;e.actor_npc=actor;e.membership_seq=epoch;
            e.stance=stance;e.delta=attribution_delta(stance);
            e.feeling=std::clamp(prior_feeling+e.delta,-100,100);
            e.prior_id=prior;e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(m.get(),1),attribution_body(e));
            Statement add(db_,"INSERT INTO group_attributions VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            add.i64(1,e.id);add.i64(2,e.interaction_id);add.i64(3,e.observer_group);
            add.i64(4,e.actor_group);add.text(5,std::to_string(e.observer_npc));
            add.text(6,std::to_string(e.actor_npc));add.i64(7,e.membership_seq);
            add.i64(8,static_cast<int>(e.stance));add.i64(9,e.delta);
            add.i64(10,e.feeling);add.i64(11,e.prior_id);add.i64(12,e.generation);
            add.i64(13,e.tick);add.text(14,tip);add.execute();
            Statement update(db_,"UPDATE attribution_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,e.id);update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing group attribution metadata");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    std::vector<PhysicalAppraisalEvent> recent_physical_appraisals(std::size_t limit) const {
        if(!limit||limit>256)throw std::invalid_argument("physical appraisal limit 1..256");
        Statement rows(db_,"SELECT id,observation_id,incident_id,observer,context_group,"
            "membership_seq,stance,confidence,generation,tick FROM physical_appraisals "
            "ORDER BY id DESC LIMIT ?");
        rows.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<PhysicalAppraisalEvent> out;
        while(rows.row())out.push_back(read_physical_appraisal(rows.get()));
        return out;
    }
    bool appraise_witnessed_incident(const Simulation& world,std::uint64_t gen,
             std::uint64_t observation_id,std::uint64_t context_group,
             PhysicalAppraisalStance stance,int confidence){
        if(!observation_id||confidence<0||confidence>100||
           static_cast<int>(stance)<1||static_cast<int>(stance)>3)return false;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale physical appraisal generation");
            Statement source(db_,"SELECT incident_id,COALESCE(parent_id,0),recipient_id,"
                "fidelity,generation,tick FROM npc_knowledge WHERE id=?");
            source.i64(1,static_cast<sqlite3_int64>(observation_id));
            if(!source.row()||sqlite3_column_int64(source.get(),1)!=0||
               sqlite3_column_int(source.get(),3)!=static_cast<int>(ClaimFidelity::LocalObservation)||
               sqlite3_column_int64(source.get(),4)!=static_cast<sqlite3_int64>(gen)||
               sqlite3_column_int64(source.get(),5)!=static_cast<sqlite3_int64>(world.tick())){
                run(db_,"ROLLBACK");return false;
            }
            const auto incident=sqlite_str(source.get(),0);
            const auto observer=parse_u64(sqlite3_column_text(source.get(),2));
            if(world.npc_records().find(observer)==world.npc_records().end()){
                run(db_,"ROLLBACK");return false;
            }
            Statement physical(db_,"SELECT generation,tick FROM physical_incidents WHERE incident_id=?");
            physical.text(1,incident);
            if(!physical.row()||
               sqlite3_column_int64(physical.get(),0)!=static_cast<sqlite3_int64>(gen)||
               sqlite3_column_int64(physical.get(),1)!=static_cast<sqlite3_int64>(world.tick())){
                run(db_,"ROLLBACK");return false;
            }
            std::uint64_t epoch=0;
            if(context_group){
                const auto group=load_social_group(context_group);
                if(!group.id||!std::binary_search(group.members.begin(),group.members.end(),observer)){
                    run(db_,"ROLLBACK");return false;
                }
                epoch=last_membership_seq();
            }
            Statement previous(db_,"SELECT id FROM physical_appraisals WHERE observation_id=? AND context_group=?");
            previous.i64(1,static_cast<sqlite3_int64>(observation_id));
            previous.i64(2,static_cast<sqlite3_int64>(context_group));
            if(previous.row()){run(db_,"ROLLBACK");return false;}
            Statement meta(db_,"SELECT event_count,tip FROM physical_appraisal_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing appraisal metadata");
            const auto n=sqlite3_column_int64(meta.get(),0);
            if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("appraisal sequence exhausted");
            PhysicalAppraisalEvent e;
            e.id=n+1;e.observation_id=observation_id;e.incident_id=incident;
            e.observer_npc=observer;e.context_group=context_group;e.membership_seq=epoch;
            e.stance=stance;e.confidence=confidence;e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(meta.get(),1),physical_appraisal_body(e));
            Statement insert(db_,"INSERT INTO physical_appraisals VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,e.id);insert.i64(2,e.observation_id);insert.text(3,e.incident_id);
            insert.text(4,std::to_string(observer));insert.i64(5,e.context_group);
            insert.i64(6,e.membership_seq);insert.i64(7,static_cast<int>(stance));
            insert.i64(8,confidence);insert.i64(9,gen);insert.i64(10,world.tick());
            insert.text(11,tip);insert.execute();
            Statement update(db_,"UPDATE physical_appraisal_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,e.id);update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing appraisal metadata update");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    bool npc_busy_this_tick(std::uint64_t npc_id,std::uint64_t tick) const {
        for(const auto* table:{"npc_mining_assignments","npc_fabrication_assignments"}){
            Statement q(db_,(std::string("SELECT id FROM ")+table+" WHERE npc_id=? AND tick=?").c_str());
            q.text(1,std::to_string(npc_id));q.i64(2,static_cast<sqlite3_int64>(tick));
            if(q.row())return true;
        }
        return false;
    }
    std::uint64_t next_npc_fabrication_assignment_id() const {
        Statement m(db_,"SELECT event_count FROM npc_fabrication_assignment_meta WHERE singleton=1");
        if(!m.row())throw std::runtime_error("missing NPC fabrication assignment sequence");
        const auto n=sqlite3_column_int64(m.get(),0);
        if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
            throw std::runtime_error("fabrication assignment sequence exhausted");
        return static_cast<std::uint64_t>(n+1);
    }
    std::vector<NpcFabricationAssignment> recent_npc_fabrication_assignments(std::size_t limit) const {
        if(!limit||limit>256)throw std::invalid_argument("fabrication assignment limit 1..256");
        Statement q(db_,"SELECT id,command_id,npc_id,ship_id,issuer,action,material_code,quantity,generation,tick "
            "FROM npc_fabrication_assignments ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<NpcFabricationAssignment> out;
        while(q.row())out.push_back(read_npc_fabrication_assignment(q.get()));
        return out;
    }
    std::vector<NpcFabricationExecution> recent_npc_fabrication_executions(std::size_t limit) const {
        if(!limit||limit>256)throw std::invalid_argument("fabrication execution limit 1..256");
        Statement q(db_,"SELECT id,assignment_id,command_id,npc_id,incident_id,generation,tick,transferred "
            "FROM npc_fabrication_executions ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<NpcFabricationExecution> out;
        while(q.row())out.push_back(read_npc_fabrication_execution(q.get()));
        return out;
    }
    bool verified_npc_fabrication_execution(std::uint64_t command_id,std::uint64_t npc_id) const {
        if(!command_id||!npc_id)return false;
        Statement q(db_,"SELECT id FROM npc_fabrication_executions WHERE command_id=? AND npc_id=?");
        q.text(1,std::to_string(command_id));q.text(2,std::to_string(npc_id));return q.row();
    }
    std::uint64_t next_npc_mining_assignment_id() const {
        Statement m(db_,"SELECT event_count FROM npc_mining_assignment_meta WHERE singleton=1");
        if(!m.row())throw std::runtime_error("missing NPC mining assignment counter");
        const auto n=sqlite3_column_int64(m.get(),0);
        if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
            throw std::runtime_error("NPC assignment counter overflow");
        return static_cast<std::uint64_t>(n+1);
    }
    std::vector<NpcMiningAssignment> recent_npc_mining_assignments(std::size_t limit) const {
        if(!limit||limit>256)throw std::invalid_argument("NPC assignment limit 1..256");
        Statement q(db_,"SELECT id,command_id,npc_id,ship_id,rock_id,generation,tick "
                        "FROM npc_mining_assignments ORDER BY id DESC LIMIT ?");
        q.i64(1,limit);std::vector<NpcMiningAssignment> out;
        while(q.row())out.push_back(read_npc_mining_assignment(q.get()));
        return out;
    }
    std::vector<NpcMiningExecution> recent_npc_mining_executions(std::size_t limit) const {
        if(!limit||limit>256)throw std::invalid_argument("NPC execution limit 1..256");
        Statement q(db_,"SELECT id,assignment_id,command_id,npc_id,incident_id,generation,tick,amount "
                        "FROM npc_mining_executions ORDER BY id DESC LIMIT ?");
        q.i64(1,limit);std::vector<NpcMiningExecution> out;
        while(q.row())out.push_back(read_npc_mining_execution(q.get()));
        return out;
    }
    bool verified_npc_mining_execution(std::uint64_t command_id,std::uint64_t npc_id) const {
        if(!command_id||!npc_id)return false;
        Statement q(db_,"SELECT id FROM npc_mining_executions WHERE command_id=? AND npc_id=?");
        q.text(1,std::to_string(command_id));q.text(2,std::to_string(npc_id));return q.row();
    }
    std::vector<IncidentRoleReceipt> recent_incident_roles(std::size_t limit) const{
        if(!limit||limit>256)throw std::invalid_argument("incident role limit 1..256");
        Statement q(db_,"SELECT id,incident_id,npc_id,role,observation_id,generation,tick "
            "FROM incident_role_receipts ORDER BY id DESC LIMIT ?");
        q.i64(1,static_cast<sqlite3_int64>(limit));
        std::vector<IncidentRoleReceipt> out;
        while(q.row())out.push_back(read_incident_role(q.get()));
        return out;
    }
    bool is_verified_incident_role(const std::string& incident_id,std::uint64_t npc,
                                  VerifiedNpcRole role) const{
        if(!npc||(role!=VerifiedNpcRole::Victim&&role!=VerifiedNpcRole::FirsthandObserver))return false;
        Statement q(db_,"SELECT id FROM incident_role_receipts "
            "WHERE incident_id=? AND npc_id=? AND role=?");
        q.text(1,incident_id);q.text(2,std::to_string(npc));q.i64(3,static_cast<int>(role));
        return q.row();
    }
    bool attest_incident_role(const Simulation& world,std::uint64_t gen,
            const std::string& incident_id,std::uint64_t npc,
            VerifiedNpcRole role,std::uint64_t observation_id){
        if(incident_id.empty()||!npc||
           (role!=VerifiedNpcRole::Victim&&role!=VerifiedNpcRole::FirsthandObserver)||
           ((role==VerifiedNpcRole::Victim)!=(observation_id==0)))return false;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale NPC role generation");
            Statement physical(db_,"SELECT generation,tick,kind,destination FROM physical_incidents WHERE incident_id=?");
            physical.text(1,incident_id);
            if(!physical.row()||
               sqlite3_column_int64(physical.get(),0)!=static_cast<sqlite3_int64>(gen)||
               sqlite3_column_int64(physical.get(),1)!=static_cast<sqlite3_int64>(world.tick())){
                run(db_,"ROLLBACK");return false;
            }
            if(role==VerifiedNpcRole::Victim){
                if(sqlite3_column_int(physical.get(),2)!=static_cast<int>(PhysicalKind::NpcLost)||
                   sqlite_str(physical.get(),3)!=std::to_string(npc)){
                    run(db_,"ROLLBACK");return false;
                }
            }else{
                if(world.npc_records().find(npc)==world.npc_records().end()){
                    run(db_,"ROLLBACK");return false;
                }
                Statement claim(db_,"SELECT incident_id,COALESCE(parent_id,0),recipient_id,"
                    "fidelity,generation,tick FROM npc_knowledge WHERE id=?");
                claim.i64(1,static_cast<sqlite3_int64>(observation_id));
                if(!claim.row()||sqlite_str(claim.get(),0)!=incident_id||
                   sqlite3_column_int64(claim.get(),1)!=0||
                   parse_u64(sqlite3_column_text(claim.get(),2))!=npc||
                   sqlite3_column_int(claim.get(),3)!=static_cast<int>(ClaimFidelity::LocalObservation)||
                   sqlite3_column_int64(claim.get(),4)!=static_cast<sqlite3_int64>(gen)||
                   sqlite3_column_int64(claim.get(),5)!=static_cast<sqlite3_int64>(world.tick())){
                    run(db_,"ROLLBACK");return false;
                }
            }
            if(is_verified_incident_role(incident_id,npc,role)){
                run(db_,"ROLLBACK");return false;
            }
            Statement meta(db_,"SELECT event_count,tip FROM incident_role_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("SQLite: missing role metadata");
            const auto count=sqlite3_column_int64(meta.get(),0);
            if(count<0||count==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("SQLite: incident role count overflow");
            IncidentRoleReceipt e;
            e.id=count+1;e.incident_id=incident_id;e.npc_id=npc;
            e.role=role;e.observation_id=observation_id;e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(meta.get(),1),incident_role_body(e));
            Statement ins(db_,"INSERT INTO incident_role_receipts VALUES(?,?,?,?,?,?,?,?)");
            ins.i64(1,e.id);ins.text(2,incident_id);ins.text(3,std::to_string(npc));
            ins.i64(4,static_cast<int>(role));ins.i64(5,observation_id);
            ins.i64(6,gen);ins.i64(7,world.tick());ins.text(8,tip);ins.execute();
            Statement update(db_,"UPDATE incident_role_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,e.id);update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("SQLite: missing role metadata update");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    bool record_social_interaction(const Simulation& world,std::uint64_t gen,
            std::uint64_t encounter_id,std::uint64_t observer,std::uint64_t actor,
            SocialInteractionKind kind){
        if(!encounter_id||!observer||!actor||observer==actor||
           (kind!=SocialInteractionKind::Assistance&&kind!=SocialInteractionKind::Harm))return false;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale interaction world generation");
            if(!co_located(world,observer,actor)){
                run(db_,"ROLLBACK");return false;
            }
            Statement contact(db_,"SELECT npc_low,npc_high,generation,tick FROM social_encounters WHERE id=?");
            contact.i64(1,static_cast<sqlite3_int64>(encounter_id));
            if(!contact.row()||
               parse_u64(sqlite3_column_text(contact.get(),0))!=std::min(observer,actor)||
               parse_u64(sqlite3_column_text(contact.get(),1))!=std::max(observer,actor)||
               sqlite3_column_int64(contact.get(),2)>static_cast<sqlite3_int64>(gen)||
               sqlite3_column_int64(contact.get(),3)!=static_cast<sqlite3_int64>(world.tick())){
                run(db_,"ROLLBACK");return false;
            }
            Statement used(db_,"SELECT id FROM social_interactions WHERE encounter_id=? AND observer=?");
            used.i64(1,static_cast<sqlite3_int64>(encounter_id));
            used.text(2,std::to_string(observer));
            if(used.row()) {run(db_,"ROLLBACK");return false;}
            Statement previous(db_,"SELECT id,rapport FROM social_interactions WHERE observer=? AND actor=? "
                "ORDER BY id DESC LIMIT 1");
            previous.text(1,std::to_string(observer));previous.text(2,std::to_string(actor));
            const bool existed=previous.row();
            const auto prior=existed?sqlite3_column_int64(previous.get(),0):0;
            const auto prior_rapport=existed?sqlite3_column_int(previous.get(),1):0;
            Statement meta(db_,"SELECT event_count,tip FROM interaction_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing interaction metadata");
            const auto n=sqlite3_column_int64(meta.get(),0);
            if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("interaction sequence exhausted");
            SocialInteractionEvent e;
            e.id=static_cast<std::uint64_t>(n+1);e.encounter_id=encounter_id;
            e.observer_npc=observer;e.actor_npc=actor;e.prior_id=prior;
            e.kind=kind;e.delta=kind==SocialInteractionKind::Assistance?8:-12;
            e.rapport=std::clamp(prior_rapport+e.delta,-100,100);
            e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(meta.get(),1),interaction_body(e));
            Statement add(db_,"INSERT INTO social_interactions VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            add.i64(1,e.id);add.i64(2,e.encounter_id);
            add.text(3,std::to_string(observer));add.text(4,std::to_string(actor));
            add.i64(5,static_cast<int>(kind));add.i64(6,e.delta);add.i64(7,e.rapport);
            add.i64(8,e.prior_id);add.i64(9,e.generation);add.i64(10,e.tick);
            add.text(11,tip);add.execute();
            Statement update(db_,"UPDATE interaction_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,e.id);update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing interaction metadata");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    std::size_t schedule_social_encounters(const Simulation& world,std::uint64_t gen,
                                          std::size_t scan_budget,std::size_t contact_budget){
        if(!scan_budget||scan_budget>64||!contact_budget||contact_budget>8)
            throw std::invalid_argument("social encounter budgets exceed 1..64 / 1..8");
        const auto n=world.npc_records().size();
        if(n<2)return 0;
        if(n>1024)throw std::invalid_argument("bounded encounter pilot allows <=1024 NPCs");
        std::vector<std::uint64_t> ids;
        ids.reserve(n);
        for(const auto& kv:world.npc_records())ids.push_back(kv.first);
        const auto total=static_cast<std::uint64_t>(n)*(n-1)/2;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale encounter world generation");
            Statement meta(db_,"SELECT scan_count,scan_tip,contact_count,contact_tip,cursor "
                "FROM encounter_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing encounter metadata");
            const auto scans=sqlite3_column_int64(meta.get(),0),
                 prior_contacts=sqlite3_column_int64(meta.get(),2),prior_cursor=sqlite3_column_int64(meta.get(),4);
            if(scans<0||prior_contacts<0||prior_cursor<0||
               scans==std::numeric_limits<sqlite3_int64>::max()||
               prior_contacts>std::numeric_limits<sqlite3_int64>::max()-8)
                throw std::runtime_error("encounter sequence exhausted");
            const auto scan_tip=sqlite_str(meta.get(),1),old_contact_tip=sqlite_str(meta.get(),3);
            EncounterCycle cycle;
            cycle.id=static_cast<std::uint64_t>(scans)+1;
            cycle.generation=gen;cycle.tick=world.tick();
            cycle.population=n;cycle.pairs=total;
            cycle.start=static_cast<std::uint64_t>(prior_cursor)%total;
            std::vector<SocialEncounter> contacts;
            std::string next_contact_tip=old_contact_tip;
            const auto scan_max=std::min(static_cast<std::uint64_t>(scan_budget),total);
            for(std::uint64_t step=0;step<scan_max;step++){
                const auto slot=(cycle.start+step)%total;
                cycle.scanned=step+1;
                const auto pair=encounter_pair_at(ids,slot);
                const auto a=world.npc_records().at(pair.first),b=world.npc_records().at(pair.second);
                if(a.location!=b.location)continue;
                Statement prev(db_,"SELECT id,tick,ordinal FROM social_encounters "
                    "WHERE npc_low=? AND npc_high=? ORDER BY id DESC LIMIT 1");
                prev.text(1,std::to_string(pair.first));prev.text(2,std::to_string(pair.second));
                const bool existed=prev.row();
                const auto prev_id=existed?sqlite3_column_int64(prev.get(),0):0,
                           prev_tick=existed?sqlite3_column_int64(prev.get(),1):0,
                           prev_ordinal=existed?sqlite3_column_int64(prev.get(),2):0;
                if(existed && (prev_tick<0||cycle.tick<static_cast<std::uint64_t>(prev_tick)||
                   cycle.tick-static_cast<std::uint64_t>(prev_tick)<8))continue;
                if(existed && prev_ordinal==std::numeric_limits<sqlite3_int64>::max())
                    throw std::runtime_error("encounter familiarity counter exhausted");
                SocialEncounter e;
                e.id=static_cast<std::uint64_t>(prior_contacts)+contacts.size()+1;
                e.cycle_id=cycle.id;e.npc_low=pair.first;e.npc_high=pair.second;
                e.generation=gen;e.tick=cycle.tick;e.slot=slot;
                e.prior_id=static_cast<std::uint64_t>(prev_id);
                e.ordinal=static_cast<std::uint64_t>(prev_ordinal)+1;
                next_contact_tip=social_digest(next_contact_tip,encounter_body(e));
                contacts.push_back(e);
                if(contacts.size()==contact_budget)break;
            }
            cycle.contacts=contacts.size();cycle.end=(cycle.start+cycle.scanned)%total;
            const auto next_scan_tip=social_digest(scan_tip,encounter_cycle_body(cycle));
            Statement add_cycle(db_,"INSERT INTO encounter_scans VALUES(?,?,?,?,?,?,?,?,?,?)");
            add_cycle.i64(1,cycle.id);add_cycle.i64(2,cycle.generation);add_cycle.i64(3,cycle.tick);
            add_cycle.i64(4,cycle.population);add_cycle.i64(5,cycle.pairs);
            add_cycle.i64(6,cycle.start);add_cycle.i64(7,cycle.scanned);
            add_cycle.i64(8,cycle.end);add_cycle.i64(9,cycle.contacts);
            add_cycle.text(10,next_scan_tip);add_cycle.execute();
            // Recompute chained tips during the *write*, not merely in memory.
            std::string tip=old_contact_tip;
            for(const auto& e:contacts){
                tip=social_digest(tip,encounter_body(e));
                Statement add(db_,"INSERT INTO social_encounters VALUES(?,?,?,?,?,?,?,?,?,?)");
                add.i64(1,e.id);add.i64(2,e.cycle_id);add.text(3,std::to_string(e.npc_low));
                add.text(4,std::to_string(e.npc_high));add.i64(5,e.generation);
                add.i64(6,e.tick);add.i64(7,e.slot);add.i64(8,e.prior_id);
                add.i64(9,e.ordinal);add.text(10,tip);add.execute();
            }
            if(tip!=next_contact_tip)throw std::runtime_error("encounter digest internal mismatch");
            Statement update(db_,"UPDATE encounter_meta SET scan_count=?,scan_tip=?,"
                "contact_count=?,contact_tip=?,cursor=? WHERE singleton=1");
            update.i64(1,cycle.id);update.text(2,next_scan_tip);
            update.i64(3,static_cast<sqlite3_int64>(prior_contacts+contacts.size()));
            update.text(4,tip);update.i64(5,cycle.end);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("missing encounter metadata");
            run(db_,"COMMIT");return contacts.size();
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    bool co_located(const Simulation& world,std::uint64_t a,std::uint64_t b) const {
        if(!a||!b||a==b)return false;
        const auto x=world.npc_records().find(a),y=world.npc_records().find(b);
        return x!=world.npc_records().end()&&y!=world.npc_records().end()&&
            x->second.location==y->second.location;
    }
    bool record_social_contact(const Simulation& world,std::uint64_t gen,
                               std::uint64_t observer,std::uint64_t source){
        if(!co_located(world,observer,source))return false;
        return append_social_tie(world,gen,observer,source,SocialTieKind::Contact,0,0);
    }
    bool reassess_social_trust(const Simulation& world,std::uint64_t gen,
            std::uint64_t observer,std::uint64_t source,std::uint64_t claim_id,int delta){
        if(!claim_id||delta==0||delta<-25||delta>25||
           !co_located(world,observer,source))return false;
        return append_social_tie(world,gen,observer,source,SocialTieKind::Reassessment,claim_id,delta);
    }
    bool relay_trusted_claim(const Simulation& world,std::uint64_t gen,
            std::uint64_t parent,std::uint64_t listener,ClaimFidelity fidelity,
            int valence,int confidence,const std::string& account){
        if(!parent||!listener||fidelity==ClaimFidelity::LocalObservation)return false;
        Statement source(db_,"SELECT id,incident_id,COALESCE(parent_id,0),speaker_id,recipient_id,"
            "fidelity,valence,confidence,generation,tick,account FROM npc_knowledge WHERE id=?");
        source.i64(1,static_cast<sqlite3_int64>(parent));
        if(!source.row())return false;
        const auto from=read_knowledge(source.get());
        if(!co_located(world,listener,from.recipient_npc))return false;
        KnowledgeRecord e;
        e.parent_id=parent;e.incident_id=from.incident_id;
        e.speaker_npc=from.recipient_npc;e.recipient_npc=listener;
        e.fidelity=fidelity;e.valence=valence;e.confidence=confidence;
        e.generation=gen;e.tick=world.tick();e.account=account;
        return append_knowledge(e,gen,0,true);
    }
private:
    bool append_social_tie(const Simulation& world,std::uint64_t gen,
            std::uint64_t observer,std::uint64_t source,SocialTieKind kind,
            std::uint64_t claim_id,int delta){
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=gen)
                throw std::runtime_error("stale interpersonal trust generation");
            if(!co_located(world,observer,source)){
                run(db_,"ROLLBACK");return false;
            }
            Statement last(db_,"SELECT id,observer,source,prior_id,knowledge_id,kind,delta,trust,"
                "generation,tick FROM social_tie_events WHERE observer=? AND source=? ORDER BY id DESC LIMIT 1");
            last.text(1,std::to_string(observer));last.text(2,std::to_string(source));
            const bool prior=last.row();
            const auto previous=prior?read_tie(last.get()):SocialTieEvent{};
            if((kind==SocialTieKind::Contact&&prior)||
               (kind==SocialTieKind::Reassessment&&!prior)){
                run(db_,"ROLLBACK");return false;
            }
            if(kind==SocialTieKind::Reassessment){
                Statement claim(db_,"SELECT speaker_id,recipient_id,generation,tick,parent_id FROM npc_knowledge WHERE id=?");
                claim.i64(1,static_cast<sqlite3_int64>(claim_id));
                if(!claim.row()||parse_u64(sqlite3_column_text(claim.get(),0))!=source||
                   parse_u64(sqlite3_column_text(claim.get(),1))!=observer||
                   sqlite3_column_type(claim.get(),4)==SQLITE_NULL||
                   sqlite3_column_int64(claim.get(),2)>static_cast<sqlite3_int64>(gen)||
                   sqlite3_column_int64(claim.get(),3)>static_cast<sqlite3_int64>(world.tick())){
                    run(db_,"ROLLBACK");return false;
                }
                Statement used(db_,"SELECT id FROM social_tie_events WHERE knowledge_id=?");
                used.i64(1,static_cast<sqlite3_int64>(claim_id));
                if(used.row()) {run(db_,"ROLLBACK");return false;}
            }
            Statement meta(db_,"SELECT event_count,tip FROM social_tie_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing trust metadata");
            const auto n=sqlite3_column_int64(meta.get(),0);
            if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("trust sequence exhausted");
            SocialTieEvent e;e.id=static_cast<std::uint64_t>(n+1);
            e.observer_npc=observer;e.source_npc=source;e.kind=kind;
            e.prior_id=prior?previous.id:0;e.knowledge_id=claim_id;
            e.delta=delta;e.trust=prior?std::clamp(previous.trust+delta,0,100):50;
            e.generation=gen;e.tick=world.tick();
            const auto tip=social_digest(sqlite_str(meta.get(),1),tie_body(e));
            Statement insert(db_,"INSERT INTO social_tie_events VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,static_cast<sqlite3_int64>(e.id));
            insert.text(2,std::to_string(observer));insert.text(3,std::to_string(source));
            insert.i64(4,static_cast<sqlite3_int64>(e.prior_id));
            insert.i64(5,static_cast<sqlite3_int64>(claim_id));
            insert.i64(6,static_cast<int>(kind));insert.i64(7,delta);insert.i64(8,e.trust);
            insert.i64(9,static_cast<sqlite3_int64>(gen));
            insert.i64(10,static_cast<sqlite3_int64>(world.tick()));
            insert.text(11,tip);insert.execute();
            Statement update(db_,"UPDATE social_tie_meta SET event_count=?,tip=? WHERE singleton=1");
            update.i64(1,static_cast<sqlite3_int64>(e.id));update.text(2,tip);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("trust metadata absent");
            run(db_,"COMMIT");return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    bool append_knowledge(KnowledgeRecord e,std::uint64_t generation,std::uint64_t group_id=0,
                          bool require_social_trust=false){
        if(!valid_knowledge(e))return false;
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale knowledge witness generation");
            if(require_social_trust){
                Statement tie(db_,"SELECT trust FROM social_tie_events WHERE observer=? AND source=? ORDER BY id DESC LIMIT 1");
                tie.text(1,std::to_string(e.recipient_npc));
                tie.text(2,std::to_string(e.speaker_npc));
                if(!tie.row()||sqlite3_column_int(tie.get(),0)<25){
                    run(db_,"ROLLBACK");return false;
                }
                const auto trust=sqlite3_column_int(tie.get(),0);
                Statement origin(db_,"SELECT confidence,recipient_id FROM npc_knowledge WHERE id=?");
                origin.i64(1,static_cast<sqlite3_int64>(e.parent_id));
                if(!origin.row()||parse_u64(sqlite3_column_text(origin.get(),1))!=e.speaker_npc){
                    run(db_,"ROLLBACK");return false;
                }
                e.confidence=std::min({e.confidence,trust,sqlite3_column_int(origin.get(),0)});
            }
            std::uint64_t membership_seq=0;
            if(group_id){
                membership_seq=last_membership_seq();
                const auto active=group_members_at(group_id,membership_seq);
                if(!std::binary_search(active.begin(),active.end(),e.speaker_npc)||
                   !std::binary_search(active.begin(),active.end(),e.recipient_npc)){
                    run(db_,"ROLLBACK");return false;
                }
            }
            Statement meta(db_,"SELECT row_count,tip_digest FROM knowledge_meta WHERE singleton=1");
            if(!meta.row())throw std::runtime_error("missing knowledge metadata");
            const auto count=sqlite3_column_int64(meta.get(),0);
            if(count<0 || count==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("knowledge sequence exhausted");
            const auto previous=sqlite_str(meta.get(),1);
            e.id=static_cast<std::uint64_t>(count+1);
            const auto digest=knowledge_digest(previous,e);
            Statement insert(db_,"INSERT INTO npc_knowledge(id,incident_id,parent_id,speaker_id,recipient_id,"
                "fidelity,valence,confidence,generation,tick,account,digest) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,static_cast<sqlite3_int64>(e.id));insert.text(2,e.incident_id);
            if(e.parent_id)insert.i64(3,static_cast<sqlite3_int64>(e.parent_id));else insert.null(3);
            insert.text(4,std::to_string(e.speaker_npc));insert.text(5,std::to_string(e.recipient_npc));
            insert.i64(6,static_cast<int>(e.fidelity));insert.i64(7,e.valence);
            insert.i64(8,e.confidence);insert.i64(9,static_cast<sqlite3_int64>(e.generation));
            insert.i64(10,static_cast<sqlite3_int64>(e.tick));insert.text(11,e.account);
            insert.text(12,digest);insert.execute();
            Statement update(db_,"UPDATE knowledge_meta SET row_count=?,tip_digest=? WHERE singleton=1");
            update.i64(1,static_cast<sqlite3_int64>(e.id));update.text(2,digest);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("knowledge metadata missing");
            if(group_id){
                Statement sm(db_,"SELECT relays_count,relays_tip FROM social_meta WHERE singleton=1");
                if(!sm.row())throw std::runtime_error("missing social relay metadata");
                const auto n=sqlite3_column_int64(sm.get(),0);
                if(n<0||n==std::numeric_limits<sqlite3_int64>::max())
                    throw std::runtime_error("social relay sequence exhausted");
                const auto id=n+1;
                const auto tip=social_digest(sqlite_str(sm.get(),1),std::to_string(id)+"|"+
                    std::to_string(group_id)+"|"+std::to_string(e.id)+
                    (membership_seq?"|epoch:"+std::to_string(membership_seq):""));
                Statement relay(db_,"INSERT INTO group_relays(id,group_id,knowledge_id,digest,membership_seq) "
                    "VALUES(?,?,?,?,?)");
                relay.i64(1,id);relay.i64(2,static_cast<sqlite3_int64>(group_id));
                relay.i64(3,static_cast<sqlite3_int64>(e.id));relay.text(4,tip);
                relay.i64(5,static_cast<sqlite3_int64>(membership_seq));relay.execute();
                Statement up(db_,"UPDATE social_meta SET relays_count=?,relays_tip=? WHERE singleton=1");
                up.i64(1,id);up.text(2,tip);up.execute();
                if(sqlite3_changes(db_)!=1)throw std::runtime_error("social relay metadata missing");
            }
            run(db_,"COMMIT");
            return true;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
public:
    // Explicit repair on a COPY ONLY. This never operates on the original file.
    void repair_latest_from_journal() {
        quick_integrity(db_);
        const auto newest = latest_generation(db_);
        if (newest < 2) throw std::runtime_error("recovery: no earlier checkpoint available");
        Statement cp(db_, "SELECT generation,state,checksum,ore_total,credit_total FROM snapshots "
                          "WHERE generation<? ORDER BY generation DESC");
        cp.i64(1, static_cast<std::int64_t>(newest));
        std::string reconstructed;
        std::int64_t ore = -1;
        bool found = false;
        while (cp.row()) {
            const auto gen = static_cast<std::uint64_t>(sqlite3_column_int64(cp.get(), 0));
            const auto size = sqlite3_column_bytes(cp.get(), 1);
            const auto* ptr = static_cast<const char*>(sqlite3_column_blob(cp.get(), 1));
            if (gen == 0 || size <= 0 || size > 16 * 1024 * 1024 || !ptr) continue;
            try {
                Simulation sim = Simulation::from_snapshot(std::string(ptr, static_cast<std::size_t>(size)));
                const auto hash = parse_u64(sqlite3_column_text(cp.get(), 2));
                ore = sqlite3_column_int64(cp.get(), 3);
                const auto credit_total=sqlite3_column_int64(cp.get(),4);
                if (sim.state_hash() != hash || sim.total_ore() != ore || sim.total_credits()!=sqlite3_column_int64(cp.get(),4)) continue;
                Statement journal(db_, "SELECT generation,kind,command,state_hash,tick FROM operation_log "
                                       "WHERE generation>? AND generation<=? ORDER BY generation");
                journal.i64(1, static_cast<std::int64_t>(gen));
                journal.i64(2, static_cast<std::int64_t>(newest));
                auto previous = gen;
                while (journal.row()) {
                    const auto g = static_cast<std::uint64_t>(sqlite3_column_int64(journal.get(), 0));
                    const auto kind = sqlite3_column_int(journal.get(), 1);
                    if (g != previous + 1 || g > newest) throw std::runtime_error("recovery: missing operation");
                    if (kind == 1) {
                        const auto* raw = sqlite3_column_text(journal.get(), 2);
                        if (!raw || !sim.submit(decode_command(reinterpret_cast<const char*>(raw))))
                            throw std::runtime_error("recovery: command replay refused");
                    } else if (kind == 2) {
                        sim.step();
                    } else throw std::runtime_error("recovery: invalid journal kind");
                    if (sim.state_hash() != parse_u64(sqlite3_column_text(journal.get(), 3)) ||
                        sim.tick() != static_cast<std::uint64_t>(sqlite3_column_int64(journal.get(), 4)) ||
                        sim.total_ore() != ore || sim.total_credits()!=credit_total)
                        throw std::runtime_error("recovery: replay hash or resource mismatch");
                    previous = g;
                }
                if (previous != newest) continue;
                Statement expected(db_, "SELECT checksum,ore_total,credit_total FROM snapshots WHERE generation=?");
                expected.i64(1, static_cast<std::int64_t>(newest));
                if (!expected.row() || sim.state_hash() != parse_u64(sqlite3_column_text(expected.get(),0)) ||
                    (ore != sqlite3_column_int64(expected.get(),1) || credit_total != sqlite3_column_int64(expected.get(),2))) continue;
                reconstructed = sim.canonical_state();
                found = true;
                break;
            } catch (const std::exception&) {
                // Try another retained checkpoint. Never guess around missing operations.
            }
        }
        if (!found) throw std::runtime_error("recovery: cannot verify latest state; refusing repair");
        run(db_, "BEGIN IMMEDIATE");
        try {
            if (latest_generation(db_) != newest) throw std::runtime_error("recovery: source changed");
            Statement fix(db_, "UPDATE snapshots SET state=? WHERE generation=?");
            fix.blob(1, reconstructed);
            fix.i64(2, static_cast<std::int64_t>(newest));
            fix.execute();
            if (sqlite3_changes(db_) != 1) throw std::runtime_error("recovery: snapshot not found");
            run(db_, "COMMIT");
        } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        Simulation checked(1);
        std::uint64_t gen = 0;
        std::int64_t origin = 0, credit_origin=0;
        if (!restore(checked, gen, origin, credit_origin) || gen != newest)
            throw std::runtime_error("recovery: repaired copy did not pass full validation");
    }
private:
    sqlite3* db_ = nullptr;
};

void PersistentWorld::recover_copy(const std::string& source, const std::string& destination) {
    if (source.empty() || destination.empty() || source == destination)
        throw std::invalid_argument("recovery requires distinct nonempty paths");
    // Reserve destination with an OS-level exclusive create: an existing file,
    // including a dangling symlink, cannot be clobbered by a path race.
#ifdef _WIN32
    int reserved = -1;
    const errno_t open_error = _sopen_s(&reserved, destination.c_str(),
        _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (open_error != 0) throw std::runtime_error("recovery: destination already exists or cannot be created");
    _close(reserved);
#else
    const int reserved = open(destination.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (reserved < 0) throw std::runtime_error("recovery: destination already exists or cannot be created");
    close(reserved);
#endif
    sqlite3* src = nullptr;
    sqlite3* dest = nullptr;
    try {
        if (sqlite3_open_v2(source.c_str(), &src, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            throw std::runtime_error("recovery: cannot open original read-only");
        if (sqlite3_open_v2(destination.c_str(), &dest, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
            throw std::runtime_error("recovery: cannot create destination");
        sqlite3_backup* backup = sqlite3_backup_init(dest, "main", src, "main");
        if (!backup) throw std::runtime_error("recovery: SQLite backup initialization failed");
        const int rc = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if (rc != SQLITE_DONE || finish != SQLITE_OK)
            throw std::runtime_error("recovery: SQLite backup failed");
        sqlite3_close(dest); dest = nullptr;
        sqlite3_close(src); src = nullptr;
        Store copy(destination);
        copy.repair_latest_from_journal();
    } catch (...) {
        if (dest) sqlite3_close(dest);
        if (src) sqlite3_close(src);
        // Delete only the attempted destination; never remove/modify the original.
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        std::filesystem::remove(destination + "-wal", ec);
        std::filesystem::remove(destination + "-shm", ec);
        throw;
    }
}

PersistentWorld::PersistentWorld(const std::string& path, Simulation initial_world)
    : store_(std::make_unique<Store>(path)), world_(std::move(initial_world)) {
    if (!store_->restore(world_, generation_, resource_origin_, credit_origin_)) {
        if (world_.tick() != 0 || !world_.events().empty() || world_.accepted_command_count() != 0)
            throw std::invalid_argument("initial world must be at tick zero without commands");
        resource_origin_ = world_.total_ore();
        credit_origin_ = world_.total_credits();
        store_->commit(world_, 0, resource_origin_, credit_origin_, nullptr, {}, {}, {}, FailPoint::None);
        generation_ = 1;
    }
}
PersistentWorld::~PersistentWorld() = default;

bool PersistentWorld::submit(const Command& command, FailPoint fail) {
    if (command.action == Action::HostDestroyShip || command.action == Action::HostCrewLoss ||
        command.action == Action::SetGatePairOnline ||
        command.action == Action::SetGateStaff ||
        command.action == Action::DeliverGateSpares ||
        command.issuer == Simulation::kServerAuthority) return false;
    auto next = world_;
    if (!next.submit(command)) return false;
    store_->commit(next, generation_, resource_origin_, credit_origin_, &command, {}, {}, {}, fail);
    world_.swap(next); // noexcept: in-memory state published only after COMMIT
    ++generation_;
    return true;
}

bool PersistentWorld::submit_testing_gate_change(EntityId forward,EntityId reverse,
                                                  bool online,std::uint64_t command_id,
                                                  FailPoint fail) {
    const auto* a=world_.find(forward),*b=world_.find(reverse);
    if (!a || !b || a->type!=EntityType::Stargate || b->type!=EntityType::Stargate ||
        forward==reverse || a->system_id!=b->gate_destination ||
        b->system_id!=a->gate_destination) return false;
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=reverse;c.target=forward;
    c.action=Action::SetGatePairOnline;c.amount=online?1:0;
    auto next=world_;
    if (!next.submit(c)) return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::submit_testing_gate_operations(EntityId forward,EntityId reverse,
        Action action,std::int64_t amount,std::uint64_t command_id,FailPoint fail) {
    if(action!=Action::SetGateStaff && action!=Action::DeliverGateSpares)return false;
    const auto* a=world_.find(forward),*b=world_.find(reverse);
    if(!a || !b || forward==reverse || a->type!=EntityType::Stargate ||
       b->type!=EntityType::Stargate || a->system_id!=b->gate_destination ||
       b->system_id!=a->gate_destination || !world_.gate_operations(forward,reverse))return false;
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=reverse;c.target=forward;
    c.action=action;c.amount=amount;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::submit_npc_mining_work(std::uint64_t npc_id,EntityId ship,EntityId rock,
        std::int64_t amount,std::uint64_t command_id,FailPoint fail){
    if(!npc_id||!ship||!rock||!command_id||amount<1||amount>1000)return false;
    if(store_->npc_busy_this_tick(npc_id,world_.tick()))return false;
    const auto it=world_.npc_records().find(npc_id);
    const auto* entity=world_.find(ship);
    if(it==world_.npc_records().end()||!entity||entity->type!=EntityType::Ship||
       !entity->owner_id||entity->owner_id==Simulation::kServerAuthority||
       it->second.home_ship!=ship||it->second.location!=ship||
       it->second.role!=NpcRole::Ship)return false;
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=entity->owner_id;c.actor=ship;c.target=rock;
    c.action=Action::Mine;c.amount=amount;
    auto next=world_;
    if(!next.submit(c))return false;
    NpcMiningAssignment e;
    e.id=store_->next_npc_mining_assignment_id();e.command_id=command_id;
    e.npc_id=npc_id;e.ship_id=ship;e.rock_id=rock;
    e.generation=generation_+1;e.tick=world_.tick();
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail,&e);
    world_.swap(next);++generation_;return true;
}
bool PersistentWorld::verified_npc_mining_execution(std::uint64_t command_id,std::uint64_t npc_id) const{
    return store_->verified_npc_mining_execution(command_id,npc_id);
}
std::vector<NpcMiningAssignment> PersistentWorld::recent_npc_mining_assignments(std::size_t limit) const{
    return store_->recent_npc_mining_assignments(limit);
}
std::vector<NpcMiningExecution> PersistentWorld::recent_npc_mining_executions(std::size_t limit) const{
    return store_->recent_npc_mining_executions(limit);
}
bool PersistentWorld::submit_npc_fabrication_work(std::uint64_t npc_id,EntityId ship,
        Action action,EntityId material_code,std::int64_t quantity,
        std::uint64_t command_id,FailPoint fail){
    if(!npc_id||!ship||!command_id||quantity<1||quantity>1000||
       (action!=Action::FabricateModule && action!=Action::FabricateMaterialModule)||
       (action==Action::FabricateModule && material_code!=0)||
       (action==Action::FabricateMaterialModule && material_code!=1 && material_code!=2)||
       store_->npc_busy_this_tick(npc_id,world_.tick()))return false;
    const auto it=world_.npc_records().find(npc_id);
    const auto* entity=world_.find(ship);
    if(it==world_.npc_records().end()||!entity||entity->type!=EntityType::Ship||
       !entity->owner_id||entity->owner_id==Simulation::kServerAuthority||
       it->second.home_ship!=ship||it->second.location!=ship||
       it->second.role!=NpcRole::Ship)return false;
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=entity->owner_id;c.actor=ship;c.target=material_code;
    c.action=action;c.amount=quantity;
    auto next=world_;
    if(!next.submit(c))return false;
    NpcFabricationAssignment e;
    e.id=store_->next_npc_fabrication_assignment_id();
    e.command_id=command_id;e.npc_id=npc_id;e.ship_id=ship;
    e.issuer=entity->owner_id;e.action=action;e.material_code=material_code;
    e.quantity=quantity;e.generation=generation_+1;e.tick=world_.tick();
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail,nullptr,&e);
    world_.swap(next);++generation_;return true;
}
bool PersistentWorld::verified_npc_fabrication_execution(std::uint64_t command_id,std::uint64_t npc_id)const{
    return store_->verified_npc_fabrication_execution(command_id,npc_id);
}
std::vector<NpcFabricationAssignment> PersistentWorld::recent_npc_fabrication_assignments(std::size_t limit)const{
    return store_->recent_npc_fabrication_assignments(limit);
}
std::vector<NpcFabricationExecution> PersistentWorld::recent_npc_fabrication_executions(std::size_t limit)const{
    return store_->recent_npc_fabrication_executions(limit);
}
bool PersistentWorld::submit_testing_destroy_ship(EntityId attacker, EntityId victim,
        std::uint64_t command_id,FailPoint fail) {
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=attacker;c.target=victim;
    c.action=Action::HostDestroyShip;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;return true;
}
bool PersistentWorld::submit_testing_crew_loss(EntityId ship,std::int64_t count,
        std::uint64_t command_id,FailPoint fail) {
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=ship;c.target=ship;
    c.action=Action::HostCrewLoss;c.amount=count;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;return true;
}
void PersistentWorld::step(FailPoint fail) {
    auto next = world_;
    const auto before = next.events().size();
    const auto before_activity = next.activity_events().size();
    std::map<std::uint64_t, Command> due;
    // One logical step processes precisely the commands scheduled for tick+1.
    if (auto it = next.commands_.find(next.tick() + 1); it != next.commands_.end()) {
        for (const auto& c : it->second) due.emplace(c.id, c);
    }
    next.step();
    const std::vector<Event> new_events(next.events().begin() + static_cast<std::ptrdiff_t>(before), next.events().end());
    const std::vector<ActivityEvent> new_activity(
        next.activity_events().begin() + static_cast<std::ptrdiff_t>(before_activity), next.activity_events().end());
    store_->commit(next, generation_, resource_origin_, credit_origin_, nullptr, new_events, due, new_activity, fail);
    world_.swap(next);
    ++generation_;
}

std::vector<HistoricalEvent> PersistentWorld::recent_history(std::size_t limit) const {
    return store_->recent_history(limit);
}
std::vector<PhysicalIncident> PersistentWorld::recent_physical_incidents(std::size_t limit) const {
    return store_->recent_physical_incidents(limit);
}
bool PersistentWorld::record_local_observation(const std::string& incident_id,std::uint64_t npc_id) {
    return store_->record_local_observation(world_,generation_,incident_id,npc_id);
}
bool PersistentWorld::transmit_claim(std::uint64_t source_record_id,std::uint64_t listener_npc,
        ClaimFidelity fidelity,int valence,int confidence,const std::string& account) {
    return store_->transmit_claim(world_,generation_,source_record_id,listener_npc,
                                  fidelity,valence,confidence,account);
}
std::vector<KnowledgeRecord> PersistentWorld::recent_knowledge(std::size_t limit) const {
    return store_->recent_knowledge(limit);
}
std::uint64_t PersistentWorld::found_social_group(const std::string& name,
        const std::vector<std::uint64_t>& members){
    return store_->found_social_group(world_,generation_,name,members);
}
bool PersistentWorld::change_group_membership(std::uint64_t gid,std::uint64_t npc,
        GroupMembershipKind kind,std::uint64_t sponsor){
    return store_->change_group_membership(world_,generation_,gid,npc,kind,sponsor);
}
std::vector<std::uint64_t> PersistentWorld::active_social_group_members(std::uint64_t gid) const{
    return store_->active_social_group_members(gid);
}
std::vector<GroupMembershipEvent> PersistentWorld::recent_group_membership(std::size_t limit) const{
    return store_->recent_group_membership(limit);
}
bool PersistentWorld::relay_group_claim(std::uint64_t group_id,std::uint64_t parent_knowledge_id,
        std::uint64_t listener,ClaimFidelity fidelity,int valence,int confidence,
        const std::string& account){
    return store_->relay_group_claim(world_,generation_,group_id,parent_knowledge_id,
                                     listener,fidelity,valence,confidence,account);
}
bool PersistentWorld::record_group_view(std::uint64_t group_id,std::uint64_t relay_knowledge_id,
        GroupStance stance,int confidence,const std::string& account){
    return store_->record_group_view(world_,generation_,group_id,relay_knowledge_id,
                                     stance,confidence,account);
}
std::vector<SocialGroup> PersistentWorld::recent_social_groups(std::size_t limit) const {
    return store_->recent_social_groups(limit);
}
std::vector<GroupView> PersistentWorld::recent_group_views(std::size_t limit) const {
    return store_->recent_group_views(limit);
}
bool PersistentWorld::record_social_contact(std::uint64_t observer,std::uint64_t source){
    return store_->record_social_contact(world_,generation_,observer,source);
}
bool PersistentWorld::reassess_social_trust(std::uint64_t observer,std::uint64_t source,
                                            std::uint64_t knowledge_id,int delta){
    return store_->reassess_social_trust(world_,generation_,observer,source,knowledge_id,delta);
}
bool PersistentWorld::relay_trusted_claim(std::uint64_t parent,std::uint64_t listener,
              ClaimFidelity fidelity,int valence,int confidence,const std::string& account){
    return store_->relay_trusted_claim(world_,generation_,parent,listener,fidelity,
                                       valence,confidence,account);
}
std::vector<SocialTieEvent> PersistentWorld::recent_social_ties(std::size_t limit) const {
    return store_->recent_social_ties(limit);
}
std::size_t PersistentWorld::schedule_social_encounters(std::size_t scan_budget,
                                                        std::size_t contact_budget){
    return store_->schedule_social_encounters(world_,generation_,scan_budget,contact_budget);
}
std::vector<SocialEncounter> PersistentWorld::recent_social_encounters(std::size_t limit) const {
    return store_->recent_social_encounters(limit);
}
bool PersistentWorld::record_social_interaction(std::uint64_t encounter_id,
        std::uint64_t observer,std::uint64_t actor,SocialInteractionKind kind){
    return store_->record_social_interaction(world_,generation_,encounter_id,observer,actor,kind);
}
std::vector<SocialInteractionEvent> PersistentWorld::recent_social_interactions(std::size_t limit) const {
    return store_->recent_social_interactions(limit);
}
int PersistentWorld::directed_rapport(std::uint64_t observer,std::uint64_t actor) const {
    return store_->directed_rapport(observer,actor);
}
bool PersistentWorld::attribute_interaction_to_group(std::uint64_t interaction_id,
        std::uint64_t observer_group,std::uint64_t actor_group,
        GroupAttributionStance stance){
    return store_->attribute_interaction_to_group(world_,generation_,interaction_id,
        observer_group,actor_group,stance);
}
int PersistentWorld::individual_group_feeling(std::uint64_t observer,
        std::uint64_t observer_group,std::uint64_t actor_group) const{
    return store_->individual_group_feeling(observer,observer_group,actor_group);
}
GroupRelationSummary PersistentWorld::group_relation_summary(
        std::uint64_t observer_group,std::uint64_t actor_group) const{
    return store_->group_relation_summary(observer_group,actor_group);
}
std::vector<GroupAttributionEvent> PersistentWorld::recent_group_attributions(std::size_t limit) const{
    return store_->recent_group_attributions(limit);
}
bool PersistentWorld::appraise_witnessed_incident(std::uint64_t observation_id,
        std::uint64_t context_group,PhysicalAppraisalStance stance,int confidence){
    return store_->appraise_witnessed_incident(world_,generation_,observation_id,context_group,stance,confidence);
}
std::vector<PhysicalAppraisalEvent> PersistentWorld::recent_physical_appraisals(std::size_t limit) const {
    return store_->recent_physical_appraisals(limit);
}
bool PersistentWorld::attest_incident_role(const std::string& incident_id,
        std::uint64_t npc_id,VerifiedNpcRole role,std::uint64_t observation_id){
    return store_->attest_incident_role(world_,generation_,incident_id,npc_id,role,observation_id);
}
bool PersistentWorld::is_verified_incident_role(const std::string& incident_id,
        std::uint64_t npc_id,VerifiedNpcRole role) const{
    return store_->is_verified_incident_role(incident_id,npc_id,role);
}
std::vector<IncidentRoleReceipt> PersistentWorld::recent_incident_roles(std::size_t limit) const{
    return store_->recent_incident_roles(limit);
}
void PersistentWorld::steps(std::uint64_t n) { for (std::uint64_t i = 0; i < n; ++i) step(); }
} // namespace galaxy
