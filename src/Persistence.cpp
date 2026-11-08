#include "galaxy/Persistence.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/SeamlessSpatialOwnership.hpp"
#include "galaxy/UniversalNpcMigration.hpp"
#include "galaxy/CelestialFrameTransition.hpp"
#include "galaxy/ColonySafety.hpp"
#include "galaxy/ColonyDefense.hpp"
#include "galaxy/ColonyConflict.hpp"
#include "galaxy/ColonySafetyJournal.hpp"
#include "galaxy/ColonySafetyNotice.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include "galaxy/SplitRescueIntent.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include <algorithm>
#include <array>
#include <sqlite3.h>

#include <filesystem>
#include <cerrno>
#include <cstring>
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
#include <tuple>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy {
void enforce_physical_guardianship_at_commit(sqlite3*,std::uint64_t);
// Canonical legacy storage ledger labels remain unchanged; v28 adds a
// distinct source/destination class for actual machine material buffers.
static std::string storage_endpoint_label(EntityId site,std::uint32_t node,const char* item){
    if(node==kWorkshopConveyorNode)
        return "workshop:"+std::to_string(site)+":"+item;
    return "storage:"+std::to_string(site)+":"+std::to_string(node)+":"+item;
}

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
    if (c.transport_id || !c.passenger_ids.empty()) {
        out << " T31 " << c.transport_id << ' ' << c.passenger_ids.size();
        for (auto npc : c.passenger_ids) out << ' ' << npc;
        if(c.action==Action::HostSplitEvacuatePassengers)
            out << " S48 " << c.secondary_receiver << ' ' << c.secondary_transport;
    }
    return out.str();
}
bool split_receiver_matches(const Command& c,std::uint64_t ferry,
        std::uint64_t voyage,std::uint64_t guardian,std::uint64_t co_guardian){
    if(c.action!=Action::HostSplitEvacuatePassengers||c.amount<=0||
       c.thrust_x<=0||static_cast<std::size_t>(c.thrust_x)>=c.passenger_ids.size())
        return false;
    const auto first=static_cast<std::size_t>(c.thrust_x);
    const bool primary=c.target==ferry&&static_cast<std::uint64_t>(c.amount)==voyage;
    const bool secondary=c.secondary_receiver==ferry&&c.secondary_transport==voyage;
    if(primary==secondary)return false;
    const auto begin=c.passenger_ids.begin()+(primary?0:first);
    const auto end=c.passenger_ids.begin()+(primary?first:c.passenger_ids.size());
    return std::find(begin,end,guardian)!=end &&
        (!co_guardian||std::find(begin,end,co_guardian)!=end);
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
    if (in >> extra) {
        if (extra != "T31" || !(in >> c.transport_id))
            throw std::runtime_error("recovery: invalid transport envelope");
        std::size_t count=0;
        const bool colony_departure=action==static_cast<int>(Action::HostColonyDeparture);
        if (!(in >> count) || count==0 || count>(colony_departure?16U:10000U) ||
            (colony_departure ? c.transport_id!=0 : c.transport_id==0))
            throw std::runtime_error("recovery: invalid transport/departure manifest size");
        std::set<std::uint64_t> unique;
        for (std::size_t i=0;i<count;++i) {
            std::uint64_t npc=0;
            if (!(in >> npc) || !npc || !unique.insert(npc).second)
                throw std::runtime_error("recovery: invalid transport passenger");
            c.passenger_ids.push_back(npc);
        }
        if(colony_departure && !std::is_sorted(c.passenger_ids.begin(),c.passenger_ids.end()))
            throw std::runtime_error("recovery: departure IDs not canonical");
        if(action==static_cast<int>(Action::HostSplitEvacuatePassengers)){
            if(!(in>>extra)||extra!="S48"||!(in>>c.secondary_receiver>>c.secondary_transport)||
               !c.secondary_receiver||!c.secondary_transport)
                throw std::runtime_error("recovery: malformed split rescue envelope");
        }
    }
    if (in >> extra || action < 0 || action > static_cast<int>(Action::CancelWorkshopJob) || c.id == 0 || c.issuer == 0)
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
    bool manifest_transfer_allowed(const Command& c) const {return shipping::manifest_transfer_authorized(db_,c) && shipping::third_party_transfer_authorized(db_,c);}
    // A canonical PhysicalDependent stores one guardian, while ART02 retains
    // both birth parents. Until planetary family berths are atomic, an adult
    // with an ART02 child cannot use the independent-adult shuttle interface.
    // Consult the existing authoritative family registry, not a copied NPC
    // roster or an assertion by the incoming client.
    bool has_art02_child_parent(std::uint64_t adult_id) const {
        if(!adult_id)return false;
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='npc_family_births'");
        if(!table.row())return false; // no ART02 families registered in legacy DB
        Statement children(db_,"SELECT 1 FROM npc_family_births "
            "WHERE parent_a=? OR parent_b=? LIMIT 1");
        children.text(1,std::to_string(adult_id));
        children.text(2,std::to_string(adult_id));
        return children.row(); // conservative for unproven custody/death
    }
    // Original ART02 and original physical-child bridge are both required.
    // Even the co-parent's children must form ONE complete, colocated set.
    // The same predicate is evaluated during booking and again at takeoff.
    bool valid_planetary_family_births(const Simulation& world,EntityId ship,
        std::uint64_t guardian,std::uint64_t co_parent,
        const std::vector<std::uint64_t>& children,
        EntityId expected_legal_residence=0) const {
        if(!ship || !guardian || guardian==co_parent || children.empty() ||
           children.size()>2 || !std::is_sorted(children.begin(),children.end()) ||
           std::adjacent_find(children.begin(),children.end())!=children.end())
            return false;
        Statement table(db_,"SELECT name FROM sqlite_master WHERE type='table' "
                         "AND name IN ('npc_family_births','sg_med_society_birth',"
                         "'sg_colony_physical_child_authority','npc_family_moves')");
        std::set<std::string> tables;
        while(table.row())tables.insert(sqlite_str(table.get(),0));
        if(tables.size()!=4)return false;
        Statement births(db_,"SELECT child_id,parent_a,parent_b,settlement,scope "
            "FROM npc_family_births WHERE parent_a=? OR parent_b=? "
            "OR parent_a=? OR parent_b=? ORDER BY child_id");
        births.text(1,std::to_string(guardian));
        births.text(2,std::to_string(guardian));
        births.text(3,std::to_string(co_parent));
        births.text(4,std::to_string(co_parent));
        std::vector<std::uint64_t> original;
        std::string common_scope;
        while(births.row()){
            const auto id=parse_u64(sqlite3_column_text(births.get(),0));
            const auto a=parse_u64(sqlite3_column_text(births.get(),1));
            const auto b=parse_u64(sqlite3_column_text(births.get(),2));
            const auto birthplace=parse_u64(sqlite3_column_text(births.get(),3));
            const auto scope=sqlite_str(births.get(),4);
            if(id<(1ULL<<62) || !id || !birthplace || !ship ||
               scope.empty() || (co_parent &&
                !((a==guardian && b==co_parent) ||
                  (a==co_parent && b==guardian))) ||
               (a!=guardian && b!=guardian &&
                (!co_parent || (a!=co_parent && b!=co_parent))) ||
               birthplace!=ship ||
               (!common_scope.empty() && scope!=common_scope))
                return false;
            common_scope=scope;
            // An active historical ferry dependent berth cannot be silently
            // rebound to a planetary shuttle, even if the cached world
            // happens to show the child at its original ship.
            {
                Statement active_table(db_,"SELECT 1 FROM sqlite_master "
                    "WHERE type='table' AND name='sg_dependent_travel'");
                if(active_table.row()) {
                    Statement competing(db_,"SELECT 1 FROM sg_dependent_travel "
                        "WHERE child_id=? AND status=1 LIMIT 1");
                    competing.text(1,std::to_string(id));
                    if(competing.row())return false;
                }
            }
            const auto physical=world.physical_dependents().find(id);
            if(physical==world.physical_dependents().end() ||
               physical->second.home_ship!=ship ||
               (physical->second.guardian_id!=guardian &&
                physical->second.guardian_id!=co_parent) ||
               world.npc_records().count(id) ||
               world.npc_casualties().count(id))
                return false;
            Statement bridge(db_,"SELECT guardian_id,home_ship,command_id "
                "FROM sg_colony_physical_child_authority WHERE child_id=?");
            bridge.text(1,std::to_string(id));
            if(!bridge.row() ||
               parse_u64(sqlite3_column_text(bridge.get(),0))!=
                    physical->second.guardian_id ||
               parse_u64(sqlite3_column_text(bridge.get(),1))!=ship ||
               parse_u64(sqlite3_column_text(bridge.get(),2))!=
                    physical->second.registration_command ||
               bridge.row())return false;
            Statement natal(db_,"SELECT 1 FROM sg_med_society_birth "
                "WHERE child_id=? AND family_scope=? AND home_ship=?");
            natal.text(1,std::to_string(id));natal.text(2,scope);
            natal.text(3,std::to_string(ship));
            if(!natal.row() || natal.row())return false;
            Statement dead(db_,"SELECT 1 FROM npc_family_deaths "
                "WHERE scope=? AND npc_id=? LIMIT 1");
            dead.text(1,scope);dead.text(2,std::to_string(id));
            if(dead.row())return false;
            // Flight is physical visitation, not a legal residence change.
            // Born children with prior ART02 residence changes may not
            // silently move through a ship-home-only contract.
            Statement moved(db_,"SELECT destination FROM npc_family_moves "
                "WHERE scope=? AND npc_id=? ORDER BY rowid DESC LIMIT 1");
            moved.text(1,scope);moved.text(2,std::to_string(id));
            const auto actual_address=moved.row()?
                parse_u64(sqlite3_column_text(moved.get(),0)):ship;
            if(actual_address!=(expected_legal_residence?
                expected_legal_residence:ship))return false;
            original.push_back(id);
            if(original.size()>2)return false;
        }
        return original==children;
    }
    std::string sqlite_path() const {
        const char* path=sqlite3_db_filename(db_,"main");
        if(!path)throw std::runtime_error("world SQLite file path unavailable");
        return path;
    }
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
        if (v < 0 || v > 18) throw std::runtime_error("SQLite: incompatible schema version");
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
        if(v<=17){
            // The original world-step operation_log kind=2 cannot contain a
            // command payload. Native pilot admissions use a separate
            // append-only companion receipt with original writer provenance.
            run(db_,"BEGIN IMMEDIATE");
            try {
                run(db_,"CREATE TABLE sg_native_npc_commands("
                    "id TEXT PRIMARY KEY REFERENCES receipts(id),"
                    "generation INTEGER NOT NULL CHECK(generation>0),"
                    "command TEXT NOT NULL,"
                    "pilot TEXT NOT NULL,"
                    "world_hash TEXT NOT NULL,"
                    "digest TEXT NOT NULL)");
                run(db_,"CREATE INDEX sg_native_npc_commands_generation "
                    "ON sg_native_npc_commands(generation)");
                run(db_,"CREATE TRIGGER sg_native_npc_commands_no_update "
                    "BEFORE UPDATE ON sg_native_npc_commands "
                    "BEGIN SELECT RAISE(ABORT,'native command immutable'); END");
                run(db_,"CREATE TRIGGER sg_native_npc_commands_no_delete "
                    "BEFORE DELETE ON sg_native_npc_commands "
                    "BEGIN SELECT RAISE(ABORT,'native command immutable'); END");
                run(db_,"PRAGMA user_version=18");run(db_,"COMMIT");
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        } catch (...) { sqlite3_close_v2(db_); db_ = nullptr; throw; }
    }
    ~Store() { sqlite3_close(db_); }
    // This is the canonical writer connection, not a second SQLite handle.
    // All safety evidence changes and the durable count anchor commit together.
    void verify_colony_safety_evidence() const {
        Statement found(db_,"SELECT name FROM sqlite_master WHERE type='table' "
            "AND name IN ('sg_colony_safety_evidence','sg_colony_safety_meta')");
        std::size_t present=0;
        while(found.row())++present;
        if(!present)return; // An unmodified v64 or v65 save has no host records.
        if(present!=2)throw std::runtime_error("incomplete colony safety evidence schema");
        Statement q(db_,"SELECT event_count,last_id FROM sg_colony_safety_meta WHERE singleton=1");
        if(!q.row())throw std::runtime_error("colony safety count anchor missing");
        const auto count=sqlite3_column_int64(q.get(),0),last=sqlite3_column_int64(q.get(),1);
        if(count<0||last<0)throw std::runtime_error("invalid colony safety count anchor");
        Statement actual(db_,"SELECT COUNT(*),COALESCE(MAX(event_id),0) FROM sg_colony_safety_evidence");
        if(!actual.row()||sqlite3_column_int64(actual.get(),0)!=count||
           sqlite3_column_int64(actual.get(),1)!=last)
            throw std::runtime_error("colony safety evidence does not match count anchor");
    }
    std::uint64_t colony_safety_evidence_count() const {
        Statement found(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_safety_meta'");
        if(!found.row())return 0;
        verify_colony_safety_evidence();
        Statement q(db_,"SELECT event_count FROM sg_colony_safety_meta WHERE singleton=1");
        if(!q.row())throw std::runtime_error("missing colony safety count");
        return static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),0));
    }
    void record_colony_safety_preflight(const colony_safety::Decision& d,
                                       std::uint64_t expected_generation){
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=expected_generation)
                throw std::runtime_error("stale world generation for colony safety order");
            colony_safety::create_evidence_table(db_);
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_safety_meta("
                "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                "event_count INTEGER NOT NULL CHECK(event_count>=0),"
                "last_id INTEGER NOT NULL CHECK(last_id>=0))");
            // A removed meta table must not be silently recreated around retained evidence.
            Statement actual(db_,"SELECT COUNT(*),COALESCE(MAX(event_id),0) FROM sg_colony_safety_evidence");
            if(!actual.row())throw std::runtime_error("safety evidence query failed");
            const auto count=sqlite3_column_int64(actual.get(),0),last=sqlite3_column_int64(actual.get(),1);
            if(count<0||last<0||last==std::numeric_limits<sqlite3_int64>::max())
                throw std::runtime_error("safety evidence index invalid");
            if(count==0)run(db_,"INSERT OR IGNORE INTO sg_colony_safety_meta VALUES(1,0,0)");
            Statement anchor(db_,"SELECT event_count,last_id FROM sg_colony_safety_meta WHERE singleton=1");
            if(!anchor.row()||sqlite3_column_int64(anchor.get(),0)!=count||
               sqlite3_column_int64(anchor.get(),1)!=last)
                throw std::runtime_error("colony safety evidence anchor mismatch");
            colony_safety::record_decision(db_,static_cast<std::uint64_t>(last+1),d);
            Statement update(db_,"UPDATE sg_colony_safety_meta SET event_count=?,last_id=? WHERE singleton=1");
            update.i64(1,count+1);update.i64(2,last+1);update.execute();
            if(sqlite3_changes(db_)!=1)throw std::runtime_error("failed safety anchor update");
            run(db_,"COMMIT");
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void verify_colony_notifications(const Simulation& state,std::uint64_t gen) const {
        colony_safety::notice::verify(db_,state,gen);
    }
    std::vector<colony_safety::notice::Pending> pending_colony_notifications(std::uint64_t limit) const {
        return colony_safety::notice::pending(db_,limit);
    }
    bool acknowledge_colony_notification(const Simulation& state,std::uint64_t gen,
                                         std::uint64_t event,std::uint64_t npc) {
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=gen)throw std::runtime_error("stale world for NPC notification");
            colony_safety::notice::verify(db_,state,gen);
            const auto accepted=colony_safety::notice::acknowledge(db_,state,gen,event,npc);
            run(db_,"COMMIT");return accepted;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    std::uint64_t colony_notification_opportunity_count() const {
        return colony_safety::notice::opportunity_count(db_);
    }
    std::uint64_t colony_notification_acknowledgement_count() const {
        return colony_safety::notice::acknowledgement_count(db_);
    }
    // AUTO-04D.5A: independent admission provenance. No caller-provided
    // NPC story or receipt header can bypass the persisted real-world state.
    std::optional<PersistentWorld::AtomicColonyFamilyPlan> supervisor_plan(
        std::uint64_t command_id) const{
        if(!command_id)return std::nullopt;
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_supervisor_plan'");
        if(!table.row())return std::nullopt; // pre-v79 legacy admission
        Statement q(db_,"SELECT p.intent_id,p.admission_generation,p.family_seed,"
            "p.origin_colony,p.receiver_colony,p.regional_revision,p.owner_generation,"
            "p.origin_family_revision,p.receiver_family_revision,p.adult_count,"
            "p.child_count,d.source,d.receiver,d.family_scope,d.region_scope,"
            "d.admission_generation,d.intent_id "
            "FROM sg_colony_npc_supervisor_plan p "
            "JOIN sg_colony_npc_autonomous_dispatch d ON d.command_id=p.command_id "
            "WHERE p.command_id=?");
        q.text(1,std::to_string(command_id));
        if(!q.row())return std::nullopt;
        PersistentWorld::AtomicColonyFamilyPlan p{};
        p.command_id=command_id;
        p.family.intent_id=parse_u64(sqlite3_column_text(q.get(),0));
        const auto gen=sqlite3_column_int64(q.get(),1);
        p.family.family_seed=parse_u64(sqlite3_column_text(q.get(),2));
        p.origin_colony=sqlite_str(q.get(),3);
        p.receiver_colony=sqlite_str(q.get(),4);
        const auto reg_rev=sqlite3_column_int64(q.get(),5);
        const auto owner_gen=sqlite3_column_int64(q.get(),6);
        const auto from_rev=sqlite3_column_int64(q.get(),7);
        const auto to_rev=sqlite3_column_int64(q.get(),8);
        const auto adults=sqlite3_column_int64(q.get(),9);
        const auto children=sqlite3_column_int64(q.get(),10);
        p.family.source=parse_u64(sqlite3_column_text(q.get(),11));
        p.family.receiver=parse_u64(sqlite3_column_text(q.get(),12));
        p.family.family_scope=sqlite_str(q.get(),13);
        p.region_scope=sqlite_str(q.get(),14);
        if(gen<2 || sqlite3_column_int64(q.get(),15)!=gen ||
           parse_u64(sqlite3_column_text(q.get(),16))!=p.family.intent_id ||
           !p.family.source || !p.family.receiver ||
           p.family.source==p.family.receiver ||
           !p.family.family_seed || !p.family.intent_id ||
           p.family.intent_id==command_id ||
           p.family.family_scope.empty() || p.region_scope.empty() ||
           p.origin_colony.empty() || p.receiver_colony.empty() ||
           p.origin_colony==p.receiver_colony ||
           reg_rev<1 || owner_gen<1 || from_rev<0 || to_rev<0 ||
           adults<1 || children<1 || adults+children>16 || q.row())
            throw std::runtime_error("supervisor saved transfer plan invalid or mismatched");
        p.family.expected_world_generation=static_cast<std::uint64_t>(gen-1);
        p.expected_region_revision=static_cast<std::uint64_t>(reg_rev);
        p.expected_owner_generation=static_cast<std::uint64_t>(owner_gen);
        p.expected_origin_family_revision=static_cast<std::uint64_t>(from_rev);
        p.expected_receiver_family_revision=static_cast<std::uint64_t>(to_rev);
        Statement rows(db_,"SELECT ordinal,person_id,guardian_id,kind,event_id "
            "FROM sg_colony_npc_supervisor_member WHERE command_id=? ORDER BY ordinal");
        rows.text(1,std::to_string(command_id));
        std::set<std::uint64_t> people,events{command_id,p.family.intent_id};
        std::uint64_t prev_adult=0,prev_child=0;
        sqlite3_int64 index=0;
        while(rows.row()){
            const auto ordinal=sqlite3_column_int64(rows.get(),0);
            const auto person=parse_u64(sqlite3_column_text(rows.get(),1));
            const auto guardian=parse_u64(sqlite3_column_text(rows.get(),2));
            const auto kind=sqlite3_column_int(rows.get(),3);
            const auto event=parse_u64(sqlite3_column_text(rows.get(),4));
            if(ordinal!=index || index>=adults+children || !person || !event ||
               !people.insert(person).second || !events.insert(event).second)
                throw std::runtime_error("supervisor plan has missing or duplicate member");
            if(index<adults){
                if(kind!=0 || guardian || person<=prev_adult ||
                   person>=(1ULL<<62))
                    throw std::runtime_error("supervisor adult roster not canonical");
                prev_adult=person;p.family.adult_ids.push_back(person);
            }else{
                if(kind!=1 || !guardian || person<(1ULL<<62) ||
                   person<=prev_child ||
                   !std::binary_search(p.family.adult_ids.begin(),
                                       p.family.adult_ids.end(),guardian))
                    throw std::runtime_error("supervisor child guardian/roster not canonical");
                prev_child=person;
                p.family.children.push_back({person,guardian});
            }
            p.member_event_ids.push_back(event);
            ++index;
        }
        if(index!=adults+children)
            throw std::runtime_error("supervisor plan omitted an adult or dependent");
        return p;
    }
    void verify_supervisor_plans(std::uint64_t max_generation) const{
        Statement schema(db_,"SELECT name FROM sqlite_master WHERE type='table' "
            "AND name IN ('sg_colony_npc_supervisor_activation',"
            "'sg_colony_npc_supervisor_plan','sg_colony_npc_supervisor_member')");
        unsigned seen=0;while(schema.row())++seen;
        if(!seen)return; // pre-v79 world
        if(seen!=3)throw std::runtime_error("partial NPC supervisor ledger");
        for(const auto* tab:{"sg_colony_npc_supervisor_activation",
                             "sg_colony_npc_supervisor_plan",
                             "sg_colony_npc_supervisor_member"})
            for(const auto* suffix:{"_immutable_update","_immutable_delete"}){
                Statement trigger(db_,"SELECT 1 FROM sqlite_master "
                    "WHERE type='trigger' AND name=?");
                trigger.text(1,std::string(tab)+suffix);
                if(!trigger.row() || trigger.row())
                    throw std::runtime_error("supervisor immutable evidence trigger missing");
            }
        Statement first(db_,"SELECT first_generation FROM "
            "sg_colony_npc_supervisor_activation WHERE singleton=1");
        if(!first.row())throw std::runtime_error("supervisor activation missing");
        const auto activation=sqlite3_column_int64(first.get(),0);
        if(activation<2 || static_cast<std::uint64_t>(activation)>max_generation ||
           first.row())
            throw std::runtime_error("supervisor activation invalid");
        Statement dispatch(db_,"SELECT command_id,admission_generation "
            "FROM sg_colony_npc_autonomous_dispatch "
            "WHERE admission_generation>=? ORDER BY admission_generation");
        dispatch.i64(1,activation);
        while(dispatch.row()){
            const auto id=parse_u64(sqlite3_column_text(dispatch.get(),0));
            const auto generation=sqlite3_column_int64(dispatch.get(),1);
            auto plan=supervisor_plan(id);
            if(!plan || generation!=static_cast<sqlite3_int64>(
                   plan->family.expected_world_generation+1) ||
               generation>static_cast<sqlite3_int64>(max_generation))
                throw std::runtime_error("accepted NPC rescue lost its durable executable plan");
            Statement accepted(db_,"SELECT kind,command FROM operation_log "
                "WHERE generation=?");
            accepted.i64(1,generation);
            if(!accepted.row() || sqlite3_column_int(accepted.get(),0)!=1)
                throw std::runtime_error("supervisor recovery has no admitted world command");
            const auto command=decode_command(sqlite_str(accepted.get(),1));
            if(command.id!=id || command.action!=Action::HostColonyDeparture ||
               command.issuer!=Simulation::kServerAuthority ||
               command.actor!=plan->family.source ||
               command.target!=plan->family.receiver ||
               command.passenger_ids!=plan->family.adult_ids ||
               accepted.row())
                throw std::runtime_error("supervisor plan does not match actual host command");
            Statement prepared(db_,"SELECT generation,source,receiver,family_scope,"
                "family_seed,adult_roster,child_guardians "
                "FROM sg_colony_family_departure_prepare WHERE intent_id=?");
            prepared.text(1,std::to_string(plan->family.intent_id));
            if(!prepared.row() ||
               sqlite3_column_int64(prepared.get(),0)!=generation-1 ||
               sqlite_str(prepared.get(),1)!=std::to_string(plan->family.source) ||
               sqlite_str(prepared.get(),2)!=std::to_string(plan->family.receiver) ||
               sqlite_str(prepared.get(),3)!=plan->family.family_scope ||
               sqlite_str(prepared.get(),4)!=std::to_string(plan->family.family_seed))
                throw std::runtime_error("supervisor plan contradicts original preparation");
            std::string adult_roster,child_roster;
            for(const auto person:plan->family.adult_ids){
                if(!adult_roster.empty())adult_roster.push_back(',');
                adult_roster+=std::to_string(person);
            }
            for(const auto& child:plan->family.children){
                if(!child_roster.empty())child_roster.push_back(',');
                child_roster+=std::to_string(child.child_id)+":"+
                    std::to_string(child.guardian_id);
            }
            if(sqlite_str(prepared.get(),5)!=adult_roster ||
               sqlite_str(prepared.get(),6)!=child_roster || prepared.row())
                throw std::runtime_error("supervisor saved member manifest was corrupted");
            Statement checkpoint(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            checkpoint.i64(1,generation);
            if(!checkpoint.row())throw std::runtime_error("supervisor admission snapshot pruned");
            const auto* ptr=sqlite3_column_blob(checkpoint.get(),0);
            const auto len=sqlite3_column_bytes(checkpoint.get(),0);
            if(!ptr || len<=0 || len>16*1024*1024)
                throw std::runtime_error("invalid supervisor admission checkpoint");
            const auto admitted=Simulation::from_snapshot(std::string(
                static_cast<const char*>(ptr),static_cast<std::size_t>(len)));
            if(admitted.state_hash()!=parse_u64(sqlite3_column_text(checkpoint.get(),1)) ||
               checkpoint.row())
                throw std::runtime_error("supervisor checkpoint has wrong physical hash");
            std::vector<std::uint64_t> world_adults,world_children;
            for(const auto& [person,record]:admitted.npc_records())
                if(record.home_ship==plan->family.source)
                    world_adults.push_back(person);
            for(const auto& [person,child]:admitted.physical_dependents())
                if(child.home_ship==plan->family.source){
                    world_children.push_back(person);
                    const auto member=std::find_if(plan->family.children.begin(),
                        plan->family.children.end(),
                        [&](const auto& x){return x.child_id==person &&
                            x.guardian_id==child.guardian_id;});
                    if(member==plan->family.children.end() ||
                       child.location!=plan->family.source)
                        throw std::runtime_error("supervisor saved dependent guardian forged");
                }
            std::vector<std::uint64_t> plan_children;
            for(const auto& c:plan->family.children)plan_children.push_back(c.child_id);
            if(world_adults!=plan->family.adult_ids ||
               world_children!=plan_children)
                throw std::runtime_error("supervisor plan did not contain entire real family");
        }
        Statement dangling(db_,"SELECT 1 FROM sg_colony_npc_supervisor_plan p "
            "LEFT JOIN sg_colony_npc_autonomous_dispatch d "
            "ON d.command_id=p.command_id WHERE d.command_id IS NULL LIMIT 1");
        if(dangling.row())
            throw std::runtime_error("orphaned supervisor plan header");
        Statement orphan(db_,"SELECT 1 FROM sg_colony_npc_supervisor_member m "
            "LEFT JOIN sg_colony_npc_supervisor_plan p "
            "ON p.command_id=m.command_id WHERE p.command_id IS NULL LIMIT 1");
        if(orphan.row())
            throw std::runtime_error("orphaned supervisor plan member");
    }
    void verify_autonomous_dispatch(std::uint64_t max_generation) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_autonomous_dispatch'");
        if(!exists.row())return; // older worlds have no autonomous admissions
        for(const auto* name:{"sg_colony_npc_auto_no_update",
                              "sg_colony_npc_auto_no_delete"}){
            Statement trigger(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' "
                "AND name=?");
            trigger.text(1,name);
            if(!trigger.row() || trigger.row())
                throw std::runtime_error("autonomous NPC dispatch immutable guard removed");
        }
        Statement all(db_,"SELECT command_id,intent_id,admission_generation,world_hash,"
            "leader_id,observation_id,source,receiver,family_scope,region_scope,risk "
            "FROM sg_colony_npc_autonomous_dispatch");
        while(all.row()){
            const auto command=parse_u64(sqlite3_column_text(all.get(),0));
            const auto intent=parse_u64(sqlite3_column_text(all.get(),1));
            const auto gen=sqlite3_column_int64(all.get(),2);
            const auto hash=sqlite_str(all.get(),3);
            const auto npc=parse_u64(sqlite3_column_text(all.get(),4));
            const auto observation=sqlite3_column_int64(all.get(),5);
            const auto source=parse_u64(sqlite3_column_text(all.get(),6));
            const auto receiver=parse_u64(sqlite3_column_text(all.get(),7));
            const auto family=sqlite_str(all.get(),8);
            const auto region=sqlite_str(all.get(),9);
            const auto risk=sqlite3_column_int(all.get(),10);
            if(!command || !intent || command==intent || gen<2 ||
               static_cast<std::uint64_t>(gen)>max_generation ||
               !npc || observation<=0 || !source || !receiver || source==receiver ||
               family.empty() || region.empty() || (risk!=2 && risk!=3))
                throw std::runtime_error("autonomous NPC dispatch identity corrupted");
            Statement snap(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
            snap.i64(1,gen);
            if(!snap.row())
                throw std::runtime_error("autonomous dispatch admission snapshot pruned");
            const auto* bytes=sqlite3_column_blob(snap.get(),0);
            const auto size=sqlite3_column_bytes(snap.get(),0);
            if(!bytes || size<=0 || size>16*1024*1024)
                throw std::runtime_error("autonomous dispatch snapshot malformed");
            const auto admitted=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(size)));
            if(admitted.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(admitted.state_hash())!=hash || snap.row() ||
               !admitted.has_command_id(command))
                throw std::runtime_error("autonomous dispatch lacks real accepted world command");
            const auto leader=admitted.npc_records().find(npc);
            if(leader==admitted.npc_records().end() ||
               leader->second.home_ship!=source ||
               leader->second.location!=source ||
               leader->second.transport_id ||
               static_cast<int>(colony_safety::assess(admitted,source,source).risk)!=risk)
                throw std::runtime_error("autonomous dispatch was not a local urgent NPC response");
            Statement op(db_,"SELECT kind,state_hash FROM operation_log WHERE generation=?");
            op.i64(1,gen);
            if(!op.row() || sqlite3_column_int(op.get(),0)!=1 ||
               sqlite_str(op.get(),1)!=hash || op.row())
                throw std::runtime_error("autonomous dispatch lacks canonical admission journal");
            Statement planned(db_,"SELECT generation,source,receiver,family_scope,status "
                "FROM sg_colony_family_departure_prepare WHERE intent_id=?");
            planned.text(1,std::to_string(intent));
            if(!planned.row() || sqlite3_column_int64(planned.get(),0)!=gen-1 ||
               sqlite_str(planned.get(),1)!=std::to_string(source) ||
               sqlite_str(planned.get(),2)!=std::to_string(receiver) ||
               sqlite_str(planned.get(),3)!=family ||
               sqlite3_column_int(planned.get(),4)!=1 || planned.row())
                throw std::runtime_error("autonomous dispatch lacks prepared real family");
            Statement notice(db_,"SELECT o.generation,o.tick,o.site,o.risk,"
                "e.ship,a.delivery_generation "
                "FROM sg_colony_npc_opportunities o "
                "JOIN sg_colony_npc_acks a ON a.event_id=o.event_id AND a.npc_id=o.npc_id "
                "JOIN sg_colony_response_events e ON e.id=o.event_id "
                "WHERE o.event_id=? AND o.npc_id=?");
            notice.i64(1,observation);notice.i64(2,static_cast<sqlite3_int64>(npc));
            if(!notice.row() ||
               sqlite3_column_int64(notice.get(),0)>gen ||
               sqlite3_column_int64(notice.get(),1)>
                   static_cast<sqlite3_int64>(admitted.tick()) ||
               sqlite3_column_int64(notice.get(),2)!=
                   static_cast<sqlite3_int64>(source) ||
               sqlite3_column_int(notice.get(),3)!=risk ||
               sqlite3_column_int64(notice.get(),4)!=
                   static_cast<sqlite3_int64>(source) ||
               sqlite3_column_int64(notice.get(),5)>gen || notice.row())
                throw std::runtime_error("autonomous dispatch lacks actually delivered NPC notice");
            Statement newest(db_,"SELECT id FROM sg_colony_response_events "
                "WHERE site=? AND generation<=? ORDER BY id DESC LIMIT 1");
            newest.i64(1,static_cast<sqlite3_int64>(source));newest.i64(2,gen);
            if(!newest.row() ||
               sqlite3_column_int64(newest.get(),0)!=observation || newest.row())
                throw std::runtime_error("autonomous dispatch used obsolete warning");
        }
    }
    // AUTO-04D.5C1: independently check every NPC-initiated local transfer.
    // A durable intent is not an executed delivery: a positive result requires
    // the real Applied world event, inventory delta and deterministic replay.
    void verify_npc_habitat_supplies(std::uint64_t max_generation) const{
        Statement schema(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_emergency_supply'");
        if(!schema.row())return;
        for(const auto* suffix:{"_no_update","_no_delete"}){
            Statement trigger(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            trigger.text(1,std::string("sg_colony_npc_emergency_supply")+suffix);
            if(!trigger.row() || trigger.row())
                throw std::runtime_error("NPC emergency supply immutable witness missing");
        }
        Statement all(db_,"SELECT command_id,generation,world_hash,npc_id,"
            "notice_id,source_ship,habitat,resource,amount_kg,"
            "source_before_kg,habitat_before_kg,risk "
            "FROM sg_colony_npc_emergency_supply ORDER BY generation");
        while(all.row()){
            const auto id=parse_u64(sqlite3_column_text(all.get(),0));
            const auto gen=sqlite3_column_int64(all.get(),1);
            const auto hash=sqlite_str(all.get(),2);
            const auto npc=parse_u64(sqlite3_column_text(all.get(),3));
            const auto notice=sqlite3_column_int64(all.get(),4);
            const auto source=parse_u64(sqlite3_column_text(all.get(),5));
            const auto site=parse_u64(sqlite3_column_text(all.get(),6));
            const int selector=sqlite3_column_int(all.get(),7);
            const auto amount=sqlite3_column_int64(all.get(),8);
            const auto from=sqlite3_column_int64(all.get(),9);
            const auto at=sqlite3_column_int64(all.get(),10);
            const auto risk=sqlite3_column_int(all.get(),11);
            if(!id || gen<2 || static_cast<std::uint64_t>(gen)>max_generation ||
               !npc || notice<=0 || !source || !site || source==site ||
               selector<1 || selector>3 || amount<1 || amount>10000 ||
               from<amount || at<0 || (risk!=2 && risk!=3))
                throw std::runtime_error("NPC emergency supply record malformed");
            Statement op(db_,"SELECT kind,command,state_hash FROM operation_log "
                "WHERE generation=?");
            op.i64(1,gen);
            if(!op.row() || sqlite3_column_int(op.get(),0)!=1 ||
               sqlite_str(op.get(),2)!=hash)
                throw std::runtime_error("NPC supply missing canonical admission");
            const auto command=decode_command(sqlite_str(op.get(),1));
            if(command.id!=id || command.action!=Action::TransferHabitatStock ||
               command.actor!=source || command.target!=site ||
               command.thrust_x!=selector || command.thrust_y!=0 ||
               command.amount!=amount || command.transport_id ||
               !command.passenger_ids.empty() || op.row())
                throw std::runtime_error("NPC supply command/receipt substituted");
            Statement before(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            before.i64(1,gen);
            if(!before.row())throw std::runtime_error("NPC supply admission snapshot missing");
            const auto* bytes=sqlite3_column_blob(before.get(),0);
            const auto size=sqlite3_column_bytes(before.get(),0);
            if(!bytes || size<=0 || size>16*1024*1024)
                throw std::runtime_error("NPC supply snapshot malformed");
            const auto admitted=Simulation::from_snapshot(
                std::string(static_cast<const char*>(bytes),static_cast<std::size_t>(size)));
            if(admitted.state_hash()!=parse_u64(sqlite3_column_text(before.get(),1)) ||
               std::to_string(admitted.state_hash())!=hash || before.row() ||
               !admitted.has_command_id(id))
                throw std::runtime_error("NPC supply snapshot/accepted command mismatch");
            const auto* owner=admitted.find(source);
            const auto* shipstock=admitted.life_support(source);
            const auto* habitat=admitted.habitat(site);
            const auto npc_record=admitted.npc_records().find(npc);
            if(!owner || command.issuer!=owner->owner_id ||
               command.issuer==Simulation::kServerAuthority ||
               !shipstock || !habitat || habitat->source_ship!=source ||
               !habitat->residents ||
               npc_record==admitted.npc_records().end() ||
               npc_record->second.home_ship!=source ||
               npc_record->second.location!=site ||
               npc_record->second.transport_id ||
               static_cast<int>(colony_safety::assess(admitted,source,site).risk)!=risk)
                throw std::runtime_error("NPC supply admission lacked living local emergency");
            const auto select=[&](const ColonyLifeSupport& l){
                return selector==1?l.food_kg:selector==2?l.water_kg:l.oxygen_kg;
            };
            if(select(*shipstock)!=from || select(habitat->life)!=at ||
               from<amount)
                throw std::runtime_error("NPC supply source inventory authority modified");
            Statement proof(db_,"SELECT o.site,o.risk,e.ship,a.delivery_generation "
                "FROM sg_colony_npc_opportunities o "
                "JOIN sg_colony_response_events e ON e.id=o.event_id "
                "JOIN sg_colony_npc_acks a ON a.event_id=o.event_id AND a.npc_id=o.npc_id "
                "WHERE o.event_id=? AND o.npc_id=?");
            proof.i64(1,notice);proof.i64(2,static_cast<sqlite3_int64>(npc));
            if(!proof.row() ||
               sqlite3_column_int64(proof.get(),0)!=static_cast<sqlite3_int64>(site) ||
               sqlite3_column_int(proof.get(),1)!=risk ||
               sqlite3_column_int64(proof.get(),2)!=static_cast<sqlite3_int64>(source) ||
               sqlite3_column_int64(proof.get(),3)>gen || proof.row())
                throw std::runtime_error("NPC supply has no delivered local warning");
            Statement latest(db_,"SELECT id FROM sg_colony_response_events "
                "WHERE site=? AND generation<=? ORDER BY id DESC LIMIT 1");
            latest.i64(1,static_cast<sqlite3_int64>(site));latest.i64(2,gen);
            if(!latest.row() || sqlite3_column_int64(latest.get(),0)!=notice ||
               latest.row())
                throw std::runtime_error("NPC supply used obsolete risk event");
            Statement event(db_,"SELECT generation,outcome,transferred FROM historical_events "
                "WHERE command_id=?");
            event.text(1,std::to_string(id));
            if(event.row()){
                const auto completed=sqlite3_column_int64(event.get(),0);
                if(completed!=gen+1 ||
                   sqlite3_column_int(event.get(),1)!=static_cast<int>(Outcome::Applied) ||
                   sqlite3_column_int64(event.get(),2)!=amount || event.row())
                    throw std::runtime_error("NPC emergency transfer not actually applied");
                Statement after(db_,"SELECT state,checksum FROM snapshots "
                    "WHERE generation=?");
                after.i64(1,completed);
                if(!after.row())throw std::runtime_error("NPC delivery snapshot pruned");
                const auto* later=sqlite3_column_blob(after.get(),0);
                const auto length=sqlite3_column_bytes(after.get(),0);
                if(!later || length<=0 || length>16*1024*1024)
                    throw std::runtime_error("NPC delivered snapshot invalid");
                const auto physical=Simulation::from_snapshot(
                    std::string(static_cast<const char*>(later),static_cast<std::size_t>(length)));
                auto replay=admitted;
                replay.step();
                if(physical.state_hash()!=parse_u64(sqlite3_column_text(after.get(),1)) ||
                   physical.state_hash()!=replay.state_hash() ||
                   physical.total_material_kg()!=admitted.total_material_kg() ||
                   after.row())
                    throw std::runtime_error("NPC stock delivery lacks physical replay");
            }else if(static_cast<std::uint64_t>(gen)!=max_generation){
                throw std::runtime_error("NPC stock admission lost its due execution");
            }
        }
    }
    bool npc_habitat_supply_applied(std::uint64_t command_id,std::uint64_t gen) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_emergency_supply'");
        if(!exists.row())return false;
        verify_npc_habitat_supplies(gen);
        Statement q(db_,"SELECT 1 FROM sg_colony_npc_emergency_supply s "
            "JOIN historical_events e ON e.command_id=s.command_id "
            "WHERE s.command_id=? AND e.outcome=? AND e.transferred=s.amount_kg");
        q.text(1,std::to_string(command_id));
        q.i64(2,static_cast<int>(Outcome::Applied));
        return q.row() && !q.row();
    }
    // DEF-01A. A human-on-watch actually observes a nearby ship. The
    // presence of a foreign ship alone never authorizes weapons or a strike.
    colony_defense::Contact record_colony_watch_contact(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t event_id,EntityId source,
        std::uint64_t observer,EntityId foreign){
        if(!event_id || event_id>
           static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid bounded defense event ID");
        const auto contacts=colony_defense::observe_local_contacts(world,source,observer);
        const auto it=std::find_if(contacts.begin(),contacts.end(),
            [&](const auto& c){return c.foreign_ship==foreign;});
        if(it==contacts.end())throw std::runtime_error(
            "no real local NPC sighting of requested foreign ship");
        const auto record=*it;
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale world generation for watch contact");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_watch_contacts("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,"
                "tick INTEGER NOT NULL CHECK(tick>=0),"
                "source TEXT NOT NULL,observer TEXT NOT NULL,"
                "foreign_ship TEXT NOT NULL,foreign_owner TEXT NOT NULL,"
                "system INTEGER NOT NULL,distance_squared INTEGER NOT NULL CHECK(distance_squared>=0))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_watch_no_update "
                "BEFORE UPDATE ON sg_colony_watch_contacts "
                "BEGIN SELECT RAISE(ABORT,'immutable local contact'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_watch_no_delete "
                "BEFORE DELETE ON sg_colony_watch_contacts "
                "BEGIN SELECT RAISE(ABORT,'immutable local contact'); END");
            Statement q(db_,"INSERT INTO sg_colony_watch_contacts VALUES("
                "?,?,?,?,?,?,?,?,?,?)");
            q.i64(1,static_cast<sqlite3_int64>(event_id));
            q.i64(2,static_cast<sqlite3_int64>(generation));
            q.text(3,std::to_string(world.state_hash()));
            q.i64(4,static_cast<sqlite3_int64>(record.world_tick));
            q.text(5,std::to_string(source));
            q.text(6,std::to_string(observer));
            q.text(7,std::to_string(foreign));
            q.text(8,std::to_string(record.foreign_owner));
            q.i64(9,record.system);
            q.i64(10,static_cast<sqlite3_int64>(record.distance_squared));
            q.execute();
            run(db_,"COMMIT");
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            throw;
        }
        return record;
    }
    void verify_colony_watch_contacts(std::uint64_t maximum_generation) const{
        Statement schema(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_watch_contacts'");
        if(!schema.row())return;
        for(const auto* guard:{"sg_colony_watch_no_update",
                               "sg_colony_watch_no_delete"}){
            Statement trigger(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            trigger.text(1,guard);
            if(!trigger.row() || trigger.row())
                throw std::runtime_error("local NPC watch immutable guard missing");
        }
        Statement q(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "source,observer,foreign_ship,foreign_owner,system,distance_squared "
            "FROM sg_colony_watch_contacts ORDER BY event_id");
        while(q.row()){
            const auto event=sqlite3_column_int64(q.get(),0);
            const auto generation=sqlite3_column_int64(q.get(),1);
            const auto hash=sqlite_str(q.get(),2);
            const auto tick=sqlite3_column_int64(q.get(),3);
            const auto source=parse_u64(sqlite3_column_text(q.get(),4));
            const auto npc=parse_u64(sqlite3_column_text(q.get(),5));
            const auto foreign=parse_u64(sqlite3_column_text(q.get(),6));
            const auto owner=parse_u64(sqlite3_column_text(q.get(),7));
            const auto system=sqlite3_column_int64(q.get(),8);
            const auto distance=sqlite3_column_int64(q.get(),9);
            if(event<1 || generation<1 ||
               static_cast<std::uint64_t>(generation)>maximum_generation ||
               tick<0 || !source || !npc || !foreign || !owner ||
               source==foreign || system<1 || distance<0)
                throw std::runtime_error("local colony contact evidence malformed");
            Statement snap(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            snap.i64(1,generation);
            if(!snap.row())throw std::runtime_error("local watch snapshot pruned");
            const auto* ptr=sqlite3_column_blob(snap.get(),0);
            const auto len=sqlite3_column_bytes(snap.get(),0);
            if(!ptr || len<=0 || len>16*1024*1024)
                throw std::runtime_error("local watch snapshot invalid");
            const auto observed=Simulation::from_snapshot(
                std::string(static_cast<const char*>(ptr),static_cast<std::size_t>(len)));
            if(observed.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(observed.state_hash())!=hash ||
               observed.tick()!=static_cast<std::uint64_t>(tick) || snap.row())
                throw std::runtime_error("local watch world hash or time corrupted");
            const auto contacts=colony_defense::observe_local_contacts(observed,source,npc);
            const auto it=std::find_if(contacts.begin(),contacts.end(),
                [&](const auto& c){return c.foreign_ship==foreign;});
            if(it==contacts.end() || it->foreign_owner!=owner ||
               it->system!=static_cast<std::uint32_t>(system) ||
               it->distance_squared!=static_cast<std::uint64_t>(distance))
                throw std::runtime_error("local watch witness forged or not physically possible");
        }
    }
    // CONFLICT-01A: colony political agenda exists independently of any
    // player's conduct. Host approves a real, named, resident leader. Every
    // later warning/raid preparation needs one specific observed contact.
    void establish_colony_agenda(const Simulation& world,
         std::uint64_t generation,std::uint64_t agenda_id,
         EntityId source,std::uint64_t npc,colony_conflict::Agenda agenda){
        if(!agenda_id || agenda_id>
              static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           !source || !npc || static_cast<int>(agenda)<0 ||
           static_cast<int>(agenda)>3)
            throw std::invalid_argument("invalid colony political agenda");
        const auto* entity=world.find(source);
        const auto person=world.npc_records().find(npc);
        if(!entity || entity->type!=EntityType::Ship ||
           !entity->owner_id || !world.colony_ship(source) ||
           person==world.npc_records().end() ||
           person->second.home_ship!=source || person->second.location!=source ||
           person->second.transport_id || !world.npc_available_for_duty(npc))
            throw std::runtime_error("agenda leader is not living physically present colony NPC");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale world before colony political agenda");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_conflict_agenda("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL UNIQUE,leader TEXT NOT NULL,"
                "agenda INTEGER NOT NULL CHECK(agenda BETWEEN 0 AND 3))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_conflict_agenda_no_update "
                "BEFORE UPDATE ON sg_colony_conflict_agenda "
                "BEGIN SELECT RAISE(ABORT,'immutable founding colony agenda'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_conflict_agenda_no_delete "
                "BEFORE DELETE ON sg_colony_conflict_agenda "
                "BEGIN SELECT RAISE(ABORT,'immutable founding colony agenda'); END");
            Statement insert(db_,"INSERT INTO sg_colony_conflict_agenda "
                "VALUES(?,?,?,?,?,?,?)");
            insert.i64(1,static_cast<sqlite3_int64>(agenda_id));
            insert.i64(2,static_cast<sqlite3_int64>(generation));
            insert.text(3,std::to_string(world.state_hash()));
            insert.i64(4,static_cast<sqlite3_int64>(world.tick()));
            insert.text(5,std::to_string(source));
            insert.text(6,std::to_string(npc));
            insert.i64(7,static_cast<int>(agenda));
            insert.execute();
            run(db_,"COMMIT");
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;
        }
    }

    // CONFLICT-01B3: effective diplomacy is an as-of-generation lookup.
    // A transition published at generation N first controls generation N+1.
    // This preserves the meaning of every already-accepted old decision.
    colony_conflict::GovernanceEpoch effective_governance(
        EntityId source,std::uint64_t decision_generation) const{
        if(!source || !decision_generation)
            throw std::invalid_argument("governance lookup needs a physical colony and generation");
        Statement foundation(db_,"SELECT event_id,leader,agenda,world_generation "
            "FROM sg_colony_conflict_agenda WHERE colony=?");
        foundation.text(1,std::to_string(source));
        if(!foundation.row())throw std::runtime_error("no founded colony government");
        colony_conflict::GovernanceEpoch out;
        out.colony=source;
        const auto founding_id=sqlite3_column_int64(foundation.get(),0);
        out.leader_id=parse_u64(sqlite3_column_text(foundation.get(),1));
        const int first_agenda=sqlite3_column_int(foundation.get(),2);
        const auto established=sqlite3_column_int64(foundation.get(),3);
        if(founding_id<1 || !out.leader_id ||
           first_agenda<0 || first_agenda>3 || established<1 ||
           static_cast<std::uint64_t>(established)>decision_generation ||
           foundation.row())
            throw std::runtime_error("invalid founding leadership history");
        out.founding_event_id=static_cast<std::uint64_t>(founding_id);
        out.agenda=static_cast<colony_conflict::Agenda>(first_agenda);
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_governance_epochs'");
        if(!exists.row())return out;
        Statement epoch(db_,"SELECT event_id,prior_event,ordinal,leader,agenda,"
            "policy,kind,world_generation FROM sg_colony_governance_epochs "
            "WHERE colony=? AND world_generation<? "
            "ORDER BY ordinal DESC LIMIT 1");
        epoch.text(1,std::to_string(source));
        epoch.i64(2,static_cast<sqlite3_int64>(decision_generation));
        if(epoch.row()){
            const auto event=sqlite3_column_int64(epoch.get(),0);
            const auto prev=sqlite3_column_int64(epoch.get(),1);
            const auto ordinal=sqlite3_column_int64(epoch.get(),2);
            const auto leader=parse_u64(sqlite3_column_text(epoch.get(),3));
            const auto agenda=sqlite3_column_int(epoch.get(),4);
            const auto policy=sqlite3_column_int(epoch.get(),5);
            const auto kind=sqlite3_column_int(epoch.get(),6);
            const auto born=sqlite3_column_int64(epoch.get(),7);
            if(event<1 || prev<0 || ordinal<1 || !leader ||
               agenda<0 || agenda>3 || policy<0 || policy>2 ||
               kind<0 || kind>2 || born<1 ||
               static_cast<std::uint64_t>(born)>=decision_generation ||
               epoch.row())
                throw std::runtime_error("historical leadership epoch malformed");
            out.event_id=static_cast<std::uint64_t>(event);
            out.prior_event_id=static_cast<std::uint64_t>(prev);
            out.ordinal=static_cast<std::uint64_t>(ordinal);
            out.leader_id=leader;
            out.agenda=static_cast<colony_conflict::Agenda>(agenda);
            out.policy=static_cast<colony_conflict::DiplomaticPolicy>(policy);
            out.kind=static_cast<colony_conflict::TransitionKind>(kind);
        }
        return out;
    }
    void validate_governance_handoff(const Simulation& world,
        EntityId source,const colony_conflict::GovernanceEpoch& prior,
        std::uint64_t successor,colony_conflict::Agenda agenda,
        colony_conflict::DiplomaticPolicy policy,
        colony_conflict::TransitionKind kind,PlayerId approving_owner) const{
        const auto* entity=world.find(source);
        if(!entity || entity->type!=EntityType::Ship ||
           !world.colony_ship(source) || !entity->owner_id ||
           entity->owner_id!=approving_owner ||
           static_cast<int>(agenda)<0 || static_cast<int>(agenda)>3 ||
           static_cast<int>(policy)<0 || static_cast<int>(policy)>2 ||
           static_cast<int>(kind)<0 || static_cast<int>(kind)>2)
            throw std::runtime_error("unauthorized or invalid colony constitutional handoff");
        const auto next=world.npc_records().find(successor);
        if(!successor || next==world.npc_records().end() ||
           next->second.home_ship!=source ||
           next->second.location!=source ||
           next->second.transport_id ||
           !world.npc_available_for_duty(successor))
            throw std::runtime_error("successor not an available resident adult NPC");
        const auto prior_person=world.npc_records().find(prior.leader_id);
        const bool incumbent_present=prior_person!=world.npc_records().end() &&
            prior_person->second.home_ship==source &&
            prior_person->second.location==source &&
            !prior_person->second.transport_id &&
            world.npc_available_for_duty(prior.leader_id);
        const bool replacement=successor!=prior.leader_id;
        if((kind==colony_conflict::TransitionKind::Appointment &&
            (!replacement || !incumbent_present)) ||
           (kind==colony_conflict::TransitionKind::EmergencyVacancy &&
            (!replacement || incumbent_present)) ||
           (kind==colony_conflict::TransitionKind::PolicyRevision &&
            (replacement || !incumbent_present ||
             (agenda==prior.agenda && policy==prior.policy))))
            throw std::runtime_error("succession cause does not match physical authority");
    }
    colony_conflict::GovernanceEpoch record_governance_transition(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t event,EntityId source,std::uint64_t successor,
        colony_conflict::Agenda agenda,colony_conflict::DiplomaticPolicy policy,
        colony_conflict::TransitionKind kind,PlayerId approving_owner){
        if(!event || event>static_cast<std::uint64_t>(
             std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid durable governance event");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale generation for leadership succession");
            const auto previous=effective_governance(source,generation);
            validate_governance_handoff(
                world,source,previous,successor,agenda,policy,kind,approving_owner);
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_governance_epochs("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL,owner TEXT NOT NULL,"
                "ordinal INTEGER NOT NULL CHECK(ordinal>0),"
                "prior_event INTEGER NOT NULL CHECK(prior_event>=0),"
                "prior_leader TEXT NOT NULL,"
                "prior_agenda INTEGER NOT NULL CHECK(prior_agenda BETWEEN 0 AND 3),"
                "prior_policy INTEGER NOT NULL CHECK(prior_policy BETWEEN 0 AND 2),"
                "leader TEXT NOT NULL,"
                "agenda INTEGER NOT NULL CHECK(agenda BETWEEN 0 AND 3),"
                "policy INTEGER NOT NULL CHECK(policy BETWEEN 0 AND 2),"
                "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),"
                "UNIQUE(colony,ordinal),UNIQUE(colony,world_generation))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_governance_no_update "
                "BEFORE UPDATE ON sg_colony_governance_epochs "
                "BEGIN SELECT RAISE(ABORT,'immutable constitutional epoch'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_governance_no_delete "
                "BEFORE DELETE ON sg_colony_governance_epochs "
                "BEGIN SELECT RAISE(ABORT,'immutable constitutional epoch'); END");
            Statement insert(db_,"INSERT INTO sg_colony_governance_epochs "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,static_cast<sqlite3_int64>(event));
            insert.i64(2,static_cast<sqlite3_int64>(generation));
            insert.text(3,std::to_string(world.state_hash()));
            insert.i64(4,static_cast<sqlite3_int64>(world.tick()));
            insert.text(5,std::to_string(source));
            insert.text(6,std::to_string(approving_owner));
            insert.i64(7,static_cast<sqlite3_int64>(previous.ordinal+1));
            insert.i64(8,static_cast<sqlite3_int64>(previous.event_id));
            insert.text(9,std::to_string(previous.leader_id));
            insert.i64(10,static_cast<int>(previous.agenda));
            insert.i64(11,static_cast<int>(previous.policy));
            insert.text(12,std::to_string(successor));
            insert.i64(13,static_cast<int>(agenda));
            insert.i64(14,static_cast<int>(policy));
            insert.i64(15,static_cast<int>(kind));
            insert.execute();
            run(db_,"COMMIT");
            colony_conflict::GovernanceEpoch out=previous;
            out.prior_event_id=previous.event_id;
            out.event_id=event;
            ++out.ordinal;
            out.leader_id=successor;
            out.agenda=agenda;
            out.policy=policy;
            out.kind=kind;
            return out; // published now, effective strictly next generation
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;
        }
    }
    void verify_colony_governance(std::uint64_t max_generation) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_governance_epochs'");
        if(!exists.row())return;
        for(const auto* name:{"sg_colony_governance_no_update",
                              "sg_colony_governance_no_delete"}){
            Statement guard(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            guard.text(1,name);
            if(!guard.row() || guard.row())
                throw std::runtime_error("governance immutable guard absent");
        }
        Statement rows(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "colony,owner,ordinal,prior_event,prior_leader,prior_agenda,"
            "prior_policy,leader,agenda,policy,kind "
            "FROM sg_colony_governance_epochs "
            "ORDER BY colony,ordinal");
        std::map<EntityId,std::uint64_t> previous_generation;
        while(rows.row()){
            const auto id=sqlite3_column_int64(rows.get(),0);
            const auto gen=sqlite3_column_int64(rows.get(),1);
            const auto hash=sqlite_str(rows.get(),2);
            const auto tick=sqlite3_column_int64(rows.get(),3);
            const auto source=parse_u64(sqlite3_column_text(rows.get(),4));
            const auto owner=parse_u64(sqlite3_column_text(rows.get(),5));
            const auto ordinal=sqlite3_column_int64(rows.get(),6);
            const auto previous_event=sqlite3_column_int64(rows.get(),7);
            const auto previous_leader=parse_u64(sqlite3_column_text(rows.get(),8));
            const auto previous_agenda=sqlite3_column_int(rows.get(),9);
            const auto previous_policy=sqlite3_column_int(rows.get(),10);
            const auto successor=parse_u64(sqlite3_column_text(rows.get(),11));
            const auto agenda=sqlite3_column_int(rows.get(),12);
            const auto policy=sqlite3_column_int(rows.get(),13);
            const auto kind=sqlite3_column_int(rows.get(),14);
            if(id<1 || gen<1 || static_cast<std::uint64_t>(gen)>max_generation ||
               tick<0 || !source || !owner || ordinal<1 || previous_event<0 ||
               !previous_leader || !successor ||
               previous_agenda<0 || previous_agenda>3 ||
               previous_policy<0 || previous_policy>2 ||
               agenda<0 || agenda>3 || policy<0 || policy>2 ||
               kind<0 || kind>2 ||
               previous_generation[source]>=static_cast<std::uint64_t>(gen))
                throw std::runtime_error("invalid dated leadership succession chronology");
            Statement snap(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            snap.i64(1,gen);
            if(!snap.row())throw std::runtime_error("governance origin snapshot pruned");
            const auto* bytes=sqlite3_column_blob(snap.get(),0);
            const auto len=sqlite3_column_bytes(snap.get(),0);
            if(!bytes || len<=0 || len>16*1024*1024)
                throw std::runtime_error("governance origin snapshot malformed");
            const auto original=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(len)));
            if(original.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(original.state_hash())!=hash ||
               original.tick()!=static_cast<std::uint64_t>(tick) || snap.row())
                throw std::runtime_error("governance original world rewritten");
            const auto previous=effective_governance(
                source,static_cast<std::uint64_t>(gen));
            if(previous.event_id!=static_cast<std::uint64_t>(previous_event) ||
               previous.ordinal+1!=static_cast<std::uint64_t>(ordinal) ||
               previous.leader_id!=previous_leader ||
               static_cast<int>(previous.agenda)!=previous_agenda ||
               static_cast<int>(previous.policy)!=previous_policy)
                throw std::runtime_error("governance epoch ancestry forged");
            validate_governance_handoff(
                original,source,previous,successor,
                static_cast<colony_conflict::Agenda>(agenda),
                static_cast<colony_conflict::DiplomaticPolicy>(policy),
                static_cast<colony_conflict::TransitionKind>(kind),owner);
            previous_generation[source]=static_cast<std::uint64_t>(gen);
        }
    }


    // EXIT-01A: only a real eligible nonleader adult actually living under
    // critical conditions can produce a record of pressure to seek departure.
    // It is a physical-risk witness, NOT proof of subjective intent to leave.
    std::optional<colony_conflict::ExitPressure> assess_colony_exit_pressure(
        const Simulation& world,std::uint64_t generation,
        EntityId source,std::uint64_t npc_id) const{
        if(!source || !npc_id || !generation)
            throw std::invalid_argument("exit pressure requires named NPC and real colony");
        const auto* entity=world.find(source);
        const auto* colony=world.colony_ship(source);
        const auto person=world.npc_records().find(npc_id);
        if(!entity || !colony || entity->type!=EntityType::Ship ||
           !entity->owner_id || person==world.npc_records().end() ||
           person->second.home_ship!=source ||
           person->second.location!=source ||
           person->second.transport_id ||
           !world.npc_available_for_duty(npc_id))
            return std::nullopt;
        const auto govt=effective_governance(source,generation);
        // Leadership vacancy has its own already-verified B3 process. Do not
        // strand the local government through the first individual exit gate.
        if(govt.leader_id==npc_id)return std::nullopt;
        const auto risk=colony_safety::assess(world,source,source).risk;
        if(static_cast<int>(risk)<static_cast<int>(colony_safety::Risk::Critical))
            return std::nullopt;
        colony_conflict::ExitPressure p;
        p.world_generation=generation;
        p.governance_event_id=govt.event_id;
        p.source_colony=source;
        p.source_owner=entity->owner_id;
        p.npc_id=npc_id;
        p.source_risk=risk;
        return p;
    }
    colony_conflict::ExitPressure record_colony_exit_pressure(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t event_id,EntityId source,std::uint64_t npc_id){
        if(!event_id || event_id>static_cast<std::uint64_t>(
             std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid exit-pressure evidence event id");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("exit pressure uses stale physical generation");
            auto p=assess_colony_exit_pressure(world,generation,source,npc_id);
            if(!p)
                throw std::runtime_error("named NPC lacks real exit-pressure evidence");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_npc_exit_pressure("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL,source_owner TEXT NOT NULL,npc TEXT NOT NULL,"
                "governance_event INTEGER NOT NULL CHECK(governance_event>=0),"
                "risk INTEGER NOT NULL CHECK(risk IN(2,3)),"
                "UNIQUE(colony,npc,governance_event))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_npc_exit_pressure_no_update "
                "BEFORE UPDATE ON sg_colony_npc_exit_pressure "
                "BEGIN SELECT RAISE(ABORT,'immutable real NPC exit pressure'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_npc_exit_pressure_no_delete "
                "BEFORE DELETE ON sg_colony_npc_exit_pressure "
                "BEGIN SELECT RAISE(ABORT,'immutable real NPC exit pressure'); END");
            Statement ins(db_,"INSERT INTO sg_colony_npc_exit_pressure "
                "VALUES(?,?,?,?,?,?,?,?,?)");
            ins.i64(1,static_cast<sqlite3_int64>(event_id));
            ins.i64(2,static_cast<sqlite3_int64>(generation));
            ins.text(3,std::to_string(world.state_hash()));
            ins.i64(4,static_cast<sqlite3_int64>(world.tick()));
            ins.text(5,std::to_string(source));
            ins.text(6,std::to_string(p->source_owner));
            ins.text(7,std::to_string(npc_id));
            ins.i64(8,static_cast<sqlite3_int64>(p->governance_event_id));
            ins.i64(9,static_cast<int>(p->source_risk));
            ins.execute();
            run(db_,"COMMIT");
            p->event_id=event_id;
            return *p;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;
        }
    }
    void verify_colony_exit_pressure(std::uint64_t max_generation) const{
        Statement present(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_exit_pressure'");
        if(!present.row())return;
        for(const auto* name:{"sg_npc_exit_pressure_no_update",
                              "sg_npc_exit_pressure_no_delete"}){
            Statement guard(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            guard.text(1,name);
            if(!guard.row() || guard.row())
                throw std::runtime_error("NPC exit evidence immutable guard missing");
        }
        Statement q(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "colony,source_owner,npc,governance_event,risk "
            "FROM sg_colony_npc_exit_pressure");
        while(q.row()){
            const auto id=sqlite3_column_int64(q.get(),0);
            const auto generation=sqlite3_column_int64(q.get(),1);
            const auto hash=sqlite_str(q.get(),2);
            const auto tick=sqlite3_column_int64(q.get(),3);
            const auto source=parse_u64(sqlite3_column_text(q.get(),4));
            const auto owner=parse_u64(sqlite3_column_text(q.get(),5));
            const auto npc=parse_u64(sqlite3_column_text(q.get(),6));
            const auto govt=sqlite3_column_int64(q.get(),7);
            const auto risk=sqlite3_column_int(q.get(),8);
            if(id<1 || generation<1 ||
               static_cast<std::uint64_t>(generation)>max_generation ||
               tick<0 || !source || !owner || !npc || govt<0)
                throw std::runtime_error("invalid NPC exit evidence chronology");
            Statement cp(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
            cp.i64(1,generation);
            if(!cp.row())throw std::runtime_error("NPC exit evidence original snapshot pruned");
            const auto* bytes=sqlite3_column_blob(cp.get(),0);
            const auto size=sqlite3_column_bytes(cp.get(),0);
            if(!bytes || size<=0 || size>16*1024*1024)
                throw std::runtime_error("NPC exit evidence snapshot malformed");
            const auto original=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(size)));
            if(original.state_hash()!=parse_u64(sqlite3_column_text(cp.get(),1)) ||
               std::to_string(original.state_hash())!=hash ||
               original.tick()!=static_cast<std::uint64_t>(tick) || cp.row())
                throw std::runtime_error("NPC exit evidence physical state forged");
            const auto proof=assess_colony_exit_pressure(
                original,static_cast<std::uint64_t>(generation),source,npc);
            if(!proof || proof->source_owner!=owner ||
               proof->governance_event_id!=static_cast<std::uint64_t>(govt) ||
               static_cast<int>(proof->source_risk)!=risk)
                throw std::runtime_error("NPC exit evidence invented a resident or shortage");
        }
    }



    // EXIT-01C: read the committed latest appraisal strictly prior to a
    // generation. No retroactive recalculation under later leadership or
    // rapport, even if a different appraisal was recorded on the same tick.
    std::optional<colony_conflict::PersonalOutlook> personal_outlook_before(
        EntityId source,std::uint64_t npc,std::uint64_t generation,
        bool across_colonies=false) const{
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_personal_outlook'");
        if(!table.row())return std::nullopt;
        const std::string sql=std::string(
            "SELECT event_id,prior_event,world_generation,tick,")+
            "colony,npc,leader,interaction_event,governance_event,"+
            "risk,rapport,delta,discontent,considering_departure "+
            "FROM sg_colony_npc_personal_outlook "+
            (across_colonies?"WHERE npc=? AND world_generation<? ":
                             "WHERE colony=? AND npc=? AND world_generation<? ")+
            "ORDER BY world_generation DESC LIMIT 1";
        Statement q(db_,sql.c_str());
        if(!across_colonies)q.text(1,std::to_string(source));
        q.text(across_colonies?1:2,std::to_string(npc));
        q.i64(across_colonies?2:3,static_cast<sqlite3_int64>(generation));
        if(!q.row())return std::nullopt;
        colony_conflict::PersonalOutlook p;
        const auto event=sqlite3_column_int64(q.get(),0);
        const auto prior=sqlite3_column_int64(q.get(),1);
        const auto gen=sqlite3_column_int64(q.get(),2);
        const auto tick=sqlite3_column_int64(q.get(),3);
        const auto interaction=sqlite3_column_int64(q.get(),7);
        const auto governance=sqlite3_column_int64(q.get(),8);
        if(event<1 || prior<0 || gen<1 || tick<0 ||
           interaction<0 || governance<0)
            throw std::runtime_error("malformed historical personal appraisal");
        p.event_id=static_cast<std::uint64_t>(event);
        p.prior_event_id=static_cast<std::uint64_t>(prior);
        p.world_generation=static_cast<std::uint64_t>(gen);
        p.world_tick=static_cast<std::uint64_t>(tick);
        p.colony=parse_u64(sqlite3_column_text(q.get(),4));
        p.npc_id=parse_u64(sqlite3_column_text(q.get(),5));
        p.leader_id=parse_u64(sqlite3_column_text(q.get(),6));
        p.interaction_event_id=static_cast<std::uint64_t>(interaction);
        p.government_event_id=static_cast<std::uint64_t>(governance);
        const auto risk=sqlite3_column_int(q.get(),9);
        p.leader_rapport=sqlite3_column_int(q.get(),10);
        p.delta=sqlite3_column_int(q.get(),11);
        p.discontent=sqlite3_column_int(q.get(),12);
        const auto intending=sqlite3_column_int(q.get(),13);
        if(!p.colony || !p.npc_id || !p.leader_id ||
           (!across_colonies && p.colony!=source) ||
           p.npc_id!=npc || p.world_generation>=generation ||
           risk<0 || risk>3 || p.leader_rapport<-100 ||
           p.leader_rapport>100 || p.discontent<0 || p.discontent>100 ||
           (intending!=0 && intending!=1) || q.row())
            throw std::runtime_error("personal appraisal values or uniqueness forged");
        p.risk=static_cast<colony_safety::Risk>(risk);
        p.considering_departure=(intending==1);
        return p;
    }
    std::optional<colony_conflict::PersonalOutlook> assess_personal_outlook(
        const Simulation& world,std::uint64_t generation,
        EntityId source,std::uint64_t npc_id) const{
        if(!source || !npc_id || !generation)
            throw std::invalid_argument("personal outlook needs a named resident");
        const auto* colony=world.find(source);
        const auto* owned=world.colony_ship(source);
        const auto resident=world.npc_records().find(npc_id);
        if(!colony || !owned || colony->type!=EntityType::Ship ||
           !colony->owner_id || resident==world.npc_records().end() ||
           resident->second.home_ship!=source ||
           resident->second.location!=source ||
           resident->second.transport_id ||
           !world.npc_available_for_duty(npc_id))
            return std::nullopt;
        const auto govt=effective_governance(source,generation);
        if(govt.leader_id==npc_id)return std::nullopt;
        // An immigrant carries the same NPC identity and biographical
        // discontent into the new colony. Appraisals never reset to zero
        // merely because the legal home changed in ART02/ECON03.
        const auto previous=personal_outlook_before(
            source,npc_id,generation,true);
        if(previous){
            if(world.tick()<previous->world_tick ||
               world.tick()-previous->world_tick<8)return std::nullopt;
        }
        colony_conflict::PersonalOutlook p;
        p.world_generation=generation;p.world_tick=world.tick();
        p.colony=source;p.npc_id=npc_id;p.leader_id=govt.leader_id;
        p.government_event_id=govt.event_id;
        p.risk=colony_safety::assess(world,source,source).risk;
        // Only historically committed, directed actual interaction appraisals
        // involving this named resident and the then-current local leader.
        // Rapport is PERSON-SPECIFIC, and absence means neutral; neither
        // group-wide rumor nor leadership policy becomes everyone else's mood.
        Statement relation(db_,"SELECT id,rapport FROM social_interactions "
            "WHERE observer=? AND actor=? AND generation<? "
            "ORDER BY generation DESC,id DESC LIMIT 1");
        relation.text(1,std::to_string(npc_id));
        relation.text(2,std::to_string(govt.leader_id));
        relation.i64(3,static_cast<sqlite3_int64>(generation));
        if(relation.row()){
            const auto id=sqlite3_column_int64(relation.get(),0);
            const auto score=sqlite3_column_int(relation.get(),1);
            if(id<1 || score<-100 || score>100 || relation.row())
                throw std::runtime_error("malformed dated directional social interaction");
            p.interaction_event_id=static_cast<std::uint64_t>(id);
            p.leader_rapport=score;
        }
        switch(p.risk){
            case colony_safety::Risk::Safe: p.delta=-28;break;
            case colony_safety::Risk::ForecastShortage: p.delta=8;break;
            case colony_safety::Risk::Critical: p.delta=34;break;
            case colony_safety::Risk::Emergency: p.delta=46;break;
        }
        if(p.interaction_event_id){
            if(p.leader_rapport<=-12)p.delta+=12;
            else if(p.leader_rapport>=8)p.delta-=20;
        }
        const int old=previous?previous->discontent:0;
        p.discontent=std::clamp(old+p.delta,0,100);
        // A real deteriorating personal history, not one global crisis tick.
        // Once considering a move, the resident needs substantial sustained
        // improvement to abandon it; one repaired supply tick is not magic.
        // A legitimate NEW healthy residence relieves the immediate desire
        // to abandon the former colony without destroying historical feelings.
        // A new unsafe home can still activate a genuinely new local intent.
        const bool arrived_in_new_home=previous && previous->colony!=source;
        p.considering_departure=
            (arrived_in_new_home && p.risk==colony_safety::Risk::Safe)
            ? false :
            ((previous && previous->considering_departure &&
              !arrived_in_new_home)
              ? p.discontent>=30 : p.discontent>=65);
        p.prior_event_id=previous?previous->event_id:0;
        return p;
    }
    colony_conflict::PersonalOutlook record_personal_outlook(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t event_id,EntityId source,std::uint64_t npc_id){
        if(!event_id || event_id>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid dated personal appraisal event");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("personal appraisal world generation changed");
            auto p=assess_personal_outlook(world,generation,source,npc_id);
            if(!p)throw std::runtime_error(
                "no new physical or dated social conditions for this resident");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_npc_personal_outlook("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL CHECK(tick>=0),"
                "colony TEXT NOT NULL,npc TEXT NOT NULL,"
                "leader TEXT NOT NULL,governance_event INTEGER NOT NULL CHECK(governance_event>=0),"
                "interaction_event INTEGER NOT NULL CHECK(interaction_event>=0),"
                "risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3),"
                "rapport INTEGER NOT NULL CHECK(rapport BETWEEN -100 AND 100),"
                "prior_event INTEGER NOT NULL CHECK(prior_event>=0),"
                "delta INTEGER NOT NULL,discontent INTEGER NOT NULL "
                "CHECK(discontent BETWEEN 0 AND 100),"
                "considering_departure INTEGER NOT NULL CHECK(considering_departure IN(0,1)),"
                "UNIQUE(colony,npc,world_generation))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_personal_outlook_no_update "
                "BEFORE UPDATE ON sg_colony_npc_personal_outlook "
                "BEGIN SELECT RAISE(ABORT,'immutable dated personal history'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_personal_outlook_no_delete "
                "BEFORE DELETE ON sg_colony_npc_personal_outlook "
                "BEGIN SELECT RAISE(ABORT,'immutable dated personal history'); END");
            Statement ins(db_,"INSERT INTO sg_colony_npc_personal_outlook "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            ins.i64(1,static_cast<sqlite3_int64>(event_id));
            ins.i64(2,static_cast<sqlite3_int64>(generation));
            ins.text(3,std::to_string(world.state_hash()));
            ins.i64(4,static_cast<sqlite3_int64>(world.tick()));
            ins.text(5,std::to_string(source));ins.text(6,std::to_string(npc_id));
            ins.text(7,std::to_string(p->leader_id));
            ins.i64(8,static_cast<sqlite3_int64>(p->government_event_id));
            ins.i64(9,static_cast<sqlite3_int64>(p->interaction_event_id));
            ins.i64(10,static_cast<int>(p->risk));
            ins.i64(11,p->leader_rapport);
            ins.i64(12,static_cast<sqlite3_int64>(p->prior_event_id));
            ins.i64(13,p->delta);
            ins.i64(14,p->discontent);
            ins.i64(15,p->considering_departure?1:0);
            ins.execute();
            run(db_,"COMMIT");
            p->event_id=event_id;
            return *p;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void verify_personal_outlooks(std::uint64_t max_generation) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_personal_outlook'");
        if(!exists.row())return;
        for(const auto* name:{"sg_personal_outlook_no_update",
                              "sg_personal_outlook_no_delete"}){
            Statement guard(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            guard.text(1,name);
            if(!guard.row() || guard.row())
                throw std::runtime_error("personal-history immutability guard missing");
        }
        Statement rows(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "colony,npc,leader,governance_event,interaction_event,risk,rapport,"
            "prior_event,delta,discontent,considering_departure "
            "FROM sg_colony_npc_personal_outlook");
        while(rows.row()){
            const auto id=sqlite3_column_int64(rows.get(),0);
            const auto gen=sqlite3_column_int64(rows.get(),1);
            const auto hash=sqlite_str(rows.get(),2);
            const auto tick=sqlite3_column_int64(rows.get(),3);
            const auto source=parse_u64(sqlite3_column_text(rows.get(),4));
            const auto npc=parse_u64(sqlite3_column_text(rows.get(),5));
            if(id<1 || gen<1 || static_cast<std::uint64_t>(gen)>max_generation ||
               tick<0 || !source || !npc)
                throw std::runtime_error("invalid resident personal-history chronology");
            Statement snap(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            snap.i64(1,gen);
            if(!snap.row())throw std::runtime_error("personal-history source world pruned");
            const auto* bytes=sqlite3_column_blob(snap.get(),0);
            const auto n=sqlite3_column_bytes(snap.get(),0);
            if(!bytes || n<=0 || n>16*1024*1024)
                throw std::runtime_error("personal-history original snapshot malformed");
            const auto original=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(n)));
            if(original.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(original.state_hash())!=hash ||
               original.tick()!=static_cast<std::uint64_t>(tick) || snap.row())
                throw std::runtime_error("personal-history original world modified");
            const auto predicted=assess_personal_outlook(
                original,static_cast<std::uint64_t>(gen),source,npc);
            if(!predicted ||
               predicted->leader_id!=parse_u64(sqlite3_column_text(rows.get(),6)) ||
               predicted->government_event_id!=static_cast<std::uint64_t>(
                   sqlite3_column_int64(rows.get(),7)) ||
               predicted->interaction_event_id!=static_cast<std::uint64_t>(
                   sqlite3_column_int64(rows.get(),8)) ||
               static_cast<int>(predicted->risk)!=sqlite3_column_int(rows.get(),9) ||
               predicted->leader_rapport!=sqlite3_column_int(rows.get(),10) ||
               predicted->prior_event_id!=static_cast<std::uint64_t>(
                   sqlite3_column_int64(rows.get(),11)) ||
               predicted->delta!=sqlite3_column_int(rows.get(),12) ||
               predicted->discontent!=sqlite3_column_int(rows.get(),13) ||
               static_cast<int>(predicted->considering_departure)!=
                   sqlite3_column_int(rows.get(),14))
                throw std::runtime_error(
                    "resident personal memory, lived risk or social evidence forged");
        }
    }

    // EXIT-01B: a concrete receiving-owner-approved immigration admission.
    // This only makes an adult eligible for an existing real ferry voyage;
    // neither a fabricated departure nor an automatic labor/population edit.
    PersistentWorld::VoluntaryImmigrationAdmission assess_voluntary_admission(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t pressure_event,EntityId origin,EntityId destination,
        EntityId ferry,std::uint64_t npc,std::uint64_t transport_id,
        PlayerId receiving_owner,bool require_current_custody=false,
        bool historical_legacy_personal_rule=false) const{
        if(!pressure_event || !origin || !destination || !ferry ||
           origin==destination || ferry==origin || ferry==destination ||
           !npc || !transport_id || !receiving_owner)
            throw std::invalid_argument("incomplete NPC immigration admission");
        const auto* source=world.find(origin);
        const auto* target=world.find(destination);
        const auto* carrier=world.find(ferry);
        const auto* target_life=world.life_support(destination);
        const auto* ferry_life=world.life_support(ferry);
        const auto from=world.colony_ship(origin),to=world.colony_ship(destination);
        const auto person=world.npc_records().find(npc);
        if(!source || !target || !carrier || !from || !to ||
           !target_life || !ferry_life ||
           source->type!=EntityType::Ship ||
           target->type!=EntityType::Ship ||
           carrier->type!=EntityType::Ship ||
           !source->owner_id || target->owner_id!=receiving_owner ||
           (carrier->owner_id!=source->owner_id &&
            carrier->owner_id!=receiving_owner) ||
           carrier->system_id!=source->system_id ||
           person==world.npc_records().end() ||
           person->second.home_ship!=origin ||
           person->second.location!=origin ||
           person->second.transport_id ||
           person->second.assigned_site!=origin ||
           person->second.role!=NpcRole::Ship ||
           !world.npc_available_for_duty(npc) ||
           world.named_medics().count(npc) || from->crew<=1 ||
           to->crew>=1000 || world.voyage_status(transport_id)!=0)
            throw std::runtime_error("immigration admission lacks eligible real person or receiving colony");
        // The current solo-adult entry gate is deliberately conservative:
        // family closure needs the existing two-parent/child migration path.
        if(require_current_custody && has_child_or_dependent_link(origin))
            throw std::runtime_error("adult immigration must not bypass ART02 child custody");
        if(colony_safety::assess(world,destination,destination).risk!=
               colony_safety::Risk::Safe)
            throw std::runtime_error("receiver lacks healthy life support");
        const auto govt=effective_governance(origin,generation);
        if(govt.leader_id==npc)
            throw std::runtime_error("individual immigrant would vacate incumbent government");
        // Only this particular resident's PREVIOUSLY committed personal
        // history—not collective panic, a guessed sentiment or a single
        // physical shortage—can authorize their attempt to emigrate. A
        // recovering resident may cease wanting to leave even with a
        // historical critical-shortage witness on file.
        const auto own=personal_outlook_before(origin,npc,generation);
        if(!historical_legacy_personal_rule &&
           (!own || !own->considering_departure))
            throw std::runtime_error("resident has no active historically grounded decision to depart");
        Statement pressure(db_,"SELECT world_generation,colony,source_owner,npc,"
            "governance_event,risk FROM sg_colony_npc_exit_pressure WHERE event_id=?");
        pressure.i64(1,static_cast<sqlite3_int64>(pressure_event));
        if(!pressure.row())throw std::runtime_error("immigrant lacks real individual exit-pressure witness");
        const auto born=sqlite3_column_int64(pressure.get(),0);
        const auto source_id=parse_u64(sqlite3_column_text(pressure.get(),1));
        const auto source_owner=parse_u64(sqlite3_column_text(pressure.get(),2));
        const auto witnessed=parse_u64(sqlite3_column_text(pressure.get(),3));
        const auto past_govt=sqlite3_column_int64(pressure.get(),4);
        const auto old_risk=sqlite3_column_int(pressure.get(),5);
        if(born<=0 || static_cast<std::uint64_t>(born)>generation ||
           source_id!=origin || source_owner!=source->owner_id ||
           witnessed!=npc || past_govt<0 ||
           old_risk<2 || old_risk>3 || pressure.row())
            throw std::runtime_error("pressure witness does not belong to this adult/current government");
        // Read-only backward compatibility: the earlier EXIT-01B issue
        // required critical scarcity *at the actual permit generation*, not
        // a personal mood record which did not yet exist.
        if(historical_legacy_personal_rule &&
           (static_cast<int>(colony_safety::assess(
                  world,origin,origin).risk)<
                static_cast<int>(colony_safety::Risk::Critical) ||
            static_cast<std::uint64_t>(past_govt)!=govt.event_id))
            throw std::runtime_error("legacy immigrant had no real crisis at issuance");
        PersistentWorld::VoluntaryImmigrationAdmission a;
        a.exit_pressure_event_id=pressure_event;
        a.personal_outlook_event_id=historical_legacy_personal_rule?0:own->event_id;
        a.world_generation=generation;
        a.npc_id=npc;a.transport_id=transport_id;
        a.origin=origin;a.destination=destination;a.ferry=ferry;
        a.original_owner=source->owner_id;a.receiving_owner=receiving_owner;
        return a;
    }
    PersistentWorld::VoluntaryImmigrationAdmission record_voluntary_admission(
        const Simulation& world,std::uint64_t generation,std::uint64_t event,
        std::uint64_t pressure,EntityId origin,EntityId destination,EntityId ferry,
        std::uint64_t npc,std::uint64_t transport,PlayerId receiving_owner){
        if(!event || event>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid immigrant admission event ID");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale world for immigrant admission");
            auto a=assess_voluntary_admission(
                world,generation,pressure,origin,destination,ferry,npc,
                transport,receiving_owner,true);
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_voluntary_immigration("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "pressure_event INTEGER NOT NULL UNIQUE,"
                "origin TEXT NOT NULL,destination TEXT NOT NULL,"
                "ferry TEXT NOT NULL,npc TEXT NOT NULL,"
                "transport TEXT NOT NULL UNIQUE,"
                "original_owner TEXT NOT NULL,receiving_owner TEXT NOT NULL,"
                "governance_event INTEGER NOT NULL CHECK(governance_event>=0),"
                "personal_intent_event INTEGER NOT NULL DEFAULT 0 "
                "CHECK(personal_intent_event>=0))");
            // Pre-EXIT01C saves have a 13-column immutable admission table;
            // preserve all its original records with a typed zero legacy
            // personal intent, rather than rewriting their historical terms.
            bool has_intent=false;
            {Statement info(db_,"PRAGMA table_info(sg_colony_voluntary_immigration)");
             while(info.row())if(sqlite_str(info.get(),1)=="personal_intent_event")
                 has_intent=true;}
            if(!has_intent)
                run(db_,"ALTER TABLE sg_colony_voluntary_immigration ADD COLUMN "
                    "personal_intent_event INTEGER NOT NULL DEFAULT 0 "
                    "CHECK(personal_intent_event>=0)");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_voluntary_immigration_no_update "
                "BEFORE UPDATE ON sg_colony_voluntary_immigration "
                "BEGIN SELECT RAISE(ABORT,'immutable receiving colony admission'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_voluntary_immigration_no_delete "
                "BEFORE DELETE ON sg_colony_voluntary_immigration "
                "BEGIN SELECT RAISE(ABORT,'immutable receiving colony admission'); END");
            Statement ins(db_,"INSERT INTO sg_colony_voluntary_immigration ("
                "event_id,world_generation,world_hash,tick,pressure_event,"
                "origin,destination,ferry,npc,transport,original_owner,"
                "receiving_owner,governance_event,personal_intent_event) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            ins.i64(1,static_cast<sqlite3_int64>(event));
            ins.i64(2,static_cast<sqlite3_int64>(generation));
            ins.text(3,std::to_string(world.state_hash()));
            ins.i64(4,static_cast<sqlite3_int64>(world.tick()));
            ins.i64(5,static_cast<sqlite3_int64>(pressure));
            ins.text(6,std::to_string(origin));
            ins.text(7,std::to_string(destination));
            ins.text(8,std::to_string(ferry));
            ins.text(9,std::to_string(npc));
            ins.text(10,std::to_string(transport));
            ins.text(11,std::to_string(a.original_owner));
            ins.text(12,std::to_string(receiving_owner));
            ins.i64(13,static_cast<sqlite3_int64>(
                effective_governance(origin,generation).event_id));
            ins.i64(14,static_cast<sqlite3_int64>(a.personal_outlook_event_id));
            if(!a.personal_outlook_event_id)
                throw std::runtime_error("new admission cannot omit actual personal intent");
            ins.execute();
            run(db_,"COMMIT");
            a.event_id=event;
            return a;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    PersistentWorld::VoluntaryImmigrationAdmission immigration_admission(
        std::uint64_t event) const{
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration'");
        if(!table.row())throw std::runtime_error("no immigrant admission ledger");
        bool has_intent=false;
        {Statement cols(db_,"PRAGMA table_info(sg_colony_voluntary_immigration)");
         while(cols.row())if(sqlite_str(cols.get(),1)=="personal_intent_event")
             has_intent=true;}
        const std::string read_sql=std::string(
            "SELECT world_generation,pressure_event,origin,")+
            "destination,ferry,npc,transport,original_owner,receiving_owner,"+
            (has_intent?"personal_intent_event":"0")+
            " FROM sg_colony_voluntary_immigration WHERE event_id=?";
        Statement q(db_,read_sql.c_str());
        q.i64(1,static_cast<sqlite3_int64>(event));
        if(!q.row())throw std::runtime_error("unknown immigrant admission");
        PersistentWorld::VoluntaryImmigrationAdmission a;
        a.event_id=event;
        a.world_generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),0));
        a.exit_pressure_event_id=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),1));
        a.origin=parse_u64(sqlite3_column_text(q.get(),2));
        a.destination=parse_u64(sqlite3_column_text(q.get(),3));
        a.ferry=parse_u64(sqlite3_column_text(q.get(),4));
        a.npc_id=parse_u64(sqlite3_column_text(q.get(),5));
        a.transport_id=parse_u64(sqlite3_column_text(q.get(),6));
        a.original_owner=parse_u64(sqlite3_column_text(q.get(),7));
        a.receiving_owner=parse_u64(sqlite3_column_text(q.get(),8));
        const auto intended=sqlite3_column_int64(q.get(),9);
        if(intended<0)throw std::runtime_error("invalid immigration personal reason");
        a.personal_outlook_event_id=static_cast<std::uint64_t>(intended);
        if(q.row())throw std::runtime_error("duplicate immigrant admission");
        return a;
    }
    // EXIT-01D: host supervisor helpers read only the authoritative SQLite
    // journals. Auto allocated IDs come from the committed real ledger, not
    // from untrusted client arguments or guessed world timestamps.
    bool has_founded_governance(EntityId source) const{
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_conflict_agenda'");
        if(!t.row())return false;
        Statement q(db_,"SELECT 1 FROM sg_colony_conflict_agenda WHERE colony=?");
        q.text(1,std::to_string(source));
        return q.row();
    }
    std::uint64_t next_personal_outlook_event_id() const{
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_personal_outlook'");
        if(!t.row())return 1;
        Statement q(db_,"SELECT COALESCE(MAX(event_id),0) "
            "FROM sg_colony_npc_personal_outlook");
        if(!q.row())throw std::runtime_error("missing personal history counter");
        const auto old=sqlite3_column_int64(q.get(),0);
        if(old<0 || old==std::numeric_limits<sqlite3_int64>::max())
            throw std::overflow_error("personal history event IDs exhausted");
        return static_cast<std::uint64_t>(old)+1;
    }
    bool personal_outlook_recorded_at(EntityId origin,std::uint64_t npc,
                                      std::uint64_t generation) const{
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_personal_outlook'");
        if(!t.row())return false;
        Statement q(db_,"SELECT 1 FROM sg_colony_npc_personal_outlook "
            "WHERE colony=? AND npc=? AND world_generation=?");
        q.text(1,std::to_string(origin));
        q.text(2,std::to_string(npc));
        q.i64(3,static_cast<sqlite3_int64>(generation));
        return q.row();
    }
    std::optional<std::uint64_t> existing_exit_pressure(
        EntityId origin,std::uint64_t npc) const{
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_npc_exit_pressure'");
        if(!t.row())return std::nullopt;
        Statement q(db_,"SELECT event_id FROM sg_colony_npc_exit_pressure "
            "WHERE colony=? AND npc=? ORDER BY world_generation DESC LIMIT 1");
        q.text(1,std::to_string(origin));
        q.text(2,std::to_string(npc));
        if(!q.row())return std::nullopt;
        const auto v=sqlite3_column_int64(q.get(),0);
        if(v<=0 || q.row())throw std::runtime_error("invalid past scarcity witness");
        return static_cast<std::uint64_t>(v);
    }
    std::optional<PersistentWorld::VoluntaryImmigrationAdmission>
    outstanding_voluntary_admission(EntityId origin,std::uint64_t npc) const{
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration'");
        if(!t.row())return std::nullopt;
        Statement settled(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration_settled'");
        const bool has_receipts=settled.row();
        const std::string sql=std::string(
            "SELECT a.event_id FROM sg_colony_voluntary_immigration a ")+
            (has_receipts?
             "LEFT JOIN sg_colony_voluntary_immigration_settled s "
             "ON s.admission_event=a.event_id ":"")+
            "WHERE a.origin=? AND a.npc=? "+
            (has_receipts?"AND s.admission_event IS NULL ":"")+
            "ORDER BY a.event_id DESC LIMIT 1";
        Statement q(db_,sql.c_str());
        q.text(1,std::to_string(origin));q.text(2,std::to_string(npc));
        if(!q.row())return std::nullopt;
        const auto event=sqlite3_column_int64(q.get(),0);
        if(event<=0 || q.row())throw std::runtime_error("invalid outstanding immigration");
        return immigration_admission(static_cast<std::uint64_t>(event));
    }

    bool immigration_pending_for_transport(
        std::uint64_t npc,std::uint64_t transport) const{
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration'");
        if(!table.row())return false;
        Statement q(db_,"SELECT 1 FROM sg_colony_voluntary_immigration "
            "WHERE npc=? AND transport=?");
        q.text(1,std::to_string(npc));q.text(2,std::to_string(transport));
        return q.row();
    }
    void verify_colony_voluntary_immigration(std::uint64_t max_generation) const{
        Statement present(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration'");
        Statement finalized(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration_settled'");
        const bool have_permits=present.row(),have_settled=finalized.row();
        if(!have_permits){if(have_settled)throw std::runtime_error(
            "immigration settlement without admissions");return;}
        for(const auto* trigger:{"sg_voluntary_immigration_no_update",
                                 "sg_voluntary_immigration_no_delete"}){
            Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?");
            t.text(1,trigger);
            if(!t.row() || t.row())throw std::runtime_error(
                "missing immutable immigration admission guard");
        }
        if(have_settled)for(const auto* trigger:{
            "sg_voluntary_immigration_settled_no_update",
            "sg_voluntary_immigration_settled_no_delete"}){
            Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?");
            t.text(1,trigger);
            if(!t.row() || t.row())throw std::runtime_error(
                "missing immutable immigration settlement guard");
        }
        bool has_intent=false;
        {Statement cols(db_,"PRAGMA table_info(sg_colony_voluntary_immigration)");
         while(cols.row())if(sqlite_str(cols.get(),1)=="personal_intent_event")
             has_intent=true;}
        const std::string verify_sql=std::string(
            "SELECT event_id,world_generation,world_hash,tick,")+
            "pressure_event,origin,destination,ferry,npc,transport,"+
            "original_owner,receiving_owner,governance_event,"+
            (has_intent?"personal_intent_event":"0")+
            " FROM sg_colony_voluntary_immigration";
        Statement rows(db_,verify_sql.c_str());
        while(rows.row()){
            const auto id=sqlite3_column_int64(rows.get(),0);
            const auto gen=sqlite3_column_int64(rows.get(),1);
            const auto hash=sqlite_str(rows.get(),2);
            const auto tick=sqlite3_column_int64(rows.get(),3);
            const auto pressure=sqlite3_column_int64(rows.get(),4);
            const auto origin=parse_u64(sqlite3_column_text(rows.get(),5));
            const auto dest=parse_u64(sqlite3_column_text(rows.get(),6));
            const auto ferry=parse_u64(sqlite3_column_text(rows.get(),7));
            const auto npc=parse_u64(sqlite3_column_text(rows.get(),8));
            const auto transport=parse_u64(sqlite3_column_text(rows.get(),9));
            const auto old_owner=parse_u64(sqlite3_column_text(rows.get(),10));
            const auto receiver=parse_u64(sqlite3_column_text(rows.get(),11));
            const auto gov=sqlite3_column_int64(rows.get(),12);
            const auto intent=sqlite3_column_int64(rows.get(),13);
            if(id<1 || gen<1 || static_cast<std::uint64_t>(gen)>max_generation ||
               tick<0 || pressure<1 || gov<0 || intent<0 ||
               !origin || !dest || !ferry ||
               !npc || !transport || !old_owner || !receiver)
                throw std::runtime_error("invalid immigration admission chronology");
            Statement snap(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
            snap.i64(1,gen);
            if(!snap.row())throw std::runtime_error("immigration source generation pruned");
            const auto* bytes=sqlite3_column_blob(snap.get(),0);
            const auto len=sqlite3_column_bytes(snap.get(),0);
            if(!bytes || len<=0 || len>16*1024*1024)
                throw std::runtime_error("immigration source snapshot malformed");
            const auto original=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(len)));
            if(original.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(original.state_hash())!=hash ||
               original.tick()!=static_cast<std::uint64_t>(tick) || snap.row())
                throw std::runtime_error("immigration admission original world forged");
            // Preserve authentic EXIT-01B admissions issued before the
            // personal-history scheme existed: they have no intent column or
            // a zero default after the additive schema upgrade. A post-v88
            // forged admission cannot erase its nonzero personal decision and
            // claim the older scarcity-only rule instead.
            const bool legacy=intent==0;
            // A schema-column downgrade is not sufficient to masquerade a
            // newly issued personal-intent permit as an older one.
            if(legacy){
                Statement personal_table(db_,"SELECT 1 FROM sqlite_master "
                    "WHERE type='table' AND name='sg_colony_npc_personal_outlook'");
                if(personal_table.row()){
                    Statement before(db_,"SELECT 1 FROM sg_colony_npc_personal_outlook "
                        "WHERE world_generation<? LIMIT 1");
                    before.i64(1,gen);
                    if(before.row())throw std::runtime_error(
                        "post-personal-history admission stripped its actual NPC decision");
                }
            }
            const auto calculated=assess_voluntary_admission(original,
                static_cast<std::uint64_t>(gen),static_cast<std::uint64_t>(pressure),
                origin,dest,ferry,npc,transport,receiver,false,legacy);
            if(calculated.original_owner!=old_owner ||
               calculated.personal_outlook_event_id!=static_cast<std::uint64_t>(intent) ||
               effective_governance(origin,static_cast<std::uint64_t>(gen)).event_id!=
                   static_cast<std::uint64_t>(gov))
                throw std::runtime_error("immigration consent, personal intent or former government forged");
            bool moved=false;
            std::string command;
            std::uint64_t committed=0;
            // If not yet migrated, admission remains pending after a restart.
            Statement migrate_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_society_migration'");
            if(migrate_table.row()){
                Statement migration(db_,"SELECT command_id,world_generation "
                    "FROM sg_society_migration WHERE npc_id=? AND transport_id=? "
                    "AND origin=? AND destination=?");
                migration.text(1,std::to_string(npc));migration.text(2,std::to_string(transport));
                migration.text(3,std::to_string(origin));migration.text(4,std::to_string(dest));
                if(migration.row()){
                    moved=true;command=sqlite_str(migration.get(),0);
                    committed=static_cast<std::uint64_t>(sqlite3_column_int64(migration.get(),1));
                    if(migration.row() || committed<=static_cast<std::uint64_t>(gen))
                        throw std::runtime_error("migration time-traveled before acceptance");
                }
            }
            if(moved){
                // The original authoritative post-execution world must
                // independently contain the SAME physical ferry and named
                // manifest promised by the receiving-colony admission.
                Statement arrived(db_,"SELECT state,checksum FROM snapshots "
                    "WHERE generation=?");
                arrived.i64(1,static_cast<sqlite3_int64>(committed));
                if(!arrived.row())throw std::runtime_error(
                    "immigration original arrival snapshot pruned");
                const auto* arrived_bytes=sqlite3_column_blob(arrived.get(),0);
                const auto arrived_len=sqlite3_column_bytes(arrived.get(),0);
                if(!arrived_bytes || arrived_len<=0 || arrived_len>16*1024*1024)
                    throw std::runtime_error("immigration original arrival snapshot malformed");
                const auto arrival_world=Simulation::from_snapshot(std::string(
                    static_cast<const char*>(arrived_bytes),
                    static_cast<std::size_t>(arrived_len)));
                if(arrival_world.state_hash()!=parse_u64(
                       sqlite3_column_text(arrived.get(),1)) ||
                   !arrival_world.completed_voyage_matches(
                       transport,ferry,origin,dest,std::vector<std::uint64_t>{npc}) ||
                   arrival_world.npc_records().at(npc).home_ship!=dest ||
                   arrived.row())
                    throw std::runtime_error("immigration ferry/custody history was forged");
            }
            if(moved && !have_settled)
                throw std::runtime_error("voluntary immigration lost settlement receipt");
            if(have_settled){
                Statement receipt(db_,"SELECT migration_command,world_generation "
                    "FROM sg_colony_voluntary_immigration_settled WHERE admission_event=?");
                receipt.i64(1,id);
                const bool arrived=receipt.row();
                if(arrived){
                    const auto linked=sqlite_str(receipt.get(),0);
                    const auto linked_gen=sqlite3_column_int64(receipt.get(),1);
                    if(!moved || command!=linked ||
                       linked_gen!=static_cast<sqlite3_int64>(committed) ||
                       receipt.row())throw std::runtime_error(
                           "voluntary immigration receipt is not a committed migration");
                }else if(moved)throw std::runtime_error(
                    "voluntary migrant moved without immutable matching receipt");
            }
        }
        if(have_settled){
            Statement orphans(db_,"SELECT 1 FROM sg_colony_voluntary_immigration_settled s "
                "LEFT JOIN sg_colony_voluntary_immigration a "
                "ON a.event_id=s.admission_event WHERE a.event_id IS NULL LIMIT 1");
            if(orphans.row())throw std::runtime_error(
                "orphan completed immigration without an original accepted source");
        }
    }
    bool verified_voluntary_immigration(std::uint64_t event) const{
        Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_voluntary_immigration_settled'");
        if(!table.row())return false;
        Statement q(db_,"SELECT 1 FROM sg_colony_voluntary_immigration_settled "
            "WHERE admission_event=?");
        q.i64(1,static_cast<sqlite3_int64>(event));
        return q.row();
    }


    void validate_voluntary_settlement(
        const Simulation& world,std::uint64_t generation,
        const SocietyMigrationPlan& plan) const{
        if(!plan.voluntary_admission_event_id || plan.co_parent_id ||
           !plan.dependents.empty())
            throw std::runtime_error("solo voluntary immigration cannot bypass household closure");
        const auto a=immigration_admission(plan.voluntary_admission_event_id);
        if(a.world_generation>=generation ||
           a.npc_id!=plan.npc_id || a.transport_id!=plan.transport_id ||
           a.origin!=plan.origin || a.destination!=plan.destination)
            throw std::runtime_error("settlement is not the admitted physical immigration route");
        const auto* source=world.find(a.origin),*dest=world.find(a.destination);
        if(!source || !dest || source->owner_id!=a.original_owner ||
           dest->owner_id!=a.receiving_owner)
            throw std::runtime_error("receiving-owner or source-owner authority changed");
        if(colony_safety::assess(world,a.destination,a.destination).risk!=
           colony_safety::Risk::Safe)
            throw std::runtime_error("receiving colony became unsafe during voyage");
        if(!world.completed_voyage_matches(
               a.transport_id,a.ferry,a.origin,a.destination,
               std::vector<std::uint64_t>{a.npc_id}))
            throw std::runtime_error("immigrant lacks exact completed admitted ferry and passenger manifest");
        const auto person=world.npc_records().find(a.npc_id);
        if(person==world.npc_records().end() ||
           person->second.home_ship!=a.origin ||
           person->second.location!=a.destination ||
           person->second.transport_id)
            throw std::runtime_error("immigrant not at admitted destination");
        if(verified_voluntary_immigration(a.event_id))
            throw std::runtime_error("voluntary immigration already settled");
    }
    void insert_voluntary_settlement_receipt(sqlite3* db,
        std::uint64_t admission_event,const SocietyMigrationPlan& p,
        std::uint64_t committed_generation) const{
        if(!db || !admission_event || !p.command_id || !committed_generation)
            throw std::runtime_error("incomplete immigration effect binding");
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_voluntary_immigration_settled("
            "admission_event INTEGER PRIMARY KEY CHECK(admission_event>0),"
            "migration_command TEXT NOT NULL UNIQUE,"
            "world_generation INTEGER NOT NULL CHECK(world_generation>0))");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_voluntary_immigration_settled_no_update "
            "BEFORE UPDATE ON sg_colony_voluntary_immigration_settled "
            "BEGIN SELECT RAISE(ABORT,'immutable completed immigration'); END");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_voluntary_immigration_settled_no_delete "
            "BEFORE DELETE ON sg_colony_voluntary_immigration_settled "
            "BEGIN SELECT RAISE(ABORT,'immutable completed immigration'); END");
        // The normal authoritative ART02 family move, ECON03 regional
        // transfer and source/destination world update have already been
        // checked/inserted in this SAME Store::commit SQL transaction.
        Statement registered(db,"SELECT command_id,world_generation,npc_id,"
            "transport_id,origin,destination,household_count "
            "FROM sg_society_migration WHERE command_id=?");
        registered.text(1,std::to_string(p.command_id));
        if(!registered.row() ||
           sqlite_str(registered.get(),0)!=std::to_string(p.command_id) ||
           sqlite3_column_int64(registered.get(),1)!=
               static_cast<sqlite3_int64>(committed_generation) ||
           sqlite_str(registered.get(),2)!=std::to_string(p.npc_id) ||
           sqlite_str(registered.get(),3)!=std::to_string(p.transport_id) ||
           sqlite_str(registered.get(),4)!=std::to_string(p.origin) ||
           sqlite_str(registered.get(),5)!=std::to_string(p.destination) ||
           sqlite3_column_int64(registered.get(),6)!=1 ||
           registered.row())
            throw std::runtime_error("voluntary settlement lacks exactly one native immigration transfer");
        Statement insert(db,"INSERT INTO sg_colony_voluntary_immigration_settled "
            "VALUES(?,?,?)");
        insert.i64(1,static_cast<sqlite3_int64>(admission_event));
        insert.text(2,std::to_string(p.command_id));
        insert.i64(3,static_cast<sqlite3_int64>(committed_generation));
        insert.execute();
    }

    colony_conflict::Decision assess_colony_aggression(
        const Simulation& world,std::uint64_t generation,
        EntityId source,std::uint64_t contact_event_id,
        bool governed=false) const{
        if(!source || !contact_event_id || contact_event_id>
           static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("missing physical aggression provenance");
        Statement agenda(db_,"SELECT event_id,leader,agenda FROM "
            "sg_colony_conflict_agenda WHERE colony=?");
        agenda.text(1,std::to_string(source));
        if(!agenda.row())throw std::runtime_error("colony has no durable leadership agenda");
        auto leader=parse_u64(sqlite3_column_text(agenda.get(),1));
        int disposition=sqlite3_column_int(agenda.get(),2);
        if(!leader || disposition<0 || disposition>3 || agenda.row())
            throw std::runtime_error("invalid founding colony agenda");
        colony_conflict::GovernanceEpoch epoch;
        if(governed){
            epoch=effective_governance(source,generation);
            leader=epoch.leader_id;
            disposition=static_cast<int>(epoch.agenda);
        }
        Statement contact(db_,"SELECT world_generation,tick,source,observer,"
            "foreign_ship,foreign_owner,system,distance_squared "
            "FROM sg_colony_watch_contacts WHERE event_id=?");
        contact.i64(1,static_cast<sqlite3_int64>(contact_event_id));
        if(!contact.row())throw std::runtime_error("no real witnessed foreign-ship contact");
        const auto observed_generation=sqlite3_column_int64(contact.get(),0);
        colony_defense::Contact seen;
        seen.world_tick=static_cast<std::uint64_t>(
            sqlite3_column_int64(contact.get(),1));
        seen.source=parse_u64(sqlite3_column_text(contact.get(),2));
        seen.observer_npc=parse_u64(sqlite3_column_text(contact.get(),3));
        seen.foreign_ship=parse_u64(sqlite3_column_text(contact.get(),4));
        seen.foreign_owner=parse_u64(sqlite3_column_text(contact.get(),5));
        seen.system=static_cast<std::uint32_t>(
            sqlite3_column_int64(contact.get(),6));
        seen.distance_squared=static_cast<std::uint64_t>(
            sqlite3_column_int64(contact.get(),7));
        if(observed_generation!=static_cast<sqlite3_int64>(generation) ||
           seen.source!=source || seen.world_tick!=world.tick() ||
           !seen.foreign_ship || contact.row())
            throw std::runtime_error("stale/wrong-colony contact cannot motivate action");
        auto d=colony_conflict::evaluate(world,source,leader,
            static_cast<colony_conflict::Agenda>(disposition),seen,contact_event_id);
        if(governed){
            d.governance_event_id=epoch.event_id;
            d.diplomatic_policy=epoch.policy;
        }
        return d;
    }
    colony_conflict::Decision record_colony_aggression(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t decision_id,EntityId source,std::uint64_t contact_id){
        if(!decision_id || decision_id>
           static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           decision_id==contact_id)
            throw std::invalid_argument("invalid or reused offensive decision id");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale world generation for offensive intent");
            Statement dated(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_diplomatic_intent'");
            if(dated.row()){
                Statement used(db_,"SELECT 1 FROM sg_colony_diplomatic_intent "
                    "WHERE contact_event=?");
                used.i64(1,static_cast<sqlite3_int64>(contact_id));
                if(used.row())throw std::runtime_error(
                    "contact already consumed by a dated strategic intent");
            }
            // After an epoch takes effect, the legacy founder-only API may
            // not bypass the newly resident representative's political rules.
            if(effective_governance(source,generation).event_id)
                throw std::runtime_error("founder-only strategy retired after succession");
            const auto choice=assess_colony_aggression(world,generation,source,contact_id);
            Statement agenda(db_,"SELECT event_id FROM sg_colony_conflict_agenda "
                "WHERE colony=?");
            agenda.text(1,std::to_string(source));
            if(!agenda.row())throw std::runtime_error("no leadership agenda");
            const auto agenda_event=sqlite3_column_int64(agenda.get(),0);
            if(agenda.row())throw std::runtime_error("multiple colony agendas");
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_conflict_intent("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL,leader TEXT NOT NULL,"
                "agenda_event INTEGER NOT NULL,contact_event INTEGER NOT NULL UNIQUE,"
                "target_ship TEXT NOT NULL,target_owner TEXT NOT NULL,"
                "motive INTEGER NOT NULL CHECK(motive BETWEEN 0 AND 4),"
                "posture INTEGER NOT NULL CHECK(posture BETWEEN 0 AND 4),"
                "risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_conflict_intent_no_update "
                "BEFORE UPDATE ON sg_colony_conflict_intent "
                "BEGIN SELECT RAISE(ABORT,'immutable colony strategic intent'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_conflict_intent_no_delete "
                "BEFORE DELETE ON sg_colony_conflict_intent "
                "BEGIN SELECT RAISE(ABORT,'immutable colony strategic intent'); END");
            Statement ins(db_,"INSERT INTO sg_colony_conflict_intent "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
            ins.i64(1,static_cast<sqlite3_int64>(decision_id));
            ins.i64(2,static_cast<sqlite3_int64>(generation));
            ins.text(3,std::to_string(world.state_hash()));
            ins.i64(4,static_cast<sqlite3_int64>(world.tick()));
            ins.text(5,std::to_string(source));
            ins.text(6,std::to_string(choice.leader_id));
            ins.i64(7,agenda_event);
            ins.i64(8,static_cast<sqlite3_int64>(contact_id));
            ins.text(9,std::to_string(choice.target_ship));
            ins.text(10,std::to_string(choice.target_owner));
            ins.i64(11,static_cast<int>(choice.motive));
            ins.i64(12,static_cast<int>(choice.posture));
            ins.i64(13,static_cast<int>(choice.source_risk));
            ins.execute();
            run(db_,"COMMIT");
            return choice;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;
        }
    }
    void verify_colony_conflicts(std::uint64_t max_generation) const{
        Statement have_agenda(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_conflict_agenda'");
        Statement have_intent(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_conflict_intent'");
        const bool agenda_exists=have_agenda.row(),intent_exists=have_intent.row();
        if(!agenda_exists && !intent_exists)return;
        if(!agenda_exists)throw std::runtime_error("colony intent without agenda");
        for(const auto* tr:{"sg_conflict_agenda_no_update",
                            "sg_conflict_agenda_no_delete"}){
            Statement q(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            q.text(1,tr);
            if(!q.row() || q.row())
                throw std::runtime_error("colony founding intent guard missing");
        }
        if(intent_exists){
            for(const auto* tr:{"sg_conflict_intent_no_update",
                                "sg_conflict_intent_no_delete"}){
                Statement q(db_,"SELECT 1 FROM sqlite_master "
                    "WHERE type='trigger' AND name=?");
                q.text(1,tr);
                if(!q.row() || q.row())
                    throw std::runtime_error("colony strategic intent guard missing");
            }
        }
        // Cross-check leadership founding against the original preserved
        // physical colony/leader snapshot, not current possibly changed roles.
        Statement agendas(db_,"SELECT event_id,world_generation,world_hash,"
            "tick,colony,leader,agenda FROM sg_colony_conflict_agenda");
        while(agendas.row()){
            const auto generation=sqlite3_column_int64(agendas.get(),1);
            const auto expected_hash=sqlite_str(agendas.get(),2);
            const auto tick=sqlite3_column_int64(agendas.get(),3);
            const auto colony=parse_u64(sqlite3_column_text(agendas.get(),4));
            const auto leader=parse_u64(sqlite3_column_text(agendas.get(),5));
            const auto disposition=sqlite3_column_int(agendas.get(),6);
            if(sqlite3_column_int64(agendas.get(),0)<1 || generation<1 ||
               static_cast<std::uint64_t>(generation)>max_generation ||
               tick<0 || !colony || !leader || disposition<0 || disposition>3)
                throw std::runtime_error("historical colony agenda invalid");
            Statement snap(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
            snap.i64(1,generation);
            if(!snap.row())throw std::runtime_error("founding colony snapshot pruned");
            const auto size=sqlite3_column_bytes(snap.get(),0);
            const auto* bytes=sqlite3_column_blob(snap.get(),0);
            if(!bytes || size<=0 || size>16*1024*1024)
                throw std::runtime_error("founding colony snapshot malformed");
            const auto w=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(size)));
            const auto* entity=w.find(colony);
            const auto person=w.npc_records().find(leader);
            if(w.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(w.state_hash())!=expected_hash ||
               w.tick()!=static_cast<std::uint64_t>(tick) || snap.row() ||
               !entity || entity->type!=EntityType::Ship ||
               !entity->owner_id || !w.colony_ship(colony) ||
               person==w.npc_records().end() ||
               person->second.home_ship!=colony ||
               person->second.location!=colony ||
               person->second.transport_id ||
               !w.npc_available_for_duty(leader))
                throw std::runtime_error("colony political agenda origin forged");
        }
        if(!intent_exists)return;
        Statement decisions(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "colony,leader,agenda_event,contact_event,target_ship,target_owner,"
            "motive,posture,risk FROM sg_colony_conflict_intent");
        while(decisions.row()){
            const auto event=sqlite3_column_int64(decisions.get(),0);
            const auto gen=sqlite3_column_int64(decisions.get(),1);
            const auto hash=sqlite_str(decisions.get(),2);
            const auto tick=sqlite3_column_int64(decisions.get(),3);
            const auto source=parse_u64(sqlite3_column_text(decisions.get(),4));
            const auto leader=parse_u64(sqlite3_column_text(decisions.get(),5));
            const auto agenda_event=sqlite3_column_int64(decisions.get(),6);
            const auto contact_event=sqlite3_column_int64(decisions.get(),7);
            const auto target=parse_u64(sqlite3_column_text(decisions.get(),8));
            const auto owner=parse_u64(sqlite3_column_text(decisions.get(),9));
            const auto motive=sqlite3_column_int(decisions.get(),10);
            const auto posture=sqlite3_column_int(decisions.get(),11);
            const auto risk=sqlite3_column_int(decisions.get(),12);
            if(event<1 || gen<1 || static_cast<std::uint64_t>(gen)>max_generation ||
               tick<0 || !source || !leader || !target || !owner ||
               agenda_event<1 || contact_event<1)
                throw std::runtime_error("offensive strategic intent malformed");
            Statement linked(db_,"SELECT event_id FROM sg_colony_conflict_agenda "
                "WHERE colony=? AND leader=?");
            linked.text(1,std::to_string(source));linked.text(2,std::to_string(leader));
            if(!linked.row() ||
               sqlite3_column_int64(linked.get(),0)!=agenda_event ||
               linked.row())
                throw std::runtime_error("offensive intent invented a leader");
            Statement cp(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
            cp.i64(1,gen);
            if(!cp.row())throw std::runtime_error("offensive decision snapshot pruned");
            const auto len=sqlite3_column_bytes(cp.get(),0);
            const auto* bytes=sqlite3_column_blob(cp.get(),0);
            if(!bytes || len<=0 || len>16*1024*1024)
                throw std::runtime_error("offensive decision snapshot malformed");
            const auto w=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(len)));
            if(w.state_hash()!=parse_u64(sqlite3_column_text(cp.get(),1)) ||
               std::to_string(w.state_hash())!=hash ||
               w.tick()!=static_cast<std::uint64_t>(tick) || cp.row())
                throw std::runtime_error("offensive decision physical provenance forged");
            const auto recomputed=assess_colony_aggression(w,
                static_cast<std::uint64_t>(gen),source,
                static_cast<std::uint64_t>(contact_event));
            if(recomputed.leader_id!=leader || recomputed.target_ship!=target ||
               recomputed.target_owner!=owner ||
               static_cast<int>(recomputed.motive)!=motive ||
               static_cast<int>(recomputed.posture)!=posture ||
               static_cast<int>(recomputed.source_risk)!=risk)
                throw std::runtime_error("offensive posture or motive replaced");
        }
    }
    struct GrievanceEvidence {
        std::uint64_t leader=0,agenda_event=0,incident_generation=0;
        PlayerId actor_owner=0;
        std::string incident_id;
        int fidelity=0,valence=0,confidence=0;
    };
    GrievanceEvidence interpret_colony_incident(
        const Simulation& present,std::uint64_t generation,
        EntityId source,std::uint64_t knowledge_id) const{
        if(!source || !knowledge_id)throw std::invalid_argument(
            "colony interpretation requires real source and NPC knowledge");
        // A newly appointed leader can now receive and interpret genuine
        // GRSM claims. Earlier revisions still re-evaluate under the founder
        // when audited against their original generation.
        const auto epoch=effective_governance(source,generation);
        GrievanceEvidence proof;
        proof.agenda_event=epoch.event_id?epoch.event_id:epoch.founding_event_id;
        proof.leader=epoch.leader_id;
        const auto* entity=present.find(source);
        const auto leader=present.npc_records().find(proof.leader);
        if(!entity || !present.colony_ship(source) || !entity->owner_id ||
           leader==present.npc_records().end() ||
           leader->second.home_ship!=source ||
           leader->second.location!=source ||
           leader->second.transport_id ||
           !present.npc_available_for_duty(proof.leader))
            throw std::runtime_error("colony representative not physically available");
        Statement opinion(db_,"SELECT id,incident_id,COALESCE(parent_id,0),"
            "speaker_id,recipient_id,fidelity,valence,confidence,generation,tick,"
            "account FROM npc_knowledge WHERE id=?");
        opinion.i64(1,static_cast<sqlite3_int64>(knowledge_id));
        if(!opinion.row())throw std::runtime_error("NPC was not informed of historical event");
        const auto claim=read_knowledge(opinion.get());
        if(!valid_knowledge(claim) || claim.id!=knowledge_id ||
           !claim.parent_id || claim.recipient_npc!=proof.leader ||
           claim.fidelity==ClaimFidelity::LocalObservation ||
           claim.generation>generation || claim.tick>present.tick() ||
           opinion.row())
            throw std::runtime_error("no delivered subjective claim to real colony leader");
        proof.fidelity=static_cast<int>(claim.fidelity);
        proof.valence=claim.valence;
        proof.confidence=claim.confidence;
        proof.incident_id=claim.incident_id;
        Statement physical(db_,"SELECT generation,kind,source,destination,"
            "command_id FROM physical_incidents WHERE incident_id=?");
        physical.text(1,proof.incident_id);
        if(!physical.row() ||
           sqlite3_column_int(physical.get(),1)!=
                static_cast<int>(PhysicalKind::ShipDestroyed) ||
           sqlite3_column_int64(physical.get(),0)<1 ||
           static_cast<std::uint64_t>(sqlite3_column_int64(physical.get(),0))>generation)
            throw std::runtime_error("claim lacks real ship-destruction event");
        proof.incident_generation=static_cast<std::uint64_t>(
            sqlite3_column_int64(physical.get(),0));
        const auto actor=parse_u64(sqlite3_column_text(physical.get(),2));
        const auto victim=parse_u64(sqlite3_column_text(physical.get(),3));
        const auto command=parse_u64(sqlite3_column_text(physical.get(),4));
        if(!actor || !victim || !command || actor==victim || physical.row())
            throw std::runtime_error("original damage participants corrupted");
        // Use the actual incident-generation world snapshot to identify the
        // attacker owner; a player-entered rumor must never supply that ID.
        Statement cp(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
        cp.i64(1,static_cast<sqlite3_int64>(proof.incident_generation));
        if(!cp.row())throw std::runtime_error(
            "historical attack snapshot pruned; attribution refused");
        const auto* bytes=sqlite3_column_blob(cp.get(),0);
        const auto length=sqlite3_column_bytes(cp.get(),0);
        if(!bytes || length<=0 || length>16*1024*1024)
            throw std::runtime_error("historical attack snapshot malformed");
        const auto actual=Simulation::from_snapshot(std::string(
            static_cast<const char*>(bytes),static_cast<std::size_t>(length)));
        const auto* attacker=actual.find(actor);
        const auto* wreck=actual.find(victim);
        if(actual.state_hash()!=parse_u64(sqlite3_column_text(cp.get(),1)) ||
           !attacker || attacker->type!=EntityType::Ship ||
           !attacker->owner_id || !wreck || wreck->type!=EntityType::Wreck ||
           !actual.has_command_id(command) || cp.row())
            throw std::runtime_error("original physical attack no longer verifiable");
        if(attacker->owner_id==entity->owner_id)
            throw std::runtime_error("self-attacks cannot become foreign-owner grievance");
        proof.actor_owner=attacker->owner_id;
        return proof;
    }
    colony_conflict::RelationRevision latest_colony_relationship(
        EntityId source,PlayerId target) const{
        colony_conflict::RelationRevision out;
        out.colony=source;out.target_owner=target;
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_relationship_revisions'");
        if(!exists.row())return out;
        Statement q(db_,"SELECT event_id,revision,leader,knowledge_id,"
            "incident_generation,prior_event,old_score,delta,new_score "
            "FROM sg_colony_relationship_revisions "
            "WHERE colony=? AND target_owner=? ORDER BY revision DESC LIMIT 1");
        q.text(1,std::to_string(source));q.text(2,std::to_string(target));
        if(q.row()){
            out.event_id=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),0));
            out.revision=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),1));
            out.leader_id=parse_u64(sqlite3_column_text(q.get(),2));
            out.knowledge_id=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),3));
            out.incident_generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),4));
            out.prior_event_id=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),5));
            out.old_score=sqlite3_column_int(q.get(),6);
            out.delta=sqlite3_column_int(q.get(),7);
            out.new_score=sqlite3_column_int(q.get(),8);
            out.attitude=colony_conflict::attitude_for_score(out.new_score);
            if(q.row())throw std::runtime_error("ambiguous colony relationship tip");
        }
        return out;
    }
    colony_conflict::RelationRevision assess_colony_relationship(
        const Simulation& present,std::uint64_t generation,
        EntityId source,std::uint64_t knowledge_id) const{
        const auto proof=interpret_colony_incident(
            present,generation,source,knowledge_id);
        colony_conflict::RelationRevision out=latest_colony_relationship(
            source,proof.actor_owner);
        // One physical event can be discussed by multiple NPCs, but may not
        // be counted more than once for a given colony and foreign owner.
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_relationship_revisions'");
        if(exists.row()){
            Statement duplicate(db_,"SELECT 1 FROM sg_colony_relationship_revisions "
                "WHERE colony=? AND target_owner=? AND incident_id=?");
            duplicate.text(1,std::to_string(source));
            duplicate.text(2,std::to_string(proof.actor_owner));
            duplicate.text(3,proof.incident_id);
            if(duplicate.row())throw std::runtime_error(
                "colony already interpreted this same real incident");
        }
        out.prior_event_id=out.event_id;
        out.event_id=0;
        out.revision+=1;
        out.old_score=out.new_score;
        out.leader_id=proof.leader;
        out.knowledge_id=knowledge_id;
        out.incident_generation=proof.incident_generation;
        out.delta=colony_conflict::relationship_delta(
            proof.fidelity,proof.valence,proof.confidence);
        out.new_score=std::clamp(out.old_score+out.delta,-100,100);
        out.attitude=colony_conflict::attitude_for_score(out.new_score);
        if(!out.delta)throw std::runtime_error(
            "zero-confidence claim cannot rewrite diplomatic history");
        return out;
    }
    colony_conflict::RelationRevision record_colony_relationship(
        const Simulation& world,std::uint64_t generation,std::uint64_t id,
        EntityId source,std::uint64_t knowledge_id){
        if(!id || id>static_cast<std::uint64_t>(
            std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("invalid diplomatic revision event ID");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale world during colony diplomacy revision");
            auto out=assess_colony_relationship(
                world,generation,source,knowledge_id);
            const auto proof=interpret_colony_incident(
                world,generation,source,knowledge_id);
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_relationship_revisions("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "generation INTEGER NOT NULL CHECK(generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL,leader TEXT NOT NULL,agenda_event INTEGER NOT NULL,"
                "knowledge_id INTEGER NOT NULL UNIQUE,"
                "incident_id TEXT NOT NULL,incident_generation INTEGER NOT NULL,"
                "target_owner TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),"
                "prior_event INTEGER NOT NULL,old_score INTEGER NOT NULL,"
                "delta INTEGER NOT NULL,new_score INTEGER NOT NULL,"
                "fidelity INTEGER NOT NULL,valence INTEGER NOT NULL,"
                "confidence INTEGER NOT NULL,"
                "UNIQUE(colony,target_owner,incident_id),"
                "UNIQUE(colony,target_owner,revision))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_relation_no_update "
                "BEFORE UPDATE ON sg_colony_relationship_revisions "
                "BEGIN SELECT RAISE(ABORT,'immutable colony diplomatic decision'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_relation_no_delete "
                "BEFORE DELETE ON sg_colony_relationship_revisions "
                "BEGIN SELECT RAISE(ABORT,'immutable colony diplomatic decision'); END");
            Statement q(db_,"INSERT INTO sg_colony_relationship_revisions "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            q.i64(1,static_cast<sqlite3_int64>(id));
            q.i64(2,static_cast<sqlite3_int64>(generation));
            q.text(3,std::to_string(world.state_hash()));
            q.i64(4,static_cast<sqlite3_int64>(world.tick()));
            q.text(5,std::to_string(source));
            q.text(6,std::to_string(proof.leader));
            q.i64(7,static_cast<sqlite3_int64>(proof.agenda_event));
            q.i64(8,static_cast<sqlite3_int64>(knowledge_id));
            q.text(9,proof.incident_id);
            q.i64(10,static_cast<sqlite3_int64>(proof.incident_generation));
            q.text(11,std::to_string(proof.actor_owner));
            q.i64(12,static_cast<sqlite3_int64>(out.revision));
            q.i64(13,static_cast<sqlite3_int64>(out.prior_event_id));
            q.i64(14,out.old_score);q.i64(15,out.delta);q.i64(16,out.new_score);
            q.i64(17,proof.fidelity);q.i64(18,proof.valence);
            q.i64(19,proof.confidence);
            q.execute();
            run(db_,"COMMIT");
            out.event_id=id;
            return out;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            throw;
        }
    }
    void verify_colony_relationships(std::uint64_t max_generation) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_relationship_revisions'");
        if(!exists.row())return;
        for(const auto* guard:{"sg_colony_relation_no_update",
                               "sg_colony_relation_no_delete"}){
            Statement q(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='trigger' AND name=?");
            q.text(1,guard);
            if(!q.row() || q.row())
                throw std::runtime_error("colony relation immutability removed");
        }
        Statement rows(db_,"SELECT event_id,generation,world_hash,tick,colony,"
            "leader,agenda_event,knowledge_id,incident_id,incident_generation,"
            "target_owner,revision,prior_event,old_score,delta,new_score,"
            "fidelity,valence,confidence "
            "FROM sg_colony_relationship_revisions "
            "ORDER BY generation,revision,event_id");
        std::map<std::pair<EntityId,PlayerId>,colony_conflict::RelationRevision> prior;
        while(rows.row()){
            const auto event=sqlite3_column_int64(rows.get(),0);
            const auto generation=sqlite3_column_int64(rows.get(),1);
            const auto hash=sqlite_str(rows.get(),2);
            const auto tick=sqlite3_column_int64(rows.get(),3);
            const auto source=parse_u64(sqlite3_column_text(rows.get(),4));
            const auto leader=parse_u64(sqlite3_column_text(rows.get(),5));
            const auto agenda=sqlite3_column_int64(rows.get(),6);
            const auto knowledge=sqlite3_column_int64(rows.get(),7);
            const auto incident=sqlite_str(rows.get(),8);
            const auto incident_generation=sqlite3_column_int64(rows.get(),9);
            const auto owner=parse_u64(sqlite3_column_text(rows.get(),10));
            const auto revision=sqlite3_column_int64(rows.get(),11);
            const auto prev=sqlite3_column_int64(rows.get(),12);
            const auto before=sqlite3_column_int(rows.get(),13);
            const auto delta=sqlite3_column_int(rows.get(),14);
            const auto after=sqlite3_column_int(rows.get(),15);
            const auto fidelity=sqlite3_column_int(rows.get(),16);
            const auto valence=sqlite3_column_int(rows.get(),17);
            const auto confidence=sqlite3_column_int(rows.get(),18);
            if(event<1 || generation<1 || generation>
               static_cast<sqlite3_int64>(max_generation) ||
               tick<0 || !source || !leader || agenda<1 ||
               knowledge<1 || incident.empty() ||
               incident_generation<1 || incident_generation>generation ||
               !owner || revision<1 || prev<0 ||
               before< -100 || before>100 ||
               after< -100 || after>100)
                throw std::runtime_error("colony diplomatic revision metadata corrupted");
            Statement snap(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            snap.i64(1,generation);
            if(!snap.row())throw std::runtime_error("colony relation world snapshot missing");
            const auto* ptr=sqlite3_column_blob(snap.get(),0);
            const auto len=sqlite3_column_bytes(snap.get(),0);
            if(!ptr || len<=0 || len>16*1024*1024)
                throw std::runtime_error("colony relation physical snapshot malformed");
            const auto historical=Simulation::from_snapshot(std::string(
                static_cast<const char*>(ptr),static_cast<std::size_t>(len)));
            if(historical.state_hash()!=parse_u64(sqlite3_column_text(snap.get(),1)) ||
               std::to_string(historical.state_hash())!=hash ||
               historical.tick()!=static_cast<std::uint64_t>(tick) || snap.row())
                throw std::runtime_error("colony relationship world claim substituted");
            const auto proof=interpret_colony_incident(
                historical,static_cast<std::uint64_t>(generation),
                source,static_cast<std::uint64_t>(knowledge));
            if(proof.leader!=leader || proof.agenda_event!=
               static_cast<std::uint64_t>(agenda) ||
               proof.incident_id!=incident ||
               proof.incident_generation!=
                    static_cast<std::uint64_t>(incident_generation) ||
               proof.actor_owner!=owner ||
               proof.fidelity!=fidelity ||
               proof.valence!=valence || proof.confidence!=confidence)
                throw std::runtime_error("colony grievance lost its actual NPC evidence");
            auto& prior_row=prior[{source,owner}];
            if(static_cast<std::uint64_t>(revision)!=prior_row.revision+1 ||
               static_cast<std::uint64_t>(prev)!=prior_row.event_id ||
               before!=prior_row.new_score ||
               delta!=colony_conflict::relationship_delta(
                   fidelity,valence,confidence) ||
               after!=std::clamp(before+delta,-100,100) ||
               delta==0)
                throw std::runtime_error("colony relation revision chain or score forged");
            prior_row.event_id=static_cast<std::uint64_t>(event);
            prior_row.revision=static_cast<std::uint64_t>(revision);
            prior_row.new_score=after;
        }
    }

    // 01B2 generations are deliberate evidence fences: later relations in
    // the SAME generation cannot change an already accepted intent.
    colony_conflict::RelationRevision relation_before_generation(
        EntityId colony,PlayerId owner,std::uint64_t decision_generation) const{
        colony_conflict::RelationRevision out;
        out.colony=colony;out.target_owner=owner;
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_relationship_revisions'");
        if(!exists.row())return out;
        Statement q(db_,"SELECT event_id,revision,new_score,generation "
            "FROM sg_colony_relationship_revisions "
            "WHERE colony=? AND target_owner=? AND generation<? "
            "ORDER BY generation DESC,revision DESC LIMIT 1");
        q.text(1,std::to_string(colony));q.text(2,std::to_string(owner));
        q.i64(3,static_cast<sqlite3_int64>(decision_generation));
        if(q.row()){
            const auto id=sqlite3_column_int64(q.get(),0);
            const auto rev=sqlite3_column_int64(q.get(),1);
            const auto score=sqlite3_column_int(q.get(),2);
            const auto gen=sqlite3_column_int64(q.get(),3);
            if(id<1 || rev<1 || gen<1 ||
               static_cast<std::uint64_t>(gen)>=decision_generation ||
               score< -100 || score>100 || q.row())
                throw std::runtime_error("invalid historical relation epoch");
            out.event_id=static_cast<std::uint64_t>(id);
            out.revision=static_cast<std::uint64_t>(rev);
            out.new_score=score;
            out.attitude=colony_conflict::attitude_for_score(score);
        }
        return out;
    }
    colony_conflict::Decision assess_colony_diplomacy(
        const Simulation& world,std::uint64_t generation,
        EntityId source,std::uint64_t contact) const{
        const auto base=assess_colony_aggression(
            world,generation,source,contact,true);
        const auto opinion=relation_before_generation(
            source,base.target_owner,generation);
        return colony_conflict::evaluate_diplomatic(
            base,opinion.new_score,opinion.event_id,base.diplomatic_policy);
    }
    colony_conflict::Decision record_colony_diplomacy(
        const Simulation& world,std::uint64_t generation,
        std::uint64_t id,EntityId source,std::uint64_t contact){
        if(!id || id>static_cast<std::uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()) || id==contact)
            throw std::invalid_argument("invalid diplomatic intent id");
        run(db_,"BEGIN IMMEDIATE");
        try{
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("stale diplomatic decision generation");
            Statement old(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_conflict_intent'");
            if(old.row()){
                Statement used(db_,"SELECT 1 FROM sg_colony_conflict_intent "
                    "WHERE contact_event=?");
                used.i64(1,static_cast<sqlite3_int64>(contact));
                if(used.row())throw std::runtime_error(
                    "contact already consumed by original strategic intent");
            }
            const auto d=assess_colony_diplomacy(world,generation,source,contact);
            const auto govt=effective_governance(source,generation);
            if(d.leader_id!=govt.leader_id ||
               d.governance_event_id!=govt.event_id ||
               d.agenda!=govt.agenda)
                throw std::runtime_error("dated intent did not use active government");
            const auto agenda=static_cast<sqlite3_int64>(
                govt.event_id?govt.event_id:govt.founding_event_id);
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_diplomatic_intent("
                "event_id INTEGER PRIMARY KEY CHECK(event_id>0),"
                "world_generation INTEGER NOT NULL CHECK(world_generation>0),"
                "world_hash TEXT NOT NULL,tick INTEGER NOT NULL,"
                "colony TEXT NOT NULL,leader TEXT NOT NULL,"
                "agenda_event INTEGER NOT NULL,"
                "contact_event INTEGER NOT NULL UNIQUE,"
                "target_ship TEXT NOT NULL,target_owner TEXT NOT NULL,"
                "relation_event INTEGER NOT NULL CHECK(relation_event>=0),"
                "relation_score INTEGER NOT NULL CHECK(relation_score BETWEEN -100 AND 100),"
                "motive INTEGER NOT NULL CHECK(motive BETWEEN 0 AND 4),"
                "posture INTEGER NOT NULL CHECK(posture BETWEEN 0 AND 4),"
                "risk INTEGER NOT NULL CHECK(risk BETWEEN 0 AND 3))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_diplomatic_intent_no_update "
                "BEFORE UPDATE ON sg_colony_diplomatic_intent "
                "BEGIN SELECT RAISE(ABORT,'immutable diplomatic intent'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_diplomatic_intent_no_delete "
                "BEFORE DELETE ON sg_colony_diplomatic_intent "
                "BEGIN SELECT RAISE(ABORT,'immutable diplomatic intent'); END");
            Statement insert(db_,"INSERT INTO sg_colony_diplomatic_intent "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            insert.i64(1,static_cast<sqlite3_int64>(id));
            insert.i64(2,static_cast<sqlite3_int64>(generation));
            insert.text(3,std::to_string(world.state_hash()));
            insert.i64(4,static_cast<sqlite3_int64>(world.tick()));
            insert.text(5,std::to_string(source));insert.text(6,std::to_string(d.leader_id));
            insert.i64(7,agenda);insert.i64(8,static_cast<sqlite3_int64>(contact));
            insert.text(9,std::to_string(d.target_ship));insert.text(10,std::to_string(d.target_owner));
            insert.i64(11,static_cast<sqlite3_int64>(d.relation_event_id));
            insert.i64(12,d.relation_score);
            insert.i64(13,static_cast<int>(d.motive));insert.i64(14,static_cast<int>(d.posture));
            insert.i64(15,static_cast<int>(d.source_risk));insert.execute();
            run(db_,"COMMIT");
            return d;
        }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void verify_colony_diplomatic_intents(std::uint64_t max_generation) const{
        Statement exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_diplomatic_intent'");
        if(!exists.row())return;
        for(const auto* guard:{"sg_diplomatic_intent_no_update",
                               "sg_diplomatic_intent_no_delete"}){
            Statement check(db_,"SELECT 1 FROM sqlite_master WHERE type='trigger' "
                "AND name=?");
            check.text(1,guard);
            if(!check.row() || check.row())
                throw std::runtime_error("missing immutable diplomatic intent guard");
        }
        Statement rows(db_,"SELECT event_id,world_generation,world_hash,tick,"
            "colony,leader,agenda_event,contact_event,target_ship,target_owner,"
            "relation_event,relation_score,motive,posture,risk "
            "FROM sg_colony_diplomatic_intent");
        while(rows.row()){
            const auto id=sqlite3_column_int64(rows.get(),0);
            const auto gen=sqlite3_column_int64(rows.get(),1);
            const auto hash=sqlite_str(rows.get(),2);
            const auto tick=sqlite3_column_int64(rows.get(),3);
            const auto colony=parse_u64(sqlite3_column_text(rows.get(),4));
            const auto leader=parse_u64(sqlite3_column_text(rows.get(),5));
            const auto agenda=sqlite3_column_int64(rows.get(),6);
            const auto contact=sqlite3_column_int64(rows.get(),7);
            const auto target=parse_u64(sqlite3_column_text(rows.get(),8));
            const auto owner=parse_u64(sqlite3_column_text(rows.get(),9));
            const auto relation=sqlite3_column_int64(rows.get(),10);
            const auto score=sqlite3_column_int(rows.get(),11);
            const auto motive=sqlite3_column_int(rows.get(),12);
            const auto posture=sqlite3_column_int(rows.get(),13);
            const auto risk=sqlite3_column_int(rows.get(),14);
            if(id<1 || gen<1 || static_cast<std::uint64_t>(gen)>max_generation ||
               tick<0 || !colony || !leader || agenda<1 || contact<1 ||
               !target || !owner || relation<0 || score< -100 || score>100)
                throw std::runtime_error("malformed historical diplomatic intent");
            const auto past=effective_governance(
                colony,static_cast<std::uint64_t>(gen));
            const auto original_agenda=static_cast<sqlite3_int64>(
                past.event_id?past.event_id:past.founding_event_id);
            if(past.leader_id!=leader || original_agenda!=agenda)
                throw std::runtime_error("diplomatic decision forged former government");
            Statement old(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_conflict_intent'");
            if(old.row()){
                Statement used(db_,"SELECT 1 FROM sg_colony_conflict_intent "
                    "WHERE contact_event=?");
                used.i64(1,contact);
                if(used.row())throw std::runtime_error("strategic double use of contact");
            }
            Statement snapshot(db_,"SELECT state,checksum FROM snapshots "
                "WHERE generation=?");
            snapshot.i64(1,gen);
            if(!snapshot.row())throw std::runtime_error("diplomacy snapshot pruned");
            const auto* bytes=sqlite3_column_blob(snapshot.get(),0);
            const auto len=sqlite3_column_bytes(snapshot.get(),0);
            if(!bytes || len<=0 || len>16*1024*1024)
                throw std::runtime_error("diplomacy snapshot malformed");
            const auto w=Simulation::from_snapshot(std::string(
                static_cast<const char*>(bytes),static_cast<std::size_t>(len)));
            if(w.state_hash()!=parse_u64(sqlite3_column_text(snapshot.get(),1)) ||
               std::to_string(w.state_hash())!=hash ||
               w.tick()!=static_cast<std::uint64_t>(tick) || snapshot.row())
                throw std::runtime_error("diplomatic intent lost original world");
            const auto recomputed=assess_colony_diplomacy(
                w,static_cast<std::uint64_t>(gen),colony,
                static_cast<std::uint64_t>(contact));
            if(recomputed.leader_id!=leader || recomputed.target_ship!=target ||
               recomputed.target_owner!=owner ||
               recomputed.relation_event_id!=static_cast<std::uint64_t>(relation) ||
               recomputed.relation_score!=score ||
               static_cast<int>(recomputed.motive)!=motive ||
               static_cast<int>(recomputed.posture)!=posture ||
               static_cast<int>(recomputed.source_risk)!=risk)
                throw std::runtime_error("historical diplomatic intent changed");
        }
    }
    void verify_colony_response(const Simulation& state,std::uint64_t gen) const {
        colony_safety::journal::verify(db_,state,gen);
    }
    std::uint64_t colony_response_event_count() const {
        return colony_safety::journal::event_count(db_);
    }
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    colony_family::Audit audit_colony_family_departure(
            const Simulation& world,const colony_family::DeparturePlan& request) const {
        return colony_family::assess(world,db_,request);
    }
    bool prepare_colony_family_departure(
            const Simulation& world,std::uint64_t generation,
            const colony_family::DeparturePlan& plan,FailPoint fail){
        if(!plan.intent_id || plan.intent_id>=static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
           !plan.expected_world_generation ||
           plan.expected_world_generation!=generation)return false;
        const auto encode_adults=[&](){
            std::string out;
            for(auto id:plan.adult_ids){
                if(!out.empty())out.push_back(',');
                out+=std::to_string(id);
            }
            return out;
        };
        const auto encode_children=[&](){
            std::string out;
            for(const auto& c:plan.children){
                if(!out.empty())out.push_back(',');
                out+=std::to_string(c.child_id);
                out.push_back(':');out+=std::to_string(c.guardian_id);
            }
            return out;
        };
        const auto adults=encode_adults(),children=encode_children();
        run(db_,"BEGIN IMMEDIATE");
        try {
            if(latest_generation(db_)!=generation)
                throw std::runtime_error("family preparation stale canonical world generation");
            const auto audited=colony_family::assess(world,db_,plan);
            if(!audited.ready_for_transaction){
                run(db_,"ROLLBACK");
                return false;
            }
            // This is a PREPARED manifest, never an executed relocation.
            // Triggers protect against accidental rewriting of witnessed data.
            run(db_,"CREATE TABLE IF NOT EXISTS sg_colony_family_departure_prepare("
                    "intent_id TEXT PRIMARY KEY, generation INTEGER NOT NULL CHECK(generation>0),"
                    "world_hash TEXT NOT NULL, source TEXT NOT NULL, receiver TEXT NOT NULL,"
                    "family_scope TEXT NOT NULL, family_seed TEXT NOT NULL,"
                    "adult_roster TEXT NOT NULL, child_guardians TEXT NOT NULL,"
                    "status INTEGER NOT NULL CHECK(status=1))");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_family_prepare_no_update "
                    "BEFORE UPDATE ON sg_colony_family_departure_prepare "
                    "BEGIN SELECT RAISE(ABORT,'prepared family evidence is immutable'); END");
            run(db_,"CREATE TRIGGER IF NOT EXISTS sg_colony_family_prepare_no_delete "
                    "BEFORE DELETE ON sg_colony_family_departure_prepare "
                    "BEGIN SELECT RAISE(ABORT,'prepared family evidence is immutable'); END");
            const auto id=std::to_string(plan.intent_id);
            const auto hash=std::to_string(world.state_hash());
            const auto source=std::to_string(plan.source);
            const auto receiver=std::to_string(plan.receiver);
            const auto seed=std::to_string(plan.family_seed);
            Statement prior(db_,"SELECT generation,world_hash,source,receiver,family_scope,"
                "family_seed,adult_roster,child_guardians,status "
                "FROM sg_colony_family_departure_prepare WHERE intent_id=?");
            prior.text(1,id);
            if(prior.row()){
                const bool identical=
                    sqlite3_column_int64(prior.get(),0)==static_cast<sqlite3_int64>(generation) &&
                    sqlite_str(prior.get(),1)==hash &&
                    sqlite_str(prior.get(),2)==source &&
                    sqlite_str(prior.get(),3)==receiver &&
                    sqlite_str(prior.get(),4)==plan.family_scope &&
                    sqlite_str(prior.get(),5)==seed &&
                    sqlite_str(prior.get(),6)==adults &&
                    sqlite_str(prior.get(),7)==children &&
                    sqlite3_column_int(prior.get(),8)==1 &&
                    !prior.row();
                run(db_,"COMMIT");
                return identical;
            }
            Statement insert(db_,"INSERT INTO sg_colony_family_departure_prepare "
                "VALUES(?,?,?,?,?,?,?,?,?,1)");
            insert.text(1,id);
            insert.i64(2,static_cast<std::int64_t>(generation));
            insert.text(3,hash);
            insert.text(4,source);
            insert.text(5,receiver);
            insert.text(6,plan.family_scope);
            insert.text(7,seed);
            insert.text(8,adults);
            insert.text(9,children);
            insert.execute();
            if(fail==FailPoint::AfterReceiptWrite || fail==FailPoint::AfterSnapshotWrite)
                throw std::runtime_error("injected family preparation crash before commit");
            run(db_,"COMMIT");
            return true;
        }catch(...){
            sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);
            throw;
        }
    }
    // An ART02 child enters the physical world only when the existing birth
    // registry and the independently witnessed world-birth bridge agree.
    // Call under the SAME SQLite writer transaction as the new world event.
    void verify_physical_dependent_birth(const Simulation& world,
        const Command& c,const std::string& scope,std::uint64_t seed) const {
        if(!c.transport_id || c.transport_id<(1ULL<<62) ||
           c.passenger_ids.size()!=1 || !c.passenger_ids.front() ||
           !c.actor || c.actor!=c.target || scope.empty() || !seed)
            throw std::runtime_error("invalid dependent registration identity");
        if(world.physical_dependents().count(c.transport_id) ||
           world.npc_records().count(c.transport_id) ||
           world.npc_casualties().count(c.transport_id))
            throw std::runtime_error("dependent already physically registered");
        const auto guardian=world.npc_records().find(c.passenger_ids.front());
        if(guardian==world.npc_records().end() ||
           guardian->second.location!=c.actor ||
           guardian->second.home_ship!=c.actor || guardian->second.transport_id)
            throw std::runtime_error("dependent guardian lacks physical residence");
        Statement bridge(db_,"SELECT event_id,parent_a,parent_b,family_scope FROM "
                             "sg_med_society_birth WHERE child_id=? AND home_ship=?");
        bridge.text(1,std::to_string(c.transport_id));
        bridge.text(2,std::to_string(c.actor));
        if(!bridge.row())throw std::runtime_error("dependent birth has no world bridge");
        const auto birth_event=sqlite_str(bridge.get(),0);
        const auto parent_a=parse_u64(sqlite3_column_text(bridge.get(),1));
        const auto parent_b=parse_u64(sqlite3_column_text(bridge.get(),2));
        if(sqlite_str(bridge.get(),3)!=scope || bridge.row())
            throw std::runtime_error("dependent birth bridge family scope conflict");
        Statement natal(db_,"SELECT parent_a,parent_b,settlement FROM npc_family_births "
                            "WHERE scope=? AND event_id=? AND child_id=?");
        natal.text(1,scope);natal.text(2,birth_event);
        natal.text(3,std::to_string(c.transport_id));
        if(!natal.row() ||
           parse_u64(sqlite3_column_text(natal.get(),0))!=parent_a ||
           parse_u64(sqlite3_column_text(natal.get(),1))!=parent_b ||
           parse_u64(sqlite3_column_text(natal.get(),2))!=c.actor ||
           natal.row())
            throw std::runtime_error("world/ART02 original parent records diverge");
        Statement deceased(db_,"SELECT 1 FROM npc_family_deaths "
            "WHERE scope=? AND npc_id=? LIMIT 1");
        deceased.text(1,scope);deceased.text(2,std::to_string(c.transport_id));
        if(deceased.row())throw std::runtime_error("cannot register deceased dependent");
        if(has_dependent_table()){
            Statement ferry(db_,"SELECT 1 FROM sg_dependent_travel WHERE child_id=? "
                                 "AND status=1 LIMIT 1");
            ferry.text(1,std::to_string(c.transport_id));
            if(ferry.row())throw std::runtime_error("cannot register child aboard an active ferry");
        }
        NpcAppearanceStore family(db_,scope,seed,
            [&](std::uint64_t id){return world.npc_records().count(id)!=0;});
        if(family.current_residence(c.transport_id)!=c.actor ||
           family.current_residence(c.passenger_ids.front())!=c.actor)
            throw std::runtime_error("child/guardian census location mismatch");
        const auto lawful=family.current_nonparent_guardian(c.transport_id);
        if(lawful ? lawful!=c.passenger_ids.front() :
           (parent_a!=c.passenger_ids.front() && parent_b!=c.passenger_ids.front()))
            throw std::runtime_error("not the child's legally recorded guardian");
    }
    void verify_physical_dependent_authority(const Simulation& world) const{
        Statement tables(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
            "AND name='sg_colony_physical_child_authority'");
        if(!tables.row()){
            if(!world.physical_dependents().empty())
                throw std::runtime_error("physical child exists without durable birth authority table");
            return;
        }
        for(const auto& [id,d]:world.physical_dependents()){
            Statement auth(db_,"SELECT command_id,guardian_id,home_ship FROM "
                "sg_colony_physical_child_authority WHERE child_id=?");
            auth.text(1,std::to_string(id));
            if(!auth.row() ||
               parse_u64(sqlite3_column_text(auth.get(),0))!=d.registration_command ||
               parse_u64(sqlite3_column_text(auth.get(),1))!=d.guardian_id)
                throw std::runtime_error("physical dependent lacks immutable birth authority");
            const auto original_home=parse_u64(sqlite3_column_text(auth.get(),2));
            if(auth.row())throw std::runtime_error("duplicate physical birth authority");
            if(original_home!=d.home_ship){
                // The registration remains immutable at its ORIGINAL home.
                // A real v60 physical transfer is separately witnessed by
                // the entire family evacuation and its ART02 member receipt.
                Statement moved(db_,"SELECT e.source,e.receiver,m.guardian_id "
                    "FROM sg_colony_family_evacuation_member m "
                    "JOIN sg_colony_family_evacuation e ON e.command_id=m.command_id "
                    "WHERE m.person_id=? AND m.kind=1");
                moved.text(1,std::to_string(id));
                if(!moved.row() ||
                   parse_u64(sqlite3_column_text(moved.get(),0))!=original_home ||
                   parse_u64(sqlite3_column_text(moved.get(),1))!=d.home_ship ||
                   parse_u64(sqlite3_column_text(moved.get(),2))!=d.guardian_id ||
                   moved.row())
                    throw std::runtime_error("child home changed without verified evacuation receipt");
            }
        }
    }
    bool has_dependent_table() const {
        Statement t(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                        "AND name='sg_dependent_travel'");
        return t.row();
    }
    // Whole-colony adult departure cannot silently orphan any known ART02
    // dependent or world-recorded birth. Historical child links are refused
    // conservatively until a complete guardian+births closure adapter exists.
    bool has_child_or_dependent_link(EntityId scope) const {
        const auto key=std::to_string(scope);
        if(has_dependent_table()){
            Statement child(db_,"SELECT 1 FROM sg_dependent_travel "
                                 "WHERE ferry=? OR origin=? OR destination=? LIMIT 1");
            child.text(1,key);child.text(2,key);child.text(3,key);
            if(child.row())return true;
        }
        Statement table(db_,"SELECT 1 FROM sqlite_master "
                             "WHERE type='table' AND name='sg_med_society_birth'");
        if(table.row()){
            Statement birth(db_,"SELECT 1 FROM sg_med_society_birth WHERE home_ship=? LIMIT 1");
            birth.text(1,key);
            if(birth.row())return true;
        }
        return false;
    }
    std::int64_t active_dependent_berths(EntityId ferry) const {
        if(!has_dependent_table())return 0;
        Statement q(db_,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? AND status=1");
        q.text(1,std::to_string(ferry));
        if(!q.row())throw std::runtime_error("missing dependent berth count");
        return sqlite3_column_int64(q.get(),0);
    }
    std::vector<DependentTravelRecord> dependent_travel(const std::string& scope) const {
        std::vector<DependentTravelRecord> out;
        if(!has_dependent_table())return out;
        bool paired=false;
        {Statement columns(db_,"PRAGMA table_info(sg_dependent_travel)");
         while(columns.row())if(sqlite_str(columns.get(),1)=="co_guardian_id")paired=true;}
        bool has_transfer=false;
        {Statement columns(db_,"PRAGMA table_info(sg_dependent_travel)");
         while(columns.row())if(sqlite_str(columns.get(),1)=="transfer_from_transport")has_transfer=true;}
        const std::string sql=std::string("SELECT child_id,transport_id,guardian_id,ferry,origin,destination,")+
            "board_command,arrival_command,board_generation,arrival_generation,status,"+
            (paired?"co_guardian_id":"'0'")+","+
            (has_transfer?"transfer_from_transport":"'0'")+","+
            (has_transfer?"termination_kind":"0")+
            " FROM sg_dependent_travel WHERE family_scope=? ORDER BY child_id,transport_id";
        Statement q(db_,sql.c_str());
        q.text(1,scope);
        while(q.row()){
            DependentTravelRecord r;
            r.child_id=parse_u64(sqlite3_column_text(q.get(),0));
            r.transport_id=parse_u64(sqlite3_column_text(q.get(),1));
            r.guardian_id=parse_u64(sqlite3_column_text(q.get(),2));
            r.co_guardian_id=parse_u64(sqlite3_column_text(q.get(),11));
            r.transfer_from_transport=parse_u64(sqlite3_column_text(q.get(),12));
            r.termination_kind=static_cast<unsigned>(sqlite3_column_int(q.get(),13));
            r.ferry=parse_u64(sqlite3_column_text(q.get(),3));
            r.origin=parse_u64(sqlite3_column_text(q.get(),4));
            r.destination=parse_u64(sqlite3_column_text(q.get(),5));
            r.board_command=parse_u64(sqlite3_column_text(q.get(),6));
            if(sqlite3_column_type(q.get(),7)!=SQLITE_NULL)
                r.arrival_command=parse_u64(sqlite3_column_text(q.get(),7));
            r.board_generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),8));
            if(sqlite3_column_type(q.get(),9)!=SQLITE_NULL)
                r.arrival_generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),9));
            r.status=static_cast<unsigned>(sqlite3_column_int(q.get(),10));
            out.push_back(r);
        }
        return out;
    }
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
        // A native NPC flight command is emitted INSIDE a trusted world tick.
        // Its separate immutable companion row and normal receipt must agree
        // with the original step hash, source flight, pilot and command ID.
        // Historic snapshots may be pruned; a source-bound digest survives.
        {
            Statement native(db_,"SELECT id,generation,command,pilot,world_hash,digest "
                "FROM sg_native_npc_commands ORDER BY generation,id");
            while(native.row()){
                const auto id=parse_u64(sqlite3_column_text(native.get(),0));
                const auto g=sqlite3_column_int64(native.get(),1);
                if(g<=1 || !restored.has_command_id(id))
                    throw std::runtime_error("SQLite: invalid original NPC admission row");
                const auto command=sqlite_str(native.get(),2);
                const auto c=decode_command(command);
                const auto pilot=parse_u64(sqlite3_column_text(native.get(),3));
                const auto world_hash=sqlite_str(native.get(),4);
                const auto digest=sqlite_str(native.get(),5);
                constexpr std::uint64_t tag=std::uint64_t{1}<<61;
                const auto flight_id=(c.id & ~tag)>>9;
                const auto seq=c.id & 511;
                if(c.id!=id || !(id & tag) || (id & (std::uint64_t{1}<<62)) ||
                   !flight_id || flight_id>=(std::uint64_t{1}<<40) || !seq ||
                   id!=(tag|(flight_id<<9)|seq) || c.sequence!=id ||
                   (c.action!=Action::Thrust &&
                    c.action!=Action::RequestDock &&
                    c.action!=Action::RequestUndock) || !pilot)
                    throw std::runtime_error("SQLite: forged native flight identity");
                const auto signed_body=std::to_string(id)+"|"+
                    std::to_string(g)+"|"+command+"|"+
                    std::to_string(pilot)+"|"+world_hash;
                if(digest!=social_digest("original-npc-flight-v1",signed_body))
                    throw std::runtime_error("SQLite: native flight immutable provenance altered");
                Statement source(db_,"SELECT kind,state_hash FROM operation_log "
                    "WHERE generation=?");
                source.i64(1,g);
                if(!source.row() || sqlite3_column_int(source.get(),0)!=2 ||
                   sqlite_str(source.get(),1)!=world_hash || source.row())
                    throw std::runtime_error("SQLite: native flight missing original writer step");
                Statement rec(db_,"SELECT accepted_generation FROM receipts WHERE id=?");
                rec.text(1,std::to_string(id));
                if(!rec.row() || sqlite3_column_int64(rec.get(),0)!=g || rec.row())
                    throw std::runtime_error("SQLite: native flight receipt generation mismatch");
                const auto flight=restored.npc_flight_orders().find(flight_id);
                if(flight==restored.npc_flight_orders().end() ||
                   flight->second.owner!=c.issuer ||
                   flight->second.ship!=c.actor ||
                   flight->second.pilot_npc!=pilot ||
                   (flight->second.mode!=NpcFlightMode::LocalDock &&
                    flight->second.mode!=NpcFlightMode::LocalRendezvous) ||
                   flight->second.local_commands<seq)
                    throw std::runtime_error("SQLite: native flight absent from original crew lineage");
                if(!journal_commands.emplace(id,c).second)
                    throw std::runtime_error("SQLite: duplicate autonomous command history");
            }
        }
        // WO2-I: original SQLite accepted-command history proves the exact
        // shop, owner, worker and bounded window of every surviving shift.
        // The shift may expire without an extra command, but its source
        // admission must never be faked or reassigned in a snapshot.
        for(const auto& [site,shop]:restored.workshops()){
            // A pending shift cancellation is evidence of a real owner
            // cancellation command, never an arbitrary skipped appointment.
            for(const auto canceled:shop.cancelled_pending_shifts){
                const auto original=journal_commands.find(canceled);
                bool cancellation_proven=false;
                if(original!=journal_commands.end() &&
                   original->second.action==Action::StartWorkshopShift &&
                   original->second.target==site &&
                   original->second.actor==shop.source_ship){
                    for(const auto& [id,command]:journal_commands){
                        const auto receipt=event_by_id.find(id);
                        if(command.action==Action::CancelWorkshopShift &&
                           command.actor==shop.source_ship &&
                           command.target==site &&
                           command.thrust_x==original->second.thrust_x &&
                           command.amount==static_cast<std::int64_t>(canceled) &&
                           receipt!=event_by_id.end() &&
                           receipt->second.result==Outcome::Applied &&
                           receipt->second.tick<original->second.execute_tick){
                            cancellation_proven=true;break;
                        }
                    }
                }
                if(!cancellation_proven)
                    throw std::runtime_error("SQLite: workshop pending cancellation lacks original owner receipt");
            }
            for(unsigned slot=0;slot<2;++slot){
                const auto& shift=shop.shifts[slot];
                if(!shift.command_id)continue;
                const auto it=journal_commands.find(shift.command_id);
                const auto ev=event_by_id.find(shift.command_id);
                const auto* ship=restored.find(shop.source_ship);
                if(it==journal_commands.end() || ev==event_by_id.end() ||
                   it->second.action!=Action::StartWorkshopShift ||
                   !ship || it->second.issuer!=ship->owner_id ||
                   it->second.actor!=shop.source_ship ||
                   it->second.target!=site ||
                   it->second.thrust_x!=static_cast<int>(slot+1) ||
                   it->second.execute_tick!=shift.start_tick ||
                   it->second.thrust_y<1 ||
                   shift.end_tick-shift.start_tick!=
                     static_cast<std::uint64_t>(it->second.thrust_y) ||
                   ev->second.tick!=shift.start_tick ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=0)
                    throw std::runtime_error("SQLite: original workshop shift proof mismatch");
                // A newly relieved shift retains its first worker in the
                // original Start command. The current worker must instead
                // match the *latest* real applied, same-owner relief receipt.
                std::uint64_t last_relief=0,last_relief_tick=0;
                std::set<std::uint64_t> seen_relief_ticks;
                std::uint64_t current_worker=static_cast<std::uint64_t>(it->second.amount);
                for(const auto& [id,change]:journal_commands){
                    if(change.action!=Action::RelieveWorkshopShift ||
                       change.amount!=static_cast<std::int64_t>(shift.command_id) ||
                       change.target!=site ||
                       change.thrust_x!=static_cast<int>(slot+1))continue;
                    const auto applied=event_by_id.find(id);
                    if(applied==event_by_id.end() ||
                       applied->second.result!=Outcome::Applied)continue;
                    if(change.issuer!=ship->owner_id ||
                       change.actor!=shop.source_ship ||
                       change.thrust_y<1 || change.thrust_y>1000000 ||
                       change.execute_tick<=shift.start_tick ||
                       change.execute_tick>=shift.end_tick ||
                       applied->second.tick!=change.execute_tick ||
                       applied->second.transferred!=0 ||
                       !seen_relief_ticks.insert(change.execute_tick).second)
                        throw std::runtime_error("SQLite: workshop relief contradicted original owner or chronology");
                    // Command IDs need not increase with tick order.
                    // The event's applied tick, not its arbitrary ID, orders
                    // the latest authoritative replacement.
                    if(change.execute_tick>last_relief_tick){
                        last_relief=id;
                        last_relief_tick=change.execute_tick;
                        current_worker=static_cast<std::uint64_t>(change.thrust_y);
                    }
                }
                if(last_relief!=shift.relief_command_id ||
                   current_worker!=shift.worker)
                    throw std::runtime_error("SQLite: workshop current worker lacks latest relief proof");
            }
        }
        // Latest real appointed manager is derived from immutable ORIGINAL
        // journal + Applied ticks. A superseded or revoked manager's older
        // receipt cannot be reused to forge an active mentor or job policy.
        const auto latest_workshop_appointment=[&](
            EntityId site,std::uint64_t until_tick,bool inclusive){
            std::uint64_t last_id=0,last_tick=0;
            for(const auto& [id,order]:journal_commands){
                if(order.action!=Action::AppointWorkshopSupervisor ||
                   order.target!=site)continue;
                const auto effect=event_by_id.find(id);
                if(effect==event_by_id.end() ||
                   effect->second.result!=Outcome::Applied)continue;
                const auto tick=effect->second.tick;
                if(tick>until_tick || (!inclusive && tick==until_tick))continue;
                if(tick==last_tick && last_id)
                    throw std::runtime_error("SQLite: ambiguous same-tick workshop manager");
                if(tick>last_tick){last_tick=tick;last_id=id;}
            }
            return last_id;
        };
        // WO2-J: the installed shop supervisor and bounded owner-approved
        // batch program must point to original applied owner commands.
        for(const auto& [site,shop]:restored.workshops()){
            const auto* ship=restored.find(shop.source_ship);
            if(shop.supervisor_npc_id){
                const auto app=journal_commands.find(shop.supervisor_appointment_id);
                const auto ev=event_by_id.find(shop.supervisor_appointment_id);
                if(!ship || app==journal_commands.end() ||
                   ev==event_by_id.end() ||
                   app->second.action!=Action::AppointWorkshopSupervisor ||
                   app->second.amount!=static_cast<std::int64_t>(shop.supervisor_npc_id) ||
                   app->second.actor!=shop.source_ship ||
                   app->second.target!=site ||
                   app->second.issuer!=ship->owner_id ||
                   app->second.thrust_x!=static_cast<int>(shop.supervisor_trade) ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.tick!=app->second.execute_tick ||
                   ev->second.transferred!=0 ||
                   latest_workshop_appointment(site,restored.tick(),true)!=
                       shop.supervisor_appointment_id)
                    throw std::runtime_error("SQLite: supervisor missing current original owner appointment");
            }
            for(unsigned slot=0;slot<2;++slot){
                const auto& job=shop.approved_jobs[slot];
                if(!job.command_id)continue;
                const auto order=journal_commands.find(job.command_id);
                const auto ev=event_by_id.find(job.command_id);
                if(!ship || order==journal_commands.end() ||
                   ev==event_by_id.end() ||
                   order->second.action!=Action::ApproveWorkshopJob ||
                   order->second.actor!=shop.source_ship ||
                   order->second.target!=site ||
                   order->second.issuer!=ship->owner_id ||
                   order->second.thrust_x!=static_cast<int>(slot+1) ||
                   order->second.amount!=static_cast<std::int64_t>(job.target_batches) ||
                   (shop.supervisor_npc_id &&
                    (job.supervisor_appointment_id!=shop.supervisor_appointment_id ||
                     job.supervisor_npc_id!=shop.supervisor_npc_id)) ||
                   ev->second.tick!=order->second.execute_tick ||
                   ev->second.tick!=job.approved_tick ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=0)
                    throw std::runtime_error("SQLite: job missing original owner approval");
                // Supervisor withdrawal does not erase the owner's quota.
                // Independently prove the original prior appointment even
                // when the active manager slot is empty/suspended.
                const auto app=journal_commands.find(job.supervisor_appointment_id);
                const auto appointment=event_by_id.find(job.supervisor_appointment_id);
                if(app==journal_commands.end() || appointment==event_by_id.end() ||
                   app->second.action!=Action::AppointWorkshopSupervisor ||
                   app->second.actor!=shop.source_ship ||
                   app->second.issuer!=ship->owner_id ||
                   app->second.target!=site ||
                   app->second.amount!=static_cast<std::int64_t>(job.supervisor_npc_id) ||
                   app->second.thrust_x!=static_cast<int>(slot+1) ||
                   appointment->second.result!=Outcome::Applied ||
                   appointment->second.tick!=app->second.execute_tick ||
                   appointment->second.tick>=job.approved_tick ||
                   latest_workshop_appointment(site,job.approved_tick,false)!=
                       job.supervisor_appointment_id)
                    throw std::runtime_error("SQLite: suspended job has no genuine supervisor provenance");
            }
        }
        // WO2-H/J: original SQLite accepted-command history proves the exact
        // worker, trade, supervisor and site of each qualification. Matching any generic
        // Applied event is NOT enough to fabricate a credential for another NPC.
        std::set<std::uint64_t> verified_training;
        const auto verify_training=[&](const NpcRecord& npc) {
            for(const auto& credit:npc.industrial_training){
                const auto command=journal_commands.find(credit.command_id);
                const auto applied=event_by_id.find(credit.command_id);
                if(!verified_training.insert(credit.command_id).second ||
                   command==journal_commands.end() ||
                   (command->second.action!=Action::TrainWorkshopTrade &&
                    command->second.action!=Action::MentorWorkshopTrade) ||
                   command->second.issuer==Simulation::kServerAuthority ||
                   command->second.amount!=static_cast<std::int64_t>(npc.id) ||
                   command->second.target!=credit.workshop_site ||
                   command->second.thrust_x!=static_cast<int>(credit.trade) ||
                   applied==event_by_id.end() ||
                   applied->second.result!=Outcome::Applied ||
                   applied->second.tick!=credit.tick ||
                   applied->second.transferred!=1)
                    throw std::runtime_error("SQLite: industrial certification contradicts original NPC command");
                const bool mentored=command->second.action==Action::MentorWorkshopTrade;
                if(mentored!=(credit.mentor_id!=0))
                    throw std::runtime_error("SQLite: industrial mentorship receipt type mismatch");
                if(mentored){
                    const auto app=journal_commands.find(credit.supervisor_appointment_id);
                    const auto appointment=event_by_id.find(credit.supervisor_appointment_id);
                    if(!credit.mentor_id || !credit.supervisor_appointment_id ||
                       app==journal_commands.end() || appointment==event_by_id.end() ||
                       app->second.action!=Action::AppointWorkshopSupervisor ||
                       app->second.actor!=command->second.actor ||
                       app->second.issuer!=command->second.issuer ||
                       app->second.target!=credit.workshop_site ||
                       app->second.amount!=static_cast<std::int64_t>(credit.mentor_id) ||
                       app->second.thrust_x!=static_cast<int>(credit.trade) ||
                       appointment->second.result!=Outcome::Applied ||
                       appointment->second.tick>=credit.tick ||
                       appointment->second.tick!=app->second.execute_tick ||
                       latest_workshop_appointment(credit.workshop_site,credit.tick,false)!=
                           credit.supervisor_appointment_id)
                        throw std::runtime_error("SQLite: forged, revoked or superseded workshop mentor appointment");
                }else if(credit.supervisor_appointment_id)
                    throw std::runtime_error("SQLite: solitary practice forged mentor evidence");
            }
        };
        for(const auto& [id,npc]:restored.npc_records()){
            (void)id;verify_training(npc);
        }
        for(const auto& [id,dead]:restored.npc_casualties()){
            (void)id;verify_training(dead.identity);
        }
        for(const auto& [id,command]:journal_commands){
            if(command.action!=Action::TrainWorkshopTrade &&
               command.action!=Action::MentorWorkshopTrade)continue;
            const auto event=event_by_id.find(id);
            if(event!=event_by_id.end() && event->second.result==Outcome::Applied &&
               !verified_training.count(id) &&
               (restored.npc_records().count(static_cast<std::uint64_t>(command.amount)) ||
                restored.npc_casualties().count(static_cast<std::uint64_t>(command.amount))))
                throw std::runtime_error("SQLite: live NPC industrial qualification receipt missing");
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
            Statement steps(db_, "SELECT generation,tick,kind,command FROM operation_log WHERE generation>? AND kind IN (1,2) ORDER BY generation");
            steps.i64(1, static_cast<std::int64_t>(history_start));
            std::map<std::uint64_t,std::uint64_t> tick_generation;
            // A kind=2 original-world commit can be a schema/coordinate
            // enablement at the *same* tick, rather than an advanced game
            // tick. Only a new tick can own historical execution receipts.
            std::int64_t last_step_tick = 0;
            while (steps.row()) {
                const auto g = sqlite3_column_int64(steps.get(), 0);
                const auto tick = sqlite3_column_int64(steps.get(), 1);
                const auto kind = sqlite3_column_int(steps.get(), 2);
                // The v52 privileged warehouse action is one atomic submit+execute
                // entry, stored as kind=1 for legacy operation_log compatibility.
                // Its command is replayed through a tick in the same transaction.
                if (kind == 1) {
                    const auto* raw = sqlite3_column_text(steps.get(), 3);
                    if (!raw) continue;
                    const auto type=decode_command(reinterpret_cast<const char*>(raw)).action;
                    if(type!=Action::HostEscrowConstructedStock &&
                       type!=Action::HostReturnConstructedStock)continue;
                }
                if (g <= 0 || tick < 0)
                    throw std::runtime_error("SQLite: invalid historical step generation");
                if (kind == 2) {
                    if (tick < last_step_tick)
                        throw std::runtime_error("SQLite: historical tick went backwards");
                    if (tick == last_step_tick) continue; // no simulation tick advanced
                    last_step_tick = tick;
                }
                if (tick == 0 || !tick_generation.emplace(static_cast<std::uint64_t>(tick),
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
        std::size_t workshop_builds=0,workshop_feeds=0,workshop_outputs=0,workshop_finisher_builds=0,workshop_final_outputs=0,workshop_stacks=0,workshop_practice=0,storage_builds=0,storage_stacks=0,extra_storage_builds=0,extra_storage_stacks=0;
        std::size_t auto_transfers = 0, auto_storage_transfers=0, storage_link_builds=0, conveyor_cells=0, world_escrows=0, world_returns=0;
        std::size_t probe_device_transfers=0;
        std::vector<ActivityEvent> recorded_auto, recorded_storage_auto;
        for (const auto& a : restored.activity_events()) {
            if (a.kind == ActivityKind::AutoMiningTransfer) recorded_auto.push_back(a);
            if (a.kind == ActivityKind::AutoStorageTransfer) recorded_storage_auto.push_back(a);
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
            } else if (reason=="survey_probe_deployed" ||
                       reason=="geological_drone_deployed" ||
                       reason=="probe_device_recovered") {
                ++probe_device_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);
                const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   ev->second.result!=Outcome::Applied || amount!=1 ||
                   ev->second.transferred!=1 ||
                   ev->second.tick!=static_cast<std::uint64_t>(
                       sqlite3_column_int64(ledger.get(),1)))
                    throw std::runtime_error("SQLite: invalid physical probe-device receipt");
                const auto action=cmd->second.action;
                const bool recovered=action==Action::RecoverSurveyProbe;
                const bool drone=action==Action::LaunchGeologicalDrone;
                if((!recovered && !drone && action!=Action::DeploySurveyProbe) ||
                   (recovered?reason!="probe_device_recovered":
                    drone?reason!="geological_drone_deployed":
                          reason!="survey_probe_deployed"))
                    throw std::runtime_error("SQLite: probe-device reason disagrees with command");
                const auto identity=recovered?cmd->second.target:cmd->second.id;
                const auto ship="ship:"+std::to_string(cmd->second.actor)+":survey_probe_devices";
                const auto probe="probe:"+std::to_string(identity)+":device";
                if(!identity || sqlite_str(ledger.get(),4)!=(recovered?probe:ship) ||
                   sqlite_str(ledger.get(),5)!=(recovered?ship:probe))
                    throw std::runtime_error("SQLite: probe-device custody endpoint mismatch");
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
            } else if(reason=="industrial_training_practice_kg"){
                ++workshop_practice;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);
                const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   (cmd->second.action!=Action::TrainWorkshopTrade &&
                     cmd->second.action!=Action::MentorWorkshopTrade) ||
                   (cmd->second.thrust_x!=1 && cmd->second.thrust_x!=2) ||
                   cmd->second.amount<1 || cmd->second.amount>1000000 ||
                   cmd->second.issuer==Simulation::kServerAuthority ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=1 ||
                   amount!=1 ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   sqlite_str(ledger.get(),4)!="workshop:"+std::to_string(cmd->second.target)+
                       (cmd->second.thrust_x==1?":aluminum":":aluminum_panels") ||
                   sqlite_str(ledger.get(),5)!="workshop:"+std::to_string(cmd->second.target)+
                       ":aluminum_scrap")
                    throw std::runtime_error("SQLite: invalid original industrial training practice receipt");
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
            } else if(reason=="storage_construction_t" || reason=="storage_stack_transfer_kg"){
                const bool build=reason=="storage_construction_t";
                if(build)++storage_builds;else ++storage_stacks;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   (build ? cmd->second.action!=Action::ConstructStorage :
                    (cmd->second.action!=Action::TransferStorageStack &&
                     cmd->second.action!=Action::HostTransferConsignedStock)) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?2:cmd->second.amount) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: storage receipt mismatch");
                const auto& c=cmd->second;
                if(!build && (c.thrust_x<0 || c.thrust_x>3 || c.thrust_y<0 || c.thrust_y>3))
                    throw std::runtime_error("SQLite: invalid storage routing");
                static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                const std::string suffix=build?"":":"+std::string(names[c.thrust_x]);
                const auto base=build?"modules:"+std::to_string(c.actor):
                    (c.thrust_y==0?"goods:"+std::to_string(c.actor):
                     c.thrust_y==1?"storage:"+std::to_string(c.target):
                     c.thrust_y==2?"workshop:"+std::to_string(c.target):
                                   "storage:"+std::to_string(c.target))+suffix;
                const auto dest=build?"storage:"+std::to_string(c.target):
                    (c.thrust_y==0?"storage:"+std::to_string(c.target):
                     c.thrust_y==1?"goods:"+std::to_string(c.actor):
                     c.thrust_y==2?"storage:"+std::to_string(c.target):
                                   "workshop:"+std::to_string(c.target))+suffix;
                if(base!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: storage endpoints mismatch");
            } else if(reason=="storage_node_construction_t" || reason=="storage_node_stack_transfer_kg"){
                const bool build=reason=="storage_node_construction_t";
                if(build)++extra_storage_builds;else ++extra_storage_stacks;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructStorageNode:Action::TransferStorageNodeStack) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?2:cmd->second.amount) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: extra storage receipt mismatch");
                const auto& c=cmd->second;
                const auto node=build?c.amount:c.thrust_x/4;
                const auto item=build?0:c.thrust_x%4;
                if(node<1 || node>std::numeric_limits<int>::max()/4 || (!build && (c.thrust_y<0 || c.thrust_y>3 || item<0 || item>3)))
                    throw std::runtime_error("SQLite: invalid extra storage routing");
                static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                const std::string endpoint="storage:"+std::to_string(c.target)+":"+std::to_string(node);
                const std::string suffix=build?std::string{}:":"+std::string(names[item]);
                const std::string src=build?"modules:"+std::to_string(c.actor):
                    (c.thrust_y==0?"goods:"+std::to_string(c.actor):
                     c.thrust_y==1?endpoint:
                     c.thrust_y==2?"workshop:"+std::to_string(c.target):endpoint)+suffix;
                const std::string dst=build?endpoint:
                    (c.thrust_y==0?endpoint:
                     c.thrust_y==1?"goods:"+std::to_string(c.actor):
                     c.thrust_y==2?endpoint:"workshop:"+std::to_string(c.target))+suffix;
                if(src!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dst!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: forged extra storage endpoints");
            } else if(reason=="conveyor_cell_construction_t"){
                ++conveyor_cells;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::PlaceConveyorCell ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=2 ||
                   amount!=2 || ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   sqlite_str(ledger.get(),4)!="modules:"+std::to_string(cmd->second.actor) ||
                   sqlite_str(ledger.get(),5)!="conveyor:"+std::to_string(cmd->second.target)+":"+
                       std::to_string(cmd->second.amount)+":"+
                       std::to_string(cmd->second.thrust_x)+":"+
                       std::to_string(cmd->second.thrust_y))
                    throw std::runtime_error("SQLite: invalid conveyor segment construction receipt");
            } else if(reason=="storage_link_construction_t"){
                ++storage_link_builds;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   (cmd->second.action!=Action::ConstructStorageLink &&
                    cmd->second.action!=Action::ConstructWorkshopLink) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=2 ||
                   amount!=2 || ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   sqlite_str(ledger.get(),4)!="modules:"+std::to_string(cmd->second.actor) ||
                   sqlite_str(ledger.get(),5)!="storage-link:"+std::to_string(cmd->second.target)+":"+std::to_string(id))
                    throw std::runtime_error("SQLite: invalid transport-link construction receipt");
            } else if(reason=="workshop_stack_transfer_kg"){
                ++workshop_stacks;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::TransferWorkshopStack ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=amount || amount!=std::abs(cmd->second.amount) ||
                   cmd->second.thrust_x<0 || cmd->second.thrust_x>3 ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: item-stack receipt mismatch");
                const auto& c=cmd->second;
                const std::string kind[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                const auto source=(c.amount>0?"goods:":"workshop:")+
                    std::to_string(c.amount>0?c.actor:c.target)+":"+kind[c.thrust_x];
                const auto dest=(c.amount>0?"workshop:":"goods:")+
                    std::to_string(c.amount>0?c.target:c.actor)+":"+kind[c.thrust_x];
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: item-stack ledger endpoints mismatch");
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
            } else if(reason=="constructed_network_stock_kg"){
                ++world_escrows;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=Action::HostEscrowConstructedStock||
                   cmd->second.issuer!=Simulation::kServerAuthority||
                   ev->second.result!=Outcome::Applied||
                   ev->second.transferred!=amount||amount<=0||
                   cmd->second.amount!=amount||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT||
                   sqlite_str(ledger.get(),4)!="storage:"+std::to_string(cmd->second.target)+":aluminum"||
                   sqlite_str(ledger.get(),5)!="network-escrow:"+std::to_string(cmd->second.target))
                    throw std::runtime_error("SQLite: world inventory escrow source/quantity mismatch");
            } else if(reason=="constructed_network_return_kg"){
                ++world_returns;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=Action::HostReturnConstructedStock||
                   cmd->second.issuer!=Simulation::kServerAuthority||
                   ev->second.result!=Outcome::Applied||
                   ev->second.transferred!=amount||amount<=0||
                   cmd->second.amount!=amount||
                   (cmd->second.thrust_x!=1&&cmd->second.thrust_x!=2)||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT||
                   sqlite_str(ledger.get(),4)!="network-escrow:"+std::to_string(cmd->second.target)||
                   sqlite_str(ledger.get(),5)!="storage:"+std::to_string(cmd->second.target)+":"+
                     (cmd->second.thrust_x==1?"aluminum_panels":"aluminum_frames"))
                    throw std::runtime_error("SQLite: finished goods return endpoint mismatch");
            } else if (reason == "automated_storage_link_transfer") {
                if(auto_storage_transfers>=recorded_storage_auto.size() ||
                   sqlite3_column_type(ledger.get(),0)!=SQLITE_NULL ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: unexpected automatic logistics transfer");
                const auto& ev=recorded_storage_auto[auto_storage_transfers++];
                const auto* route=restored.storage_link(ev.actor,ev.target);
                static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                if(!route || route->id!=ev.start_command_id ||
                   sqlite_str(ledger.get(),4)!=storage_endpoint_label(ev.actor,route->source,names[route->item_index]) ||
                   sqlite_str(ledger.get(),5)!=storage_endpoint_label(ev.actor,route->destination,names[route->item_index]) ||
                   amount!=ev.transferred ||
                   sqlite3_column_int64(ledger.get(),1)!=static_cast<sqlite3_int64>(ev.tick))
                    throw std::runtime_error("SQLite: automatic logistics effect disagrees with world");
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
        if (auto_storage_transfers!=recorded_storage_auto.size())
            throw std::runtime_error("SQLite: missing automated logistics transfers");
        if (auto_transfers != recorded_auto.size())
            throw std::runtime_error("SQLite: missing auto mining resource ledger rows");
        std::size_t expected_probe_device_transfers=0;
        std::size_t expected_transfers = 0;
        std::size_t expected_hangar_transfers = 0;
        std::size_t expected_load_transfers = 0;
        std::size_t expected_sale_transfers = 0;
        std::size_t expected_refine=0,expected_fabricate=0,expected_build=0;
        std::size_t expected_mineral_refine=0,expected_material_fabricate=0;
        std::size_t expected_life_transfers=0,expected_farm_builds=0,expected_farm_stock_transfers=0;
        std::size_t expected_habitat_builds=0, expected_habitat_transfers=0;
        std::size_t expected_utility_builds=0,expected_utility_repairs=0,expected_utility_spares=0,expected_community_builds=0,expected_community_transfers=0,expected_clinic_builds=0,expected_clinic_transfers=0,expected_reclaimer_builds=0,expected_waterworks_builds=0,expected_waterworks_transfers=0,expected_workshop_builds=0,expected_workshop_feeds=0,expected_workshop_outputs=0,expected_workshop_finisher_builds=0,expected_workshop_final_outputs=0,expected_workshop_stacks=0,expected_workshop_practice=0,expected_storage_builds=0,expected_storage_stacks=0,expected_extra_storage_builds=0,expected_extra_storage_stacks=0,expected_link_builds=0,expected_conveyor_cells=0, expected_world_escrows=0,expected_world_returns=0;

        for (const auto& ev : restored.events()) {
            if (ev.result != Outcome::Applied || ev.transferred <= 0) continue;
            const auto cmd = journal_commands.find(ev.command_id);
            if (cmd == journal_commands.end()) {
                // Pre-v2 snapshots can have valid mining receipts predating the
                // operation journal. No hangar transfer existed before B5.
                ++expected_transfers;
            } else if (cmd->second.action==Action::ScanSurveyProbe) {
                // Discovery counts are not physical resource transfers.
                continue;
            } else if (cmd->second.action==Action::DeploySurveyProbe ||
                       cmd->second.action==Action::LaunchGeologicalDrone ||
                       cmd->second.action==Action::RecoverSurveyProbe)
                ++expected_probe_device_transfers;
            else if (cmd->second.action == Action::Mine) ++expected_transfers;
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
            else if (cmd->second.action==Action::TransferWorkshopStack) ++expected_workshop_stacks;
            else if (cmd->second.action==Action::TrainWorkshopTrade ||
                     cmd->second.action==Action::MentorWorkshopTrade) ++expected_workshop_practice;
            else if (cmd->second.action==Action::HostEscrowConstructedStock) ++expected_world_escrows;
            else if (cmd->second.action==Action::HostReturnConstructedStock) ++expected_world_returns;
            else if (cmd->second.action==Action::ConstructStorage) ++expected_storage_builds;
            else if (cmd->second.action==Action::TransferStorageStack || cmd->second.action==Action::HostTransferConsignedStock) ++expected_storage_stacks;
            else if (cmd->second.action==Action::ConstructStorageNode) ++expected_extra_storage_builds;
            else if (cmd->second.action==Action::TransferStorageNodeStack) ++expected_extra_storage_stacks;
            else if (cmd->second.action==Action::ConstructStorageLink || cmd->second.action==Action::ConstructWorkshopLink) ++expected_link_builds;
            else if (cmd->second.action==Action::PlaceConveyorCell) ++expected_conveyor_cells;
            else if (cmd->second.action==Action::ConstructClinic) ++expected_clinic_builds;
            else if (cmd->second.action==Action::TransferClinicStock) ++expected_clinic_transfers;
            else throw std::runtime_error("SQLite: resource transfer tied to non-transfer command");
        }
        // World return commands must have matching immutable network-effect
        // receipts even if the production adapter has not yet been opened.
        if(expected_world_returns){
            Statement matching(db_,"SELECT COUNT(*) FROM sg_v53_world_return r "
                "JOIN sg_v51_event e ON e.scope=r.scope AND e.event_id=r.event_id "
                "AND e.kind='world-return' AND e.world_generation=r.world_generation");
            if(!matching.row()||sqlite3_column_int64(matching.get(),0)!=
               static_cast<sqlite3_int64>(expected_world_returns))
                throw std::runtime_error("SQLite: returned stock lacks factory custody proof");
        }
        if (!saw_genesis ||
            probe_device_transfers!=expected_probe_device_transfers ||
            transfers != expected_transfers || hangar_transfers != expected_hangar_transfers ||
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
            workshop_final_outputs!=expected_workshop_final_outputs ||
            workshop_stacks!=expected_workshop_stacks ||
             workshop_practice!=expected_workshop_practice ||
            storage_builds!=expected_storage_builds || storage_stacks!=expected_storage_stacks ||
            extra_storage_builds!=expected_extra_storage_builds ||
            extra_storage_stacks!=expected_extra_storage_stacks ||
            storage_link_builds!=expected_link_builds ||
            conveyor_cells!=expected_conveyor_cells || world_escrows!=expected_world_escrows || world_returns!=expected_world_returns)
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
               ((a.kind==ActivityKind::ShipDestroyed &&
                    cmd->second.action!=Action::HostDestroyShip &&
                    cmd->second.action!=Action::HostDestroyPassengerFerry &&
                    cmd->second.action!=Action::HostResolveMixedFerryLoss) ||
                (a.kind==ActivityKind::NpcLost &&
                    cmd->second.action!=Action::HostCrewLoss &&
                    cmd->second.action!=Action::HostDestroyPassengerFerry &&
                    cmd->second.action!=Action::HostResolveMixedFerryLoss)) ||
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
        // DEMOG-05q6b: recoverable cross-store effect receipt. Never trust a
        // surviving medical/family receipt without its committed world step,
        // physical casualty tombstones, and regional receipt counterpart.
        {
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                "AND name='sg_med_society_effect'");
            const bool has_effect=table.row();
            Statement regional_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                         "AND name='sg_regional_command_receipt'");
            const bool has_regional=regional_table.row();
            if(has_regional){
                Statement names(db_,"SELECT command_id FROM sg_regional_command_receipt "
                                    "WHERE actor='society-host'");
                while(names.row()){
                    if(!has_effect)throw std::runtime_error("SQLite: society effect table missing");
                    Statement count(db_,"SELECT 1 FROM sg_med_society_effect WHERE command_id=?");
                    count.text(1,sqlite_str(names.get(),0));
                    if(!count.row() || count.row())
                        throw std::runtime_error("SQLite: society regional receipt without world effect");
                }
            }
            if(has_effect){
                Statement rows(db_,"SELECT command_id,world_generation,tick,family_scope,region_scope,"
                                   "region_revision,owner_generation,casualty_count "
                                   "FROM sg_med_society_effect");
                while(rows.row()){
                    const auto id=parse_u64(sqlite3_column_text(rows.get(),0));
                    const auto gen=sqlite3_column_int64(rows.get(),1);
                    const auto tick=sqlite3_column_int64(rows.get(),2);
                    const auto fam=sqlite_str(rows.get(),3), reg=sqlite_str(rows.get(),4);
                    const auto rev=sqlite3_column_int64(rows.get(),5);
                    const auto owner=sqlite3_column_int64(rows.get(),6);
                    const auto casualty_count=sqlite3_column_int64(rows.get(),7);
                    const auto ev=event_by_id.find(id);
                    if(!id || gen<=1 || static_cast<std::uint64_t>(gen)>generation ||
                       tick<=0 || fam.empty() || reg.empty() || rev<=1 || owner<1 ||
                       casualty_count<=0 || ev==event_by_id.end() ||
                       ev->second.result!=Outcome::Applied ||
                       ev->second.tick!=static_cast<std::uint64_t>(tick))
                        throw std::runtime_error("SQLite: malformed society effect or world event");
                    Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                    op.i64(1,gen);
                    if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                       sqlite3_column_int64(op.get(),1)!=tick || op.row())
                        throw std::runtime_error("SQLite: society effect lacks exact historical step");
                    Statement receipt(db_,"SELECT revision,actor,request FROM "
                                          "sg_regional_command_receipt WHERE scope=? AND command_id=?");
                    receipt.text(1,reg);receipt.text(2,std::to_string(id));
                    if(!receipt.row() || sqlite3_column_int64(receipt.get(),0)!=rev ||
                       sqlite_str(receipt.get(),1)!="society-host" ||
                       sqlite_str(receipt.get(),2)!="society-deaths-generation:"+std::to_string(tick) ||
                       receipt.row())
                        throw std::runtime_error("SQLite: society effect regional receipt mismatch");
                    std::int64_t matched=0;
                    for(const auto& [npc,dead]:restored.npc_casualties()){
                        if(dead.command_id!=id)continue;
                        if(dead.death_tick!=static_cast<std::uint64_t>(tick))
                            throw std::runtime_error("SQLite: society death tick altered");
                        Statement family(db_,"SELECT 1 FROM npc_family_deaths WHERE scope=? "
                                             "AND npc_id=? AND settlement=? AND tick=?");
                        family.text(1,fam);family.text(2,std::to_string(npc));
                        family.text(3,std::to_string(dead.identity.home_ship));
                        family.i64(4,tick);
                        if(!family.row() || family.row())
                            throw std::runtime_error("SQLite: society death lacks family tombstone");
                        ++matched;
                    }
                    if(matched!=casualty_count)
                        throw std::runtime_error("SQLite: society casualty count contradiction");
                }
            }
        }
        // DEMOG-05q6c: independent recovery proof for each atomically admitted
        // ART02 reserved birth. The child is a family cohort, never a fake NPC.
        {
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                "AND name='sg_med_society_birth'");
            const bool has_births=table.row();
            Statement regional_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_regional_command_receipt'");
            if(regional_table.row()){
                Statement ids(db_,"SELECT command_id FROM sg_regional_command_receipt "
                    "WHERE actor='society-birth-host'");
                while(ids.row()){
                    if(!has_births)throw std::runtime_error("SQLite: birth effect table missing");
                    Statement effect(db_,"SELECT 1 FROM sg_med_society_birth WHERE event_id=?");
                    effect.text(1,sqlite_str(ids.get(),0));
                    if(!effect.row() || effect.row())
                        throw std::runtime_error("SQLite: regional birth receipt missing world effect");
                }
            }
            if(has_births){
                Statement rows(db_,"SELECT event_id,world_generation,tick,family_scope,region_scope,"
                    "region_revision,owner_generation,home_ship,regional_colony,"
                    "child_id,parent_a,parent_b,family_revision FROM sg_med_society_birth");
                while(rows.row()){
                    const auto event_id=parse_u64(sqlite3_column_text(rows.get(),0));
                    const auto gen=sqlite3_column_int64(rows.get(),1);
                    const auto tick=sqlite3_column_int64(rows.get(),2);
                    const auto fam=sqlite_str(rows.get(),3), reg=sqlite_str(rows.get(),4);
                    const auto revision=sqlite3_column_int64(rows.get(),5);
                    const auto owner=sqlite3_column_int64(rows.get(),6);
                    const auto home=parse_u64(sqlite3_column_text(rows.get(),7));
                    const auto colony=sqlite_str(rows.get(),8);
                    const auto child=parse_u64(sqlite3_column_text(rows.get(),9));
                    const auto a=parse_u64(sqlite3_column_text(rows.get(),10));
                    const auto b=parse_u64(sqlite3_column_text(rows.get(),11));
                    const auto family_rev=sqlite3_column_int64(rows.get(),12);
                    if(!event_id || gen<=1 || static_cast<std::uint64_t>(gen)>generation ||
                       tick<=0 || fam.empty() || reg.empty() || colony.empty() ||
                       revision<=1 || owner<1 || !home || child<(1ULL<<62) ||
                       !a || !b || a>=b || family_rev<1 ||
                       restored.npc_records().count(child)!=0 ||
                       restored.npc_casualties().count(child)!=0)
                        throw std::runtime_error("SQLite: malformed atomic dependent birth receipt");
                    Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                    op.i64(1,gen);
                    if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                       sqlite3_column_int64(op.get(),1)!=tick || op.row())
                        throw std::runtime_error("SQLite: dependent birth lacks world step");
                    Statement regional(db_,"SELECT revision,actor,request,outcome "
                        "FROM sg_regional_command_receipt WHERE scope=? AND command_id=?");
                    regional.text(1,reg);regional.text(2,std::to_string(event_id));
                    if(!regional.row() || sqlite3_column_int64(regional.get(),0)!=revision ||
                       sqlite_str(regional.get(),1)!="society-birth-host" ||
                       sqlite_str(regional.get(),2)!="society-birth-generation:"+
                            std::to_string(tick)+":"+colony ||
                       sqlite3_column_int64(regional.get(),3)!=1 || regional.row())
                        throw std::runtime_error("SQLite: dependent birth regional receipt disagrees");
                    Statement family(db_,"SELECT settlement,child_id,parent_a,parent_b,tick,"
                        "census_revision FROM npc_family_births WHERE scope=? AND event_id=?");
                    family.text(1,fam);family.text(2,std::to_string(event_id));
                    if(!family.row() ||
                       parse_u64(sqlite3_column_text(family.get(),0))!=home ||
                       parse_u64(sqlite3_column_text(family.get(),1))!=child ||
                       parse_u64(sqlite3_column_text(family.get(),2))!=a ||
                       parse_u64(sqlite3_column_text(family.get(),3))!=b ||
                       sqlite3_column_int64(family.get(),4)!=tick ||
                       sqlite3_column_int64(family.get(),5)!=family_rev ||
                       family.row())
                        throw std::runtime_error("SQLite: dependent birth family receipt disagrees");
                    Statement member(db_,"SELECT settlement,parent_a,parent_b FROM npc_family_people "
                        "WHERE scope=? AND npc_id=?");
                    member.text(1,fam);member.text(2,std::to_string(child));
                    if(!member.row() ||
                       parse_u64(sqlite3_column_text(member.get(),0))!=home ||
                       parse_u64(sqlite3_column_text(member.get(),1))!=a ||
                       parse_u64(sqlite3_column_text(member.get(),2))!=b || member.row())
                        throw std::runtime_error("SQLite: dependent child identity/lineage absent");
                }
            }
        }
        // DEMOG-05q6d: an executed residence-transfer command MUST have
        // exactly one cross-store receipt. Verify both sides of its physical
        // world step, the immutable ART02 move and the regional owner receipt.
        {
            Statement table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                "AND name='sg_society_migration'");
            const bool has_migration=table.row();
            Statement planetary_household_table(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_planet_household_migration'");
            const bool has_planetary_household=planetary_household_table.row();
            for(const auto& [id,c]:journal_commands){
                if(c.action!=Action::HostSettleMigrant)continue;
                const auto ev=event_by_id.find(id);
                if(ev==event_by_id.end() || ev->second.result!=Outcome::Applied)continue;
                if(!has_migration)
                    throw std::runtime_error("SQLite: applied migrant missing society migration table");
                Statement row(db_,"SELECT 1 FROM sg_society_migration WHERE command_id=?");
                row.text(1,std::to_string(id));
                if(!row.row() || row.row())
                    throw std::runtime_error("SQLite: applied migrant missing cross-store receipt");
            }
            Statement region_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                         "AND name='sg_regional_command_receipt'");
            if(region_table.row()){
                Statement receipt(db_,"SELECT command_id FROM sg_regional_command_receipt "
                    "WHERE actor='society-migration-host'");
                while(receipt.row()){
                    // The v118 planetary family writer commits a distinct
                    // receipt, but uses the same original regional authority.
                    // Exactly one of the older migrant or new household
                    // transactions must own each regional migration command.
                    const auto id=sqlite_str(receipt.get(),0);
                    bool linked=false;
                    if(has_migration){
                        Statement old(db_,"SELECT 1 FROM sg_society_migration "
                                          "WHERE command_id=?");
                        old.text(1,id);
                        if(old.row()){
                            if(old.row())throw std::runtime_error(
                                "SQLite: duplicate society migration");
                            linked=true;
                        }
                    }
                    if(has_planetary_household){
                        Statement household(db_,"SELECT 1 FROM sg_planet_household_migration "
                                                 "WHERE request_id=?");
                        household.text(1,id);
                        if(household.row()){
                            if(household.row() || linked)
                                throw std::runtime_error(
                                    "SQLite: colliding planetary and society migration");
                            linked=true;
                        }
                    }
                    if(!linked)
                        throw std::runtime_error("SQLite: regional migration unlinked");
                }
            }
            Statement family_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                         "AND name='npc_family_moves'");
            if(family_table.row()){
                Statement q(db_,"SELECT scope,event_id FROM npc_family_moves");
                while(q.row()){
                    Statement evacuation_table(db_,"SELECT 1 FROM sqlite_master "
                        "WHERE type='table' AND name='sg_colony_family_evacuation'");
                    if(evacuation_table.row()){
                        Statement linked(db_,"SELECT 1 FROM sg_colony_family_evacuation_member m "
                            "JOIN sg_colony_family_evacuation e ON e.command_id=m.command_id "
                            "WHERE m.event_id=? AND e.family_scope=?");
                        linked.text(1,sqlite_str(q.get(),1));
                        linked.text(2,sqlite_str(q.get(),0));
                        if(linked.row()){
                            if(linked.row())throw std::runtime_error("duplicate evacuation family move");
                            continue; // audited against the full world receipt below
                        }
                    }
                    // The v118 household move writes THREE ART02 events
                    // (guardian, co-parent and the existing registered child).
                    // These are owned by the same atomic planetary receipt,
                    // not by the older HostSettleMigrant command.
                    if(has_planetary_household){
                        Statement household(db_,"SELECT 1 FROM sg_planet_household_migration "
                            "WHERE family_scope=? AND "
                            "(request_id=? OR coparent_event=? OR child_event=?)");
                        const auto scope=sqlite_str(q.get(),0);
                        const auto event=sqlite_str(q.get(),1);
                        household.text(1,scope);
                        household.text(2,event);
                        household.text(3,event);
                        household.text(4,event);
                        if(household.row()){
                            if(household.row())
                                throw std::runtime_error(
                                    "SQLite: family event linked to multiple planetary households");
                            continue; // validated against all three ART02 moves below
                        }
                    }
                    if(!has_migration)throw std::runtime_error("SQLite: family move without world effect");
                    Statement effect(db_,"SELECT 1 FROM sg_society_migration WHERE command_id=?");
                    effect.text(1,sqlite_str(q.get(),1));
                    if(!effect.row()){
                        Statement co_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                            "AND name='sg_society_migration'");
                        if(co_table.row()){
                            bool co_column=false;
                            {Statement cols(db_,"PRAGMA table_info(sg_society_migration)");
                             while(cols.row())if(sqlite_str(cols.get(),1)=="co_parent_event")co_column=true;}
                            if(co_column){
                                Statement co(db_,"SELECT 1 FROM sg_society_migration "
                                    "WHERE family_scope=? AND co_parent_event=?");
                                co.text(1,sqlite_str(q.get(),0));
                                co.text(2,sqlite_str(q.get(),1));
                                if(co.row())continue;
                            }
                        }
                        Statement member(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                            "AND name='sg_society_household_member'");
                        if(!member.row())
                            throw std::runtime_error("SQLite: family move missing world effect");
                        Statement link(db_,"SELECT 1 FROM sg_society_household_member "
                            "WHERE event_id=? AND family_scope=?");
                        link.text(1,sqlite_str(q.get(),1));
                        link.text(2,sqlite_str(q.get(),0));
                        if(!link.row() || link.row())
                            throw std::runtime_error("SQLite: household family move unlinked");
                    }else if(effect.row())
                        throw std::runtime_error("SQLite: duplicate family migration effect");
                }
            }
            // WO3-E/v118: independently reconcile every legal household
            // receipt against all three ART02 moves, the same regional
            // transaction and the before/after original-world snapshots.
            // A root, co-parent or child event cannot be accepted by merely
            // existing in a table without its original family and site proof.
            if(has_planetary_household){
                Statement households(db_,"SELECT request_id,world_generation,tick,"
                    "origin,destination,guardian,coparent,child,returning,"
                    "family_scope,region_scope,region_revision,owner_generation,"
                    "coparent_event,child_event FROM sg_planet_household_migration");
                while(households.row()){
                    const auto request=parse_u64(sqlite3_column_text(households.get(),0));
                    const auto gen=sqlite3_column_int64(households.get(),1);
                    const auto tick=sqlite3_column_int64(households.get(),2);
                    const auto origin=parse_u64(sqlite3_column_text(households.get(),3));
                    const auto destination=parse_u64(sqlite3_column_text(households.get(),4));
                    const auto guardian=parse_u64(sqlite3_column_text(households.get(),5));
                    const auto parent=parse_u64(sqlite3_column_text(households.get(),6));
                    const auto child=parse_u64(sqlite3_column_text(households.get(),7));
                    const auto returning=sqlite3_column_int64(households.get(),8);
                    const auto scope=sqlite_str(households.get(),9);
                    const auto region_scope=sqlite_str(households.get(),10);
                    const auto region_revision=sqlite3_column_int64(households.get(),11);
                    const auto owner_generation=sqlite3_column_int64(households.get(),12);
                    const auto coparent_event=parse_u64(sqlite3_column_text(households.get(),13));
                    const auto child_event=parse_u64(sqlite3_column_text(households.get(),14));
                    if(!request || request%16 || request>
                           std::numeric_limits<std::uint64_t>::max()-2 ||
                       coparent_event!=request+1 || child_event!=request+2 ||
                       gen<=1 || static_cast<std::uint64_t>(gen)>generation ||
                       tick<=0 || !origin || !destination || origin==destination ||
                       !guardian || !parent || guardian==parent ||
                       child<(std::uint64_t{1}<<62) ||
                       (returning!=0 && returning!=1) ||
                       scope.empty() || region_scope.empty() ||
                       region_revision<2 || owner_generation<1)
                        throw std::runtime_error("SQLite: invalid planetary household receipt");
                    const std::vector<std::pair<std::uint64_t,std::uint64_t>> members{
                        {request,guardian},{coparent_event,parent},{child_event,child}};
                    for(const auto& member:members){
                        Statement move(db_,"SELECT npc_id,origin,destination,tick "
                            "FROM npc_family_moves WHERE scope=? AND event_id=?");
                        move.text(1,scope);
                        move.text(2,std::to_string(member.first));
                        if(!move.row() ||
                           parse_u64(sqlite3_column_text(move.get(),0))!=member.second ||
                           parse_u64(sqlite3_column_text(move.get(),1))!=origin ||
                           parse_u64(sqlite3_column_text(move.get(),2))!=destination ||
                           sqlite3_column_int64(move.get(),3)!=tick || move.row())
                            throw std::runtime_error(
                                "SQLite: planetary household ART02 move missing or forged");
                    }
                    Statement region(db_,"SELECT actor,request,revision,outcome "
                        "FROM sg_regional_command_receipt WHERE scope=? AND command_id=?");
                    region.text(1,region_scope);
                    region.text(2,std::to_string(request));
                    if(!region.row() ||
                       sqlite_str(region.get(),0)!="society-migration-host" ||
                       ![&](){
                           const auto request_text=sqlite_str(region.get(),1);
                           const auto prefix="society-migration-generation:"+
                               std::to_string(tick)+":";
                           const std::string suffix=":household:3";
                           return request_text.size()>prefix.size()+suffix.size() &&
                               request_text.compare(0,prefix.size(),prefix)==0 &&
                               request_text.compare(request_text.size()-suffix.size(),
                                                    suffix.size(),suffix)==0;
                       }() ||
                       sqlite3_column_int64(region.get(),2)!=region_revision ||
                       sqlite3_column_int64(region.get(),3)!=1 || region.row())
                        throw std::runtime_error(
                            "SQLite: planetary household regional receipt missing");
                    const auto read_state=[&](std::int64_t g){
                        Statement snapshot(db_,"SELECT state FROM snapshots WHERE generation=?");
                        snapshot.i64(1,g);
                        if(!snapshot.row())throw std::runtime_error(
                            "SQLite: planetary household world checkpoint missing");
                        const auto* data=sqlite3_column_blob(snapshot.get(),0);
                        const int n=sqlite3_column_bytes(snapshot.get(),0);
                        if(!data || n<=0)throw std::runtime_error(
                            "SQLite: empty planetary household world checkpoint");
                        const std::string bytes(static_cast<const char*>(data),
                                                static_cast<std::size_t>(n));
                        if(snapshot.row())throw std::runtime_error(
                            "SQLite: duplicate planetary household world checkpoint");
                        return Simulation::from_snapshot(bytes);
                    };
                    const auto before=read_state(gen-1),after=read_state(gen);
                    if(after.tick()!=static_cast<std::uint64_t>(tick) ||
                       before.tick()!=after.tick() ||
                       before.npc_records().size()!=after.npc_records().size() ||
                       before.physical_dependents().size()!=
                           after.physical_dependents().size())
                        throw std::runtime_error(
                            "SQLite: household migration changed world identities");
                    bool matched=false;
                    for(const auto& [key,site]:after.planetary_sites()){
                        const auto entry=site.households.find(guardian);
                        if(entry==site.households.end())continue;
                        const auto& h=entry->second;
                        if(h.co_parent_id!=parent || h.child_ids.size()!=1 ||
                           h.child_ids.front()!=child ||
                           (returning ? h.return_request!=request :
                                        h.admission_request!=request))
                            continue;
                        const auto surface=site.surface_location;
                        if((returning && (origin!=surface ||
                                          destination!=site.source_ship)) ||
                           (!returning && (origin!=site.source_ship ||
                                           destination!=surface)) ||
                           matched)
                            throw std::runtime_error(
                                "SQLite: planetary household endpoint mismatch");
                        const auto earlier=before.planetary_sites().find(key);
                        if(earlier==before.planetary_sites().end())
                            throw std::runtime_error(
                                "SQLite: household site missing before admission");
                        const auto prev=earlier->second.households.find(guardian);
                        if((!returning && prev!=earlier->second.households.end()) ||
                           (returning && (prev==earlier->second.households.end() ||
                                           prev->second.return_request ||
                                           prev->second.admission_request!=h.admission_request)))
                            throw std::runtime_error(
                                "SQLite: household did not make one legal transition");
                        for(const auto adult:{guardian,parent}){
                            const auto b=before.npc_records().find(adult);
                            const auto a=after.npc_records().find(adult);
                            if(b==before.npc_records().end() ||
                               a==after.npc_records().end() ||
                               b->second.legal_residence().id!=origin ||
                               a->second.legal_residence().id!=destination)
                                throw std::runtime_error(
                                    "SQLite: household adult legal residence mismatch");
                        }
                        const auto dependent=after.physical_dependents().find(child);
                        if(dependent==after.physical_dependents().end() ||
                           dependent->second.location!=
                               (returning?site.source_ship:surface))
                            throw std::runtime_error(
                                "SQLite: household child custody mismatch");
                        matched=true;
                    }
                    if(!matched)throw std::runtime_error(
                        "SQLite: planetary household receipt has no world transition");
                }
            }
            if(has_migration){
                bool has_household_count=false,has_physical_manifest=false,has_coparent=false;
                {
                    Statement info(db_,"PRAGMA table_info(sg_society_migration)");
                    while(info.row()){
                        if(sqlite_str(info.get(),1)=="household_count")has_household_count=true;
                        if(sqlite_str(info.get(),1)=="physical_manifest")has_physical_manifest=true;
                        if(sqlite_str(info.get(),1)=="co_parent_id")has_coparent=true;
                    }
                }
                const std::string query=std::string("SELECT command_id,world_generation,tick,family_scope,")+
                    "region_scope,region_revision,owner_generation,origin,destination,"+
                    "origin_colony,destination_colony,npc_id,transport_id,"+
                    "origin_family_revision,destination_family_revision,"+
                    (has_household_count ? "household_count" : "1")+","+
                    (has_physical_manifest ? "physical_manifest" : "0")+","+
                    (has_coparent ? "co_parent_id,co_parent_event" : "'0','0'")+
                    " FROM sg_society_migration";
                Statement rows(db_,query.c_str());
                while(rows.row()){
                    const auto id=parse_u64(sqlite3_column_text(rows.get(),0));
                    const auto gen=sqlite3_column_int64(rows.get(),1);
                    const auto tick=sqlite3_column_int64(rows.get(),2);
                    const auto fam=sqlite_str(rows.get(),3), reg=sqlite_str(rows.get(),4);
                    const auto reg_rev=sqlite3_column_int64(rows.get(),5);
                    const auto owner=sqlite3_column_int64(rows.get(),6);
                    const auto src=parse_u64(sqlite3_column_text(rows.get(),7));
                    const auto dst=parse_u64(sqlite3_column_text(rows.get(),8));
                    const auto src_col=sqlite_str(rows.get(),9), dst_col=sqlite_str(rows.get(),10);
                    const auto npc=parse_u64(sqlite3_column_text(rows.get(),11));
                    const auto voyage=parse_u64(sqlite3_column_text(rows.get(),12));
                    const auto src_rev=sqlite3_column_int64(rows.get(),13);
                    const auto dst_rev=sqlite3_column_int64(rows.get(),14);
                    const auto household_count=sqlite3_column_int64(rows.get(),15);
                    const auto physical_manifest=sqlite3_column_int64(rows.get(),16);
                    const auto co=parse_u64(sqlite3_column_text(rows.get(),17));
                    const auto co_event=parse_u64(sqlite3_column_text(rows.get(),18));
                    const auto cmd=journal_commands.find(id);
                    const auto ev=event_by_id.find(id);
                    if(!id || gen<=1 || static_cast<std::uint64_t>(gen)>generation ||
                       tick<=0 || fam.empty() || reg.empty() || reg_rev<2 || owner<1 ||
                       src==dst || src_col.empty() || dst_col.empty() || src_col==dst_col ||
                       !npc || !voyage || src_rev<1 || dst_rev<1 ||
                       household_count<1 || household_count>34 ||
                       ((co==0)!=(co_event==0)) ||
                       (co && (co<=npc || co_event==id || household_count<2 ||
                         !has_coparent)) ||
                       (physical_manifest!=0 && physical_manifest!=1) ||
                       (physical_manifest==1 && household_count<2) ||
                       cmd==journal_commands.end() || ev==event_by_id.end() ||
                       cmd->second.action!=Action::HostSettleMigrant ||
                       cmd->second.actor!=src || cmd->second.target!=dst ||
                       cmd->second.transport_id!=voyage ||
                       cmd->second.passenger_ids!=(co ?
                           std::vector<std::uint64_t>{npc,co}:
                           std::vector<std::uint64_t>{npc}) ||
                       ev->second.result!=Outcome::Applied ||
                       ev->second.tick!=static_cast<std::uint64_t>(tick))
                        throw std::runtime_error("SQLite: malformed atomic migrant or wrong world command");
                    Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                    op.i64(1,gen);
                    if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                       sqlite3_column_int64(op.get(),1)!=tick || op.row())
                        throw std::runtime_error("SQLite: migration has no committed world step");
                    Statement family(db_,"SELECT npc_id,origin,destination,tick,"
                        "origin_revision,destination_revision FROM npc_family_moves "
                        "WHERE scope=? AND event_id=?");
                    family.text(1,fam);family.text(2,std::to_string(id));
                    if(!family.row() || parse_u64(sqlite3_column_text(family.get(),0))!=npc ||
                       parse_u64(sqlite3_column_text(family.get(),1))!=src ||
                       parse_u64(sqlite3_column_text(family.get(),2))!=dst ||
                       sqlite3_column_int64(family.get(),3)!=tick ||
                       sqlite3_column_int64(family.get(),4)!=src_rev ||
                       sqlite3_column_int64(family.get(),5)!=dst_rev || family.row())
                        throw std::runtime_error("SQLite: missing/forged ART02 family migration");
                    if(co){
                        Statement parent(db_,"SELECT npc_id,origin,destination,tick,"
                            "origin_revision,destination_revision FROM npc_family_moves "
                            "WHERE scope=? AND event_id=?");
                        parent.text(1,fam);parent.text(2,std::to_string(co_event));
                        if(!parent.row() ||
                           parse_u64(sqlite3_column_text(parent.get(),0))!=co ||
                           parse_u64(sqlite3_column_text(parent.get(),1))!=src ||
                           parse_u64(sqlite3_column_text(parent.get(),2))!=dst ||
                           sqlite3_column_int64(parent.get(),3)!=tick ||
                           sqlite3_column_int64(parent.get(),4)!=src_rev+1 ||
                           sqlite3_column_int64(parent.get(),5)!=dst_rev+1 || parent.row())
                            throw std::runtime_error("missing or forged co-parent family migration");
                    }
                    if(household_count>(co?2:1)){
                        Statement child_table(db_,"SELECT 1 FROM sqlite_master "
                            "WHERE type='table' AND name='sg_society_household_member'");
                        if(!child_table.row())throw std::runtime_error("missing household receipt table");
                        Statement members(db_,"SELECT event_id,child_id,guardian_id,origin,"
                            "destination,origin_revision,destination_revision "
                            "FROM sg_society_household_member WHERE parent_command_id=? "
                            "AND family_scope=? ORDER BY child_id");
                        members.text(1,std::to_string(id));
                        members.text(2,fam);
                        std::uint64_t previous_child=0;
                        std::int64_t found=co?2:1;
                        while(members.row()){
                            const auto child_event=sqlite_str(members.get(),0);
                            const auto child=parse_u64(sqlite3_column_text(members.get(),1));
                            const auto guardian=parse_u64(sqlite3_column_text(members.get(),2));
                            const auto cm_src=parse_u64(sqlite3_column_text(members.get(),3));
                            const auto cm_dst=parse_u64(sqlite3_column_text(members.get(),4));
                            const auto cr_src=sqlite3_column_int64(members.get(),5);
                            const auto cr_dst=sqlite3_column_int64(members.get(),6);
                            if(child<(1ULL<<62) || child<=previous_child || guardian!=npc ||
                               cm_src!=src || cm_dst!=dst || cr_src!=src_rev+found ||
                               cr_dst!=dst_rev+found)
                                throw std::runtime_error("forged dependent household manifest");
                            previous_child=child;
                            Statement birth(db_,"SELECT parent_a,parent_b FROM npc_family_births "
                                "WHERE scope=? AND child_id=?");
                            birth.text(1,fam);birth.text(2,std::to_string(child));
                            if(!birth.row())throw std::runtime_error("household child has no birth");
                            const auto a=parse_u64(sqlite3_column_text(birth.get(),0));
                            const auto b=parse_u64(sqlite3_column_text(birth.get(),1));
                            if((a!=npc && b!=npc) || birth.row())
                                throw std::runtime_error("household child lineage forged");
                            if(physical_manifest==0 && has_dependent_table()){
                                Statement fraud(db_,"SELECT 1 FROM sg_dependent_travel "
                                    "WHERE family_scope=? AND child_id=? AND "
                                    "transport_id=? AND guardian_id=? AND status=2");
                                fraud.text(1,fam);fraud.text(2,std::to_string(child));
                                fraud.text(3,std::to_string(voyage));
                                fraud.text(4,std::to_string(npc));
                                if(fraud.row())
                                    throw std::runtime_error("physical household receipt downgraded");
                            }
                            if(physical_manifest==1){
                                if(!has_dependent_table())
                                    throw std::runtime_error("physical family transport ledger removed");
                                Statement travel(db_,"SELECT arrival_generation FROM "
                                    "sg_dependent_travel WHERE family_scope=? AND child_id=? "
                                    "AND transport_id=? AND guardian_id=? AND origin=? "
                                    "AND destination=? AND status=2");
                                travel.text(1,fam);travel.text(2,std::to_string(child));
                                travel.text(3,std::to_string(voyage));
                                travel.text(4,std::to_string(npc));
                                travel.text(5,std::to_string(src));
                                travel.text(6,std::to_string(dst));
                                if(!travel.row() || sqlite3_column_int64(travel.get(),0)>=gen ||
                                   travel.row())
                                    throw std::runtime_error("household moved without child arrival");
                            }
                            Statement moved_child(db_,"SELECT npc_id,origin,destination,tick,"
                                "origin_revision,destination_revision FROM npc_family_moves "
                                "WHERE scope=? AND event_id=?");
                            moved_child.text(1,fam);moved_child.text(2,child_event);
                            if(!moved_child.row() ||
                               parse_u64(sqlite3_column_text(moved_child.get(),0))!=child ||
                               parse_u64(sqlite3_column_text(moved_child.get(),1))!=src ||
                               parse_u64(sqlite3_column_text(moved_child.get(),2))!=dst ||
                               sqlite3_column_int64(moved_child.get(),3)!=tick ||
                               sqlite3_column_int64(moved_child.get(),4)!=cr_src ||
                               sqlite3_column_int64(moved_child.get(),5)!=cr_dst ||
                               moved_child.row())
                                throw std::runtime_error("household child missing ART02 move");
                            ++found;
                        }
                        if(found!=household_count)
                            throw std::runtime_error("incomplete household group on replay");
                    }
                    Statement region(db_,"SELECT actor,request,revision,outcome FROM "
                        "sg_regional_command_receipt WHERE scope=? AND command_id=?");
                    region.text(1,reg);region.text(2,std::to_string(id));
                    if(!region.row() ||
                       sqlite_str(region.get(),0)!="society-migration-host" ||
                       sqlite_str(region.get(),1)!="society-migration-generation:"+
                           std::to_string(tick)+":"+src_col+":"+dst_col+
                           (household_count==1 ? "" : ":household:"+
                              std::to_string(household_count)) ||
                       sqlite3_column_int64(region.get(),2)!=reg_rev ||
                       sqlite3_column_int64(region.get(),3)!=1 || region.row())
                        throw std::runtime_error("SQLite: missing/forged regional migration receipt");
                    const auto snapshot=[&](std::int64_t generation_at)->Simulation{
                        Statement state(db_,"SELECT state FROM snapshots WHERE generation=?");
                        state.i64(1,generation_at);
                        if(!state.row())throw std::runtime_error("SQLite: missing migration checkpoint");
                        const auto* bytes=sqlite3_column_blob(state.get(),0);
                        const int size=sqlite3_column_bytes(state.get(),0);
                        if(!bytes || size<=0)throw std::runtime_error("SQLite: empty migration checkpoint");
                        const std::string serialized(static_cast<const char*>(bytes),
                                                     static_cast<std::size_t>(size));
                        if(state.row())throw std::runtime_error("SQLite: duplicate checkpoint generation");
                        return Simulation::from_snapshot(serialized);
                    };
                    const auto before=snapshot(gen-1),after=snapshot(gen);
                    if(co){
                        const auto prior_co=before.npc_records().find(co);
                        const auto now_co=after.npc_records().find(co);
                        if(prior_co==before.npc_records().end() ||
                           now_co==after.npc_records().end() ||
                           prior_co->second.home_ship!=src ||
                           prior_co->second.assigned_site!=src ||
                           prior_co->second.location!=dst || prior_co->second.transport_id ||
                           now_co->second.home_ship!=dst ||
                           now_co->second.assigned_site!=dst ||
                           now_co->second.location!=dst || now_co->second.transport_id)
                            throw std::runtime_error("co-parent migration has invalid world state");
                    }
                    const auto prior=before.npc_records().find(npc),now=after.npc_records().find(npc);
                    if(prior==before.npc_records().end() || now==after.npc_records().end() ||
                       prior->second.home_ship!=src || prior->second.assigned_site!=src ||
                       prior->second.location!=dst || prior->second.transport_id ||
                       now->second.home_ship!=dst || now->second.assigned_site!=dst ||
                       now->second.location!=dst || now->second.transport_id ||
                       before.npc_records().size()!=after.npc_records().size() ||
                       before.npc_casualties().size()!=after.npc_casualties().size() ||
                       before.colony_ship(src)->crew!=after.colony_ship(src)->crew+(co?2:1) ||
                       before.colony_ship(dst)->crew+(co?2:1)!=after.colony_ship(dst)->crew ||
                       after.tick()!=static_cast<std::uint64_t>(tick))
                        throw std::runtime_error("SQLite: migrant changed custody or population history");
                }
            }
            // In addition to checking every root receipt, reject orphaned
            // child links or a forged attempt to downgrade a household to a
            // one-person move while retaining dependent receipts.
            Statement household_table(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                     "AND name='sg_society_household_member'");
            if(household_table.row()){
                Statement children(db_,"SELECT parent_command_id,event_id,child_id,family_scope "
                    "FROM sg_society_household_member");
                while(children.row()){
                    if(!has_migration)
                        throw std::runtime_error("SQLite: orphaned dependent move table");
                    Statement parent(db_,"SELECT household_count,npc_id FROM "
                        "sg_society_migration WHERE command_id=?");
                    parent.text(1,sqlite_str(children.get(),0));
                    if(!parent.row() || sqlite3_column_int64(parent.get(),0)<2 ||
                       parent.row())
                        throw std::runtime_error("SQLite: orphaned or downgraded household receipt");
                    Statement child(db_,"SELECT npc_id FROM npc_family_moves "
                        "WHERE scope=? AND event_id=?");
                    child.text(1,sqlite_str(children.get(),3));
                    child.text(2,sqlite_str(children.get(),1));
                    if(!child.row() ||
                       sqlite_str(child.get(),0)!=sqlite_str(children.get(),2) ||
                       child.row())
                        throw std::runtime_error("SQLite: orphaned dependent family move");
                }
            }
        }
        // AUTO-04D.4D2B2: each v60 family departure has one independently
        // reconstructable ART02/ECON03 side effect with its same world step.
        {
            Statement heads(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_family_evacuation'");
            const bool exists=heads.row();
            for(const auto& [source,departure]:restored.colony_departures()){
                if(departure.relocated_dependents.empty())continue;
                if(!exists)throw std::runtime_error("SQLite: child evacuated without family ledger");
                Statement found(db_,"SELECT 1 FROM sg_colony_family_evacuation "
                    "WHERE command_id=? AND source=?");
                found.text(1,std::to_string(departure.command_id));
                found.text(2,std::to_string(source));
                if(!found.row() || found.row())
                    throw std::runtime_error("SQLite: missing completed family evacuation receipt");
            }
            if(exists){
                for(const auto* table:{"sg_colony_family_evacuation",
                                     "sg_colony_family_evacuation_member"}){
                    Statement present(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                        "AND name=?");
                    present.text(1,table);
                    if(!present.row())throw std::runtime_error("SQLite: missing evacuation ledger");
                    for(const auto* suffix:{"_immutable_update","_immutable_delete"}){
                        Statement trigger(db_,"SELECT 1 FROM sqlite_master "
                            "WHERE type='trigger' AND name=?");
                        trigger.text(1,std::string(table)+suffix);
                        if(!trigger.row())
                            throw std::runtime_error("SQLite: removed evacuation immutable trigger");
                    }
                }
                Statement all(db_,"SELECT command_id,intent_id,world_generation,tick,"
                    "family_scope,family_seed,region_scope,region_revision,owner_generation,"
                    "source,receiver,origin_colony,receiver_colony,member_count,"
                    "adult_count,child_count FROM sg_colony_family_evacuation");
                while(all.row()){
                    const auto id=parse_u64(sqlite3_column_text(all.get(),0));
                    const auto intent=parse_u64(sqlite3_column_text(all.get(),1));
                    const auto gen=sqlite3_column_int64(all.get(),2);
                    const auto at=sqlite3_column_int64(all.get(),3);
                    const auto scope=sqlite_str(all.get(),4);
                    const auto seed=parse_u64(sqlite3_column_text(all.get(),5));
                    const auto regional_scope=sqlite_str(all.get(),6);
                    const auto revision=sqlite3_column_int64(all.get(),7);
                    const auto owner=sqlite3_column_int64(all.get(),8);
                    const auto source=parse_u64(sqlite3_column_text(all.get(),9));
                    const auto target=parse_u64(sqlite3_column_text(all.get(),10));
                    const auto from_colony=sqlite_str(all.get(),11);
                    const auto to_colony=sqlite_str(all.get(),12);
                    const auto count=sqlite3_column_int64(all.get(),13);
                    const auto adults=sqlite3_column_int64(all.get(),14);
                    const auto children=sqlite3_column_int64(all.get(),15);
                    const auto cmd=journal_commands.find(id);
                    const auto ev=event_by_id.find(id);
                    const auto departure=restored.colony_departures().find(source);
                    if(!id || !intent || id==intent || gen<3 ||
                       static_cast<std::uint64_t>(gen)>generation || at<1 ||
                       scope.empty() || !seed || regional_scope.empty() || revision<2 ||
                       owner<1 || !source || !target || source==target ||
                       from_colony.empty() || to_colony.empty() ||
                       from_colony==to_colony || count<3 || count>16 ||
                       adults<2 || children<1 || adults+children!=count ||
                       cmd==journal_commands.end() || ev==event_by_id.end() ||
                       cmd->second.action!=Action::HostColonyDeparture ||
                       cmd->second.actor!=source || cmd->second.target!=target ||
                       ev->second.result!=Outcome::Applied ||
                       ev->second.tick!=static_cast<std::uint64_t>(at) ||
                       departure==restored.colony_departures().end() ||
                       departure->second.command_id!=id ||
                       departure->second.receiver!=target ||
                       static_cast<sqlite3_int64>(departure->second.relocated_npcs.size())!=adults ||
                       static_cast<sqlite3_int64>(departure->second.relocated_dependents.size())!=children ||
                       cmd->second.passenger_ids!=departure->second.relocated_npcs)
                        throw std::runtime_error("SQLite: incomplete or mismatched family evacuation");
                    Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                    op.i64(1,gen);
                    if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                       sqlite3_column_int64(op.get(),1)!=at || op.row())
                        throw std::runtime_error("SQLite: family effect not joined to applied world step");
                    Statement prepared(db_,"SELECT generation,world_hash,source,receiver,"
                        "family_scope,family_seed,adult_roster,child_guardians,status "
                        "FROM sg_colony_family_departure_prepare WHERE intent_id=?");
                    prepared.text(1,std::to_string(intent));
                    if(!prepared.row() || sqlite3_column_int64(prepared.get(),0)!=gen-2 ||
                       sqlite_str(prepared.get(),2)!=std::to_string(source) ||
                       sqlite_str(prepared.get(),3)!=std::to_string(target) ||
                       sqlite_str(prepared.get(),4)!=scope ||
                       sqlite_str(prepared.get(),5)!=std::to_string(seed) ||
                       sqlite3_column_int(prepared.get(),8)!=1)
                        throw std::runtime_error("SQLite: family prepared evidence missing");
                    const auto original_hash=sqlite_str(prepared.get(),1);
                    const auto prepared_adults=sqlite_str(prepared.get(),6);
                    const auto prepared_children=sqlite_str(prepared.get(),7);
                    if(prepared.row())throw std::runtime_error("SQLite: duplicate family preparation");
                    Statement old_op(db_,"SELECT state_hash FROM operation_log WHERE generation=?");
                    old_op.i64(1,gen-2);
                    if(!old_op.row() || sqlite_str(old_op.get(),0)!=original_hash ||
                       old_op.row())
                        throw std::runtime_error("SQLite: family preparation has no original world");
                    Statement regional(db_,"SELECT actor,request,revision,outcome "
                        "FROM sg_regional_command_receipt WHERE scope=? AND command_id=?");
                    regional.text(1,regional_scope);regional.text(2,std::to_string(id));
                    if(!regional.row() ||
                       sqlite_str(regional.get(),0)!="colony-family-evacuation-host" ||
                       sqlite_str(regional.get(),1)!="society-migration-generation:"+
                         std::to_string(at)+":"+from_colony+":"+to_colony+
                         ":household:"+std::to_string(count) ||
                       sqlite3_column_int64(regional.get(),2)!=revision ||
                       sqlite3_column_int64(regional.get(),3)!=1 || regional.row())
                        throw std::runtime_error("SQLite: missing independent ECON03 family receipt");
                    const auto snapshot=[&](sqlite3_int64 g)->Simulation{
                        Statement row(db_,"SELECT state,checksum FROM snapshots WHERE generation=?");
                        row.i64(1,g);
                        if(!row.row())throw std::runtime_error("SQLite: family proof checkpoint missing");
                        const auto* raw=sqlite3_column_blob(row.get(),0);
                        const auto n=sqlite3_column_bytes(row.get(),0);
                        if(!raw || n<1 || n>16*1024*1024)
                            throw std::runtime_error("SQLite: invalid family checkpoint");
                        Simulation state=Simulation::from_snapshot(
                            std::string(static_cast<const char*>(raw),static_cast<std::size_t>(n)));
                        if(state.state_hash()!=parse_u64(sqlite3_column_text(row.get(),1)) ||
                           row.row())
                            throw std::runtime_error("SQLite: family checkpoint hash mismatch");
                        return state;
                    };
                    const auto before=snapshot(gen-1),after=snapshot(gen);
                    const auto world_receipt=after.colony_departures().find(source);
                    if(world_receipt==after.colony_departures().end() ||
                       world_receipt->second.command_id!=id ||
                       world_receipt->second.receiver!=target ||
                       world_receipt->second.relocated_npcs!=departure->second.relocated_npcs ||
                       world_receipt->second.relocated_dependents!=departure->second.relocated_dependents ||
                       before.tick()+1!=static_cast<std::uint64_t>(at) ||
                       after.tick()!=static_cast<std::uint64_t>(at) ||
                       after.people_physically_aboard(source)!=0 ||
                       after.children_physically_aboard(source)!=0 ||
                       before.npc_records().size()!=after.npc_records().size() ||
                       before.physical_dependents().size()!=after.physical_dependents().size())
                        throw std::runtime_error("SQLite: family relocation does not match native before/after");
                    std::string adult_roster,child_roster;
                    sqlite3_int64 previous_from=-1,previous_to=-1;
                    sqlite3_int64 members=0,seen_adults=0,seen_children=0;
                    Statement rows(db_,"SELECT event_id,person_id,guardian_id,kind,"
                        "origin_revision,destination_revision FROM sg_colony_family_evacuation_member "
                        "WHERE command_id=? ORDER BY kind,LENGTH(person_id),person_id");
                    rows.text(1,std::to_string(id));
                    while(rows.row()){
                        const auto event=sqlite_str(rows.get(),0);
                        const auto person=parse_u64(sqlite3_column_text(rows.get(),1));
                        const auto guardian=parse_u64(sqlite3_column_text(rows.get(),2));
                        const auto kind=sqlite3_column_int(rows.get(),3);
                        const auto from_rev=sqlite3_column_int64(rows.get(),4);
                        const auto to_rev=sqlite3_column_int64(rows.get(),5);
                        if(from_rev<1 || to_rev<1 ||
                           (previous_from>=0 && (from_rev!=previous_from+1 ||
                            to_rev!=previous_to+1)))
                            throw std::runtime_error("SQLite: family census revisions not contiguous");
                        previous_from=from_rev;previous_to=to_rev;
                        Statement movement(db_,"SELECT npc_id,origin,destination,tick,"
                            "origin_revision,destination_revision FROM npc_family_moves "
                            "WHERE scope=? AND event_id=?");
                        movement.text(1,scope);movement.text(2,event);
                        if(!movement.row() ||
                           parse_u64(sqlite3_column_text(movement.get(),0))!=person ||
                           parse_u64(sqlite3_column_text(movement.get(),1))!=source ||
                           parse_u64(sqlite3_column_text(movement.get(),2))!=target ||
                           sqlite3_column_int64(movement.get(),3)!=at ||
                           sqlite3_column_int64(movement.get(),4)!=from_rev ||
                           sqlite3_column_int64(movement.get(),5)!=to_rev ||
                           movement.row())
                            throw std::runtime_error("SQLite: family member missing ART02 movement");
                        if(kind==0){
                            if(guardian || seen_adults>=adults ||
                               departure->second.relocated_npcs[static_cast<std::size_t>(seen_adults)]!=person)
                                throw std::runtime_error("SQLite: missing or forged adult roster");
                            const auto b=before.npc_records().find(person);
                            const auto a=after.npc_records().find(person);
                            if(b==before.npc_records().end() || a==after.npc_records().end() ||
                               b->second.home_ship!=source || a->second.home_ship!=target ||
                               a->second.location!=target || a->second.transport_id)
                                throw std::runtime_error("SQLite: adult did not actually arrive");
                            if(!adult_roster.empty())adult_roster.push_back(',');
                            adult_roster+=std::to_string(person);
                            ++seen_adults;
                        }else if(kind==1){
                            if(person<(1ULL<<62) || !guardian || seen_children>=children ||
                               departure->second.relocated_dependents[
                                   static_cast<std::size_t>(seen_children)]!=person)
                                throw std::runtime_error("SQLite: missing or forged dependent roster");
                            const auto b=before.physical_dependents().find(person);
                            const auto a=after.physical_dependents().find(person);
                            if(b==before.physical_dependents().end() ||
                               a==after.physical_dependents().end() ||
                               b->second.guardian_id!=guardian ||
                               a->second.guardian_id!=guardian ||
                               b->second.home_ship!=source || b->second.location!=source ||
                               a->second.home_ship!=target || a->second.location!=target ||
                               !std::binary_search(departure->second.relocated_npcs.begin(),
                                                   departure->second.relocated_npcs.end(),guardian))
                                throw std::runtime_error("SQLite: dependent separated from legal guardian");
                            Statement authority(db_,"SELECT home_ship,guardian_id,family_scope "
                                "FROM sg_colony_physical_child_authority WHERE child_id=?");
                            authority.text(1,std::to_string(person));
                            if(!authority.row() ||
                               parse_u64(sqlite3_column_text(authority.get(),0))!=source ||
                               parse_u64(sqlite3_column_text(authority.get(),1))!=guardian ||
                               sqlite_str(authority.get(),2)!=scope || authority.row())
                                throw std::runtime_error("SQLite: moved dependent lacks original custody");
                            if(!child_roster.empty())child_roster.push_back(',');
                            child_roster+=std::to_string(person)+":"+std::to_string(guardian);
                            ++seen_children;
                        }else throw std::runtime_error("SQLite: invalid family member kind");
                        ++members;
                    }
                    if(members!=count || seen_adults!=adults || seen_children!=children ||
                       prepared_adults!=adult_roster || prepared_children!=child_roster)
                        throw std::runtime_error("SQLite: incomplete family, birth or guardian list");
                }
                Statement orphan(db_,"SELECT 1 FROM sg_colony_family_evacuation_member m "
                    "LEFT JOIN sg_colony_family_evacuation e ON e.command_id=m.command_id "
                    "WHERE e.command_id IS NULL LIMIT 1");
                if(orphan.row())throw std::runtime_error("SQLite: orphaned family evacuation member");
                Statement regional_rows(db_,"SELECT command_id FROM sg_regional_command_receipt "
                    "WHERE actor='colony-family-evacuation-host'");
                while(regional_rows.row()){
                    Statement root(db_,"SELECT 1 FROM sg_colony_family_evacuation WHERE command_id=?");
                    root.text(1,sqlite_str(regional_rows.get(),0));
                    if(!root.row() || root.row())
                        throw std::runtime_error("SQLite: regional family movement without world effect");
                }
            }
        }
        // DEMOG-05q6e2: child physical custody is an authoritative sibling
        // receipt to the real world step, not an unverified travel annotation.
        // A v43 child-travel activation marker survives independent deletion
        // of either custody table. This is a local corroboration receipt, not
        // protection against an offline attacker who rewrites the whole DB.
        {
            Statement marker(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                 "AND name='sg_dependent_transport_anchor'");
            if(marker.row()){
                if(!has_dependent_table())
                    throw std::runtime_error("activated child custody table has vanished");
                Statement anchor(db_,"SELECT first_generation FROM "
                                     "sg_dependent_transport_anchor WHERE id=1");
                if(!anchor.row() || sqlite3_column_int64(anchor.get(),0)<2 || anchor.row())
                    throw std::runtime_error("child custody activation marker corrupted");
                Statement count(db_,"SELECT COUNT(*) FROM sg_dependent_travel");
                if(!count.row() || sqlite3_column_int64(count.get(),0)<1)
                    throw std::runtime_error("activated world lost its child custody ledger");
            }
            Statement batch(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                "AND name='sg_dependent_transport_batch'");
            if(batch.row() && !has_dependent_table())
                throw std::runtime_error("child transport batches survive missing traveler ledger");
        }
        if(has_dependent_table()){
            Statement batch_exists(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_dependent_transport_batch'");
            if(!batch_exists.row())
                throw std::runtime_error("dependent transport batch registry missing");
            std::map<std::pair<std::string,std::string>,std::int64_t> actual;
            std::map<EntityId,std::size_t> onboard;
            bool paired_column=false,transfer_column=false;
            {Statement columns(db_,"PRAGMA table_info(sg_dependent_travel)");
             while(columns.row()){
                 if(sqlite_str(columns.get(),1)=="co_guardian_id")paired_column=true;
                 if(sqlite_str(columns.get(),1)=="transfer_from_transport")transfer_column=true;
             }}
            const std::string child_sql=std::string("SELECT family_scope,child_id,transport_id,guardian_id,")+
                "ferry,origin,destination,board_command,arrival_command,board_generation,"+
                "arrival_generation,board_tick,arrival_tick,status,"+
                (paired_column?"co_guardian_id":"'0'")+","+
                (transfer_column?"transfer_from_transport":"'0'")+","+
                (transfer_column?"termination_kind":"0")+
                " FROM sg_dependent_travel";
            Statement rows(db_,child_sql.c_str());
            while(rows.row()){
                const auto fam=sqlite_str(rows.get(),0);
                const auto child=parse_u64(sqlite3_column_text(rows.get(),1));
                const auto voyage=parse_u64(sqlite3_column_text(rows.get(),2));
                const auto guardian=parse_u64(sqlite3_column_text(rows.get(),3));
                const auto ferry=parse_u64(sqlite3_column_text(rows.get(),4));
                const auto src=parse_u64(sqlite3_column_text(rows.get(),5));
                const auto dst=parse_u64(sqlite3_column_text(rows.get(),6));
                const auto board=parse_u64(sqlite3_column_text(rows.get(),7));
                const bool arrived=sqlite3_column_type(rows.get(),8)!=SQLITE_NULL;
                const auto arrival=arrived?parse_u64(sqlite3_column_text(rows.get(),8)):0;
                const auto bg=sqlite3_column_int64(rows.get(),9);
                const auto ag=arrived?sqlite3_column_int64(rows.get(),10):0;
                const auto bt=sqlite3_column_int64(rows.get(),11);
                const auto at=arrived?sqlite3_column_int64(rows.get(),12):0;
                const int status=sqlite3_column_int(rows.get(),13);
                const auto co_guardian=parse_u64(sqlite3_column_text(rows.get(),14));
                const auto transfer_from=parse_u64(sqlite3_column_text(rows.get(),15));
                const auto termination_kind=sqlite3_column_int(rows.get(),16);
                if(termination_kind<0 || termination_kind>1 ||
                   (termination_kind==1 && (!transfer_column || !arrived || transfer_from)) ||
                   (transfer_from && (termination_kind || !transfer_column)))
                    throw std::runtime_error("invalid child emergency-transfer lineage");
                if((co_guardian && co_guardian==guardian) ||
                   (co_guardian && !paired_column))
                    throw std::runtime_error("invalid paired-parent child manifest");
                if(fam.empty() || child<(1ULL<<62) || !guardian || !ferry || !src ||
                   !dst || src==dst || !voyage || !board ||
                   bg<=1 || static_cast<std::uint64_t>(bg)>generation || bt<=0 ||
                   (status!=1 && status!=2) || (status==2)!=arrived ||
                   (arrived && (ag<=bg || static_cast<std::uint64_t>(ag)>generation || at<=bt)) ||
                   restored.npc_records().count(child) || restored.npc_casualties().count(child))
                    throw std::runtime_error("invalid durable child passenger identity/state");
                auto assert_action=[&](std::uint64_t id,Action action,
                                       std::uint64_t expect_tick){
                    auto cmd=journal_commands.find(id); auto ev=event_by_id.find(id);
                    if(cmd==journal_commands.end() || ev==event_by_id.end() ||
                       ev->second.result!=Outcome::Applied ||
                       ev->second.tick!=expect_tick || cmd->second.action!=action ||
                       cmd->second.issuer!=Simulation::kServerAuthority ||
                       cmd->second.actor!=ferry || cmd->second.transport_id!=voyage ||
                       cmd->second.target!=(action==Action::HostEmbarkPassengers?src:dst) ||
                       std::find(cmd->second.passenger_ids.begin(),
                                 cmd->second.passenger_ids.end(),guardian)==
                                 cmd->second.passenger_ids.end() ||
                       (co_guardian && std::find(cmd->second.passenger_ids.begin(),
                           cmd->second.passenger_ids.end(),co_guardian)==
                           cmd->second.passenger_ids.end()))
                        throw std::runtime_error("child ferry command/journal evidence forged");
                };
                if(transfer_from){
                    const auto cmd=journal_commands.find(board);
                    const auto ev=event_by_id.find(board);
                    if(cmd==journal_commands.end() || ev==event_by_id.end() ||
                       (cmd->second.action!=Action::HostEvacuatePassengers &&
                        cmd->second.action!=Action::HostSplitEvacuatePassengers) ||
                       cmd->second.issuer!=Simulation::kServerAuthority ||
                       cmd->second.transport_id!=transfer_from ||
                       !(cmd->second.action==Action::HostSplitEvacuatePassengers ?
                         split_receiver_matches(cmd->second,ferry,voyage,guardian,co_guardian):
                         (cmd->second.target==ferry &&
                          cmd->second.amount==static_cast<std::int64_t>(voyage))) ||
                       ev->second.result!=Outcome::Applied ||
                       ev->second.tick!=static_cast<std::uint64_t>(bt) ||
                       std::find(cmd->second.passenger_ids.begin(),
                                 cmd->second.passenger_ids.end(),guardian)==
                                 cmd->second.passenger_ids.end() ||
                       (co_guardian && std::find(cmd->second.passenger_ids.begin(),
                           cmd->second.passenger_ids.end(),co_guardian)==
                           cmd->second.passenger_ids.end()))
                        throw std::runtime_error("rescued minor lacks actual ferry evacuation");
                    Statement original(db_,"SELECT ferry,origin,destination,status,"
                        "termination_kind,arrival_command FROM sg_dependent_travel "
                        "WHERE family_scope=? AND child_id=? AND transport_id=?");
                    original.text(1,fam);original.text(2,std::to_string(child));
                    original.text(3,std::to_string(transfer_from));
                    if(!original.row() ||
                       parse_u64(sqlite3_column_text(original.get(),0))!=cmd->second.actor ||
                       parse_u64(sqlite3_column_text(original.get(),1))!=src ||
                       parse_u64(sqlite3_column_text(original.get(),2))!=dst ||
                       sqlite3_column_int(original.get(),3)!=2 ||
                       sqlite3_column_int(original.get(),4)!=1 ||
                       parse_u64(sqlite3_column_text(original.get(),5))!=board || original.row())
                        throw std::runtime_error("child rescue lacks terminated original leg");
                }else
                    assert_action(board,Action::HostEmbarkPassengers,static_cast<std::uint64_t>(bt));
                Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                op.i64(1,bg);
                if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                   sqlite3_column_int64(op.get(),1)!=bt || op.row())
                    throw std::runtime_error("child boarding lacks durable world step");
                if(arrived){
                    if(termination_kind==1){
                        const auto cmd=journal_commands.find(arrival);
                        const auto ev=event_by_id.find(arrival);
                        if(cmd==journal_commands.end() || ev==event_by_id.end() ||
                           (cmd->second.action!=Action::HostEvacuatePassengers &&
                            cmd->second.action!=Action::HostSplitEvacuatePassengers) ||
                           cmd->second.actor!=ferry ||
                           cmd->second.transport_id!=voyage ||
                           (cmd->second.action==Action::HostEvacuatePassengers &&
                            cmd->second.amount<=0) ||
                           ev->second.result!=Outcome::Applied ||
                           ev->second.tick!=static_cast<std::uint64_t>(at) ||
                           restored.voyage_status(voyage)!=5)
                            throw std::runtime_error("child evacuation leg forged");
                        // Terminal evacuation is not physical destination arrival.
                        // A separate new rescue-voyage row must prove custody.
                        Statement next_leg(db_,"SELECT 1 FROM sg_dependent_travel "
                            "WHERE family_scope=? AND child_id=? AND transport_id=? "
                            "AND transfer_from_transport=? AND board_command=?");
                        next_leg.text(1,fam);next_leg.text(2,std::to_string(child));
                        const auto split_at=cmd->second.action==Action::HostSplitEvacuatePassengers ?
                            static_cast<std::size_t>(cmd->second.thrust_x) : 0U;
                        const bool second_split=split_at &&
                            std::find(cmd->second.passenger_ids.begin(),
                                cmd->second.passenger_ids.begin()+split_at,guardian)==
                                cmd->second.passenger_ids.begin()+split_at;
                        next_leg.text(3,std::to_string(second_split ?
                            cmd->second.secondary_transport:
                            static_cast<std::uint64_t>(cmd->second.amount)));
                        next_leg.text(4,std::to_string(voyage));
                        next_leg.text(5,std::to_string(arrival));
                        if(!next_leg.row() || next_leg.row())
                            throw std::runtime_error("evacuated child has no receiving ferry leg");
                        if(cmd->second.action==Action::HostSplitEvacuatePassengers){
                            Statement landed(db_,"SELECT ferry,transport_id FROM sg_dependent_travel "
                                "WHERE family_scope=? AND child_id=? AND transfer_from_transport=? "
                                "AND board_command=?");
                            landed.text(1,fam);landed.text(2,std::to_string(child));
                            landed.text(3,std::to_string(voyage));
                            landed.text(4,std::to_string(arrival));
                            if(!landed.row())throw std::runtime_error("split child receiver missing");
                            const auto receiving_ferry=parse_u64(sqlite3_column_text(landed.get(),0));
                            const auto receiving_voyage=parse_u64(sqlite3_column_text(landed.get(),1));
                            if(!split_receiver_matches(cmd->second,receiving_ferry,
                                receiving_voyage,guardian,co_guardian)||landed.row())
                                throw std::runtime_error("split child assigned to wrong receiver");
                        }
                    }else{
                        assert_action(arrival,Action::HostDisembarkPassengers,
                                      static_cast<std::uint64_t>(at));
                        if(restored.voyage_status(voyage)!=2)
                            throw std::runtime_error("child arrived on incomplete voyage");
                    }
                    Statement arrival_step(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                    arrival_step.i64(1,ag);
                    if(!arrival_step.row() || sqlite3_column_int(arrival_step.get(),0)!=2 ||
                       sqlite3_column_int64(arrival_step.get(),1)!=at || arrival_step.row())
                        throw std::runtime_error("child arrival/evacuation lacks committed world step");
                }else{
                    ++onboard[ferry];
                    if(restored.voyage_status(voyage)!=1 &&
                       restored.voyage_status(voyage)!=3)
                        throw std::runtime_error("child aboard inactive ferry");
                    auto adult=restored.npc_records().find(guardian);
                    if(adult==restored.npc_records().end() ||
                       adult->second.location!=ferry || adult->second.transport_id!=voyage)
                        throw std::runtime_error("guardian separated from active dependent");
                }
                Statement birth(db_,"SELECT parent_a,parent_b FROM npc_family_births "
                    "WHERE scope=? AND child_id=?");
                birth.text(1,fam);birth.text(2,std::to_string(child));
                if(!birth.row())throw std::runtime_error("physical child lacks actual birth");
                const auto a=parse_u64(sqlite3_column_text(birth.get(),0));
                const auto b=parse_u64(sqlite3_column_text(birth.get(),1));
                if((a!=guardian && b!=guardian) ||
                   (co_guardian && !((a==guardian && b==co_guardian) ||
                                      (b==guardian && a==co_guardian))) || birth.row())
                    throw std::runtime_error("child travel guardian lineage forged");
                if(co_guardian && !arrived){
                    // Replaying the host command proves both were admitted.
                    // While the pair remains aboard they must also occupy the
                    // current canonical physical voyage. Historical checkpoints
                    // may be pruned, so never require an old snapshot here.
                    const auto co=restored.npc_records().find(co_guardian);
                    if(co==restored.npc_records().end() ||
                       co->second.location!=ferry || co->second.transport_id!=voyage)
                        throw std::runtime_error("second parent absent from live ferry");
                }
                ++actual[{fam,std::to_string(board)}];
                if(arrived && termination_kind==0)++actual[{fam,std::to_string(arrival)}];
            }
            for(const auto& [ferry,count]:onboard)
                if(count+restored.passengers_aboard(ferry)>16)
                    throw std::runtime_error("child/adult physical seats overbooked");
            Statement batches(db_,"SELECT family_scope,command_id,transport_id,phase,"
                "member_count,world_generation FROM sg_dependent_transport_batch");
            std::map<std::pair<std::string,std::string>,std::int64_t> declared;
            while(batches.row()){
                const auto fam=sqlite_str(batches.get(),0),cmd=sqlite_str(batches.get(),1);
                const auto trans=sqlite_str(batches.get(),2);
                const auto phase=sqlite3_column_int(batches.get(),3);
                const auto count=sqlite3_column_int64(batches.get(),4);
                const auto gen=sqlite3_column_int64(batches.get(),5);
                if(fam.empty() || cmd.empty() || trans.empty() || (phase!=1 && phase!=2 && phase!=3) ||
                   count<1 || count>15 || gen<=1 || static_cast<std::uint64_t>(gen)>generation ||
                   !declared.emplace(std::make_pair(fam,cmd),count).second ||
                   actual[{fam,cmd}]!=count)
                    throw std::runtime_error("child batch physical manifest incomplete");
                // One actual world command may carry several scoped family
                // receipts, but none may be retyped into boarding/arrival or
                // rescue without the independently applied command to match.
                const auto command_id=parse_u64(reinterpret_cast<const unsigned char*>(cmd.c_str()));
                const auto voyage_id=parse_u64(reinterpret_cast<const unsigned char*>(trans.c_str()));
                const auto applied=journal_commands.find(command_id);
                const auto event=event_by_id.find(command_id);
                if(applied==journal_commands.end() || event==event_by_id.end() ||
                   event->second.result!=Outcome::Applied ||
                   (phase==1 && (applied->second.action!=Action::HostEmbarkPassengers ||
                                  applied->second.transport_id!=voyage_id)) ||
                   (phase==2 && (applied->second.action!=Action::HostDisembarkPassengers ||
                                  applied->second.transport_id!=voyage_id)) ||
                   (phase==3 && !((applied->second.action==Action::HostEvacuatePassengers &&
                       applied->second.amount>0 &&
                       static_cast<std::uint64_t>(applied->second.amount)==voyage_id) ||
                       (applied->second.action==Action::HostSplitEvacuatePassengers &&
                        (static_cast<std::uint64_t>(applied->second.amount)==voyage_id ||
                         applied->second.secondary_transport==voyage_id)))))
                    throw std::runtime_error("family transport receipt phase or voyage forged");
                Statement op(db_,"SELECT kind,tick FROM operation_log WHERE generation=?");
                op.i64(1,gen);
                if(!op.row() || sqlite3_column_int(op.get(),0)!=2 ||
                   sqlite3_column_int64(op.get(),1)!=
                        static_cast<std::int64_t>(event->second.tick) || op.row())
                    throw std::runtime_error("child family batch lacks applied world-step proof");
            }
            if(actual!=declared)
                throw std::runtime_error("orphaned child berth/arrival records");
        }
        out.swap(restored);
        return true;
    }

    void commit(const Simulation& state, std::uint64_t expected_generation, std::int64_t origin, std::int64_t credit_origin,
                const Command* accepted, const std::vector<Event>& new_events,
                const std::map<std::uint64_t, Command>& due_commands,
                const std::vector<ActivityEvent>& activity_delta, FailPoint fail,
                const NpcMiningAssignment* npc_mining_assignment=nullptr,
                const NpcFabricationAssignment* npc_fabrication_assignment=nullptr,
                const std::function<void(sqlite3*,std::uint64_t)>& society_effect={},
                const NativeCellWorkerTick* cell_worker_token=nullptr,
                const std::vector<Command>& generated_native_commands={}) {
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
            // Enforce reservations again under the SAME SQLite writer lock as
            // the authoritative cargo effects. A command queued before a new
            // manifest was booked cannot bypass the later booking at execution.
            for(const auto& [id,c]:due_commands){(void)id;
                if(!shipping::manifest_transfer_authorized(db_,c) ||
                   !shipping::third_party_transfer_authorized(db_,c))
                    throw std::runtime_error("shipment: unreserved due cargo command");
            }
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
            // An NPC autopilot command is generated only by the trusted
            // original Simulation step. Accept it in that very SAME SQLite
            // transaction; execution remains a later verified world tick.
            constexpr std::uint64_t kNativeFlightTag=std::uint64_t{1}<<61;
            for(const auto& c:generated_native_commands){
                const auto flight_id=(c.id & ~kNativeFlightTag)>>9;
                const auto seq=c.id & 511;
                const auto flight=state.npc_flight_orders().find(flight_id);
                if(!(c.id & kNativeFlightTag) || (c.id & (std::uint64_t{1}<<62)) ||
                   !flight_id || flight_id>=(std::uint64_t{1}<<40) ||
                   !seq || c.id!=(kNativeFlightTag | (flight_id<<9) | seq) ||
                   c.sequence!=c.id || c.execute_tick!=state.tick()+1 ||
                   (c.action!=Action::Thrust &&
                    c.action!=Action::RequestDock &&
                    c.action!=Action::RequestUndock) ||
                   flight==state.npc_flight_orders().end() ||
                   flight->second.last_local_command!=c.id ||
                   flight->second.local_commands!=seq ||
                   flight->second.ship!=c.actor ||
                   flight->second.owner!=c.issuer ||
                   (flight->second.mode!=NpcFlightMode::LocalDock &&
                    flight->second.mode!=NpcFlightMode::LocalRendezvous))
                    throw std::runtime_error("unauthorized native NPC command receipt");
                Statement receipt(db_,"INSERT INTO receipts(id,accepted_generation) VALUES(?,?)");
                receipt.text(1,std::to_string(c.id));
                receipt.i64(2,static_cast<std::int64_t>(next));
                receipt.execute();
                if(sqlite3_changes(db_)!=1)
                    throw std::runtime_error("native NPC command acceptance not unique");
                const auto signed_body=std::to_string(c.id)+"|"+
                    std::to_string(next)+"|"+command_record(c)+"|"+
                    std::to_string(flight->second.pilot_npc)+"|"+
                    std::to_string(state.state_hash());
                Statement provenance(db_,"INSERT INTO sg_native_npc_commands("
                    "id,generation,command,pilot,world_hash,digest) VALUES(?,?,?,?,?,?)");
                provenance.text(1,std::to_string(c.id));
                provenance.i64(2,static_cast<std::int64_t>(next));
                provenance.text(3,command_record(c));
                provenance.text(4,std::to_string(flight->second.pilot_npc));
                provenance.text(5,std::to_string(state.state_hash()));
                provenance.text(6,social_digest("original-npc-flight-v1",signed_body));
                provenance.execute();
                if(sqlite3_changes(db_)!=1)
                    throw std::runtime_error("native NPC provenance not unique");
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
                // Probe scans count observed contacts in the command receipt;
                // observation counts are not physical inventory transfers.
                if (ev.result == Outcome::Applied && ev.transferred > 0 &&
                    due->second.action != Action::ScanSurveyProbe) {
                    const auto cmd = due_commands.find(ev.command_id);
                    if (cmd == due_commands.end() ||
                        (cmd->second.action != Action::DeploySurveyProbe &&
                         cmd->second.action != Action::LaunchGeologicalDrone &&
                         cmd->second.action != Action::RecoverSurveyProbe &&
                         cmd->second.action != Action::Mine && cmd->second.action != Action::UnloadOre &&
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
                         cmd->second.action!=Action::TrainWorkshopTrade &&
                         cmd->second.action!=Action::MentorWorkshopTrade &&
                         cmd->second.action!=Action::TransferWorkshopFeed &&
                         cmd->second.action!=Action::TransferWorkshopOutput &&
                         cmd->second.action!=Action::ConstructWorkshopFinisher &&
                         cmd->second.action!=Action::TransferWorkshopFinalOutput &&
                         cmd->second.action!=Action::TransferWorkshopStack &&
                         cmd->second.action!=Action::ConstructStorage &&
                         cmd->second.action!=Action::HostEscrowConstructedStock &&
                         cmd->second.action!=Action::HostReturnConstructedStock &&
                         cmd->second.action!=Action::HostTransferConsignedStock &&
                         cmd->second.action!=Action::TransferStorageStack &&
                         cmd->second.action!=Action::ConstructStorageNode &&
                         cmd->second.action!=Action::TransferStorageNodeStack &&
                         cmd->second.action!=Action::ConstructStorageLink &&
                         cmd->second.action!=Action::ConstructWorkshopLink &&
                         cmd->second.action!=Action::PlaceConveyorCell))
                        throw std::runtime_error("unmatched resource transfer event");
                    Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,command_id,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?,?)");
                    ledger.i64(1, static_cast<std::int64_t>(next));
                    ledger.i64(2, static_cast<std::int64_t>(ev.tick));
                    ledger.text(3, std::to_string(ev.command_id));
                    if (cmd->second.action == Action::DeploySurveyProbe ||
                        cmd->second.action == Action::LaunchGeologicalDrone ||
                        cmd->second.action == Action::RecoverSurveyProbe) {
                        if(ev.transferred!=1 ||
                           (cmd->second.action != Action::RecoverSurveyProbe && cmd->second.target))
                            throw std::runtime_error("invalid probe-device custody transfer");
                        const bool recovered=cmd->second.action==Action::RecoverSurveyProbe;
                        const auto identity=recovered?cmd->second.target:cmd->second.id;
                        if(!identity)throw std::runtime_error("missing durable probe identity");
                        const auto ship="ship:"+std::to_string(cmd->second.actor)+":survey_probe_devices";
                        const auto probe="probe:"+std::to_string(identity)+":device";
                        ledger.text(4,recovered?probe:ship);
                        ledger.text(5,recovered?ship:probe);
                        ledger.i64(6,1);
                        ledger.text(7,recovered?"probe_device_recovered":
                            cmd->second.action==Action::LaunchGeologicalDrone?
                            "geological_drone_deployed":"survey_probe_deployed");
                    } else if (cmd->second.action == Action::Mine) {
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
                    } else if(cmd->second.action==Action::HostEscrowConstructedStock){
                        const auto& c=cmd->second;
                        if(c.issuer!=Simulation::kServerAuthority||c.thrust_x||c.thrust_y||
                           c.amount<=0||ev.transferred!=c.amount)
                            throw std::runtime_error("constructed world escrow transfer mismatch");
                        ledger.text(4,"storage:"+std::to_string(c.target)+":aluminum");
                        ledger.text(5,"network-escrow:"+std::to_string(c.target));
                        ledger.i64(6,ev.transferred);ledger.text(7,"constructed_network_stock_kg");
                    } else if(cmd->second.action==Action::HostReturnConstructedStock){
                        const auto& c=cmd->second;
                        if(c.issuer!=Simulation::kServerAuthority||
                           (c.thrust_x!=1&&c.thrust_x!=2)||c.thrust_y||
                           c.amount<=0||ev.transferred!=c.amount)
                            throw std::runtime_error("constructed finished-goods release mismatch");
                        ledger.text(4,"network-escrow:"+std::to_string(c.target));
                        ledger.text(5,"storage:"+std::to_string(c.target)+":"+
                            (c.thrust_x==1?"aluminum_panels":"aluminum_frames"));
                        ledger.i64(6,ev.transferred);ledger.text(7,"constructed_network_return_kg");
                    } else if(cmd->second.action==Action::ConstructStorage ||
                              cmd->second.action==Action::TransferStorageStack ||
                              cmd->second.action==Action::HostTransferConsignedStock){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructStorage;
                        if(ev.transferred!=(build?2:c.amount))
                            throw std::runtime_error("storage transfer amount mismatch");
                        static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                        const auto suffix=build?std::string{}:":"+std::string(names[c.thrust_x]);
                        const auto src=build?"modules:"+std::to_string(c.actor):
                            (c.thrust_y==0?"goods:"+std::to_string(c.actor):
                             c.thrust_y==1?"storage:"+std::to_string(c.target):
                             c.thrust_y==2?"workshop:"+std::to_string(c.target):
                                           "storage:"+std::to_string(c.target))+suffix;
                        const auto dst=build?"storage:"+std::to_string(c.target):
                            (c.thrust_y==0?"storage:"+std::to_string(c.target):
                             c.thrust_y==1?"goods:"+std::to_string(c.actor):
                             c.thrust_y==2?"storage:"+std::to_string(c.target):
                                           "workshop:"+std::to_string(c.target))+suffix;
                        ledger.text(4,src);ledger.text(5,dst);ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"storage_construction_t":"storage_stack_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructStorageNode ||
                              cmd->second.action==Action::TransferStorageNodeStack){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructStorageNode;
                        const auto node=build?c.amount:c.thrust_x/4;
                        const auto item=build?0:c.thrust_x%4;
                        if(node<1 || node>std::numeric_limits<int>::max()/4 || (!build && (item<0 || item>3 || c.thrust_y<0 || c.thrust_y>3)) ||
                           ev.transferred!=(build?2:c.amount))
                            throw std::runtime_error("invalid extra storage transfer event");
                        static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                        const auto endpoint="storage:"+std::to_string(c.target)+":"+std::to_string(node);
                        const auto suffix=build?std::string{}:":"+std::string(names[item]);
                        const auto src=build?"modules:"+std::to_string(c.actor):
                            (c.thrust_y==0?"goods:"+std::to_string(c.actor):
                             c.thrust_y==1?endpoint:
                             c.thrust_y==2?"workshop:"+std::to_string(c.target):endpoint)+suffix;
                        const auto dst=build?endpoint:
                            (c.thrust_y==0?endpoint:
                             c.thrust_y==1?"goods:"+std::to_string(c.actor):
                             c.thrust_y==2?endpoint:"workshop:"+std::to_string(c.target))+suffix;
                        ledger.text(4,src);ledger.text(5,dst);ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"storage_node_construction_t":"storage_node_stack_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructStorageLink || cmd->second.action==Action::ConstructWorkshopLink){
                        const auto& c=cmd->second;
                        if(ev.transferred!=2)throw std::runtime_error("invalid link construction mass");
                        ledger.text(4,"modules:"+std::to_string(c.actor));
                        ledger.text(5,"storage-link:"+std::to_string(c.target)+":"+std::to_string(c.id));
                        ledger.i64(6,2);ledger.text(7,"storage_link_construction_t");
                    } else if(cmd->second.action==Action::PlaceConveyorCell){
                        const auto& c=cmd->second;
                        if(ev.transferred!=2)throw std::runtime_error("invalid conveyor cell mass");
                        ledger.text(4,"modules:"+std::to_string(c.actor));
                        ledger.text(5,"conveyor:"+std::to_string(c.target)+":"+
                            std::to_string(c.amount)+":"+
                            std::to_string(c.thrust_x)+":"+
                            std::to_string(c.thrust_y));
                        ledger.i64(6,2);ledger.text(7,"conveyor_cell_construction_t");
                    } else if(cmd->second.action==Action::TransferWorkshopStack){
                        const auto& c=cmd->second;
                        if(c.thrust_x<0 || c.thrust_x>3 || ev.transferred!=std::abs(c.amount))
                            throw std::runtime_error("item-stack ledger amount mismatch");
                        const std::string kind[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                        ledger.text(4,(c.amount>0?"goods:":"workshop:")+
                            std::to_string(c.amount>0?c.actor:c.target)+":"+kind[c.thrust_x]);
                        ledger.text(5,(c.amount>0?"workshop:":"goods:")+
                            std::to_string(c.amount>0?c.target:c.actor)+":"+kind[c.thrust_x]);
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,"workshop_stack_transfer_kg");
                    } else if(cmd->second.action==Action::TrainWorkshopTrade ||
                              cmd->second.action==Action::MentorWorkshopTrade){
                        const auto& c=cmd->second;
                        if(ev.transferred!=1 || c.amount<1 || c.amount>1000000 ||
                           (c.thrust_x!=1 && c.thrust_x!=2) ||
                           c.issuer==Simulation::kServerAuthority)
                            throw std::runtime_error("invalid industrial training practice material");
                        ledger.text(4,"workshop:"+std::to_string(c.target)+
                            (c.thrust_x==1?":aluminum":":aluminum_panels"));
                        ledger.text(5,"workshop:"+std::to_string(c.target)+":aluminum_scrap");
                        ledger.i64(6,1);
                        ledger.text(7,"industrial_training_practice_kg");
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
                if(a.kind==ActivityKind::AutoStorageTransfer){
                    const auto* route=state.storage_link(a.actor,a.target);
                    if(!route || !state.has_command_id(route->id) ||
                       route->id!=a.start_command_id || a.tick!=state.tick() ||
                       a.transferred<1 || a.transferred>route->units_per_tick ||
                       a.result!=Outcome::Applied)
                        throw std::runtime_error("invalid committed auto-storage movement");
                    static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                    Statement transfer(db_, "INSERT INTO resource_ledger(generation,tick,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?)");
                    transfer.i64(1,static_cast<std::int64_t>(next));
                    transfer.i64(2,static_cast<std::int64_t>(a.tick));
                    transfer.text(3,storage_endpoint_label(a.actor,route->source,names[route->item_index]));
                    transfer.text(4,storage_endpoint_label(a.actor,route->destination,names[route->item_index]));
                    transfer.i64(5,a.transferred);transfer.text(6,"automated_storage_link_transfer");transfer.execute();
                    continue;
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
            // SPACE-01: update derived 3D computational cell custody inside
            // the SAME SQLite world snapshot/journal transaction. Ship ID,
            // finite NPC roster, carrier cargo and physical position remain
            // solely in original Simulation. A fault rolls both back.
            spatial::record_native_world_commit(
                db_,state,expected_generation,next);
            // SPACE-05: one authoritative admission point for EVERY native
            // world-changing PersistentWorld API. Previously only step()
            // checked leases, so submit/host operations could bypass them.
            // This executes after the original spatial-index update but
            // before COMMIT, inside the same BEGIN IMMEDIATE lock.
            Statement lease_table(db_,"SELECT COUNT(*) FROM sqlite_master "
               "WHERE type='table' AND name='sg_spatial_worker_lease'");
            if(!lease_table.row())
                throw std::runtime_error("spatial worker registry unreadable");
            if(sqlite3_column_int64(lease_table.get(),0)==0){
                if(cell_worker_token)
                    throw std::runtime_error("no worker registry for leased commit");
            }else{
                Statement lease_count(db_,"SELECT COUNT(*) FROM sg_spatial_worker_lease");
                if(!lease_count.row())
                    throw std::runtime_error("spatial lease count unreadable");
                const auto count=sqlite3_column_int64(lease_count.get(),0);
                if(count==0){
                    if(cell_worker_token)
                        throw std::runtime_error("no active lease for worker commit");
                }else{
                    // Multiple physically occupied cells may have active
                    // workers, but exactly ONE host-selected dispatch holds
                    // the journal writer for this world generation.
                    if(!cell_worker_token || count<1 ||
                       (count>1 && cell_worker_token->dispatch_id.empty()))
                        throw std::runtime_error(
                            "multi-cell writer requires a durable fenced dispatch");
                    const auto& token=*cell_worker_token;
                    if(!spatial::parse_scope(token.canonical_cell_scope) ||
                       token.trusted_worker.empty() ||
                       token.trusted_worker.size()>128 ||
                       !token.worker_epoch || !token.region_generation ||
                       !token.checkpoint_revision)
                        throw std::runtime_error("invalid worker commit token");
                    Statement lease(db_,"SELECT a.worker,a.epoch,a.region_generation,"
                      "a.checkpoint_revision,r.generation,r.checkpoint_revision,r.phase "
                      "FROM sg_spatial_worker_lease a "
                      "JOIN sg_regional_mode_authority r ON r.scope=a.scope "
                      "WHERE a.scope=?");
                    lease.text(1,token.canonical_cell_scope);
                    if(!lease.row())
                        throw std::runtime_error("worker lacks regional authority");
                    const auto worker=sqlite3_column_text(lease.get(),0);
                    if(!worker ||
                       reinterpret_cast<const char*>(worker)!=token.trusted_worker ||
                       sqlite3_column_int64(lease.get(),1)!=
                           static_cast<sqlite3_int64>(token.worker_epoch) ||
                       sqlite3_column_int64(lease.get(),2)!=
                           static_cast<sqlite3_int64>(token.region_generation) ||
                       sqlite3_column_int64(lease.get(),3)!=
                           static_cast<sqlite3_int64>(token.checkpoint_revision) ||
                       sqlite3_column_int64(lease.get(),4)!=
                           static_cast<sqlite3_int64>(token.region_generation) ||
                       sqlite3_column_int64(lease.get(),5)!=
                           static_cast<sqlite3_int64>(token.checkpoint_revision) ||
                       sqlite3_column_int(lease.get(),6)!=0 ||
                       !spatial::verify_native_index(db_,state,next))
                        throw std::runtime_error(
                            "stale, forged or conflicting worker world commit");
                    if(count>1 || !token.dispatch_id.empty()){
                        if(token.dispatch_id.empty() ||
                           token.dispatch_id.size()>128)
                            throw std::runtime_error("missing global dispatch identity");
                        Statement selected(db_,"SELECT scope,worker,epoch,"
                            "region_generation,checkpoint_revision,status "
                            "FROM sg_spatial_tick_dispatch "
                            "WHERE world_generation=? AND claim_id=?");
                        selected.i64(1,static_cast<std::int64_t>(
                            expected_generation));
                        selected.text(2,token.dispatch_id);
                        if(!selected.row() ||
                           sqlite_str(selected.get(),0)!=token.canonical_cell_scope ||
                           sqlite_str(selected.get(),1)!=token.trusted_worker ||
                           sqlite3_column_int64(selected.get(),2)!=
                               static_cast<sqlite3_int64>(token.worker_epoch) ||
                           sqlite3_column_int64(selected.get(),3)!=
                               static_cast<sqlite3_int64>(token.region_generation) ||
                           sqlite3_column_int64(selected.get(),4)!=
                               static_cast<sqlite3_int64>(token.checkpoint_revision) ||
                           sqlite3_column_int(selected.get(),5)!=0)
                            throw std::runtime_error(
                                "stale, unselected or already consumed global writer");
                        Statement complete(db_,"UPDATE sg_spatial_tick_dispatch "
                            "SET status=1,completed_generation=? "
                            "WHERE world_generation=? AND claim_id=? AND status=0");
                        complete.i64(1,static_cast<std::int64_t>(next));
                        complete.i64(2,static_cast<std::int64_t>(
                            expected_generation));
                        complete.text(3,token.dispatch_id);
                        complete.execute();
                        if(sqlite3_changes(db_)!=1)
                            throw std::runtime_error("global dispatch completion race");
                    }
                }
            }
            if(society_effect)society_effect(db_,next);
            // AUTO-04D.2: same exclusive world transaction; no second writer.
            colony_safety::journal::record_world_commit(db_,state,expected_generation,next);
            colony_safety::notice::publish(db_,state,next);
            enforce_physical_guardianship_at_commit(db_,next);
            // Retain genesis (generation 1) plus three latest immutable checkpoints.
            // Genesis permits independent replay of the complete operation journal; never silently recover
            // from a corrupt latest snapshot because its effects/receipts may be newer.
            // Incident case receipts require permanent provenance for their
            // observed world generation. Pin only those exact historical
            // snapshots; unrelated checkpoints retain the 3-snapshot policy.
            Statement has_cases(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                    "AND name='sg_dependent_incident_case'");
            const bool incident_cases_exist=has_cases.row();
            Statement has_rescues(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                                      "AND name='sg_dependent_rescue_outcome'");
            const bool outcomes_exist=has_rescues.row();
            Statement has_guardian_approvals(db_,"SELECT 1 FROM sqlite_master "
                                              "WHERE type='table' AND name='sg_guardian_approval'");
            const bool guardian_approvals_exist=has_guardian_approvals.row();
            Statement has_guardian_transfers(db_,"SELECT 1 FROM sqlite_master "
                                              "WHERE type='table' AND name='sg_guardian_transfer'");
            const bool guardian_transfers_exist=has_guardian_transfers.row();
            Statement has_physical(db_,"SELECT 1 FROM sqlite_master "
                                   "WHERE type='table' AND name='sg_dependent_physical_handoff'");
            const bool physical_exists=has_physical.row();
            Statement has_split(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_split_rescue_intent'");
            const bool split_exists=has_split.row();
            Statement has_split_execution(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_split_rescue_execution'");
            const bool split_execution_exists=has_split_execution.row();
            Statement family_evacuation(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_family_evacuation'");
            const bool has_family_evacuation=family_evacuation.row();
            Statement npc_dispatch(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_npc_autonomous_dispatch'");
            const bool has_npc_dispatch=npc_dispatch.row();
            Statement npc_supply(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_npc_emergency_supply'");
            const bool has_npc_supply=npc_supply.row();
            Statement contact_events(db_,"SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='sg_colony_watch_contacts'");
            const bool has_colony_contacts=contact_events.row();
            Statement conflict_agendas(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_conflict_agenda'");
            Statement conflict_intents(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_conflict_intent'");
            const bool has_conflict_agendas=conflict_agendas.row();
            const bool has_conflict_intents=conflict_intents.row();
            Statement colony_relations(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_relationship_revisions'");
            const bool has_colony_relations=colony_relations.row();
            Statement dated_intents(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_diplomatic_intent'");
            const bool has_dated_intents=dated_intents.row();
            Statement governance_epochs(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_governance_epochs'");
            const bool has_governance_epochs=governance_epochs.row();
            Statement npc_exit_pressure(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_npc_exit_pressure'");
            const bool has_npc_exit_pressure=npc_exit_pressure.row();
            Statement personal_outlooks(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_npc_personal_outlook'");
            const bool has_personal_outlooks=personal_outlooks.row();
            Statement immigration_admissions(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_voluntary_immigration'");
            const bool has_immigration_admissions=immigration_admissions.row();
            Statement immigration_settled(db_,"SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='sg_colony_voluntary_immigration_settled'");
            const bool has_immigration_settled=immigration_settled.row();
            const std::string prune_sql=std::string(
                "DELETE FROM snapshots WHERE generation > 1 AND generation < ?")+
                (incident_cases_exist ? " AND generation NOT IN "
                    "(SELECT observed_generation FROM sg_dependent_incident_case)" : "")+
                (outcomes_exist ? " AND generation NOT IN "
                    "(SELECT rescue_generation FROM sg_dependent_rescue_outcome)" : "")+
                (guardian_approvals_exist ? " AND generation NOT IN "
                    "(SELECT approved_generation FROM sg_guardian_approval)" : "")+
                (guardian_transfers_exist ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_guardian_transfer)" : "")+
                (physical_exists ? " AND generation NOT IN "
                    "(SELECT generation FROM sg_dependent_physical_handoff)" : "")+
                (split_exists ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_split_rescue_intent)" : "")+
                (split_execution_exists ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_split_rescue_execution)" : "")+
                (has_family_evacuation ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_family_evacuation)"
                    " AND generation NOT IN "
                    "(SELECT world_generation-1 FROM sg_colony_family_evacuation)" : "")+
                // The v78 NPC admission verifier independently reads this
                // exact original accepted world snapshot at every restart.
                (has_npc_dispatch ? " AND generation NOT IN "
                    "(SELECT admission_generation FROM sg_colony_npc_autonomous_dispatch)"
                    : "")+
                // Native delivery may occur many generations after admission.
                // Retain the two exact proof snapshots needed for replay.
                (has_npc_supply ? " AND generation NOT IN "
                    "(SELECT generation FROM sg_colony_npc_emergency_supply)"
                    " AND generation NOT IN "
                    "(SELECT e.generation FROM historical_events e "
                    "JOIN sg_colony_npc_emergency_supply s ON s.command_id=e.command_id)"
                    : "")+
                (has_colony_contacts ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_watch_contacts)"
                    : "")+
                (has_conflict_agendas ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_conflict_agenda)"
                    : "")+
                (has_conflict_intents ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_conflict_intent)"
                    : "")+
                (has_dated_intents ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_diplomatic_intent)"
                    : "")+
                (has_governance_epochs ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_governance_epochs)"
                    : "")+
                (has_npc_exit_pressure ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_npc_exit_pressure)"
                    : "")+
                (has_personal_outlooks ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_npc_personal_outlook)"
                    : "")+
                (has_immigration_admissions ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_voluntary_immigration)"
                    : "")+
                (has_immigration_settled ? " AND generation NOT IN "
                    "(SELECT world_generation FROM sg_colony_voluntary_immigration_settled)"
                    : "")+
                // Bind subjective diplomacy to BOTH the original physical
                // incident and the later real NPC belief/decision generation.
                (has_colony_relations ? " AND generation NOT IN "
                    "(SELECT generation FROM sg_colony_relationship_revisions)"
                    " AND generation NOT IN "
                    "(SELECT incident_generation FROM sg_colony_relationship_revisions)"
                    : "");
            Statement prune(db_,prune_sql.c_str());
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
                        if (!raw) throw std::runtime_error("recovery: missing submission command");
                        const auto command = decode_command(reinterpret_cast<const char*>(raw));
                        if (!sim.submit(command))
                            throw std::runtime_error("recovery: command replay refused");
                        // v52's atomic host warehouse escrow advances the real world
                        // in the same SQLite commit as the command submission.
                        if (command.action == Action::HostEscrowConstructedStock ||
                            command.action == Action::HostReturnConstructedStock) sim.step();
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

// Independently verify supplementary incident-case receipts against historical
// world snapshots and passenger ledgers during open and recovery.
void verify_dependent_stranding_cases(const std::string& path, std::uint64_t max_generation);

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
        // Do not certify a replay whose sidecar incident evidence was forged.
        Simulation checked(1);std::uint64_t gen=0;std::int64_t ore=0,credit=0;
        if(!copy.restore(checked,gen,ore,credit))
            throw std::runtime_error("recovery: missing world checkpoint");
        copy.verify_physical_dependent_authority(checked);
        copy.verify_autonomous_dispatch(gen);
        copy.verify_supervisor_plans(gen);
        copy.verify_npc_habitat_supplies(gen);
        copy.verify_colony_watch_contacts(gen);
        copy.verify_colony_conflicts(gen);
        copy.verify_colony_governance(gen);
        copy.verify_colony_exit_pressure(gen);
        copy.verify_personal_outlooks(gen);
        copy.verify_colony_voluntary_immigration(gen);
        copy.verify_colony_relationships(gen);
        copy.verify_colony_diplomatic_intents(gen);
        verify_dependent_stranding_cases(destination,gen);
        verify_split_rescue_intents(destination,gen);
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
    : store_(std::make_unique<Store>(path)), db_path_(path), world_(std::move(initial_world)) {
    if (!store_->restore(world_, generation_, resource_origin_, credit_origin_)) {
        if (world_.tick() != 0 || !world_.events().empty() || world_.accepted_command_count() != 0)
            throw std::invalid_argument("initial world must be at tick zero without commands");
        // Some original genesis worlds receive scanner/probe equipment
        // (and hence snapshot v64+) before named crew enrollment. Convert
        // only the already-existing finite crew at first SQLite genesis.
        if(!world_.npc_next_seen_ && world_.npcs_.empty() &&
           !world_.colony_ships_.empty())
            world_.enable_individual_workforce();
        resource_origin_ = world_.total_ore();
        credit_origin_ = world_.total_credits();
        store_->commit(world_, 0, resource_origin_, credit_origin_, nullptr, {}, {}, {}, FailPoint::None);
        generation_ = 1;
    }
    verify_dependent_stranding_cases(db_path_,generation_);
    verify_split_rescue_intents(db_path_,generation_);
    verify_split_executions(db_path_,generation_);
    shipping::verify_manifest_registry(db_path_);
    shipping::verify_third_party_registry(db_path_);
    store_->verify_colony_safety_evidence();
    store_->verify_colony_response(world_,generation_);
    store_->verify_colony_notifications(world_,generation_);
    store_->verify_physical_dependent_authority(world_);
    store_->verify_autonomous_dispatch(generation_);
    store_->verify_supervisor_plans(generation_);
    store_->verify_npc_habitat_supplies(generation_);
    store_->verify_colony_watch_contacts(generation_);
    store_->verify_colony_conflicts(generation_);
    store_->verify_colony_governance(generation_);
    store_->verify_colony_exit_pressure(generation_);
    store_->verify_personal_outlooks(generation_);
    store_->verify_colony_voluntary_immigration(generation_);
    store_->verify_colony_relationships(generation_);
    store_->verify_colony_diplomatic_intents(generation_);
}
PersistentWorld::~PersistentWorld() = default;
// Shared native transition: manual host travel and all NPC-operated
// navigation now use identical Simulation physical guards and effects.
bool PersistentWorld::begin_celestial_transit(
    PlayerId owner,EntityId ship,std::uint64_t body,std::uint64_t id,FailPoint fail) {
    // Do not commandeer an active, owner-authorized NPC navigation ship or
    // adopt its durable planned receipt as a different host command.
    for(const auto& [flight_id,order]:world_.npc_flight_orders_){
        (void)flight_id;
        if((order.phase==NpcFlightPhase::Planned ||
            order.phase==NpcFlightPhase::InTransit) &&
           (order.ship==ship || order.transit_request==id))return false;
    }
    auto next=world_;
    if(!next.begin_native_celestial_transit(owner,ship,body,id))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::complete_celestial_transit(std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.complete_native_celestial_transit(id))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::assign_planetary_shuttle_visitor_duty(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool assign,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.assign_planetary_shuttle_visitor_duty(owner,ship,key,npc,assign))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("visitor duty assignment changed conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::transfer_planetary_shuttle_visitor(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool reboard,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    // ART02 secondary parentage is not present on PhysicalDependent.
    if(reboard && store_->has_art02_child_parent(npc))return false;
    auto next=world_;
    if(!next.transfer_planetary_shuttle_visitor(owner,ship,key,npc,reboard))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary visitor transfer changed conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::book_planetary_shuttle_family(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,std::uint64_t guardian,
        std::uint64_t co_parent,const std::vector<std::uint64_t>& children,
        std::uint64_t request_id,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    if(!store_->valid_planetary_family_births(
        world_,ship,guardian,co_parent,children))return false;
    auto next=world_;
    if(!next.book_planetary_shuttle_family(
        Simulation::kServerAuthority,owner,ship,key,guardian,co_parent,
        children,request_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("family booking fabricated physical matter");
    const auto verify=[&](sqlite3*,std::uint64_t) {
        if(!store_->valid_planetary_family_births(
            world_,ship,guardian,co_parent,children))
            throw std::runtime_error("ART02 birth changed during family booking");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail,nullptr,nullptr,verify);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::release_planetary_shuttle_family(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,FailPoint fail) {
    auto next=world_;
    if(!next.release_planetary_shuttle_family(
        Simulation::kServerAuthority,owner,ship,key))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("family return clearance altered matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::book_planetary_shuttle_passenger(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool board,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    if(board && store_->has_art02_child_parent(npc))return false;
    auto next=world_;
    if(!next.book_planetary_shuttle_passenger(owner,ship,key,npc,board))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle passenger booking changed conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::launch_planetary_shuttle(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t request_id,bool ascend,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    // Recheck actual ART02 parentage at takeoff, not only booking time.
    // A birth recorded after an earlier booking must not strand a child.
    if(const auto it=world_.planetary_shuttles().find(key);
       it!=world_.planetary_shuttles().end()) {
        if(store_->has_art02_child_parent(it->second.pilot_npc))
            return false;
        const auto& family=it->second.family;
        if(family.stage && !store_->valid_planetary_family_births(
            world_,ship,family.guardian_id,family.co_parent_id,
            family.child_ids,(family.stage>=5 &&
                world_.planetary_sites().count(key))?
                world_.planetary_sites().at(key).surface_location:ship))
            return false;
        for(const auto adult:it->second.passenger_ids)
            if(store_->has_art02_child_parent(adult) &&
               (!family.stage ||
                (adult!=family.guardian_id &&
                 adult!=family.co_parent_id)))return false;
    }
    auto next=world_;
    if(!next.launch_planetary_shuttle(owner,ship,key,request_id,ascend))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle takeoff altered conserved resources");
    // Retry the ART02 query INSIDE the original SQLite world commit lock:
    // no new child may appear between preflight and actual takeoff.
    const auto source=world_.planetary_shuttles().find(key);
    const auto verify=[&](sqlite3*,std::uint64_t) {
        if(source==world_.planetary_shuttles().end())return;
        const auto& original=source->second;
        if(store_->has_art02_child_parent(original.pilot_npc))
            throw std::runtime_error("pilot acquired an unmanifested child");
        const auto& family=original.family;
        if(family.stage && !store_->valid_planetary_family_births(
            world_,ship,family.guardian_id,family.co_parent_id,
            family.child_ids,(family.stage>=5 &&
                world_.planetary_sites().count(key))?
                world_.planetary_sites().at(key).surface_location:ship))
            throw std::runtime_error("family changed during shuttle takeoff");
        for(const auto adult:original.passenger_ids)
            if(store_->has_art02_child_parent(adult) &&
               (!family.stage ||
                (adult!=family.guardian_id &&
                 adult!=family.co_parent_id)))
                throw std::runtime_error("unmanifested adult parent at takeoff");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail,nullptr,nullptr,verify);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::assign_shipyard_builders(PlayerId owner,std::uint64_t id,
        std::array<std::uint64_t,2> replacement,FailPoint fail) {
    auto next=world_;
    if(!next.assign_shipyard_builders(owner,id,replacement))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard worker reassignment changed matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::queue_shipyard_freighter(
        PlayerId owner,std::uint64_t yard_id,std::uint64_t build_id,
        FailPoint fail) {
    auto next=world_;
    if(!next.queue_shipyard_freighter(owner,yard_id,build_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("freighter build material escrow altered total matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::commission_shipyard_freighter(
        PlayerId owner,std::uint64_t build_id,
        std::array<std::uint64_t,2> crew,std::int64_t provisions_kg_each,
        FailPoint fail) {
    auto next=world_;
    if(!next.commission_shipyard_freighter(owner,build_id,crew,provisions_kg_each))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("commissioning changed conserved world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
// SHIP-01F all four contract transitions are copied-state SQLite commits;
// failure injection never publishes an unfunded/fabricated vessel or moves
// one seller module without its matched buyer debit.
// WORK-01A: durable intent and revocation use the same SQLite publication
// boundary as native material actions. The coordinator runs only within
// PersistentWorld::step and calls existing native effects on that copy.
bool PersistentWorld::delegate_customer_ship_order(
        PlayerId owner,EntityId ship,std::uint64_t id,std::uint64_t contract,
        std::array<std::uint64_t,2> crew,std::int64_t provisions,
        std::uint64_t dependency,FailPoint fail) {
    auto next=world_;
    if(!next.delegate_customer_ship_order(owner,ship,id,contract,crew,
                                          provisions,dependency))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("delegating NPC work altered conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
// WORK-01C: retain v94 original-world transaction, failpoints, conservation
// and recovery. The delegation is not a new shipment/stock/manifest writer.
// FLIGHT-01 is mission-neutral and journaled exactly like all other
// original world actions: no parallel flight database or unsupervised thread.
bool PersistentWorld::queue_npc_flight(
    PlayerId owner,EntityId ship,std::uint64_t pilot,
    std::uint64_t destination,std::uint64_t order_id,
    std::uint64_t transit_request,NpcFlightPurpose purpose,FailPoint fail){
    auto next=world_;
    if(!next.queue_npc_flight(owner,ship,pilot,destination,order_id,
                              transit_request,purpose))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("navigation authorization created material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::queue_npc_station_docking(
    PlayerId owner,EntityId ship,std::uint64_t pilot,
    EntityId station,std::uint64_t order_id,
    NpcFlightPurpose purpose,FailPoint fail){
    auto next=world_;
    if(!next.queue_npc_station_docking(
        owner,ship,pilot,station,order_id,purpose))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("local navigation created material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::register_player_person(PlayerId owner,EntityId aboard,
    FailPoint fail){
    auto next=world_;
    if(!next.register_player_person(owner,aboard))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("player registration created material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::grant_player_rescue_consent(
    PlayerId owner,EntityId receiver,std::uint64_t consent_id,FailPoint fail){
    auto next=world_;
    if(!next.grant_player_rescue_consent(owner,receiver,consent_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("player consent changed world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::revoke_player_rescue_consent(
    PlayerId owner,std::uint64_t consent_id,FailPoint fail){
    auto next=world_;
    if(!next.revoke_player_rescue_consent(owner,consent_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("player rescue revocation changed world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::complete_player_rescue(
    PlayerId owner,EntityId receiver,std::uint64_t flight_id,
    std::uint64_t receipt,FailPoint fail){
    auto next=world_;
    if(!next.complete_player_rescue(owner,receiver,flight_id,receipt))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("player rescue changed world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::queue_npc_ship_rendezvous(
    PlayerId owner,EntityId ship,std::uint64_t pilot,
    EntityId target_ship,std::uint64_t order_id,
    NpcFlightPurpose purpose,FailPoint fail){
    auto next=world_;
    if(!next.queue_npc_ship_rendezvous(
        owner,ship,pilot,target_ship,order_id,purpose))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("rendezvous navigation created material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::cancel_npc_flight(
    PlayerId owner,std::uint64_t order_id,FailPoint fail){
    auto next=world_;
    if(!next.cancel_npc_flight(owner,order_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("navigation cancellation altered matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::delegate_transport_work_order(
        PlayerId owner,EntityId origin,std::uint64_t id,WorkTaskKind kind,
        std::uint64_t source,std::uint64_t dependency,FailPoint fail) {
    auto next=world_;
    if(!next.delegate_transport_work_order(owner,origin,id,kind,source,
                                           dependency))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("transport delegation changed conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::cancel_work_directive(
        PlayerId owner,std::uint64_t directive,FailPoint fail) {
    auto next=world_;
    if(!next.cancel_work_directive(owner,directive))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("work revocation altered conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::post_shipyard_customer_order(
        PlayerId customer,EntityId buyer_ship,std::uint64_t yard,
        std::uint64_t build,std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.post_shipyard_customer_order(customer,buyer_ship,yard,build,id))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard request changed conserved resources");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::accept_shipyard_customer_order(
        PlayerId supplier,std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.accept_shipyard_customer_order(supplier,id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard acceptance changed conserved resources");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::fund_shipyard_customer_order(
        PlayerId customer,std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.fund_shipyard_customer_order(customer,id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard material funding altered conserved resources");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::deliver_shipyard_customer_order(
        PlayerId customer,std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.deliver_shipyard_customer_order(customer,id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard handover altered conserved resources");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::start_shipyard_construction(
        PlayerId owner,EntityId colony,std::uint64_t order,
        std::array<std::uint64_t,2> workers,FailPoint fail) {
    auto next=world_;
    if(!next.start_shipyard_construction(owner,colony,order,workers))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shipyard construction changed conserved matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::post_local_delivery_order(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& site,ShuttleCargo cargo,
        std::int64_t kilograms,std::uint64_t order_id,FailPoint fail) {
    auto next=world_;
    if(!next.post_local_delivery_order(owner,ship,site,cargo,kilograms,order_id))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("local contract posting changed conserved mass");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::cancel_local_delivery_order(
        PlayerId owner,std::uint64_t order_id,FailPoint fail) {
    auto next=world_;
    if(!next.cancel_local_delivery_order(owner,order_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("local contract cancellation changed conserved mass");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::post_intercolony_work_order(PlayerId owner,EntityId origin,
        const PlanetarySiteKey& site,ShuttleCargo cargo,std::int64_t kg,
        std::uint64_t id,EntityId carrier,EntityId destination,FailPoint fail) {
    auto next=world_;
    if(!next.post_local_delivery_order(owner,origin,site,cargo,kg,id) ||
       !next.plan_intercolony_freight(owner,id,carrier,destination))
        return false; // neither plan nor local instruction is published
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("intercolony plan allocated non-existent goods");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::load_intercolony_freight(PlayerId owner,
        std::uint64_t id,EntityId carrier,EntityId destination,FailPoint fail) {
    for(const auto& [request,p]:world_.pending_celestial_transits_) {
        (void)request;
        if(p.ship==carrier || p.ship==destination)return false;
    }
    auto next=world_;
    if(!next.load_intercolony_freight(owner,id,carrier,destination))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("sealed intercolony load changed world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::deliver_intercolony_freight(PlayerId owner,
        std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.deliver_intercolony_freight(owner,id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("intercolony delivery changed conserved world matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::build_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.build_planetary_shuttle(owner,ship,key))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle operation changed conserved material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::transfer_planetary_housing_module(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool load,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.transfer_planetary_housing_module(owner,ship,key,load))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 housing module transfer changed world mass");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::start_planetary_housing(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,std::uint64_t id,FailPoint fail) {
    auto next=world_;
    if(!next.start_planetary_housing(owner,ship,key,id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 original housing construction created matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::set_planetary_housing_pause(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool pause,FailPoint fail) {
    auto next=world_;
    if(!next.set_planetary_housing_pause(owner,ship,key,pause))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 construction pause changed world mass");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::start_planetary_surface_utility(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,std::uint64_t id,
        std::uint64_t supervisor,FailPoint fail) {
    auto next=world_;
    if(!next.start_planetary_surface_utility(owner,ship,key,id,supervisor))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 surface utility fabricated physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::pause_planetary_surface_utility(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool pause,FailPoint fail) {
    auto next=world_;
    if(!next.pause_planetary_surface_utility(owner,ship,key,pause))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 surface utility pause changed physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

// The original PersistentWorld clone/SQLite commit is the only authority
// for supervisor changes; failed copies never expose partial handovers.
bool PersistentWorld::handover_planetary_surface_utility(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,std::uint64_t previous,
        std::uint64_t replacement,std::uint64_t request_id,FailPoint fail) {
    auto next=world_;
    if(!next.handover_planetary_surface_utility(
         owner,ship,key,previous,replacement,request_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 utility relief altered physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

// v113 original SQLite commit/copy/swap; failed commit preserves live site
// modules, project progress and named workers.
bool PersistentWorld::start_planetary_service_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool annex,
        std::uint64_t order_id,std::uint64_t supervisor,FailPoint fail) {
    auto next=world_;
    if(!next.start_planetary_service_build(
        owner,ship,key,annex,order_id,supervisor))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 service build created physical matter");
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash())
        throw std::logic_error("WO3 service project cannot restore");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::pause_planetary_service_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool annex,
        bool paused,FailPoint fail) {
    auto next=world_;
    if(!next.pause_planetary_service_build(owner,ship,key,annex,paused))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 service pause altered physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::handover_planetary_service_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool annex,
        std::uint64_t previous,std::uint64_t replacement,
        std::uint64_t request_id,FailPoint fail) {
    auto next=world_;
    if(!next.handover_planetary_service_build(
        owner,ship,key,annex,previous,replacement,request_id))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 supervisor relief altered physical matter");
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash())
        throw std::logic_error("WO3 supervisor history cannot restore");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::start_planetary_production_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool farm,
        std::uint64_t order_id,std::uint64_t qualified_supervisor,
        FailPoint fail) {
    auto next=world_;
    if(!next.start_planetary_production_build(
        owner,ship,key,farm,order_id,qualified_supervisor))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 production build fabricated material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::pause_planetary_production_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool farm,bool pause,
        FailPoint fail) {
    auto next=world_;
    if(!next.pause_planetary_production_build(
        owner,ship,key,farm,pause))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 production pause fabricated material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::handover_planetary_production_build(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,bool farm,
        std::uint64_t previous,std::uint64_t replacement,
        std::uint64_t request_id,FailPoint fail) {
    auto next=world_;
    if(!next.handover_planetary_production_build(
        owner,ship,key,farm,previous,replacement,request_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 production supervisor relief altered matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

// Trusted single-writer import of historical legal-only planetary addresses.
// A failed copy/SQLite commit never changes the live world.
bool PersistentWorld::host_import_legacy_planetary_residences(
        PlayerId host_principal,std::uint64_t request_id,FailPoint fail) {
    if(host_principal!=Simulation::kServerAuthority)return false;
    auto next=world_;
    if(!next.host_import_legacy_planetary_residences(
        host_principal,request_id))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 historical residence import altered matter");
    // The new canonical v115 record must already be independently readable
    // before attempting to persist it. No fall-through old-address rewrite.
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash())
        throw std::logic_error("WO3 historical residence import cannot restore");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

// WO3-E/v118: the only legal household route between a physically built
// planet and its original ship. The world snapshot, original ART02 people
// and original ECON03 regional population all commit under ONE SQLite writer.
bool PersistentWorld::commit_planetary_household_settlement(
        const PlanetaryHouseholdSettlementPlan& p,FailPoint fail) {
    if(p.expected_world_generation!=generation_ || !p.request_id ||
       p.request_id%16 || p.request_id>
           std::numeric_limits<std::uint64_t>::max()-3 ||
       p.coparent_move_id!=p.request_id+1 ||
       p.child_move_id!=p.request_id+2 ||
       !p.owner || p.owner==Simulation::kServerAuthority ||
       !p.ship || !p.guardian_id || !p.family_seed ||
       p.family_scope.empty() || p.region_scope.empty() ||
       p.origin_colony.empty() || p.destination_colony.empty() ||
       p.origin_colony==p.destination_colony ||
       !p.expected_region_revision || !p.expected_owner_generation ||
       !p.guardian_consented || !p.coparent_consented)
        return false;
    const auto site=world_.planetary_sites().find(p.site);
    const auto shuttle=world_.planetary_shuttles().find(p.site);
    if(site==world_.planetary_sites().end() ||
       shuttle==world_.planetary_shuttles().end() ||
       site->second.owner!=p.owner || site->second.source_ship!=p.ship ||
       shuttle->second.owner!=p.owner ||
       shuttle->second.source_ship!=p.ship)return false;
    const auto address=site->second.surface_location;
    const auto& flight=shuttle->second.family;
    std::uint64_t parent=0,child=0;
    if(p.returning) {
        const auto household=site->second.households.find(p.guardian_id);
        if(household==site->second.households.end() ||
           household->second.return_request ||
           household->second.child_ids.size()!=1 ||
           flight.stage!=7 ||
           flight.guardian_id!=p.guardian_id ||
           flight.co_parent_id!=household->second.co_parent_id ||
           flight.child_ids!=household->second.child_ids)return false;
        parent=household->second.co_parent_id;
        child=household->second.child_ids.front();
    }else {
        if(flight.stage!=2 || flight.guardian_id!=p.guardian_id ||
           !flight.co_parent_id || flight.child_ids.size()!=1)return false;
        parent=flight.co_parent_id;child=flight.child_ids.front();
    }
    const auto origin=p.returning?address:p.ship;
    const auto destination=p.returning?p.ship:address;
    if(!store_->valid_planetary_family_births(
        world_,p.ship,p.guardian_id,parent,{child},origin))
        return false;
    for(const auto person_id:{p.guardian_id,parent}) {
        const auto n=world_.npc_records().find(person_id);
        if(n==world_.npc_records().end() ||
           n->second.home_ship!=p.ship ||
           n->second.legal_residence()!=transport::Endpoint{
               p.returning?transport::LocationKind::PlanetarySite:
                           transport::LocationKind::ColonyShip,origin})
            return false;
    }
    const auto dependent=world_.physical_dependents().find(child);
    if(dependent==world_.physical_dependents().end() ||
       dependent->second.home_ship!=p.ship ||
       dependent->second.location!=(p.returning?p.ship:address))
        return false;
    // All three actual people must have the same original legal address.
    const auto file=store_->sqlite_path();
    NpcAppearanceStore family(file,p.family_scope,p.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    const auto source=family.family_census(origin);
    std::uint64_t dest_count=0,dest_revision=0;
    bool destination_registered=false;
    try {
        const auto target=family.family_census(destination);
        destination_registered=true;
        dest_count=target.current_population;dest_revision=target.revision;
    }catch(const std::out_of_range&) {
        if(p.returning)return false; // original ship must already exist
        // The first real planetary family may register a genuine ZERO census
        // only WITH its first atomic inbound move, never ahead of the world.
    }
    if(source.revision!=p.expected_origin_family_revision ||
       dest_revision!=p.expected_destination_family_revision ||
       source.current_population<3 ||
       family.current_residence(p.guardian_id)!=origin ||
       family.current_residence(parent)!=origin ||
       family.current_residence(child)!=origin)return false;
    regional_events::CheckpointStore region(file,p.region_scope);
    const auto projection=region.load();
    if(!projection || projection->revision!=p.expected_region_revision)
        return false;
    regional_events::RegionalModeAuthority owner(region);
    const auto custody=owner.state();
    if(custody.owner!=regional_events::SimulationOwner::Cold ||
       custody.phase!=regional_events::TransitionPhase::Stable ||
       custody.generation!=p.expected_owner_generation ||
       custody.checkpoint_revision!=projection->revision ||
       projection->scheduler.colony(p.origin_colony).population!=
           source.current_population ||
       projection->scheduler.colony(p.destination_colony).population!=
           dest_count)
        return false;
    auto next=world_;
    const bool changed=p.returning?
        next.finalize_planetary_household_return(
            Simulation::kServerAuthority,p.owner,p.ship,p.site,
            p.guardian_id,p.request_id):
        next.settle_planetary_household(
            Simulation::kServerAuthority,p.owner,p.ship,p.site,
            p.request_id,p.guardian_consented,p.coparent_consented);
    if(!changed)return false;
    if(p.returning && !next.release_planetary_shuttle_family(
            Simulation::kServerAuthority,p.owner,p.ship,p.site))
        throw std::logic_error("committed return cannot release real shuttle");
    if(next.total_ore()!=resource_origin_ ||
       next.total_credits()!=credit_origin_ ||
       next.npc_records().size()!=world_.npc_records().size() ||
       next.physical_dependents().size()!=world_.physical_dependents().size())
        throw std::logic_error("household settlement changed material or people");
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash())
        throw std::logic_error("household settlement cannot canonically restore");
    const auto effect=[&](sqlite3* db,std::uint64_t new_generation) {
        // The original world owns the only ART02 connection and transaction.
        if(!store_->valid_planetary_family_births(
            world_,p.ship,p.guardian_id,parent,{child},origin))
            throw std::runtime_error("household birth or custody changed");
        NpcAppearanceStore joined(db,p.family_scope,p.family_seed,
            [&](std::uint64_t id){return next.npc_records().count(id)!=0;});
        if(!p.returning && !destination_registered)
            joined.register_settlement(destination,0);
        const auto actual_src=joined.family_census(origin);
        const auto actual_dst=joined.family_census(destination);
        if(actual_src.revision!=p.expected_origin_family_revision ||
           actual_dst.revision!=p.expected_destination_family_revision ||
           actual_src.current_population!=source.current_population ||
           actual_dst.current_population!=dest_count ||
           joined.current_residence(p.guardian_id)!=origin ||
           joined.current_residence(parent)!=origin ||
           joined.current_residence(child)!=origin)
            throw std::runtime_error("ART02 household CAS changed");
        const auto authorize=[&](std::uint64_t adult,std::uint64_t from,
                                 std::uint64_t to) {
            if(from!=origin || to!=destination ||
               (adult!=p.guardian_id && adult!=parent))return false;
            const auto old=world_.npc_records().find(adult);
            const auto now=next.npc_records().find(adult);
            return old!=world_.npc_records().end() &&
                now!=next.npc_records().end() &&
                old->second.legal_residence().id==origin &&
                now->second.legal_residence().id==destination;
        };
        const auto moves=joined.record_guardian_household_moves(
            p.request_id,p.guardian_id,{{child,p.child_move_id}},
            origin,destination,next.tick(),
            p.expected_origin_family_revision,
            p.expected_destination_family_revision,authorize,
            parent,p.coparent_move_id);
        if(moves.size()!=3)
            throw std::runtime_error("incomplete original family census move");
        const auto region_revision=
            region.commit_migration_in_world_transaction(db,
                p.expected_region_revision,p.expected_owner_generation,
                next.tick(),p.origin_colony,p.destination_colony,
                source.current_population,dest_count,
                std::to_string(p.request_id),3,"society-migration-host");
        run(db,"CREATE TABLE IF NOT EXISTS sg_planet_household_migration("
            "request_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL,"
            "tick INTEGER NOT NULL,origin TEXT NOT NULL,destination TEXT NOT NULL,"
            "guardian TEXT NOT NULL,coparent TEXT NOT NULL,child TEXT NOT NULL,"
            "returning INTEGER NOT NULL CHECK(returning IN (0,1)),"
            "family_scope TEXT NOT NULL,region_scope TEXT NOT NULL,"
            "region_revision INTEGER NOT NULL,owner_generation INTEGER NOT NULL,"
            "coparent_event TEXT NOT NULL UNIQUE,child_event TEXT NOT NULL UNIQUE)");
        Statement receipt(db,"INSERT INTO sg_planet_household_migration "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        receipt.text(1,std::to_string(p.request_id));
        receipt.i64(2,static_cast<std::int64_t>(new_generation));
        receipt.i64(3,static_cast<std::int64_t>(next.tick()));
        receipt.text(4,std::to_string(origin));
        receipt.text(5,std::to_string(destination));
        receipt.text(6,std::to_string(p.guardian_id));
        receipt.text(7,std::to_string(parent));
        receipt.text(8,std::to_string(child));
        receipt.i64(9,p.returning?1:0);
        receipt.text(10,p.family_scope);
        receipt.text(11,p.region_scope);
        receipt.i64(12,static_cast<std::int64_t>(region_revision));
        receipt.i64(13,static_cast<std::int64_t>(p.expected_owner_generation));
        receipt.text(14,std::to_string(p.coparent_move_id));
        receipt.text(15,std::to_string(p.child_move_id));
        receipt.execute();
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail,nullptr,nullptr,effect);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::book_planetary_household_return(PlayerId owner,
        EntityId ship,const PlanetarySiteKey& key,std::uint64_t guardian,
        std::uint64_t request_id,FailPoint fail) {
    const auto current=world_.planetary_sites().find(key);
    if(current==world_.planetary_sites().end())return false;
    const auto registered=current->second.households.find(guardian);
    if(registered==current->second.households.end() ||
       !registered->second.co_parent_id ||
       registered->second.child_ids.size()!=1 ||
       !store_->valid_planetary_family_births(
           world_,ship,guardian,registered->second.co_parent_id,
           registered->second.child_ids,
           current->second.surface_location))
        return false;
    auto next=world_;
    if(!next.book_planetary_household_return(
        Simulation::kServerAuthority,owner,ship,key,guardian,request_id))
        return false;
    if(next.total_ore()!=resource_origin_)return false;
    const auto verify=[&](sqlite3*,std::uint64_t) {
        if(!store_->valid_planetary_family_births(
            world_,ship,guardian,registered->second.co_parent_id,
            registered->second.child_ids,
            current->second.surface_location))
            throw std::runtime_error("ART02 family changed at return booking");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail,nullptr,nullptr,verify);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::settle_planetary_founder(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc_id,std::uint64_t request_id,
        bool voluntary_consent_attested,FailPoint fail) {
    if(store_->has_art02_child_parent(npc_id))return false;
    auto next=world_;
    if(!next.settle_planetary_founder(owner,ship,key,npc_id,request_id,
                                      voluntary_consent_attested))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 permanent residence fabricated stock");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::settle_planetary_visitor(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc_id,std::uint64_t request_id,
        bool consent,FailPoint fail) {
    if(store_->has_art02_child_parent(npc_id))return false;
    auto next=world_;
    if(!next.settle_planetary_visitor(owner,ship,key,npc_id,request_id,consent))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 visitor immigration fabricated physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::depart_planetary_visitor(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc_id,std::uint64_t request_id,
        bool consent,FailPoint fail) {
    if(store_->has_art02_child_parent(npc_id))return false;
    auto next=world_;
    if(!next.depart_planetary_visitor(owner,ship,key,npc_id,request_id,consent))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("WO3 visitor departure changed physical matter");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::load_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,bool water,std::int64_t kg,FailPoint fail) {
    return load_planetary_shuttle(owner,ship,key,
        water?ShuttleCargo::Water:ShuttleCargo::Food,kg,fail);
}
bool PersistentWorld::load_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,ShuttleCargo cargo,std::int64_t kg,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.load_planetary_shuttle(owner,ship,key,cargo,kg))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle operation changed conserved material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::unload_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,bool water,std::int64_t kg,FailPoint fail) {
    return unload_planetary_shuttle(owner,ship,key,
        water?ShuttleCargo::Water:ShuttleCargo::Food,kg,fail);
}
bool PersistentWorld::unload_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,ShuttleCargo cargo,std::int64_t kg,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.unload_planetary_shuttle(owner,ship,key,cargo,kg))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle operation changed conserved material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::assign_planetary_shuttle_pilot(PlayerId owner,EntityId ship,const PlanetarySiteKey& key,std::uint64_t npc,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    if(store_->has_art02_child_parent(npc))return false;
    auto next=world_;
    if(!next.assign_planetary_shuttle_pilot(owner,ship,key,npc))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("shuttle operation changed conserved material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::build_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.build_planetary_site(owner,ship,body,latitude,longitude))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::build_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    std::uint64_t first_npc,std::uint64_t second_npc,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.build_planetary_site(owner,ship,body,latitude,longitude,
                                 first_npc,second_npc))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("surface delegation changed world resource origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::equip_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,bool farm,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.equip_planetary_site(owner,ship,body,latitude,longitude,farm))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::supply_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,bool water,std::int64_t kg,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.supply_planetary_site(owner,ship,body,latitude,longitude,water,kg))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::set_planetary_workforce_policy(PlayerId owner,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    std::uint8_t policy,FailPoint fail) {
    // A command changes supervisory policy only. All inventory movements
    // remain tied to source-backed simulation ticks and SQLite generations.
    auto next=world_;
    if(!next.set_planetary_workforce_policy(owner,body,latitude,longitude,
                                            policy))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary workforce policy created material");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::support_planetary_crew(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    bool oxygen,std::int64_t kg,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.support_planetary_crew(owner,ship,body,latitude,longitude,oxygen,kg))
        return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("surface crew resupply changed matter origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::cultivate_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,std::int64_t batches,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.cultivate_planetary_site(owner,ship,body,latitude,longitude,batches))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::extract_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,bool water,std::int64_t kg,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.extract_planetary_site(owner,ship,body,latitude,longitude,water,kg))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::retrieve_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,bool food,std::int64_t kg,FailPoint fail) {
    // Every mutation happens on an isolated candidate. Commit succeeds before
    // world publication; injected SQLite failures preserve the old world.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.retrieve_planetary_site(owner,ship,body,latitude,longitude,food,kg))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("planetary regional operation changed world material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::install_oasis_extractor(PlayerId owner,EntityId ship,
    std::uint64_t body,FailPoint fail) {
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.install_oasis_extractor(owner,ship,body))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("oasis extractor construction changed material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
bool PersistentWorld::collect_oasis_resource(PlayerId owner,EntityId ship,
    std::uint64_t body,bool water,std::int64_t kilograms,FailPoint fail) {
    // Authenticated host-only. Immutable old world on all failures.
    // No collection during an unfinished inter-frame transit.
    for(const auto& [id,p]:world_.pending_celestial_transits_) {
        (void)id;
        if(p.ship==ship)return false;
    }
    auto next=world_;
    if(!next.collect_oasis_resource(owner,ship,body,water,kilograms))return false;
    if(next.total_ore()!=resource_origin_)
        throw std::logic_error("oasis extraction changed material origin");
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}
void PersistentWorld::ensure_local_environment_profiles() {
    if(world_.generated_local_regions_.empty())return;
    auto next=world_;
    next.ensure_local_environment_profiles();
    if(next.canonical_state()==world_.canonical_state())return;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},FailPoint::None);
    world_.swap(next);
    ++generation_;
}
void PersistentWorld::ensure_visited_local_regions() {
    if(world_.ship_frames_.empty())return;
    auto next=world_;
    for(const auto& [ship,frame]:next.ship_frames_) {
        (void)ship;
        if(frame.anchor_body &&
           !next.generated_local_regions_.count(frame.anchor_body))
            next.seed_celestial_local_region(frame.anchor_body);
    }
    if(next.canonical_state()==world_.canonical_state())return;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},FailPoint::None);
    world_.swap(next);
    ++generation_;
}
void PersistentWorld::ensure_ship_frame_registry() {
    auto next=world_;
    next.ensure_ship_frame_registry();
    if(next.canonical_state()==world_.canonical_state())return;
    // The existing journal commits the complete v71 snapshot before memory
    // publication. On failure both the previous world and cargo remain intact.
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},FailPoint::None);
    world_.swap(next);
    ++generation_;
}
void PersistentWorld::ensure_physical_celestial_systems() {
    auto next=world_;
    next.ensure_physical_celestial_systems();
    if(next.canonical_state()==world_.canonical_state())return;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},FailPoint::None);
    world_.swap(next);
    ++generation_;
}
void PersistentWorld::configure_probe_report_tick_ms(std::uint64_t tick_ms) {
    // Commit policy through the existing durable single-writer journal.
    // No private mutation of world_ that could disappear at restart.
    auto next=world_;
    next.configure_probe_report_tick_ms(tick_ms);
    if(next.canonical_state()==world_.canonical_state())return;
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   {},{},{},FailPoint::None);
    world_.swap(next);
    ++generation_;
}


colony_safety::Decision PersistentWorld::record_colony_safety_preflight(
        EntityId home_ship,EntityId site,PlayerId issuer,colony_safety::Order order){
    const auto d=colony_safety::preflight(world_,home_ship,site,issuer,order);
    store_->record_colony_safety_preflight(d,generation_);
    return d;
}
std::uint64_t PersistentWorld::colony_safety_evidence_count() const{
    return store_->colony_safety_evidence_count();
}
std::uint64_t PersistentWorld::colony_response_event_count() const{
    return store_->colony_response_event_count();
}
std::vector<colony_safety::notice::Pending> PersistentWorld::pending_colony_notifications(
        std::uint64_t limit) const {
    return store_->pending_colony_notifications(limit);
}
bool PersistentWorld::acknowledge_colony_notification(std::uint64_t event,std::uint64_t npc){
    return store_->acknowledge_colony_notification(world_,generation_,event,npc);
}
std::uint64_t PersistentWorld::colony_notification_opportunity_count() const{
    return store_->colony_notification_opportunity_count();
}
std::uint64_t PersistentWorld::colony_notification_acknowledgement_count() const{
    return store_->colony_notification_acknowledgement_count();
}


std::vector<DependentTravelRecord> PersistentWorld::dependent_travel(
    const std::string& family_scope) const {
    return store_->dependent_travel(family_scope);
}

bool PersistentWorld::submit_host_dependent_rescue_action(const Command& command,
        FailPoint fail){
    if(command.action!=Action::HostEvacuatePassengers ||
       command.issuer!=Simulation::kServerAuthority || !command.actor ||
       !command.target || command.actor==command.target || command.amount<=0 ||
       store_->active_dependent_berths(command.actor)<=0)return false;
    auto next=world_;
    if(!next.submit(command))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}

bool PersistentWorld::submit_host_medical_action(const Command& command, FailPoint fail){
    if(command.action!=Action::HostTrainMedic &&
       command.action!=Action::HostTriageRescued &&
       command.action!=Action::HostTreatInjuries &&
       command.action!=Action::HostScheduleMedicShift &&
       command.action!=Action::HostLoadMedicalKit &&
       command.action!=Action::HostMobileStabilize)return false;
    return submit_host_passenger_action(command,fail);
}

// Dedicated fail-closed witness for a PREPARED manifest. Preparation and
// queueing do not move anyone; only the execution transaction can do that.
static void verify_atomic_colony_family_preparation(
    sqlite3* db,const Simulation& before,
    const PersistentWorld::AtomicColonyFamilyPlan& p){
    if(!p.command_id || !p.family.intent_id || p.command_id==p.family.intent_id ||
       !p.family.expected_world_generation || p.family.family_scope.empty() ||
       !p.family.family_seed || p.region_scope.empty() ||
       p.origin_colony.empty() || p.receiver_colony.empty() ||
       p.origin_colony==p.receiver_colony ||
       !p.expected_region_revision || !p.expected_owner_generation ||
       p.family.adult_ids.empty() || p.family.children.empty() ||
       p.member_event_ids.size()!=p.family.adult_ids.size()+p.family.children.size() ||
       p.member_event_ids.size()>16)
        throw std::runtime_error("incomplete whole-family evacuation admission");
    std::set<std::uint64_t> events{p.command_id,p.family.intent_id};
    for(auto event:p.member_event_ids)if(!event || !events.insert(event).second)
        throw std::runtime_error("duplicate or zero household event identifier");
    const auto audited=colony_family::assess(before,db,p.family);
    if(!audited.ready_for_transaction)
        throw std::runtime_error("whole-family authoritative audit refused: "+audited.refusal);
    std::string adults,children;
    for(const auto id:p.family.adult_ids){
        if(!adults.empty())adults.push_back(',');
        adults+=std::to_string(id);
    }
    for(const auto& child:p.family.children){
        if(!children.empty())children.push_back(',');
        children+=std::to_string(child.child_id)+":"+std::to_string(child.guardian_id);
        const auto it=before.physical_dependents().find(child.child_id);
        if(it==before.physical_dependents().end() ||
           it->second.guardian_id!=child.guardian_id ||
           it->second.home_ship!=p.family.source ||
           it->second.location!=p.family.source)
            throw std::runtime_error("unregistered, wrong-guardian or nonresident physical dependent");
        Statement auth(db,"SELECT guardian_id,home_ship,family_scope,family_seed "
            "FROM sg_colony_physical_child_authority WHERE child_id=?");
        auth.text(1,std::to_string(child.child_id));
        if(!auth.row() || sqlite_str(auth.get(),0)!=std::to_string(child.guardian_id) ||
           sqlite_str(auth.get(),1)!=std::to_string(p.family.source) ||
           sqlite_str(auth.get(),2)!=p.family.family_scope ||
           sqlite_str(auth.get(),3)!=std::to_string(p.family.family_seed) || auth.row())
            throw std::runtime_error("physical dependent lacks immutable ART02 registration authority");
    }
    std::size_t physical_at_source=0;
    for(const auto& [id,child]:before.physical_dependents()){
        (void)id;
        if(child.location==p.family.source || child.home_ship==p.family.source)
            ++physical_at_source;
    }
    if(physical_at_source!=p.family.children.size())
        throw std::runtime_error("unlisted physical dependent at evacuation source");
    Statement prepared(db,"SELECT generation,world_hash,source,receiver,family_scope,"
        "family_seed,adult_roster,child_guardians,status "
        "FROM sg_colony_family_departure_prepare WHERE intent_id=?");
    prepared.text(1,std::to_string(p.family.intent_id));
    if(!prepared.row())
        throw std::runtime_error("family evacuation has no durable prepared manifest");
    const bool matched=
        sqlite3_column_int64(prepared.get(),0)==
            static_cast<sqlite3_int64>(p.family.expected_world_generation) &&
        sqlite_str(prepared.get(),2)==std::to_string(p.family.source) &&
        sqlite_str(prepared.get(),3)==std::to_string(p.family.receiver) &&
        sqlite_str(prepared.get(),4)==p.family.family_scope &&
        sqlite_str(prepared.get(),5)==std::to_string(p.family.family_seed) &&
        sqlite_str(prepared.get(),6)==adults &&
        sqlite_str(prepared.get(),7)==children &&
        sqlite3_column_int(prepared.get(),8)==1;
    const auto prep_hash=sqlite_str(prepared.get(),1);
    if(!matched || prepared.row())
        throw std::runtime_error("prepared manifest is stale or mismatched");
    Statement original(db,"SELECT state_hash FROM operation_log WHERE generation=?");
    original.i64(1,static_cast<sqlite3_int64>(p.family.expected_world_generation));
    if(!original.row() || sqlite_str(original.get(),0)!=prep_hash || original.row())
        throw std::runtime_error("prepared world hash does not match committed generation");
}

// COLONY-AUTO-04D.4C: one canonical world writer; no secondary population
// table, orphaned station crew, or player-facing shortcut. This operation is
// adult-only until independent ART02 dependent-custody settlement is available.
colony_family::Audit PersistentWorld::audit_colony_family_departure(
        const colony_family::DeparturePlan& plan) const{
    return store_->audit_colony_family_departure(world_,plan);
}
bool PersistentWorld::prepare_host_colony_family_departure(
        const colony_family::DeparturePlan& plan,FailPoint fail){
    return store_->prepare_colony_family_departure(world_,generation_,plan,fail);
}
// AUTO-04D.4D2A: physically register an ALREADY-BORN ART02 child.
// Two durable stages (admission and execution) use the existing single world
// writer and immutable operation journal; no new NPC/workforce identity.
bool PersistentWorld::submit_host_dependent_registration(
        const Command& command,const std::string& scope,std::uint64_t seed,
        FailPoint fail){
    if(command.action!=Action::HostRegisterDependent ||
       command.issuer!=Simulation::kServerAuthority ||
       command.execute_tick!=world_.tick()+1 ||
       !command.transport_id || !command.actor || scope.empty() || !seed)return false;
    auto next=world_;
    if(!next.submit(command))return false;
    const auto effect=[&](sqlite3* db,std::uint64_t){
        store_->verify_physical_dependent_birth(world_,command,scope,seed);
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_physical_child_authority("
            "child_id TEXT PRIMARY KEY,command_id TEXT NOT NULL UNIQUE,"
            "guardian_id TEXT NOT NULL,home_ship TEXT NOT NULL,"
            "family_scope TEXT NOT NULL,family_seed TEXT NOT NULL)");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_colony_child_authority_no_update "
            "BEFORE UPDATE ON sg_colony_physical_child_authority "
            "BEGIN SELECT RAISE(ABORT,'physical child authority immutable'); END");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_colony_child_authority_no_delete "
            "BEFORE DELETE ON sg_colony_physical_child_authority "
            "BEGIN SELECT RAISE(ABORT,'physical child authority immutable'); END");
        Statement entry(db,"INSERT INTO sg_colony_physical_child_authority "
            "VALUES(?,?,?,?,?,?)");
        entry.text(1,std::to_string(command.transport_id));
        entry.text(2,std::to_string(command.id));
        entry.text(3,std::to_string(command.passenger_ids.front()));
        entry.text(4,std::to_string(command.actor));
        entry.text(5,scope);
        entry.text(6,std::to_string(seed));
        entry.execute();
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
    return true;
}
void PersistentWorld::step_with_dependent_registration(FailPoint fail){
    auto next=world_;
    const auto before=next.events().size();
    const auto activity_before=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& cmd:it->second)due.emplace(cmd.id,cmd);
    if(due.size()!=1 || due.begin()->second.action!=Action::HostRegisterDependent ||
       due.begin()->second.issuer!=Simulation::kServerAuthority)
        throw std::runtime_error("physical child registration requires one host command");
    const auto cmd=due.begin()->second;
    const auto check=[&](sqlite3* db){
        Statement record(db,"SELECT command_id,guardian_id,home_ship,"
            "family_scope,family_seed FROM sg_colony_physical_child_authority WHERE child_id=?");
        record.text(1,std::to_string(cmd.transport_id));
        if(!record.row())throw std::runtime_error("unapproved physical child admission");
        if(sqlite_str(record.get(),0)!=std::to_string(cmd.id) ||
           sqlite_str(record.get(),1)!=std::to_string(cmd.passenger_ids.front()) ||
           sqlite_str(record.get(),2)!=std::to_string(cmd.actor))
            throw std::runtime_error("physical child guardian receipt mismatch");
        const auto scope=sqlite_str(record.get(),3);
        const auto seed=parse_u64(sqlite3_column_text(record.get(),4));
        if(record.row())throw std::runtime_error("duplicate birth authority");
        store_->verify_physical_dependent_birth(world_,cmd,scope,seed);
    };
    next.step();
    if(next.events().size()!=before+1 ||
       next.events().back().command_id!=cmd.id ||
       next.events().back().result!=Outcome::Applied ||
       !next.physical_dependents().count(cmd.transport_id))
        throw std::runtime_error("dependent occupancy was not physically applied");
    const std::vector<Event> delta(next.events().begin()+
        static_cast<std::ptrdiff_t>(before),next.events().end());
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(activity_before),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t){check(db);};
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,delta,due,activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

bool PersistentWorld::submit_host_colony_departure(const Command& command,FailPoint fail){
    if(command.action!=Action::HostColonyDeparture ||
       command.issuer!=Simulation::kServerAuthority ||
       command.execute_tick!=world_.tick()+1 ||
       !command.actor || !command.target)return false;
    const auto no_child_links=[&](){
        if(store_->has_child_or_dependent_link(command.actor) ||
           store_->has_child_or_dependent_link(command.target))return false;
        for(const auto& [site,outpost]:world_.built_outposts())
            if(outpost.source_colony_ship==command.actor &&
               store_->has_child_or_dependent_link(site))return false;
        return true;
    };
    if(!no_child_links())return false;
    auto next=world_;
    if(!next.submit(command))return false;
    // Recheck under BEGIN IMMEDIATE, not only before it, so another writer
    // cannot insert a guardian/dependent between admission and publication.
    const auto locked_guard=[&](sqlite3*,std::uint64_t){
        if(!no_child_links())throw std::runtime_error("birth/guardian custody changed during departure admission");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail,nullptr,nullptr,locked_guard);
    world_.swap(next);++generation_;
    return true;
}
void PersistentWorld::step_with_colony_departure(FailPoint fail){
    auto next=world_;
    const auto before=next.events().size();
    const auto before_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1 ||
       due.begin()->second.action!=Action::HostColonyDeparture ||
       due.begin()->second.issuer!=Simulation::kServerAuthority)
        throw std::runtime_error("colony departure requires one authorized due command");
    const auto cmd=due.begin()->second;
    const auto no_child_links=[&](){
        if(store_->has_child_or_dependent_link(cmd.actor) ||
           store_->has_child_or_dependent_link(cmd.target))return false;
        for(const auto& [site,outpost]:world_.built_outposts())
            if(outpost.source_colony_ship==cmd.actor &&
               store_->has_child_or_dependent_link(site))return false;
        return true;
    };
    if(!no_child_links())
        throw std::runtime_error("colony departure conflicts with recorded dependent or birth custody");
    next.step();
    if(next.events().size()!=before+1 ||
       next.events().back().command_id!=cmd.id ||
       next.events().back().result!=Outcome::Applied ||
       !next.colony_departures_.count(cmd.actor))
        throw std::runtime_error("departure failed physical execution; original world unchanged");
    const std::vector<Event> events(next.events().begin()+
        static_cast<std::ptrdiff_t>(before),next.events().end());
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(before_activity),next.activity_events().end());
    const auto locked_guard=[&](sqlite3*,std::uint64_t){
        if(!no_child_links())throw std::runtime_error("dependent custody changed during atomic world publication");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   events,due,activity,fail,nullptr,nullptr,locked_guard);
    world_.swap(next);
    ++generation_;
}

// AUTO-04D.5A: verify a delivered, local, current threshold notice. An
// opportunity alone is NOT NPC knowledge and never authorizes movement.
static bool autonomous_notice_verified(sqlite3* db,const Simulation& world,
    const PersistentWorld::AutonomousRescueRequest& request){
    const auto actor=world.npc_records().find(request.leader_id);
    if(!db || !request.source || !request.leader_id || !request.observed_event_id ||
       actor==world.npc_records().end() ||
       actor->second.home_ship!=request.source ||
       actor->second.location!=request.source ||
       actor->second.transport_id)
        return false;
    Statement notice(db,"SELECT o.generation,o.tick,o.risk,e.ship FROM "
        "sg_colony_npc_opportunities o "
        "JOIN sg_colony_npc_acks a ON a.event_id=o.event_id AND a.npc_id=o.npc_id "
        "JOIN sg_colony_response_events e ON e.id=o.event_id "
        "WHERE o.event_id=? AND o.npc_id=? AND o.site=?");
    notice.i64(1,static_cast<sqlite3_int64>(request.observed_event_id));
    notice.i64(2,static_cast<sqlite3_int64>(request.leader_id));
    notice.i64(3,static_cast<sqlite3_int64>(request.source));
    if(!notice.row())return false;
    const auto recorded_risk=sqlite3_column_int(notice.get(),2);
    const auto source_ship=sqlite3_column_int64(notice.get(),3);
    if(source_ship!=static_cast<sqlite3_int64>(request.source) ||
       recorded_risk<static_cast<int>(colony_safety::Risk::Critical) ||
       recorded_risk>static_cast<int>(colony_safety::Risk::Emergency) ||
       notice.row())return false;
    Statement latest(db,"SELECT id FROM sg_colony_response_events "
        "WHERE site=? ORDER BY id DESC LIMIT 1");
    latest.i64(1,static_cast<sqlite3_int64>(request.source));
    if(!latest.row() ||
       sqlite3_column_int64(latest.get(),0)!=
            static_cast<sqlite3_int64>(request.observed_event_id) ||
       latest.row())return false;
    const auto present=colony_safety::assess(world,request.source,request.source);
    return static_cast<int>(present.risk)==recorded_risk &&
           present.population>0;
}

PersistentWorld::AutonomousRescueDecision
PersistentWorld::assess_autonomous_colony_emergency(
    const AutonomousRescueRequest& request) const{
    AutonomousRescueDecision result;
    try{
        if(!request.source || !request.leader_id || !request.observed_event_id ||
           !request.family_seed || !request.intent_id || !request.command_id ||
           request.intent_id==request.command_id ||
           request.family_scope.empty() || request.region_scope.empty() ||
           request.origin_colony.empty() || request.destinations.empty() ||
           request.destinations.size()>32){
            result.reason="autonomous controller requires valid bounded host routing";
            return result;
        }
        result.response=colony_safety::plan_response(world_,request.source,request.source);
        if(result.response.risk!=colony_safety::Risk::Critical &&
           result.response.risk!=colony_safety::Risk::Emergency){
            result.reason="colony is not in actual critical or emergency shortage";
            return result;
        }
        sqlite3* raw=nullptr;
        if(sqlite3_open_v2(db_path_.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
            if(raw)sqlite3_close(raw);
            throw std::runtime_error("autonomous response cannot open world evidence");
        }
        std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
        if(!autonomous_notice_verified(db.get(),world_,request)){
            result.reason="NPC leader has no delivered current local critical notice";
            return result;
        }
        result.notified_leader=true;
        regional_events::CheckpointStore region(db_path_,request.region_scope);
        const auto regional=region.load();
        if(!regional){
            result.reason="regional population checkpoint missing";
            return result;
        }
        regional_events::RegionalModeAuthority mode(region);
        const auto authority=mode.state();
        if(authority.owner!=regional_events::SimulationOwner::Cold ||
           authority.phase!=regional_events::TransitionPhase::Stable ||
           authority.checkpoint_revision!=regional->revision){
            result.reason="regional owner not stable/cold for family transfer";
            return result;
        }
        NpcAppearanceStore family(db_path_,request.family_scope,request.family_seed,
            [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
        const auto from=family.family_census(request.source);
        if(regional->scheduler.colony(request.origin_colony).population!=
           from.current_population){
            result.reason="source ART02/ECON03 census reconciliation refused";
            return result;
        }
        colony_family::DeparturePlan manifest;
        manifest.source=request.source;
        manifest.intent_id=request.intent_id;
        manifest.expected_world_generation=generation_;
        manifest.family_scope=request.family_scope;
        manifest.family_seed=request.family_seed;
        for(const auto& [id,record]:world_.npc_records())
            if(record.home_ship==request.source)manifest.adult_ids.push_back(id);
        for(const auto& [id,child]:world_.physical_dependents())
            if(child.home_ship==request.source || child.location==request.source){
                if(child.home_ship!=request.source ||
                   child.location!=request.source){
                    result.reason="physical dependent custody already split";
                    return result;
                }
                manifest.children.push_back({id,child.guardian_id});
            }
        if(manifest.adult_ids.empty() || manifest.children.empty() ||
           manifest.adult_ids.size()+manifest.children.size()>16 ||
           request.member_event_ids.size()!=
               manifest.adult_ids.size()+manifest.children.size()){
            result.reason="incomplete physical family or host member event allocation";
            return result;
        }
        std::set<std::uint64_t> unique_events{request.command_id,request.intent_id};
        for(const auto id:request.member_event_ids)
            if(!id || !unique_events.insert(id).second){
                result.reason="duplicate or absent immutable member event ID";
                return result;
            }
        const auto* src=world_.find(request.source);
        if(!src)throw std::runtime_error("autonomous source ship missing");
        long double best_distance=-1;
        for(const auto& destination:request.destinations){
            if(!destination.ship || destination.ship==request.source ||
               destination.regional_colony.empty() ||
               destination.regional_colony==request.origin_colony)continue;
            const auto* ship=world_.find(destination.ship);
            const auto* supplies=world_.life_support(destination.ship);
            if(!ship || !supplies)continue;
            manifest.receiver=destination.ship;
            const auto audit=store_->audit_colony_family_departure(world_,manifest);
            if(!audit.ready_for_transaction)continue;
            const auto census=family.family_census(destination.ship);
            if(regional->scheduler.colony(destination.regional_colony).population!=
               census.current_population)continue;
            const auto persons=world_.people_physically_aboard(destination.ship)+
                world_.children_physically_aboard(destination.ship)+
                manifest.adult_ids.size()+manifest.children.size();
            // A receiver must survive at least four *real* whole-person needs
            // cycles after this evacuation, not become the next emergency.
            if(persons>1000 || supplies->food_kg<static_cast<std::int64_t>(4*persons) ||
               supplies->water_kg<static_cast<std::int64_t>(4*persons) ||
               supplies->oxygen_kg<static_cast<std::int64_t>(4*persons))continue;
            const auto dx=static_cast<long double>(src->position.x)-ship->position.x;
            const auto dy=static_cast<long double>(src->position.y)-ship->position.y;
            const auto distance=dx*dx+dy*dy;
            if(best_distance>=0 &&
               (distance>best_distance ||
                (distance==best_distance && destination.ship>=result.selected_receiver)))
                continue;
            best_distance=distance;
            result.selected_receiver=destination.ship;
            result.transfer.family=manifest;
            result.transfer.region_scope=request.region_scope;
            result.transfer.origin_colony=request.origin_colony;
            result.transfer.receiver_colony=destination.regional_colony;
            result.transfer.command_id=request.command_id;
            result.transfer.expected_region_revision=regional->revision;
            result.transfer.expected_owner_generation=authority.generation;
            result.transfer.expected_origin_family_revision=from.revision;
            result.transfer.expected_receiver_family_revision=census.revision;
            result.transfer.member_event_ids=request.member_event_ids;
        }
        if(!result.selected_receiver){
            result.reason="no verified nearby stocked receiver: retain emergency/distress response";
            return result;
        }
        result.rescue_ready=true;
        result.reason="real recipient selected; prepared family still requires host admission and world execution";
    }catch(const std::exception& e){
        result.rescue_ready=false;
        result.selected_receiver=0;
        result.reason=e.what();
    }
    return result;
}

bool PersistentWorld::submit_autonomous_colony_rescue(
    const AutonomousRescueRequest& request,FailPoint fail){
    const auto planned=assess_autonomous_colony_emergency(request);
    if(!planned.notified_leader || !planned.rescue_ready ||
       !planned.selected_receiver)return false;
    const auto& p=planned.transfer;
    if(!prepare_host_colony_family_departure(p.family))return false;
    Command command{};
    command.id=p.command_id;command.sequence=p.command_id;
    command.execute_tick=world_.tick()+1;
    command.issuer=Simulation::kServerAuthority;
    command.actor=p.family.source;command.target=p.family.receiver;
    command.action=Action::HostColonyDeparture;
    command.passenger_ids=p.family.adult_ids;
    auto next=world_;
    if(!next.submit(command))return false;
    const auto effect=[&](sqlite3* db,std::uint64_t next_generation){
        verify_atomic_colony_family_preparation(db,world_,p);
        if(!autonomous_notice_verified(db,world_,request))
            throw std::runtime_error("NPC warning withdrawn or tampered before dispatch");
        Statement owner(db,"SELECT owner,phase,generation,checkpoint_revision "
            "FROM sg_regional_mode_authority WHERE scope=?");
        owner.text(1,request.region_scope);
        if(!owner.row() || sqlite3_column_int(owner.get(),0)!=0 ||
           sqlite3_column_int(owner.get(),1)!=0 ||
           sqlite3_column_int64(owner.get(),2)!=
               static_cast<sqlite3_int64>(p.expected_owner_generation) ||
           sqlite3_column_int64(owner.get(),3)!=
               static_cast<sqlite3_int64>(p.expected_region_revision) || owner.row())
            throw std::runtime_error("autonomous dispatch crossed a regional owner transition");
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_npc_autonomous_dispatch("
            "command_id TEXT PRIMARY KEY,intent_id TEXT NOT NULL UNIQUE,"
            "admission_generation INTEGER NOT NULL CHECK(admission_generation>1),"
            "world_hash TEXT NOT NULL,leader_id TEXT NOT NULL,observation_id INTEGER NOT NULL,"
            "source TEXT NOT NULL,receiver TEXT NOT NULL,"
            "family_scope TEXT NOT NULL,region_scope TEXT NOT NULL,"
            "risk INTEGER NOT NULL CHECK(risk IN (2,3)))");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_colony_npc_auto_no_update "
            "BEFORE UPDATE ON sg_colony_npc_autonomous_dispatch "
            "BEGIN SELECT RAISE(ABORT,'immutable autonomous response'); END");
        run(db,"CREATE TRIGGER IF NOT EXISTS sg_colony_npc_auto_no_delete "
            "BEFORE DELETE ON sg_colony_npc_autonomous_dispatch "
            "BEGIN SELECT RAISE(ABORT,'immutable autonomous response'); END");
        Statement insert(db,"INSERT INTO sg_colony_npc_autonomous_dispatch VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?)");
        insert.text(1,std::to_string(p.command_id));
        insert.text(2,std::to_string(p.family.intent_id));
        insert.i64(3,static_cast<sqlite3_int64>(next_generation));
        insert.text(4,std::to_string(next.state_hash()));
        insert.text(5,std::to_string(request.leader_id));
        insert.i64(6,static_cast<sqlite3_int64>(request.observed_event_id));
        insert.text(7,std::to_string(p.family.source));
        insert.text(8,std::to_string(p.family.receiver));
        insert.text(9,request.family_scope);
        insert.text(10,request.region_scope);
        insert.i64(11,static_cast<int>(planned.response.risk));
        insert.execute();
        // v79: the complete restart plan belongs to this SAME writer
        // transaction as the actual accepted command and NPC observation.
        // Never persist half a manifest or a speculative player-facing rescue.
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_npc_supervisor_activation("
            "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
            "first_generation INTEGER NOT NULL CHECK(first_generation>1))");
        Statement activate(db,"INSERT OR IGNORE INTO "
            "sg_colony_npc_supervisor_activation VALUES(1,?)");
        activate.i64(1,static_cast<sqlite3_int64>(next_generation));
        activate.execute();
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_npc_supervisor_plan("
            "command_id TEXT PRIMARY KEY,intent_id TEXT NOT NULL UNIQUE,"
            "admission_generation INTEGER NOT NULL CHECK(admission_generation>1),"
            "family_seed TEXT NOT NULL,origin_colony TEXT NOT NULL,"
            "receiver_colony TEXT NOT NULL,regional_revision INTEGER NOT NULL,"
            "owner_generation INTEGER NOT NULL,"
            "origin_family_revision INTEGER NOT NULL,"
            "receiver_family_revision INTEGER NOT NULL,"
            "adult_count INTEGER NOT NULL CHECK(adult_count>0),"
            "child_count INTEGER NOT NULL CHECK(child_count>0))");
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_npc_supervisor_member("
            "command_id TEXT NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>=0),"
            "person_id TEXT NOT NULL,guardian_id TEXT NOT NULL,"
            "kind INTEGER NOT NULL CHECK(kind IN(0,1)),event_id TEXT NOT NULL UNIQUE,"
            "PRIMARY KEY(command_id,ordinal),UNIQUE(command_id,person_id))");
        for(const auto* name:{"sg_colony_npc_supervisor_activation",
                              "sg_colony_npc_supervisor_plan",
                              "sg_colony_npc_supervisor_member"}){
            const auto tab=std::string(name);
            run(db,("CREATE TRIGGER IF NOT EXISTS "+tab+"_immutable_update "
                "BEFORE UPDATE ON "+tab+" BEGIN SELECT RAISE(ABORT,"
                "'immutable NPC supervisor'); END").c_str());
            run(db,("CREATE TRIGGER IF NOT EXISTS "+tab+"_immutable_delete "
                "BEFORE DELETE ON "+tab+" BEGIN SELECT RAISE(ABORT,"
                "'immutable NPC supervisor'); END").c_str());
        }
        Statement plan(db,"INSERT INTO sg_colony_npc_supervisor_plan VALUES("
            "?,?,?,?,?,?,?,?,?,?,?,?)");
        plan.text(1,std::to_string(p.command_id));
        plan.text(2,std::to_string(p.family.intent_id));
        plan.i64(3,static_cast<sqlite3_int64>(next_generation));
        plan.text(4,std::to_string(p.family.family_seed));
        plan.text(5,p.origin_colony);plan.text(6,p.receiver_colony);
        plan.i64(7,static_cast<sqlite3_int64>(p.expected_region_revision));
        plan.i64(8,static_cast<sqlite3_int64>(p.expected_owner_generation));
        plan.i64(9,static_cast<sqlite3_int64>(p.expected_origin_family_revision));
        plan.i64(10,static_cast<sqlite3_int64>(p.expected_receiver_family_revision));
        plan.i64(11,static_cast<sqlite3_int64>(p.family.adult_ids.size()));
        plan.i64(12,static_cast<sqlite3_int64>(p.family.children.size()));
        plan.execute();
        for(std::size_t i=0;i<p.member_event_ids.size();++i){
            const bool is_child=i>=p.family.adult_ids.size();
            const auto child_index=is_child?i-p.family.adult_ids.size():0;
            const auto person=is_child?p.family.children[child_index].child_id:
                p.family.adult_ids[i];
            const auto guardian=is_child?p.family.children[child_index].guardian_id:0;
            Statement member(db,"INSERT INTO sg_colony_npc_supervisor_member "
                "VALUES(?,?,?,?,?,?)");
            member.text(1,std::to_string(p.command_id));
            member.i64(2,static_cast<sqlite3_int64>(i));
            member.text(3,std::to_string(person));
            member.text(4,std::to_string(guardian));
            member.i64(5,is_child?1:0);
            member.text(6,std::to_string(p.member_event_ids[i]));
            member.execute();
        }
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
    return true;
}

// AUTO-04D.5B — Persisted NPC supervisor. A whole family plan is a durable
// admission, never an implication that its world transaction already ran.
std::optional<PersistentWorld::AtomicColonyFamilyPlan>
PersistentWorld::pending_autonomous_family_rescue(std::uint64_t command_id) const{
    if(!command_id)return std::nullopt;
    store_->verify_autonomous_dispatch(generation_);
    store_->verify_supervisor_plans(generation_);
    auto plan=store_->supervisor_plan(command_id);
    if(!plan)return std::nullopt; // v78 legacy admissions lacked a resume plan
    if(plan->family.expected_world_generation+1!=generation_)
        return std::nullopt; // already applied or obsolete; never replay it
    if(const auto result=world_.colony_departures().find(plan->family.source);
       result!=world_.colony_departures().end() &&
       result->second.command_id==command_id)
        return std::nullopt;
    const auto it=world_.commands_.find(world_.tick()+1);
    if(it==world_.commands_.end() || it->second.size()!=1 ||
       it->second.front().id!=command_id ||
       it->second.front().action!=Action::HostColonyDeparture ||
       it->second.front().issuer!=Simulation::kServerAuthority ||
       it->second.front().actor!=plan->family.source ||
       it->second.front().target!=plan->family.receiver ||
       it->second.front().passenger_ids!=plan->family.adult_ids)
        throw std::runtime_error("supervisor plan not equal to only pending native command");
    return plan;
}

bool PersistentWorld::resume_autonomous_family_rescue(
    std::uint64_t command_id,FailPoint fail){
    auto pending=pending_autonomous_family_rescue(command_id);
    if(!pending)return false;
    // Independent existing atomic writer: one world snapshot, ART02
    // residence/guardian batch and ECON03 regional population receipt.
    step_with_atomic_family_departure(*pending,fail);
    if(!verified_atomic_family_departure(command_id))
        throw std::runtime_error("supervisor resumed evacuation without final custody proof");
    return true;
}

bool PersistentWorld::resume_next_pending_autonomous_rescue(FailPoint fail){
    sqlite3* raw=nullptr;
    if(sqlite3_open_v2(db_path_.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(raw)sqlite3_close(raw);
        throw std::runtime_error("cannot inspect NPC supervisor recoverable queue");
    }
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
    Statement table(db.get(),"SELECT 1 FROM sqlite_master WHERE type='table' "
        "AND name='sg_colony_npc_supervisor_plan'");
    if(!table.row())return false;
    Statement due(db.get(),"SELECT command_id FROM sg_colony_npc_supervisor_plan "
        "WHERE admission_generation=? ORDER BY command_id");
    due.i64(1,static_cast<sqlite3_int64>(generation_));
    if(!due.row())return false;
    const auto command_id=parse_u64(sqlite3_column_text(due.get(),0));
    if(due.row())
        throw std::runtime_error("two competing autonomous rescues at same world generation");
    return resume_autonomous_family_rescue(command_id,fail);
}

PersistentWorld::SupervisorTick PersistentWorld::supervise_autonomous_colony_emergency(
    const AutonomousSupervisorRoute& route,FailPoint fail){
    SupervisorTick result;
    result.command_id=route.command_id;
    if(!route.source || !route.family_seed || !route.intent_id ||
       !route.command_id || route.command_id==route.intent_id ||
       route.family_scope.empty() || route.region_scope.empty() ||
       route.origin_colony.empty() || route.destinations.empty() ||
       route.destinations.size()>32)
        throw std::invalid_argument("NPC supervisor requires trusted bounded region bindings");
    // A supervisor for one colony cannot inadvertently execute another
    // colony's durable rescue. Only the correct site owner may drive the
    // corresponding host-scheduled follow-up.
    if(const auto due=world_.commands_.find(world_.tick()+1);
       due!=world_.commands_.end()){
        for(const auto& accepted:due->second){
            if(accepted.action!=Action::HostColonyDeparture)continue;
            const auto recorded=store_->supervisor_plan(accepted.id);
            if(!recorded)continue;
            if(recorded->family.source!=route.source ||
               recorded->region_scope!=route.region_scope ||
               recorded->family.family_scope!=route.family_scope){
                result.status=SupervisorStatus::RescuePending;
                result.command_id=accepted.id;
                result.receiver=accepted.target;
                result.reason="a different colony owns the pending rescue; route-scoped execution refused";
                return result;
            }
        }
    }
    // Highest priority: finish an already-admitted rescue after restart.
    // The host does not need to keep the plan, leader or allocated event IDs.
    if(resume_next_pending_autonomous_rescue(fail)){
        result.status=SupervisorStatus::RescueCompleted;
        result.reason="recovered and verified already-admitted whole-family evacuation";
        const auto departure=world_.colony_departures().find(route.source);
        if(departure!=world_.colony_departures().end()){
            result.receiver=departure->second.receiver;
            result.command_id=departure->second.command_id;
        }
        return result;
    }
    if(const auto completed=world_.colony_departures().find(route.source);
       completed!=world_.colony_departures().end()){
        result.status=SupervisorStatus::RescueCompleted;
        result.command_id=completed->second.command_id;
        result.receiver=completed->second.receiver;
        result.reason="family already physically evacuated; no second rescue admitted";
        return result;
    }
    const auto response=colony_safety::plan_response(world_,route.source,route.source);
    if(response.risk!=colony_safety::Risk::Critical &&
       response.risk!=colony_safety::Risk::Emergency){
        result.status=SupervisorStatus::NoCurrentEmergency;
        result.reason="canonical life support not in emergency evacuation range";
        return result;
    }
    sqlite3* raw=nullptr;
    if(sqlite3_open_v2(db_path_.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(raw)sqlite3_close(raw);
        throw std::runtime_error("cannot read NPC emergency observation ledger");
    }
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
    Statement latest(db.get(),"SELECT id,new_risk FROM sg_colony_response_events "
        "WHERE site=? ORDER BY id DESC LIMIT 1");
    latest.i64(1,static_cast<sqlite3_int64>(route.source));
    if(!latest.row() ||
       sqlite3_column_int(latest.get(),1)!=static_cast<int>(response.risk)){
        result.status=SupervisorStatus::NoCurrentEmergency;
        result.reason="no current committed life-support threshold notification";
        return result;
    }
    result.notice_id=static_cast<std::uint64_t>(sqlite3_column_int64(latest.get(),0));
    Statement eligible(db.get(),"SELECT npc_id FROM sg_colony_npc_opportunities "
        "WHERE event_id=? AND site=? ORDER BY npc_id");
    eligible.i64(1,static_cast<sqlite3_int64>(result.notice_id));
    eligible.i64(2,static_cast<sqlite3_int64>(route.source));
    while(eligible.row()){
        const auto id=static_cast<std::uint64_t>(sqlite3_column_int64(eligible.get(),0));
        const auto person=world_.npc_records().find(id);
        if(person!=world_.npc_records().end() &&
           person->second.home_ship==route.source &&
           person->second.location==route.source &&
           !person->second.transport_id){
            result.leader_id=id;break;
        }
    }
    if(!result.leader_id){
        result.status=SupervisorStatus::NoLocalNpc;
        result.reason="no living, physically present NPC received a local alert opportunity";
        return result;
    }
    // This is the host's actual delivery step. A crash immediately after
    // acknowledgment is safe: the outbox remembers delivery, and the next
    // supervisor pass will select the same latest observable alert.
    acknowledge_colony_notification(result.notice_id,result.leader_id);
    AutonomousRescueRequest request{};
    request.source=route.source;
    request.leader_id=result.leader_id;
    request.observed_event_id=result.notice_id;
    request.family_seed=route.family_seed;
    request.intent_id=route.intent_id;
    request.command_id=route.command_id;
    request.family_scope=route.family_scope;
    request.region_scope=route.region_scope;
    request.origin_colony=route.origin_colony;
    request.destinations=route.destinations;
    request.member_event_ids=route.member_event_ids;
    const auto evaluated=assess_autonomous_colony_emergency(request);
    if(!evaluated.rescue_ready){
        result.status=SupervisorStatus::AwaitingPhysicalRescue;
        result.reason=evaluated.reason;
        // The original physical shortage and delivered-NPC outbox remain
        // durable; neither a shipment nor a person movement is fabricated.
        return result;
    }
    if(!submit_autonomous_colony_rescue(request,fail)){
        result.status=SupervisorStatus::AwaitingPhysicalRescue;
        result.reason="qualified recipient could not receive an admitted command";
        return result;
    }
    result.status=SupervisorStatus::RescueQueued;
    result.receiver=evaluated.selected_receiver;
    result.reason="supervisor queued immutable whole-family transfer; execution still pending";
    return result;
}

// AUTO-04D.5C1: re-use the native physical habitat stock transfer. The
// NPC receives a genuinely delivered LOCAL danger observation; its proposed
// supplies come only from the colony ship's already-existing inventory.
PersistentWorld::NpcLocalSupplyResult
PersistentWorld::supervise_npc_habitat_resupply(
    EntityId source,EntityId site,std::uint64_t command_id,FailPoint fail){
    NpcLocalSupplyResult result;
    result.command_id=command_id;
    if(!source || !site || source==site || !command_id)
        throw std::invalid_argument("NPC resupply requires host route and unique command ID");
    const auto* origin=world_.find(source);
    const auto* destination=world_.find(site);
    const auto* stocks=world_.life_support(source);
    const auto* habitat=world_.habitat(site);
    if(!origin || !destination || !stocks || !habitat ||
       habitat->source_ship!=source || origin->type!=EntityType::Ship ||
       destination->type!=EntityType::Station ||
       origin->owner_id!=destination->owner_id ||
       origin->system_id!=destination->system_id ||
       origin->owner_id==Simulation::kServerAuthority)
        throw std::invalid_argument("NPC resupply has no valid owned local habitat/ship");
    const auto dx=origin->position.x-destination->position.x;
    const auto dy=origin->position.y-destination->position.y;
    if(dx < -Simulation::kDockingRangeMm || dx>Simulation::kDockingRangeMm ||
       dy < -Simulation::kDockingRangeMm || dy>Simulation::kDockingRangeMm ||
       dx*dx+dy*dy>Simulation::kDockingRangeMm*Simulation::kDockingRangeMm){
        result.status=NpcLocalSupplyStatus::NoUsableStock;
        result.reason="no physical delivery access to threatened habitat";
        return result;
    }
    const auto risk=colony_safety::assess(world_,source,site);
    if(risk.population==0 ||
       static_cast<int>(risk.risk)<static_cast<int>(colony_safety::Risk::Critical)){
        result.reason="occupied habitat has no critical or emergency shortage";
        return result;
    }
    if(const auto it=world_.commands_.find(world_.tick()+1);
       it!=world_.commands_.end() && !it->second.empty()){
        result.status=NpcLocalSupplyStatus::NoUsableStock;
        result.reason="pending due world commands need reconciliation before emergency logistics";
        return result;
    }
    // Observations are bound to a real committed threshold crossing, NOT a
    // hazard inferred by the NPC or a fabricated notification.
    sqlite3* raw=nullptr;
    if(sqlite3_open_v2(db_path_.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(raw)sqlite3_close(raw);
        throw std::runtime_error("NPC supply cannot open local observation ledger");
    }
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
    Statement latest(db.get(),"SELECT id,new_risk FROM sg_colony_response_events "
        "WHERE site=? ORDER BY id DESC LIMIT 1");
    latest.i64(1,static_cast<sqlite3_int64>(site));
    if(!latest.row() ||
       sqlite3_column_int(latest.get(),1)!=static_cast<int>(risk.risk)){
        result.status=NpcLocalSupplyStatus::NoResponder;
        result.reason="no current locally committed critical shortage observation";
        return result;
    }
    result.notice_id=static_cast<std::uint64_t>(
        sqlite3_column_int64(latest.get(),0));
    Statement responders(db.get(),"SELECT npc_id FROM sg_colony_npc_opportunities "
        "WHERE event_id=? AND site=? ORDER BY npc_id");
    responders.i64(1,static_cast<sqlite3_int64>(result.notice_id));
    responders.i64(2,static_cast<sqlite3_int64>(site));
    while(responders.row()){
        const auto npc=static_cast<std::uint64_t>(
            sqlite3_column_int64(responders.get(),0));
        const auto it=world_.npc_records().find(npc);
        if(it!=world_.npc_records().end() &&
           it->second.home_ship==source &&
           it->second.location==site &&
           !it->second.transport_id && world_.npc_available_for_duty(npc)){
            result.npc_id=npc;break;
        }
    }
    if(!result.npc_id){
        result.status=NpcLocalSupplyStatus::NoResponder;
        result.reason="no available physically present NPC received a current local alert";
        return result;
    }
    // Real host delivery happens before execution. It survives interruption,
    // and a retry does not generate a second notification.
    acknowledge_colony_notification(result.notice_id,result.npc_id);
    std::array<std::pair<std::int64_t,int>,3> choices{{
        {risk.remaining.food_periods,1},
        {risk.remaining.water_periods,2},
        {risk.remaining.oxygen_periods,3}
    }};
    std::stable_sort(choices.begin(),choices.end());
    const auto source_amount=[&](int r){
        return r==1?stocks->food_kg:r==2?stocks->water_kg:stocks->oxygen_kg;
    };
    const auto destination_amount=[&](int r){
        return r==1?habitat->life.food_kg:r==2?
            habitat->life.water_kg:habitat->life.oxygen_kg;
    };
    for(const auto& [runway,selector]:choices){
        (void)runway;
        const auto required=static_cast<std::int64_t>(4*risk.population);
        const auto present=destination_amount(selector);
        const auto available=source_amount(selector);
        if(present>=required || available<=0)continue;
        const auto amount=std::min<std::int64_t>(
            {required-present,available,10000});
        if(amount<=0)continue;
        result.resource_selector=selector;
        result.amount_kg=amount;
        break;
    }
    if(!result.resource_selector){
        result.status=NpcLocalSupplyStatus::NoUsableStock;
        result.reason="no physically stock-backed local replenishment is possible";
        return result;
    }
    Command command{};
    command.id=command_id;command.sequence=command_id;
    command.execute_tick=world_.tick()+1;
    command.issuer=origin->owner_id;
    command.actor=source;command.target=site;
    command.action=Action::TransferHabitatStock;
    command.thrust_x=result.resource_selector;
    command.amount=result.amount_kg;
    auto next=world_;
    if(!next.submit(command)){
        result.status=NpcLocalSupplyStatus::NoUsableStock;
        result.reason="native authorization rejected emergency habitat stock transfer";
        return result;
    }
    const auto before_source=source_amount(result.resource_selector);
    const auto before_dest=destination_amount(result.resource_selector);
    const auto effect=[&](sqlite3* writer,std::uint64_t next_generation){
        Statement observed(writer,"SELECT o.risk,e.ship,a.delivery_generation "
            "FROM sg_colony_npc_opportunities o "
            "JOIN sg_colony_response_events e ON e.id=o.event_id "
            "JOIN sg_colony_npc_acks a ON a.event_id=o.event_id AND a.npc_id=o.npc_id "
            "WHERE o.event_id=? AND o.npc_id=? AND o.site=?");
        observed.i64(1,static_cast<sqlite3_int64>(result.notice_id));
        observed.i64(2,static_cast<sqlite3_int64>(result.npc_id));
        observed.i64(3,static_cast<sqlite3_int64>(site));
        if(!observed.row() ||
           sqlite3_column_int(observed.get(),0)!=static_cast<int>(risk.risk) ||
           sqlite3_column_int64(observed.get(),1)!=static_cast<sqlite3_int64>(source) ||
           sqlite3_column_int64(observed.get(),2)>static_cast<sqlite3_int64>(generation_) ||
           observed.row())
            throw std::runtime_error("NPC resupply has no actual delivered local warning");
        Statement newest(writer,"SELECT id FROM sg_colony_response_events "
            "WHERE site=? ORDER BY id DESC LIMIT 1");
        newest.i64(1,static_cast<sqlite3_int64>(site));
        if(!newest.row() ||
           sqlite3_column_int64(newest.get(),0)!=
               static_cast<sqlite3_int64>(result.notice_id) || newest.row())
            throw std::runtime_error("NPC attempted supply using obsolete hazard");
        run(writer,"CREATE TABLE IF NOT EXISTS sg_colony_npc_emergency_supply("
            "command_id TEXT PRIMARY KEY,generation INTEGER NOT NULL CHECK(generation>1),"
            "world_hash TEXT NOT NULL,npc_id TEXT NOT NULL,notice_id INTEGER NOT NULL,"
            "source_ship TEXT NOT NULL,habitat TEXT NOT NULL,"
            "resource INTEGER NOT NULL CHECK(resource BETWEEN 1 AND 3),"
            "amount_kg INTEGER NOT NULL CHECK(amount_kg>0 AND amount_kg<=10000),"
            "source_before_kg INTEGER NOT NULL,habitat_before_kg INTEGER NOT NULL,"
            "risk INTEGER NOT NULL CHECK(risk IN(2,3)))");
        run(writer,"CREATE TRIGGER IF NOT EXISTS sg_colony_npc_emergency_supply_no_update "
            "BEFORE UPDATE ON sg_colony_npc_emergency_supply "
            "BEGIN SELECT RAISE(ABORT,'immutable emergency stock order'); END");
        run(writer,"CREATE TRIGGER IF NOT EXISTS sg_colony_npc_emergency_supply_no_delete "
            "BEFORE DELETE ON sg_colony_npc_emergency_supply "
            "BEGIN SELECT RAISE(ABORT,'immutable emergency stock order'); END");
        Statement receipt(writer,"INSERT INTO sg_colony_npc_emergency_supply "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
        receipt.text(1,std::to_string(command_id));
        receipt.i64(2,static_cast<sqlite3_int64>(next_generation));
        receipt.text(3,std::to_string(next.state_hash()));
        receipt.text(4,std::to_string(result.npc_id));
        receipt.i64(5,static_cast<sqlite3_int64>(result.notice_id));
        receipt.text(6,std::to_string(source));
        receipt.text(7,std::to_string(site));
        receipt.i64(8,result.resource_selector);
        receipt.i64(9,result.amount_kg);
        receipt.i64(10,before_source);
        receipt.i64(11,before_dest);
        receipt.i64(12,static_cast<int>(risk.risk));
        receipt.execute();
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,&command,
                   {},{},{},fail,nullptr,nullptr,effect);
    world_.swap(next);
    ++generation_;
    result.status=NpcLocalSupplyStatus::Accepted;
    result.reason="real local inventory transfer admitted; native world step pending";
    return result;
}
bool PersistentWorld::verified_npc_habitat_resupply(std::uint64_t command_id) const{
    if(!command_id)return false;
    return store_->npc_habitat_supply_applied(command_id,generation_);
}

std::vector<colony_defense::Contact>
PersistentWorld::local_colony_watch_contacts(
    EntityId source,std::uint64_t npc) const{
    return colony_defense::observe_local_contacts(world_,source,npc);
}
colony_defense::Contact PersistentWorld::record_colony_watch_contact(
    std::uint64_t id,EntityId source,std::uint64_t npc,EntityId foreign){
    return store_->record_colony_watch_contact(
        world_,generation_,id,source,npc,foreign);
}

void PersistentWorld::establish_colony_conflict_agenda(
    std::uint64_t agenda_id,EntityId source,std::uint64_t leader,
    colony_conflict::Agenda agenda){
    store_->establish_colony_agenda(
        world_,generation_,agenda_id,source,leader,agenda);
}
colony_conflict::Decision PersistentWorld::assess_colony_conflict_intent(
    EntityId source,std::uint64_t contact_event_id) const{
    return store_->assess_colony_aggression(
        world_,generation_,source,contact_event_id);
}
colony_conflict::Decision PersistentWorld::record_colony_conflict_intent(
    std::uint64_t event,EntityId source,std::uint64_t contact_event){
    return store_->record_colony_aggression(
        world_,generation_,event,source,contact_event);
}

colony_conflict::RelationRevision
PersistentWorld::assess_colony_relationship_revision(
    EntityId source,std::uint64_t knowledge_id) const{
    return store_->assess_colony_relationship(
        world_,generation_,source,knowledge_id);
}
colony_conflict::RelationRevision
PersistentWorld::record_colony_relationship_revision(
    std::uint64_t event_id,EntityId source,std::uint64_t knowledge_id){
    return store_->record_colony_relationship(
        world_,generation_,event_id,source,knowledge_id);
}
colony_conflict::RelationRevision
PersistentWorld::colony_relationship(EntityId source,PlayerId owner) const{
    if(!source || !owner)
        throw std::invalid_argument("colony diplomacy needs source and foreign owner");
    return store_->latest_colony_relationship(source,owner);
}

std::optional<colony_conflict::ExitPressure>
PersistentWorld::assess_colony_exit_pressure(
    EntityId source,std::uint64_t npc_id) const{
    return store_->assess_colony_exit_pressure(
        world_,generation_,source,npc_id);
}
colony_conflict::ExitPressure PersistentWorld::record_colony_exit_pressure(
    std::uint64_t event_id,EntityId source,std::uint64_t npc_id){
    return store_->record_colony_exit_pressure(
        world_,generation_,event_id,source,npc_id);
}

std::optional<colony_conflict::PersonalOutlook>
PersistentWorld::assess_personal_outlook(
    EntityId source,std::uint64_t npc) const{
    return store_->assess_personal_outlook(world_,generation_,source,npc);
}
colony_conflict::PersonalOutlook PersistentWorld::record_personal_outlook(
    std::uint64_t event,EntityId source,std::uint64_t npc){
    return store_->record_personal_outlook(world_,generation_,event,source,npc);
}
std::optional<colony_conflict::PersonalOutlook>
PersistentWorld::colony_personal_outlook(
    EntityId source,std::uint64_t npc) const{
    if(generation_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("world generation exhausted");
    return store_->personal_outlook_before(source,npc,generation_+1);
}

PersistentWorld::VoluntaryImmigrationAdmission
PersistentWorld::record_voluntary_immigration_admission(
    std::uint64_t event,std::uint64_t pressure,
    EntityId origin,EntityId destination,EntityId ferry,
    std::uint64_t npc,std::uint64_t transport,PlayerId receiving_owner){
    return store_->record_voluntary_admission(
        world_,generation_,event,pressure,origin,destination,ferry,
        npc,transport,receiving_owner);
}

PersistentWorld::HostVoluntaryDepartureReport
PersistentWorld::supervise_voluntary_migration(
    const HostVoluntaryDepartureSchedule& schedule,FailPoint fail){
    HostVoluntaryDepartureReport out;
    out.origin=schedule.origin;out.tick=world_.tick();
    if(!schedule.origin || !schedule.scan_budget || schedule.scan_budget>32)
        throw std::invalid_argument("voluntary supervisor requires colony and scan budget 1..32");
    const auto* site=world_.find(schedule.origin);
    if(!site || !world_.colony_ship(schedule.origin) ||
       !site->owner_id || site->type!=EntityType::Ship)
        throw std::invalid_argument("voluntary supervisor source is not a physically owned colony");
    if(!store_->has_founded_governance(schedule.origin)){
        out.status=HostVoluntaryDepartureStatus::NoResidents;
        out.reason="this colony has not yet established a legitimate local governor";
        return out;
    }
    constexpr auto max_id=static_cast<std::uint64_t>(
        std::numeric_limits<sqlite3_int64>::max());
    if(schedule.personal_event_base &&
       schedule.personal_event_base>max_id-schedule.scan_budget)
        throw std::invalid_argument("host reserved a non-viable personal history event range");
    // The host calls us every committed tick. We only assess on a bounded,
    // deterministic cadence; restarting cannot secretly advance any NPC's
    // appraisal clock or duplicate a past current-generation observation.
    if(world_.tick()<8 || world_.tick()%8){
        out.status=HostVoluntaryDepartureStatus::NotDue;
        out.reason="individual observations are due every eight world ticks";
        return out;
    }
    std::vector<std::uint64_t> people;
    for(const auto& [id,npc]:world_.npc_records())
        if(npc.home_ship==schedule.origin && npc.location==schedule.origin &&
           !npc.transport_id)people.push_back(id);
    if(people.empty()){
        out.status=HostVoluntaryDepartureStatus::NoResidents;
        out.reason="there are no named physical residents to observe";
        return out;
    }
    const auto budget=std::min<std::size_t>(
        schedule.scan_budget,people.size());
    const auto start=((world_.tick()/8)%people.size() *
                      (schedule.scan_budget%people.size()))%people.size();
    std::uint64_t eligible=0;
    int eligible_priority=-1;
    for(std::size_t i=0;i<budget;++i){
        const auto npc=people[(start+i)%people.size()];
        ++out.examined;
        if(!store_->personal_outlook_recorded_at(
               schedule.origin,npc,generation_)){
            if(store_->assess_personal_outlook(
                   world_,generation_,schedule.origin,npc)){
                const auto event=schedule.personal_event_base?
                    schedule.personal_event_base+i:
                    store_->next_personal_outlook_event_id();
                record_personal_outlook(event,schedule.origin,npc);
                ++out.observed;
            }
        }
        const auto now=colony_personal_outlook(schedule.origin,npc);
        if(!now || !now->considering_departure)continue;
        ++out.considering_departure;
        // A decision first reached at this very generation must be
        // committed before it can authorize a future immigrant permit.
        const auto previous=store_->personal_outlook_before(
            schedule.origin,npc,generation_);
        if(previous && previous->considering_departure &&
           now->world_generation>=previous->world_generation){
            // Prioritize the most distressed and personally aggrieved,
            // not whichever NPC happened to come first in one cyclic batch.
            // Equal priorities rotate with the bounded round-robin scan.
            const int priority=now->discontent+
                (now->leader_rapport<0?10:0);
            if(priority>eligible_priority){
                eligible=npc;eligible_priority=priority;
            }
        }
    }
    if(!eligible){
        out.status=out.considering_departure?
            HostVoluntaryDepartureStatus::SeekingDestination:
            (out.observed?HostVoluntaryDepartureStatus::Observed:
                          HostVoluntaryDepartureStatus::NoEligibleMigrants);
        out.reason=out.considering_departure?
            "new intent must mature into a prior committed history generation":
            "none of the bounded physical residents has mature exit intention";
        return out;
    }
    out.selected_npc=eligible;
    if(!schedule.approved_offer){
        out.status=HostVoluntaryDepartureStatus::SeekingDestination;
        out.reason="adult wants to emigrate, but no authenticated receiving-owner offer exists";
        return out;
    }
    const auto& offer=*schedule.approved_offer;
    if(!offer.destination || !offer.ferry || !offer.receiving_owner ||
       !offer.transport_id || !offer.admission_event_id ||
       !offer.boarding_command_id)
        throw std::invalid_argument("host offer must reserve route, owner and durable identifiers");
    const auto* destination=world_.find(offer.destination);
    const auto* ferry=world_.find(offer.ferry);
    const auto* origin=site;
    const auto* old=world_.npc_records().count(eligible)?
        &world_.npc_records().at(eligible):nullptr;
    // Never create even a pending admission for an ungrounded ferry:
    // physical same-system boarding, capacity/receiving owner and vehicle
    // owner must already be real. The native passenger command does the
    // final authoritative safety admission before execution.
    if(!destination || !ferry || !old ||
       destination->owner_id!=offer.receiving_owner ||
       !world_.colony_ship(offer.destination) ||
       !world_.colony_ship(offer.ferry) ||
       ferry->system_id!=origin->system_id ||
       (ferry->owner_id!=origin->owner_id &&
        ferry->owner_id!=offer.receiving_owner) ||
       colony_safety::assess(
           world_,offer.destination,offer.destination).risk!=
           colony_safety::Risk::Safe ||
       store_->has_child_or_dependent_link(schedule.origin)){
        out.status=HostVoluntaryDepartureStatus::PhysicalRouteBlocked;
        out.reason="no real, safe, approved single-adult ferry route";
        return out;
    }
    const auto dx=ferry->position.x-origin->position.x;
    const auto dy=ferry->position.y-origin->position.y;
    const auto range=Simulation::kDockingRangeMm;
    if(dx < -range || dx>range || dy < -range || dy>range ||
       dx*dx+dy*dy>range*range){
        out.status=HostVoluntaryDepartureStatus::PhysicalRouteBlocked;
        out.reason="offered ferry is not physically within boarding range";
        return out;
    }
    const auto existing=store_->outstanding_voluntary_admission(
        schedule.origin,eligible);
    if(existing){
        out.admission_event_id=existing->event_id;
        if(existing->event_id!=offer.admission_event_id ||
           existing->destination!=offer.destination ||
           existing->ferry!=offer.ferry ||
           existing->receiving_owner!=offer.receiving_owner ||
           existing->transport_id!=offer.transport_id){
            out.status=HostVoluntaryDepartureStatus::ExistingAdmission;
            out.reason="this NPC has an unresolved different immigration admission";
            return out;
        }
    }else{
        // Deliberate scarcity can create a physical eligibility witness; a
        // satisfied resident or unsupported subjective grievance cannot
        // forge a new Critical/Emergency source event.
        auto pressure=store_->existing_exit_pressure(schedule.origin,eligible);
        if(!pressure){
            if(!store_->assess_colony_exit_pressure(
                   world_,generation_,schedule.origin,eligible)){
                out.status=HostVoluntaryDepartureStatus::PhysicalRouteBlocked;
                out.reason="resident lacks an authentic earlier shortage witness";
                return out;
            }
            if(!offer.pressure_event_id)
                throw std::invalid_argument(
                    "host must reserve an event ID for the first physical shortage witness");
            pressure=record_colony_exit_pressure(
                offer.pressure_event_id,schedule.origin,eligible).event_id;
        }
        const auto accepted=record_voluntary_immigration_admission(
            offer.admission_event_id,*pressure,schedule.origin,
            offer.destination,offer.ferry,eligible,offer.transport_id,
            offer.receiving_owner);
        out.admission_event_id=accepted.event_id;
    }
    out.status=HostVoluntaryDepartureStatus::AdmissionCreated;
    out.reason="receiving-owner admission is durable but boarding not yet queued";
    if(world_.has_command_id(offer.boarding_command_id)){
        out.status=HostVoluntaryDepartureStatus::ExistingAdmission;
        out.reason="the host command identifier is already reserved; never duplicate a voyage";
        return out;
    }
    Command board{};
    board.id=offer.boarding_command_id;
    board.sequence=offer.boarding_command_id;
    board.execute_tick=world_.tick()+1;
    board.issuer=Simulation::kServerAuthority;
    board.actor=offer.ferry;
    board.target=schedule.origin;
    board.action=Action::HostEmbarkPassengers;
    board.transport_id=offer.transport_id;
    board.passenger_ids={eligible};
    if(submit_voluntary_immigration_boarding(out.admission_event_id,board,fail)){
        out.status=HostVoluntaryDepartureStatus::BoardingQueued;
        out.reason="native host boarding command admitted; next physical world step executes it";
    }
    return out;
}

bool PersistentWorld::submit_voluntary_immigration_boarding(
    std::uint64_t admission_event,const Command& c,FailPoint fail){
    const auto a=store_->immigration_admission(admission_event);
    const auto person=world_.npc_records().find(a.npc_id);
    const auto* origin=world_.find(a.origin);
    const auto* destination=world_.find(a.destination);
    const auto* ferry=world_.find(a.ferry);
    if(a.world_generation>generation_ ||
       store_->verified_voluntary_immigration(a.event_id) ||
       !origin || !destination || !ferry ||
       origin->owner_id!=a.original_owner ||
       destination->owner_id!=a.receiving_owner ||
       ferry->system_id!=origin->system_id ||
       person==world_.npc_records().end() ||
       person->second.home_ship!=a.origin ||
       person->second.location!=a.origin ||
       person->second.transport_id ||
       !world_.npc_available_for_duty(a.npc_id) ||
       store_->has_child_or_dependent_link(a.origin) ||
       world_.voyage_status(a.transport_id)!=0 ||
       store_->effective_governance(a.origin,generation_).leader_id==a.npc_id ||
       !([&]{
           if(!a.personal_outlook_event_id){
               // An authentic pre-EXIT01C permit may finish boarding under its
               // original strict, still-critical source scarcity condition.
               return static_cast<int>(colony_safety::assess(
                   world_,a.origin,a.origin).risk)>=
                   static_cast<int>(colony_safety::Risk::Critical);
           }
           const auto approved=store_->personal_outlook_before(
               a.origin,a.npc_id,a.world_generation);
           // Boarding is a new prospective action: a same-generation
           // *already committed* recovery observation can veto it. The
           // original admission still references only prior-generation
           // personal history, preserving dated decisions without time travel.
           if(generation_==std::numeric_limits<std::uint64_t>::max())
               return false;
           const auto latest=store_->personal_outlook_before(
               a.origin,a.npc_id,generation_+1);
           return approved && latest &&
                  approved->event_id==a.personal_outlook_event_id &&
                  approved->considering_departure &&
                  latest->considering_departure;
       }()) ||
       colony_safety::assess(world_,a.destination,a.destination).risk!=
           colony_safety::Risk::Safe ||
       c.action!=Action::HostEmbarkPassengers ||
       c.issuer!=Simulation::kServerAuthority ||
       c.actor!=a.ferry || c.target!=a.origin ||
       c.transport_id!=a.transport_id ||
       c.passenger_ids!=std::vector<std::uint64_t>{a.npc_id} ||
       c.execute_tick!=world_.tick()+1)
        return false;
    return submit_host_passenger_action(c,fail);
}
void PersistentWorld::step_with_voluntary_immigration(
    std::uint64_t admission_event,const SocietyMigrationPlan& plan,FailPoint fail){
    if(!admission_event || plan.voluntary_admission_event_id)
        throw std::invalid_argument("voluntary migration must use exactly one admission");
    auto linked=plan;
    linked.voluntary_admission_event_id=admission_event;
    step_with_atomic_migration(linked,fail);
}
bool PersistentWorld::verified_voluntary_immigration(
    std::uint64_t admission_event) const{
    return store_->verified_voluntary_immigration(admission_event);
}

colony_conflict::GovernanceEpoch PersistentWorld::record_colony_governance_transition(
    std::uint64_t event,EntityId colony,std::uint64_t successor,
    colony_conflict::Agenda agenda,colony_conflict::DiplomaticPolicy policy,
    colony_conflict::TransitionKind kind,PlayerId approving_owner){
    return store_->record_governance_transition(
        world_,generation_,event,colony,successor,agenda,policy,kind,approving_owner);
}
colony_conflict::GovernanceEpoch PersistentWorld::colony_governance(
    EntityId colony) const{
    return store_->effective_governance(colony,generation_);
}

colony_conflict::Decision PersistentWorld::assess_colony_diplomatic_intent(
    EntityId source,std::uint64_t contact) const{
    return store_->assess_colony_diplomacy(world_,generation_,source,contact);
}
colony_conflict::Decision PersistentWorld::record_colony_diplomatic_intent(
    std::uint64_t event,EntityId source,std::uint64_t contact){
    return store_->record_colony_diplomacy(
        world_,generation_,event,source,contact);
}

// COLONY-AUTO-04D.4D2B2: admission only. No family member relocates until
// step_with_atomic_family_departure commits world, ART02 and ECON03 together.
bool PersistentWorld::submit_host_atomic_family_departure(
    const AtomicColonyFamilyPlan& p,FailPoint fail){
    if(p.family.expected_world_generation!=generation_ ||
       !p.command_id || p.command_id==p.family.intent_id)return false;
    const auto audit=store_->audit_colony_family_departure(world_,p.family);
    if(!audit.ready_for_transaction)return false;
    Command command{};
    command.id=p.command_id;command.sequence=p.command_id;
    command.execute_tick=world_.tick()+1;
    command.issuer=Simulation::kServerAuthority;
    command.actor=p.family.source;command.target=p.family.receiver;
    command.action=Action::HostColonyDeparture;
    command.passenger_ids=p.family.adult_ids;
    auto next=world_;
    if(!next.submit(command))return false;
    const auto effect=[&](sqlite3* db,std::uint64_t){
        verify_atomic_colony_family_preparation(db,world_,p);
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
    return true;
}

// The only publication point for a whole-family rescue. ART02 identity,
 // cold-owner population, world snapshot, applied command and immutable
// physical custody proof share the Store::commit BEGIN IMMEDIATE/COMMIT.
void PersistentWorld::step_with_atomic_family_departure(
    const AtomicColonyFamilyPlan& p,FailPoint fail){
    if(!p.family.expected_world_generation ||
       p.family.expected_world_generation+1!=generation_)
        throw std::runtime_error("stale family evacuation execution generation");
    auto next=world_;
    const auto first=next.events().size(),first_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& command:it->second)due.emplace(command.id,command);
    if(due.size()!=1 || !due.count(p.command_id))
        throw std::runtime_error("whole-family rescue needs exactly one due host command");
    const auto& command=due.at(p.command_id);
    if(command.issuer!=Simulation::kServerAuthority ||
       command.action!=Action::HostColonyDeparture ||
       command.actor!=p.family.source || command.target!=p.family.receiver ||
       command.passenger_ids!=p.family.adult_ids)
        throw std::runtime_error("family evacuation command/manifest mismatch");
    const auto source_npcs=world_.npc_records().size();
    const auto source_children=world_.physical_dependents().size();
    next.step();
    if(next.events().size()!=first+1 ||
       next.events().back().command_id!=p.command_id ||
       next.events().back().result!=Outcome::Applied ||
       next.events().back().transferred!=0 ||
       next.npc_records().size()!=source_npcs ||
       next.physical_dependents().size()!=source_children)
        throw std::runtime_error("native family transfer not atomically applied");
    const auto result=next.colony_departures().find(p.family.source);
    if(result==next.colony_departures().end() ||
       result->second.receiver!=p.family.receiver ||
       result->second.command_id!=p.command_id ||
       result->second.relocated_npcs!=p.family.adult_ids ||
       result->second.relocated_dependents.size()!=p.family.children.size())
        throw std::runtime_error("native family receipt missing or partial");
    std::vector<std::uint64_t> children;
    for(const auto& c:p.family.children){
        children.push_back(c.child_id);
        const auto d=next.physical_dependents().find(c.child_id);
        if(d==next.physical_dependents().end() ||
           d->second.guardian_id!=c.guardian_id ||
           d->second.home_ship!=p.family.receiver ||
           d->second.location!=p.family.receiver)
            throw std::runtime_error("native dependent did not arrive with guardian");
    }
    if(result->second.relocated_dependents!=children ||
       next.children_physically_aboard(p.family.source)!=0)
        throw std::runtime_error("world execution stranded or misplaced child");
    const auto events=std::vector<Event>(next.events().begin()+
        static_cast<std::ptrdiff_t>(first),next.events().end());
    const auto activity=std::vector<ActivityEvent>(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(first_activity),next.activity_events().end());
    const auto path=store_->sqlite_path();
    regional_events::CheckpointStore region(path,p.region_scope);
    const auto effect=[&](sqlite3* db,std::uint64_t committed_generation){
        verify_atomic_colony_family_preparation(db,world_,p);
        NpcAppearanceStore family(db,p.family.family_scope,p.family.family_seed,
            [&](std::uint64_t id){return next.npc_records().count(id)!=0;});
        const auto original=family.family_census(p.family.source);
        const auto receiver=family.family_census(p.family.receiver);
        if(original.revision!=p.expected_origin_family_revision ||
           receiver.revision!=p.expected_receiver_family_revision)
            throw std::runtime_error("family census changed during evacuation");
        std::vector<std::pair<std::uint64_t,std::uint64_t>> members;
        for(std::size_t i=0;i<p.family.adult_ids.size();++i)
            members.emplace_back(p.family.adult_ids[i],p.member_event_ids[i]);
        for(std::size_t i=0;i<p.family.children.size();++i)
            members.emplace_back(p.family.children[i].child_id,
                p.member_event_ids[p.family.adult_ids.size()+i]);
        const auto authorize=[&](auto id,auto origin,auto destination){
            if(origin!=p.family.source || destination!=p.family.receiver)return false;
            auto adult=world_.npc_records().find(id);
            if(adult!=world_.npc_records().end()){
                const auto after=next.npc_records().find(id);
                return std::binary_search(p.family.adult_ids.begin(),
                         p.family.adult_ids.end(),id) &&
                       adult->second.home_ship==origin &&
                       after!=next.npc_records().end() &&
                       after->second.home_ship==destination &&
                       after->second.location==destination &&
                       after->second.transport_id==0;
            }
            const auto before=world_.physical_dependents().find(id);
            const auto after=next.physical_dependents().find(id);
            if(before==world_.physical_dependents().end() ||
               after==next.physical_dependents().end())return false;
            const auto selected=std::find_if(p.family.children.begin(),
                p.family.children.end(),[&](const auto& c){return c.child_id==id;});
            return selected!=p.family.children.end() &&
                   selected->guardian_id==after->second.guardian_id &&
                   before->second.home_ship==origin &&
                   before->second.location==origin &&
                   after->second.home_ship==destination &&
                   after->second.location==destination &&
                   next.npc_records().at(after->second.guardian_id).home_ship==destination;
        };
        const auto moved=family.record_complete_colony_moves(members,
            p.family.source,p.family.receiver,next.tick(),
            p.expected_origin_family_revision,p.expected_receiver_family_revision,authorize);
        if(moved.size()!=members.size())
            throw std::runtime_error("ART02 household movement incomplete");
        const auto regional_revision=region.commit_migration_in_world_transaction(
            db,p.expected_region_revision,p.expected_owner_generation,next.tick(),
            p.origin_colony,p.receiver_colony,original.current_population,
            receiver.current_population,std::to_string(p.command_id),
            members.size(),"colony-family-evacuation-host");
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_family_evacuation("
            "command_id TEXT PRIMARY KEY,intent_id TEXT NOT NULL UNIQUE,"
            "world_generation INTEGER NOT NULL,tick INTEGER NOT NULL,"
            "family_scope TEXT NOT NULL,family_seed TEXT NOT NULL,"
            "region_scope TEXT NOT NULL,region_revision INTEGER NOT NULL,"
            "owner_generation INTEGER NOT NULL,source TEXT NOT NULL,"
            "receiver TEXT NOT NULL,origin_colony TEXT NOT NULL,"
            "receiver_colony TEXT NOT NULL,member_count INTEGER NOT NULL,"
            "adult_count INTEGER NOT NULL,child_count INTEGER NOT NULL)");
        run(db,"CREATE TABLE IF NOT EXISTS sg_colony_family_evacuation_member("
            "command_id TEXT NOT NULL,event_id TEXT NOT NULL UNIQUE,"
            "person_id TEXT NOT NULL,guardian_id TEXT NOT NULL,"
            "kind INTEGER NOT NULL CHECK(kind IN (0,1)),"
            "origin_revision INTEGER NOT NULL,destination_revision INTEGER NOT NULL,"
            "PRIMARY KEY(command_id,person_id))");
        for(const auto* tab:{"sg_colony_family_evacuation",
                             "sg_colony_family_evacuation_member"}){
            const auto table=std::string(tab);
            run(db,("CREATE TRIGGER IF NOT EXISTS "+table+"_immutable_update "
                "BEFORE UPDATE ON "+table+" BEGIN SELECT RAISE(ABORT,"
                "'family evacuation immutable'); END").c_str());
            run(db,("CREATE TRIGGER IF NOT EXISTS "+table+"_immutable_delete "
                "BEFORE DELETE ON "+table+" BEGIN SELECT RAISE(ABORT,"
                "'family evacuation immutable'); END").c_str());
        }
        Statement head(db,"INSERT INTO sg_colony_family_evacuation VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        head.text(1,std::to_string(p.command_id));
        head.text(2,std::to_string(p.family.intent_id));
        head.i64(3,static_cast<sqlite3_int64>(committed_generation));
        head.i64(4,static_cast<sqlite3_int64>(next.tick()));
        head.text(5,p.family.family_scope);
        head.text(6,std::to_string(p.family.family_seed));
        head.text(7,p.region_scope);
        head.i64(8,static_cast<sqlite3_int64>(regional_revision));
        head.i64(9,static_cast<sqlite3_int64>(p.expected_owner_generation));
        head.text(10,std::to_string(p.family.source));
        head.text(11,std::to_string(p.family.receiver));
        head.text(12,p.origin_colony);head.text(13,p.receiver_colony);
        head.i64(14,static_cast<sqlite3_int64>(members.size()));
        head.i64(15,static_cast<sqlite3_int64>(p.family.adult_ids.size()));
        head.i64(16,static_cast<sqlite3_int64>(p.family.children.size()));
        head.execute();
        for(std::size_t i=0;i<moved.size();++i){
            const auto child=i>=p.family.adult_ids.size();
            const auto guardian=child?
                p.family.children[i-p.family.adult_ids.size()].guardian_id:0;
            Statement member(db,"INSERT INTO sg_colony_family_evacuation_member "
                "VALUES(?,?,?,?,?,?,?)");
            member.text(1,std::to_string(p.command_id));
            member.text(2,std::to_string(moved[i].event_id));
            member.text(3,std::to_string(moved[i].npc_id));
            member.text(4,std::to_string(guardian));
            member.i64(5,child?1:0);
            member.i64(6,static_cast<sqlite3_int64>(moved[i].origin_revision));
            member.i64(7,static_cast<sqlite3_int64>(moved[i].destination_revision));
            member.execute();
        }
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,events,due,activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

bool PersistentWorld::verified_atomic_family_departure(std::uint64_t command_id) const{
    if(!command_id)return false;
    const auto found=std::find_if(world_.colony_departures().begin(),
        world_.colony_departures().end(),
        [&](const auto& row){return row.second.command_id==command_id;});
    if(found==world_.colony_departures().end() ||
       found->second.relocated_dependents.empty())return false;
    // COLONY-AUTO-04D.4D2C: a yes/no "evacuated" query must independently
    // certify its entire durable ledger, not just trust a mutable header.
    // Store::restore audits the source/destination snapshots, birth bridge,
    // every ART02 member and census revision, and the ECON03 receipt.
    // A removed child row or changed guardian MUST throw, never return true.
    Simulation reread(1);
    std::uint64_t persisted_generation=0;
    std::int64_t persisted_ore=0,persisted_credits=0;
    if(!store_->restore(reread,persisted_generation,persisted_ore,persisted_credits) ||
       persisted_generation!=generation_ || reread.state_hash()!=world_.state_hash())
        throw std::runtime_error("family receipt does not match authoritative world");
    store_->verify_physical_dependent_authority(reread);
    store_->verify_autonomous_dispatch(persisted_generation);
    store_->verify_supervisor_plans(persisted_generation);
    sqlite3* raw=nullptr;
    if(sqlite3_open_v2(db_path_.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(raw)sqlite3_close(raw);
        throw std::runtime_error("cannot verify family evacuation database");
    }
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
    Statement present(db.get(),"SELECT 1 FROM sqlite_master "
        "WHERE type='table' AND name='sg_colony_family_evacuation'");
    if(!present.row())return false;
    Statement witness(db.get(),"SELECT receiver,member_count,child_count "
        "FROM sg_colony_family_evacuation WHERE command_id=?");
    witness.text(1,std::to_string(command_id));
    if(!witness.row())return false;
    const auto receiver=parse_u64(sqlite3_column_text(witness.get(),0));
    const auto count=sqlite3_column_int64(witness.get(),1);
    const auto children=sqlite3_column_int64(witness.get(),2);
    if(witness.row())return false;
    return receiver==found->second.receiver &&
           count==static_cast<sqlite3_int64>(found->second.relocated_npcs.size()+
                found->second.relocated_dependents.size()) &&
           children==static_cast<sqlite3_int64>(found->second.relocated_dependents.size());
}

bool PersistentWorld::submit_host_emergency_ferry_rescue(
        const Command& command,FailPoint fail){
    // This gate is deliberately limited to EXISTING ferry passengers in one
    // active/stranded voyage. It does not move home-ship crew, colonists,
    // dependents, or create a transport/repair resource.
    if(command.action!=Action::HostEvacuatePassengers ||
       command.issuer!=Simulation::kServerAuthority ||
       !command.actor || !command.target || command.actor==command.target ||
       command.execute_tick!=world_.tick()+1 ||
       command.transport_id==0 || command.amount<=0 ||
       command.passenger_ids.empty() || command.passenger_ids.size()>16)
        return false;
    const auto voyage=world_.voyages_.find(command.transport_id);
    if(voyage==world_.voyages_.end() ||
       voyage->second.ferry!=command.actor ||
       (voyage->second.status!=1 && voyage->second.status!=3) ||
       voyage->second.passengers!=command.passenger_ids)
        return false;
    const auto* source=world_.colony_ship(command.actor);
    const auto* receiver=world_.colony_ship(command.target);
    const auto* provision=world_.life_support(command.target);
    const auto* dest=world_.find(command.target);
    if(!source||!receiver||!provision||!dest||
       dest->type!=EntityType::Ship || receiver->crew<=0)
        return false;
    // No emergency authority to evacuate a healthy ferry speculatively.
    const auto hazard=colony_safety::assess(world_,command.actor,command.actor);
    if(hazard.risk!=colony_safety::Risk::Critical &&
       hazard.risk!=colony_safety::Risk::Emergency)
        return false;
    // Recipient must carry physical provisions for at least TWO whole
    // consumption periods for its native crew, existing onboard passengers,
    // and every newly rescued passenger. Unfulfilled imports do not count.
    const auto currently_present=world_.passenger_needs_enabled()?
        static_cast<std::uint64_t>(world_.people_physically_aboard(command.target)):
        static_cast<std::uint64_t>(receiver->crew)+world_.passengers_aboard(command.target);
    const auto people=currently_present+
                      static_cast<std::uint64_t>(command.passenger_ids.size());
    if(people==0||people>1000 ||
       provision->food_kg<static_cast<std::int64_t>(2*people) ||
       provision->water_kg<static_cast<std::int64_t>(2*people) ||
       provision->oxygen_kg<static_cast<std::int64_t>(2*people))
        return false;
    // Full source passenger roster, exact seat capacity, route/ship proximity,
    // reserved voyage identity, child berth protection and custody validation
    // are all rechecked by existing native command admission.
    // Success here means SCHEDULED, never arrived or rescued until execution.
    return submit_host_passenger_action(command,fail);
}

bool PersistentWorld::submit_host_passenger_action(const Command& command, FailPoint fail){
    if(command.action!=Action::HostEmbarkPassengers && command.action!=Action::HostDisembarkPassengers &&
       command.action!=Action::HostStrandFerry && command.action!=Action::HostResumeFerry &&
       command.action!=Action::HostEvacuatePassengers &&
       command.action!=Action::HostDestroyPassengerFerry &&
       command.action!=Action::HostResolveMixedFerryLoss &&
       command.action!=Action::HostTrainMedic &&
       command.action!=Action::HostTriageRescued &&
       command.action!=Action::HostTreatInjuries &&
       command.action!=Action::HostScheduleMedicShift &&
       command.action!=Action::HostLoadMedicalKit &&
       command.action!=Action::HostMobileStabilize &&
       command.action!=Action::HostSettleMigrant)return false;
    if(command.issuer!=Simulation::kServerAuthority)return false;
    // v57: boarding is physical custody, so reserve a conservative TWO-cycle
    // runway at the *actual* receiving ship, for everyone currently present
    // and every newly boarding named passenger. Legacy replay is unchanged.
    if(world_.passenger_needs_enabled() &&
       (command.action==Action::HostEmbarkPassengers ||
        command.action==Action::HostEvacuatePassengers)){
        const auto receiving=command.action==Action::HostEmbarkPassengers ?
                             command.actor:command.target;
        const auto* life=world_.life_support(receiving);
        const auto present=world_.people_physically_aboard(receiving);
        if(!life || command.passenger_ids.empty() || present>1000 ||
           command.passenger_ids.size()>1000-present)return false;
        const auto count=static_cast<std::int64_t>(present+command.passenger_ids.size());
        if(life->food_kg<2*count || life->water_kg<2*count ||
           life->oxygen_kg<2*count)return false;
    }
    // Child physical berths count against the same actual 16 seats. Host-only
    // incident handlers must not destroy/teleport a ferry full of dependents.
    if(command.action==Action::HostEmbarkPassengers &&
       store_->active_dependent_berths(command.actor)>0)return false;
    if((command.action==Action::HostEvacuatePassengers ||
        command.action==Action::HostResolveMixedFerryLoss ||
        command.action==Action::HostDestroyPassengerFerry) &&
       store_->active_dependent_berths(command.actor)>0)return false;
    auto next=world_;
    if(!next.submit(command))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&command,{},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}


bool PersistentWorld::submit_host_consigned_transfer(const Command& command,FailPoint fail){
    if(command.action!=Action::HostTransferConsignedStock ||
       command.issuer!=Simulation::kServerAuthority ||
       !store_->manifest_transfer_allowed(command))return false;
    auto next=world_;
    if(!next.submit(command))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&command,{},{},{},fail);
    world_.swap(next);++generation_;return true;
}

bool PersistentWorld::submit_testing_ship_confusion(
    EntityId source,EntityId target,std::uint64_t effect_id,
    std::uint64_t random_seed,std::uint64_t duration_ticks,
    std::uint32_t pulse_ticks,std::uint8_t intensity,
    std::uint8_t resistance,std::uint8_t exposed_hardpoint_mask,
    FailPoint fail) {
    // TEST-ONLY transition. No real mounted hacking hardware exists yet.
    // As with ordinary host world changes, a failed SQLite commit leaves
    // in-memory state unchanged. No second status database or free hardware.
    auto next=world_;
    if(!next.install_testing_confusion(source,target,effect_id,random_seed,
            duration_ticks,pulse_ticks,intensity,resistance,
            exposed_hardpoint_mask))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,{},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::submit(const Command& command, FailPoint fail) {
    return submit_with_cell_worker(command,nullptr,fail);
}
bool PersistentWorld::submit_as_cell_worker(
        const Command& command,const NativeCellWorkerTick& token,
        FailPoint fail) {
    return submit_with_cell_worker(command,&token,fail);
}
bool PersistentWorld::submit_with_cell_worker(
        const Command& command,const NativeCellWorkerTick* token,
        FailPoint fail) {
    // This identity space is reserved for host-generated NPC flight commands.
    // An outside player/worker cannot create the corresponding native receipt.
    if((command.id & (std::uint64_t{1}<<61)) &&
       !(command.id & (std::uint64_t{1}<<62)))return false;
    // Physical command actor must already inhabit the worker's genuine
    // original cell. Target range remains owned by original Simulation;
    // a scope string is never a shortcut for player/crew authorization.
    if(token){
        const auto* actor=world_.find(command.actor);
        const auto cell=spatial::ship_cell(world_,command.actor);
        if(!actor || actor->type!=EntityType::Ship || !cell ||
           spatial::scope(*cell)!=token->canonical_cell_scope)
            throw std::runtime_error("worker cannot submit outside its original cell");
    }
    // NPC-RESP-02B: independent character custody is authoritative for
    // PLAYER-operated ship controls. A rescued/detained person cannot
    // remotely pilot an abandoned hull by changing the selected cockpit.
    // The native NPC navigator submits its own original controls through
    // Simulation::submit inside the host-journaled step, unaffected here.
    if(command.action==Action::Thrust ||
       command.action==Action::Mine ||
       command.action==Action::StartMining ||
       command.action==Action::StopMining ||
       command.action==Action::RequestDock ||
       command.action==Action::RequestUndock ||
       command.action==Action::RequestJump ||
       command.action==Action::FireTrainingWeapon ||
       command.action==Action::SetHardpointPower ||
       command.action==Action::SetCyberIsolation ||
       command.action==Action::ConfigureCyberModule ||
       command.action==Action::HackConfuse ||
       command.action==Action::SalvageWreck){
        const auto person=world_.player_people_.find(command.issuer);
        if(person!=world_.player_people_.end() &&
           (person->second.state!=PlayerPersonState::AboardOwnShip ||
            person->second.physical_ship!=command.actor))
            return false;
    }
    // A player command cannot race an active NPC navigator's exclusive
    // physical ship-control session. The NPC uses Simulation::submit
    // INSIDE the same host-journaled step, not this external owner API.
    if(command.action==Action::Thrust ||
       command.action==Action::RequestDock ||
       command.action==Action::RequestUndock ||
       command.action==Action::SetHardpointPower ||
       command.action==Action::SetCyberIsolation ||
       command.action==Action::ConfigureCyberModule ||
       command.action==Action::HackConfuse ||
       command.action==Action::RequestJump){
        for(const auto& [id,flight]:world_.npc_flight_orders_){
            (void)id;
            if(flight.ship==command.actor &&
               (flight.phase==NpcFlightPhase::Planned ||
                flight.phase==NpcFlightPhase::InTransit))
                return false;
        }
    }
    if (command.action == Action::HostDestroyShip || command.action == Action::HostCrewLoss ||
        command.action == Action::SetGatePairOnline ||
        command.action == Action::SetGateStaff ||
        command.action == Action::DeliverGateSpares ||
        command.issuer == Simulation::kServerAuthority) return false;
    if(!store_->manifest_transfer_allowed(command))return false;
    auto next = world_;
    if (!next.submit(command)) return false;
    store_->commit(next, generation_, resource_origin_, credit_origin_,
                   &command, {}, {}, {}, fail,nullptr,nullptr,{},token);
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
bool PersistentWorld::submit_npc_cyber_attack(
    PlayerId owner,EntityId attacking_ship,std::uint64_t pilot_npc,
    std::uint64_t observer_npc,EntityId target,
    std::uint64_t command_id,FailPoint fail) {
    if(!owner || owner==Simulation::kServerAuthority ||
       !attacking_ship || !target || attacking_ship==target ||
       !pilot_npc || !observer_npc || !command_id ||
       command_id>=(std::uint64_t{1}<<62) ||
       store_->npc_busy_this_tick(pilot_npc,world_.tick()) ||
       store_->npc_busy_this_tick(observer_npc,world_.tick()))
        return false;
    const auto* ship=world_.find(attacking_ship);
    const auto* opponent=world_.find(target);
    const auto& crew=world_.npc_records();
    const auto p=crew.find(pilot_npc),w=crew.find(observer_npc);
    if(!ship || !opponent || ship->type!=EntityType::Ship ||
       opponent->type!=EntityType::Ship ||
       ship->owner_id!=owner || opponent->owner_id==owner ||
       !world_.colony_ship(attacking_ship) ||
       !world_.passenger_needs_enabled() ||
       !ship->fitting.intrusion_emitter() ||
       p==crew.end() || w==crew.end() ||
       p->second.home_ship!=attacking_ship ||
       p->second.location!=attacking_ship ||
       p->second.role!=NpcRole::Ship ||
       p->second.transport_id ||
       w->second.home_ship!=attacking_ship ||
       w->second.location!=attacking_ship ||
       w->second.transport_id ||
       !world_.npc_available_for_duty(pilot_npc) ||
       !world_.npc_available_for_duty(observer_npc))
        return false;
    // The original v102 admission receipt also reserves the named real
    // operator for this tick. It prevents two cyber requests (or two
    // observers acting as duplicate controllers) from scheduling the same
    // physical NPC or ship concurrently, including after a host restart.
    for(const auto& [id,prior]:world_.npc_cyber_operators()){
        (void)id;
        if(prior.admitted_tick==world_.tick() &&
           (prior.attacking_ship==attacking_ship ||
            prior.pilot_npc==pilot_npc ||
            prior.observer_npc==observer_npc ||
            prior.pilot_npc==observer_npc ||
            prior.observer_npc==pilot_npc))
            return false;
    }
    // A tactical operator cannot steal an NPC navigator's exclusive
    // control of a physically moving ship.
    for(const auto& [id,flight]:world_.npc_flight_orders()){
        (void)id;
        if(flight.ship==attacking_ship &&
           (flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit))return false;
    }
    // Recompute a fresh original named-NPC sensory observation. A supplied
    // target ID or an old territorial complaint is not a verified contact.
    const auto observed=local_colony_watch_contacts(attacking_ship,observer_npc);
    const auto found=std::find_if(observed.begin(),observed.end(),
        [&](const colony_defense::Contact& c){
            return c.source==attacking_ship && c.foreign_ship==target &&
                   c.foreign_owner==opponent->owner_id &&
                   c.observer_npc==observer_npc &&
                   c.world_tick==world_.tick();
        });
    if(found==observed.end())return false;
    Command command{};
    command.id=command_id;
    command.sequence=command_id;
    command.execute_tick=world_.tick()+1;
    command.issuer=owner;
    command.actor=attacking_ship;
    command.target=target;
    command.action=Action::HackConfuse;
    auto next=world_;
    if(!next.submit(command) ||
       next.npc_cyber_operators_.size()>=10000 ||
       next.npc_cyber_operators_.count(command_id))return false;
    next.npc_cyber_operators_.emplace(command_id,NpcCyberOperator{
        command_id,owner,attacking_ship,target,pilot_npc,observer_npc,
        world_.tick()});
    next.snapshot_version_=std::max(next.snapshot_version_,102);
    // The pilot/observer provenance and native command commit in the
    // SAME original SQLite generation, never in an orphan side journal.
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail);
    world_.swap(next);
    ++generation_;
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
void PersistentWorld::step_with_atomic_society(const SocietyAtomicPlan& plan,FailPoint fail){
    if(plan.expected_world_generation!=generation_ || !plan.command_id ||
       plan.family_scope.empty() || plan.region_scope.empty() ||
       !plan.expected_region_revision || !plan.expected_owner_generation ||
       plan.deaths.empty())throw std::invalid_argument("stale or incomplete society atomic plan");
    auto next=world_;
    const auto before=next.events().size(), before_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(const auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    // Restrict to exactly one scheduled terminal host action; no accompanying
    // commands may unknowingly change additional NPC identities in this tick.
    if(due.size()!=1 || !due.count(plan.command_id) ||
       due.at(plan.command_id).issuer!=Simulation::kServerAuthority ||
       (due.at(plan.command_id).action!=Action::HostResolveMixedFerryLoss &&
        due.at(plan.command_id).action!=Action::HostDestroyPassengerFerry))
        throw std::invalid_argument("society commit requires one host ferry-loss command");
    next.step();
    const std::vector<Event> delta(next.events().begin()+static_cast<std::ptrdiff_t>(before),next.events().end());
    if(delta.size()!=1 || delta[0].command_id!=plan.command_id || delta[0].result!=Outcome::Applied)
        throw std::runtime_error("ferry-loss command did not apply successfully");
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+static_cast<std::ptrdiff_t>(before_activity),next.activity_events().end());
    std::set<std::uint64_t> newly_dead, requested;
    std::map<std::string,std::uint64_t> reductions;
    for(const auto& [id,death]:next.npc_casualties())
        if(world_.npc_casualties().count(id)==0)newly_dead.insert(id);
    for(const auto& d:plan.deaths){
        if(!d.event_id || !d.npc_id || !d.home_ship || d.regional_colony.empty() ||
           !requested.insert(d.npc_id).second)
            throw std::invalid_argument("invalid/duplicated society death manifest");
        const auto found=next.npc_casualties().find(d.npc_id);
        if(found==next.npc_casualties().end() || found->second.death_tick!=next.tick() ||
           found->second.identity.home_ship!=d.home_ship)
            throw std::runtime_error("society manifest disagrees with world casualty");
        ++reductions[d.regional_colony];
    }
    if(requested!=newly_dead)throw std::runtime_error("society manifest must cover every new casualty");
    // Bound all regional names to their real home-ship identities before taking
    // the world writer lock. The CAS checks below repeat this within the write
    // transaction, so no caller may redirect a death into another colony.
    const auto path=store_->sqlite_path();
    regional_events::CheckpointStore region(path,plan.region_scope);
    NpcAppearanceStore preflight_family(path,plan.family_scope,plan.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    std::map<EntityId,std::pair<std::string,std::uint64_t>> homes;
    std::set<std::string> used_regions;
    for(const auto& d:plan.deaths){
        auto [it,added]=homes.emplace(d.home_ship,
            std::make_pair(d.regional_colony,d.expected_family_revision));
        if(!added && (it->second.first!=d.regional_colony ||
                       it->second.second!=d.expected_family_revision))
            throw std::runtime_error("ambiguous society home to regional colony mapping");
    }
    std::map<std::string,std::uint64_t> initial_populations;
    for(const auto& [home,binding]:homes){
        if(!used_regions.insert(binding.first).second)
            throw std::runtime_error("two home colonies mapped to one regional checkpoint");
        const auto proof=reconcile_medical_society(*this,preflight_family,path,
            plan.family_scope,home,region,binding.first,
            plan.expected_region_revision,plan.expected_world_generation);
        if(!proof.reconciled || proof.family_revision!=binding.second)
            throw std::runtime_error("society preflight family revision/census refused");
        initial_populations.emplace(binding.first,proof.family_population);
    }
    const auto effect=[&](sqlite3* db,std::uint64_t committed_generation){
        NpcAppearanceStore family(db,plan.family_scope,plan.family_seed,
            [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
        std::map<EntityId,std::uint64_t> revs;
        for(const auto& d:plan.deaths){
            auto [it,inserted]=revs.emplace(d.home_ship,d.expected_family_revision);
            if(!inserted && d.expected_family_revision!=it->second)
                throw std::runtime_error("conflicting family revision for one home ship");
        }
        // Receipt order is canonical independent of caller vector order.
        auto ordered=plan.deaths;
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){
            return std::tie(a.home_ship,a.npc_id)<std::tie(b.home_ship,b.npc_id);
        });
        for(const auto& d:ordered){
            auto& rev=revs.at(d.home_ship);
            const auto death=family.record_death(d.event_id,d.npc_id,d.home_ship,next.tick(),rev,
                [&](auto home,auto id,auto tick){
                    const auto found=next.npc_casualties().find(id);
                    return found!=next.npc_casualties().end() &&
                        found->second.identity.home_ship==home && found->second.death_tick==tick;
                });
            if(death.census_revision!=rev+1)throw std::runtime_error("family death revision did not advance");
            ++rev;
        }
        const auto regional_rev=region.commit_casualties_in_world_transaction(
            db,plan.expected_region_revision,plan.expected_owner_generation,
            next.tick(),reductions,initial_populations,std::to_string(plan.command_id));
        run(db,"CREATE TABLE IF NOT EXISTS sg_med_society_effect ("
               "command_id TEXT PRIMARY KEY, world_generation INTEGER NOT NULL,"
               "tick INTEGER NOT NULL, family_scope TEXT NOT NULL,"
               "region_scope TEXT NOT NULL, region_revision INTEGER NOT NULL,"
               "owner_generation INTEGER NOT NULL, casualty_count INTEGER NOT NULL)");
        Statement receipt(db,"INSERT INTO sg_med_society_effect VALUES (?,?,?,?,?,?,?,?)");
        receipt.text(1,std::to_string(plan.command_id));
        receipt.i64(2,static_cast<std::int64_t>(committed_generation));
        receipt.i64(3,static_cast<std::int64_t>(next.tick()));
        receipt.text(4,plan.family_scope);receipt.text(5,plan.region_scope);
        receipt.i64(6,static_cast<std::int64_t>(regional_rev));
        receipt.i64(7,static_cast<std::int64_t>(plan.expected_owner_generation));
        receipt.i64(8,static_cast<std::int64_t>(requested.size()));
        receipt.execute();
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,delta,due,activity,
                   fail,nullptr,nullptr,effect);
    world_.swap(next);
    ++generation_;
}

void PersistentWorld::step_with_atomic_dependent_birth(
    const SocietyBirthPlan& plan,FailPoint fail){
    constexpr std::uint64_t reserved=(1ULL<<62);
    if(!plan.expected_world_generation || plan.expected_world_generation!=generation_ ||
       !plan.expected_region_revision || !plan.expected_owner_generation ||
       !plan.event_id || !plan.home_ship || !plan.parent_a || !plan.parent_b ||
       plan.parent_a==plan.parent_b || plan.child_id<reserved ||
       plan.family_scope.empty() || plan.region_scope.empty() ||
       plan.regional_colony.empty() ||
       plan.event_id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("incomplete or stale atomic dependent birth");
    const auto& existing=world_.npc_records();
    for(auto parent:{plan.parent_a,plan.parent_b}){
        const auto it=existing.find(parent);
        if(it==existing.end() || it->second.home_ship!=plan.home_ship ||
           it->second.location!=plan.home_ship || it->second.transport_id!=0 ||
           world_.npc_casualties().count(parent)!=0)
            throw std::runtime_error("birth parents not physically present in home settlement");
    }
    if(!world_.colony_ship(plan.home_ship) ||
       existing.count(plan.child_id) || world_.npc_casualties().count(plan.child_id))
        throw std::runtime_error("birth settlement or child identity invalid");
    // No pending game command can be smuggled into this cohort-only world step.
    auto next=world_;
    const auto pending=next.commands_.find(next.tick()+1);
    if(pending!=next.commands_.end() && !pending->second.empty())
        throw std::runtime_error("birth world step requires no pending commands");
    const auto before=next.events().size(), activity_before=next.activity_events().size();
    next.step();
    if(next.events().size()!=before)
        throw std::runtime_error("birth step unexpectedly executed a command");
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(activity_before),next.activity_events().end());
    const auto path=store_->sqlite_path();
    regional_events::CheckpointStore region(path,plan.region_scope);
    NpcAppearanceStore family(path,plan.family_scope,plan.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    const auto projection=reconcile_medical_society(*this,family,path,plan.family_scope,
        plan.home_ship,region,plan.regional_colony,plan.expected_region_revision,
        plan.expected_world_generation);
    if(!projection.reconciled || projection.family_revision!=plan.expected_family_revision ||
       family.exists(plan.child_id))
        throw std::runtime_error("birth preflight family revision/child identity refused");
    const auto effect=[&](sqlite3* db,std::uint64_t committed_generation){
        NpcAppearanceStore joined(db,plan.family_scope,plan.family_seed,
            [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
        const auto birth=joined.record_birth(plan.event_id,plan.child_id,
            plan.parent_a,plan.parent_b,plan.home_ship,next.tick(),
            plan.expected_family_revision,[&](auto home,auto a,auto b,auto tick){
                return home==plan.home_ship && tick==next.tick() &&
                    existing.at(a).home_ship==home && existing.at(b).home_ship==home &&
                    existing.at(a).location==home && existing.at(b).location==home;
            });
        if(birth.census_revision!=plan.expected_family_revision+1 ||
           birth.child_id!=plan.child_id)
            throw std::runtime_error("birth family receipt refused");
        const auto new_revision=region.commit_birth_in_world_transaction(
            db,plan.expected_region_revision,plan.expected_owner_generation,
            next.tick(),plan.regional_colony,projection.family_population,
            std::to_string(plan.event_id));
        run(db,"CREATE TABLE IF NOT EXISTS sg_med_society_birth ("
               "event_id TEXT PRIMARY KEY, world_generation INTEGER NOT NULL,"
               "tick INTEGER NOT NULL, family_scope TEXT NOT NULL,"
               "region_scope TEXT NOT NULL, region_revision INTEGER NOT NULL,"
               "owner_generation INTEGER NOT NULL, home_ship TEXT NOT NULL,"
               "regional_colony TEXT NOT NULL, child_id TEXT NOT NULL UNIQUE,"
               "parent_a TEXT NOT NULL, parent_b TEXT NOT NULL,"
               "family_revision INTEGER NOT NULL)");
        Statement rec(db,"INSERT INTO sg_med_society_birth VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
        rec.text(1,std::to_string(plan.event_id));
        rec.i64(2,static_cast<std::int64_t>(committed_generation));
        rec.i64(3,static_cast<std::int64_t>(next.tick()));
        rec.text(4,plan.family_scope);rec.text(5,plan.region_scope);
        rec.i64(6,static_cast<std::int64_t>(new_revision));
        rec.i64(7,static_cast<std::int64_t>(plan.expected_owner_generation));
        rec.text(8,std::to_string(plan.home_ship));rec.text(9,plan.regional_colony);
        rec.text(10,std::to_string(plan.child_id));
        rec.text(11,std::to_string(birth.parent_a));rec.text(12,std::to_string(birth.parent_b));
        rec.i64(13,static_cast<std::int64_t>(birth.census_revision));rec.execute();
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,{},
                   {},activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

void PersistentWorld::step_with_atomic_migration(const SocietyMigrationPlan& plan,FailPoint fail){
    if(!plan.command_id || !plan.npc_id || !plan.transport_id ||
       !plan.expected_world_generation || plan.expected_world_generation!=generation_ ||
       !plan.expected_region_revision || !plan.expected_owner_generation ||
       !plan.origin || !plan.destination || plan.origin==plan.destination ||
       plan.origin_colony.empty() || plan.destination_colony.empty() ||
       plan.origin_colony==plan.destination_colony ||
       plan.family_scope.empty() || plan.region_scope.empty())
        throw std::invalid_argument("stale or incomplete host migration plan");
    // Typed endpoints prevent a raw numeric surface ID from being mistaken
    // for a colony ship. The existing census bridge cannot yet account for
    // planetary residences; reject rather than corrupt family population.
    if(plan.origin_kind!=transport::LocationKind::ColonyShip ||
       plan.destination_kind!=transport::LocationKind::ColonyShip)
        throw std::invalid_argument(
            "planetary migration requires typed regional/census residency support");
    if(plan.voluntary_admission_event_id)
        store_->validate_voluntary_settlement(world_,generation_,plan);
    else if(store_->immigration_pending_for_transport(plan.npc_id,plan.transport_id))
        throw std::runtime_error("admitted NPC immigration must use voluntary settlement receipt");
    auto next=world_;
    const auto before=next.events().size(), prior_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1 || !due.count(plan.command_id))
        throw std::runtime_error("migration must execute exactly one host command");
    const auto& command=due.at(plan.command_id);
    if(command.issuer!=Simulation::kServerAuthority ||
       command.action!=Action::HostSettleMigrant ||
       command.actor!=plan.origin || command.target!=plan.destination ||
       command.transport_id!=plan.transport_id ||
       command.passenger_ids!=(plan.co_parent_id ?
           std::vector<std::uint64_t>{plan.npc_id,plan.co_parent_id}:
           std::vector<std::uint64_t>{plan.npc_id}) ||
       (plan.co_parent_id && (plan.co_parent_id<=plan.npc_id ||
           !plan.co_parent_event_id || plan.co_parent_event_id==plan.command_id)) ||
       (!plan.co_parent_id && plan.co_parent_event_id))
        throw std::runtime_error("migration plan does not match authenticated passenger command");
    // Resolve from the live authoritative Simulation, not a caller-created
    // ArrivalEvidence: the same identity contract covers ferries and shuttles.
    const auto arrival=world_.npc_transport_arrival(plan.transport_id,plan.npc_id);
    if(!arrival || !transport::completed_transport(*arrival,plan.npc_id) ||
       arrival->source().id!=plan.origin ||
       arrival->destination().id!=plan.destination)
        throw std::runtime_error("migration lacks authenticated completed NPC transport");
    // Existing HostSettleMigrant still mutates only colony-ship endpoints.
    // Never route a planetary arrival into this legacy ship writer.
    if(arrival->mode()!=transport::Mode::Ferry ||
       arrival->source().kind!=transport::LocationKind::ColonyShip ||
       arrival->destination().kind!=transport::LocationKind::ColonyShip)
        throw std::runtime_error("non-ship endpoint needs universal residency transaction");
    const auto old=world_.npc_records().find(plan.npc_id);
    if(old==world_.npc_records().end() || old->second.home_ship!=plan.origin ||
       old->second.location!=plan.destination || old->second.transport_id!=0)
        throw std::runtime_error("migration requires already completed physical disembarkation");
    const auto path=store_->sqlite_path();
    regional_events::CheckpointStore region(path,plan.region_scope);
    NpcAppearanceStore family(path,plan.family_scope,plan.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    const auto src=reconcile_medical_society(*this,family,path,plan.family_scope,
        plan.origin,region,plan.origin_colony,plan.expected_region_revision,generation_);
    const auto dst=reconcile_medical_society(*this,family,path,plan.family_scope,
        plan.destination,region,plan.destination_colony,plan.expected_region_revision,generation_);
    if(!src.reconciled || !dst.reconciled ||
       src.family_revision!=plan.expected_origin_family_revision ||
       dst.family_revision!=plan.expected_destination_family_revision ||
       family.current_residence(plan.npc_id)!=plan.origin ||
       src.family_population==0)
        throw std::runtime_error("migration family or regional population preflight failed");
    // The same admission contract now runs inside the existing real SQLite
    // migration path. All values are derived from trusted host state. Legacy
    // v41 ship-to-ship migrations retain their historical crew-capacity and
    // support semantics; new planetary settlements must reserve real housing
    // and life support before entering the universal writer.
    migration::Admission admission{};
    admission.command_id=plan.command_id;
    admission.npc_id=plan.npc_id;
    admission.origin={plan.origin_kind,plan.origin};
    admission.destination={plan.destination_kind,plan.destination};
    admission.expected_generation=plan.expected_world_generation;
    admission.current_generation=generation_;
    admission.physical_custody_at_destination=
        old->second.location==plan.destination && !old->second.transport_id;
    admission.household_reconciled=src.reconciled && dst.reconciled &&
        family.current_residence(plan.npc_id)==plan.origin;
    admission.consent_required=plan.voluntary_admission_event_id!=0;
    admission.verified_consent=plan.voluntary_admission_event_id!=0;
    admission.housing_required=false; // grandfathered colony crew limit
    admission.life_support_required=false; // legacy migration baseline
    admission.no_competing_residence=
        old->second.assigned_site==plan.origin;
    if(migration::preflight(admission,*arrival)!=migration::Denial::None)
        throw std::runtime_error("universal migration admission rejected authenticated host move");
    const auto initial_named=world_.npc_records().size();
    if(plan.co_parent_id){
        const auto co=world_.npc_records().find(plan.co_parent_id);
        const auto coparent_arrival=world_.npc_transport_arrival(
            plan.transport_id,plan.co_parent_id);
        if(co==world_.npc_records().end() ||
           co->second.home_ship!=plan.origin ||
           co->second.assigned_site!=plan.origin ||
           co->second.location!=plan.destination || co->second.transport_id ||
           family.current_residence(plan.co_parent_id)!=plan.origin ||
           !coparent_arrival ||
           coparent_arrival->mode()!=arrival->mode() ||
           coparent_arrival->source()!=arrival->source() ||
           coparent_arrival->destination()!=arrival->destination() ||
           !transport::completed_transport(*coparent_arrival,plan.co_parent_id))
            throw std::runtime_error("second parent has no completed physical arrival");
    }
    if(plan.dependents.size()>32)
        throw std::invalid_argument("household manifest exceeds bounded dependent capacity");
    std::uint64_t previous_child=0;
    std::set<std::uint64_t> child_receipt_ids{plan.command_id};
    if(plan.co_parent_id)child_receipt_ids.insert(plan.co_parent_event_id);
    std::vector<std::pair<std::uint64_t,std::uint64_t>> dependent_bindings;
    for(const auto& dependent:plan.dependents){
        if(dependent.child_id<(1ULL<<62) ||
           dependent.child_id<=previous_child || !dependent.event_id ||
           !child_receipt_ids.insert(dependent.event_id).second ||
           world_.npc_records().count(dependent.child_id) ||
           world_.npc_casualties().count(dependent.child_id) ||
           family.current_residence(dependent.child_id)!=plan.origin)
            throw std::runtime_error("dependent manifest is invalid or already moved");
        // A change of census address is NOT proof that this child traveled.
        // Require completed physical berth/custody evidence on the guardian's
        // actual ferry, with arrival before this migration step.
        bool arrived=false;
        for(const auto& leg:store_->dependent_travel(plan.family_scope)){
            if(leg.child_id==dependent.child_id && leg.guardian_id==plan.npc_id &&
               leg.co_guardian_id==plan.co_parent_id &&
               leg.transport_id==plan.transport_id && leg.origin==plan.origin &&
               leg.destination==plan.destination && leg.status==2 &&
               leg.arrival_generation<=generation_ &&
               leg.termination_kind==0)arrived=true;
        }
        if(!arrived)throw std::runtime_error("child has no physically completed ferry voyage");
        previous_child=dependent.child_id;
        dependent_bindings.emplace_back(dependent.child_id,dependent.event_id);
    }
    const auto member_count=1ULL+(plan.co_parent_id?1ULL:0ULL)+dependent_bindings.size();
    if(src.family_population<member_count)
        throw std::runtime_error("household would overdraw source census");
    next.step();
    const std::vector<Event> delta(next.events().begin()+
        static_cast<std::ptrdiff_t>(before),next.events().end());
    if(delta.size()!=1 || delta[0].command_id!=plan.command_id ||
       delta[0].result!=Outcome::Applied || next.npc_records().size()!=initial_named ||
       next.npc_casualties().size()!=world_.npc_casualties().size())
        throw std::runtime_error("migration did not physically execute exactly once");
    const auto moved=next.npc_records().find(plan.npc_id);
    if(plan.co_parent_id){
        const auto co=next.npc_records().find(plan.co_parent_id);
        if(co==next.npc_records().end() || co->second.home_ship!=plan.destination ||
           co->second.assigned_site!=plan.destination ||
           co->second.location!=plan.destination || co->second.transport_id)
            throw std::runtime_error("second parent's residence failed atomic execution");
    }
    if(moved==next.npc_records().end() || moved->second.home_ship!=plan.destination ||
       moved->second.assigned_site!=plan.destination ||
       moved->second.location!=plan.destination || moved->second.transport_id)
        throw std::runtime_error("migration execution changed unexpected custody");
    const auto activity=std::vector<ActivityEvent>(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(prior_activity),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t committed_generation){
        NpcAppearanceStore joined(db,plan.family_scope,plan.family_seed,
            [&](std::uint64_t id){return next.npc_records().count(id)!=0;});
        const auto authorizer=[&](auto npc,auto orig,auto dest){
            if(orig!=plan.origin || dest!=plan.destination)return false;
            if(npc==plan.npc_id)return moved->second.home_ship==dest &&
                old->second.home_ship==orig;
            if(npc==plan.co_parent_id && plan.co_parent_id){
                const auto earlier=world_.npc_records().find(npc);
                const auto later=next.npc_records().find(npc);
                return earlier!=world_.npc_records().end() &&
                    later!=next.npc_records().end() &&
                    earlier->second.home_ship==orig && later->second.home_ship==dest;
            }
            return false;
        };
        std::vector<NpcMoveRecord> household_moves;
        if(dependent_bindings.empty() && !plan.co_parent_id)
            household_moves.push_back(joined.record_move(plan.command_id,plan.npc_id,
                plan.origin,plan.destination,next.tick(),plan.expected_origin_family_revision,
                plan.expected_destination_family_revision,authorizer));
        else
            household_moves=joined.record_guardian_household_moves(plan.command_id,plan.npc_id,
                dependent_bindings,plan.origin,plan.destination,next.tick(),
                plan.expected_origin_family_revision,
                plan.expected_destination_family_revision,authorizer,
                plan.co_parent_id,plan.co_parent_event_id);
        const auto& move=household_moves.front();
        const auto region_revision=region.commit_migration_in_world_transaction(
            db,plan.expected_region_revision,plan.expected_owner_generation,next.tick(),
            plan.origin_colony,plan.destination_colony,src.family_population,
            dst.family_population,std::to_string(plan.command_id),member_count);
        run(db,"CREATE TABLE IF NOT EXISTS sg_society_migration ("
            "command_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL,"
            "tick INTEGER NOT NULL,family_scope TEXT NOT NULL,"
            "region_scope TEXT NOT NULL,region_revision INTEGER NOT NULL,"
            "owner_generation INTEGER NOT NULL,origin TEXT NOT NULL,"
            "destination TEXT NOT NULL,origin_colony TEXT NOT NULL,"
            "destination_colony TEXT NOT NULL,npc_id TEXT NOT NULL,"
            "transport_id TEXT NOT NULL,origin_family_revision INTEGER NOT NULL,"
            "destination_family_revision INTEGER NOT NULL,"
            "household_count INTEGER NOT NULL DEFAULT 1 CHECK(household_count>=1),"
            "physical_manifest INTEGER NOT NULL DEFAULT 0 CHECK(physical_manifest IN (0,1)),"
            "co_parent_id TEXT NOT NULL DEFAULT '0',"
            "co_parent_event TEXT NOT NULL DEFAULT '0')");
        // Existing v41 saves have the 15-column schema. This is an additive,
        // transactional migration; older receipts acquire default count 1.
        bool has_count=false;
        {
            Statement info(db,"PRAGMA table_info(sg_society_migration)");
            while(info.row())if(sqlite_str(info.get(),1)=="household_count")has_count=true;
        }
        if(!has_count)run(db,"ALTER TABLE sg_society_migration ADD COLUMN "
                          "household_count INTEGER NOT NULL DEFAULT 1");
        bool has_physical=false;
        {Statement info(db,"PRAGMA table_info(sg_society_migration)");
         while(info.row())if(sqlite_str(info.get(),1)=="physical_manifest")has_physical=true;}
        if(!has_physical)run(db,"ALTER TABLE sg_society_migration ADD COLUMN "
                             "physical_manifest INTEGER NOT NULL DEFAULT 0");
        bool has_co_parent=false;
        {Statement info(db,"PRAGMA table_info(sg_society_migration)");
         while(info.row())if(sqlite_str(info.get(),1)=="co_parent_id")has_co_parent=true;}
        if(!has_co_parent){
            run(db,"ALTER TABLE sg_society_migration ADD COLUMN "
                   "co_parent_id TEXT NOT NULL DEFAULT '0'");
            run(db,"ALTER TABLE sg_society_migration ADD COLUMN "
                   "co_parent_event TEXT NOT NULL DEFAULT '0'");
        }
        Statement receipt(db,"INSERT INTO sg_society_migration VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        receipt.text(1,std::to_string(plan.command_id));
        receipt.i64(2,static_cast<std::int64_t>(committed_generation));
        receipt.i64(3,static_cast<std::int64_t>(next.tick()));
        receipt.text(4,plan.family_scope);receipt.text(5,plan.region_scope);
        receipt.i64(6,static_cast<std::int64_t>(region_revision));
        receipt.i64(7,static_cast<std::int64_t>(plan.expected_owner_generation));
        receipt.text(8,std::to_string(plan.origin));
        receipt.text(9,std::to_string(plan.destination));
        receipt.text(10,plan.origin_colony);receipt.text(11,plan.destination_colony);
        receipt.text(12,std::to_string(plan.npc_id));
        receipt.text(13,std::to_string(plan.transport_id));
        receipt.i64(14,static_cast<std::int64_t>(move.origin_revision));
        receipt.i64(15,static_cast<std::int64_t>(move.destination_revision));
        receipt.i64(16,static_cast<std::int64_t>(member_count));
        receipt.i64(17,dependent_bindings.empty()?0:1);
        receipt.text(18,std::to_string(plan.co_parent_id));
        receipt.text(19,std::to_string(plan.co_parent_event_id));
        receipt.execute();
        if(member_count>1){
            run(db,"CREATE TABLE IF NOT EXISTS sg_society_household_member ("
                "parent_command_id TEXT NOT NULL,event_id TEXT NOT NULL,"
                "family_scope TEXT NOT NULL,child_id TEXT NOT NULL,"
                "guardian_id TEXT NOT NULL,origin TEXT NOT NULL,destination TEXT NOT NULL,"
                "origin_revision INTEGER NOT NULL,destination_revision INTEGER NOT NULL,"
                "PRIMARY KEY(parent_command_id,event_id,child_id),"
                "UNIQUE(family_scope,event_id))");
            for(std::size_t i=plan.co_parent_id?2:1;i<household_moves.size();++i){
                const auto& m=household_moves[i];
                Statement child(db,"INSERT INTO sg_society_household_member "
                    "VALUES(?,?,?,?,?,?,?,?,?)");
                child.text(1,std::to_string(plan.command_id));
                child.text(2,std::to_string(m.event_id));
                child.text(3,plan.family_scope);
                child.text(4,std::to_string(m.npc_id));
                child.text(5,std::to_string(plan.npc_id));
                child.text(6,std::to_string(plan.origin));
                child.text(7,std::to_string(plan.destination));
                child.i64(8,static_cast<std::int64_t>(m.origin_revision));
                child.i64(9,static_cast<std::int64_t>(m.destination_revision));
                child.execute();
            }
        }
        if(plan.voluntary_admission_event_id)
            store_->insert_voluntary_settlement_receipt(
                db,plan.voluntary_admission_event_id,plan,committed_generation);
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,delta,due,
                   activity,fail,nullptr,nullptr,effect);
    world_.swap(next);
    ++generation_;
}

void PersistentWorld::step_with_dependent_transport(
    const DependentTravelPlan& plan,FailPoint fail){
    if(plan.expected_world_generation!=generation_ || !plan.command_id ||
       !plan.transport_id || !plan.guardian_id || !plan.origin ||
       !plan.destination || plan.origin==plan.destination || !plan.ferry ||
       plan.family_scope.empty() || !plan.family_seed ||
       plan.child_ids.empty() || plan.child_ids.size()>15 ||
       !std::is_sorted(plan.child_ids.begin(),plan.child_ids.end()) ||
       std::adjacent_find(plan.child_ids.begin(),plan.child_ids.end())!=plan.child_ids.end() ||
       (plan.co_guardian_id && (plan.co_guardian_id==plan.guardian_id ||
          plan.region_scope.empty() || !plan.expected_owner_generation)))
        throw std::invalid_argument("invalid or stale physical dependent travel plan");
    auto next=world_;
    const auto first=next.events().size(),first_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1 || !due.count(plan.command_id))
        throw std::runtime_error("dependent travel requires exactly one due host command");
    const auto& cmd=due.at(plan.command_id);
    const auto expected=plan.boarding ? Action::HostEmbarkPassengers : Action::HostDisembarkPassengers;
    if(cmd.action!=expected || cmd.issuer!=Simulation::kServerAuthority ||
       cmd.actor!=plan.ferry || cmd.target!=(plan.boarding ? plan.origin:plan.destination) ||
       cmd.transport_id!=plan.transport_id ||
       std::find(cmd.passenger_ids.begin(),cmd.passenger_ids.end(),plan.guardian_id)==
           cmd.passenger_ids.end() ||
       (plan.co_guardian_id && std::find(cmd.passenger_ids.begin(),
           cmd.passenger_ids.end(),plan.co_guardian_id)==cmd.passenger_ids.end()))
        throw std::runtime_error("child manifest not bound to actual guardian ferry command");
    NpcAppearanceStore family(store_->sqlite_path(),plan.family_scope,plan.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    if(family.current_residence(plan.guardian_id)!=plan.origin ||
       !world_.colony_ship(plan.origin) || !world_.colony_ship(plan.destination) ||
       !world_.colony_ship(plan.ferry))
        throw std::runtime_error("invalid child source, destination or guardian identity");
    auto before=world_.npc_records().find(plan.guardian_id);
    if(before==world_.npc_records().end() ||
       before->second.location!=(plan.boarding?plan.origin:plan.ferry) ||
       before->second.home_ship!=plan.origin ||
       before->second.transport_id!=(plan.boarding?0:plan.transport_id))
        throw std::runtime_error("guardian is not in expected physical custody");
    if(plan.co_guardian_id){
        const auto co=world_.npc_records().find(plan.co_guardian_id);
        if(co==world_.npc_records().end() ||
           family.current_residence(plan.co_guardian_id)!=plan.origin ||
           co->second.home_ship!=plan.origin ||
           co->second.location!=(plan.boarding?plan.origin:plan.ferry) ||
           co->second.transport_id!=(plan.boarding?0:plan.transport_id))
            throw std::runtime_error("second parent is not physically co-traveling");
        // This is a *preflight* hint; the authoritative CAS is rechecked
        // while holding the SQLite world transaction below.
        regional_events::CheckpointStore checkpoint(store_->sqlite_path(),plan.region_scope);
        regional_events::RegionalModeAuthority region(checkpoint);
        const auto owner=region.state();
        if(owner.generation!=plan.expected_owner_generation ||
           owner.owner!=regional_events::SimulationOwner::Cold ||
           owner.phase!=regional_events::TransitionPhase::Stable)
            throw std::runtime_error("paired-family region lacks stable cold authority");
    }
    // Each child must have a real ART02 birth and current legal residence,
    // but remains a non-worker cohort identity rather than an NPC clone.
    sqlite3* verify=nullptr;
    if(sqlite3_open_v2(store_->sqlite_path().c_str(),&verify,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(verify)sqlite3_close(verify);
        throw std::runtime_error("cannot inspect dependent births");
    }
    try{
        for(auto child:plan.child_ids){
            if(child<(1ULL<<62) || world_.npc_records().count(child) ||
               family.current_residence(child)!=plan.origin)
                throw std::runtime_error("dependent identity or residence invalid");
            Statement birth(verify,"SELECT parent_a,parent_b FROM npc_family_births "
                "WHERE scope=? AND child_id=?");
            birth.text(1,plan.family_scope);birth.text(2,std::to_string(child));
            if(!birth.row())throw std::runtime_error("child lacks ART02 birth certificate");
            const auto a=parse_u64(sqlite3_column_text(birth.get(),0));
            const auto b=parse_u64(sqlite3_column_text(birth.get(),1));
            if((a!=plan.guardian_id && b!=plan.guardian_id) ||
               (plan.co_guardian_id && !
                  ((a==plan.guardian_id && b==plan.co_guardian_id) ||
                   (b==plan.guardian_id && a==plan.co_guardian_id))) || birth.row())
                throw std::runtime_error("child/guardian lineage invalid");
        }
        // Boarding a guardian while omitting one of their currently resident
        // dependents is not a complete physical family itinerary.
        Statement all(verify,"SELECT child_id FROM npc_family_births WHERE scope=? "
            "AND (parent_a=? OR parent_b=?) ORDER BY child_id");
        all.text(1,plan.family_scope);
        all.text(2,std::to_string(plan.guardian_id));
        all.text(3,std::to_string(plan.guardian_id));
        std::vector<std::uint64_t> actual;
        while(all.row()){
            const auto child=parse_u64(sqlite3_column_text(all.get(),0));
            if(family.current_residence(child)==plan.origin)actual.push_back(child);
        }
        if(actual!=plan.child_ids)
            throw std::runtime_error("unmanifested guardian dependents");
    }catch(...){sqlite3_close(verify);throw;}
    sqlite3_close(verify);
    const auto legs=store_->dependent_travel(plan.family_scope);
    if(plan.boarding){
        std::uint64_t adult_aboard=0;
        for(const auto& [id,n]:world_.npc_records()){
            (void)id;
            if(n.location==plan.ferry && n.transport_id)++adult_aboard;
        }
        const auto active=store_->active_dependent_berths(plan.ferry);
        if(active<0 || adult_aboard+static_cast<std::uint64_t>(active)+
           cmd.passenger_ids.size()+plan.child_ids.size()>16)
            throw std::runtime_error("family boarding exceeds physical 16-seat limit");
        for(auto child:plan.child_ids)for(const auto& leg:legs){
            if(leg.child_id!=child)continue;
            if(leg.status==1 || (leg.status==2 &&
               family.current_residence(child)!=leg.destination))
                throw std::runtime_error("dependent already traveling or arrival unsettled");
            if(leg.transport_id==plan.transport_id)
                throw std::runtime_error("dependent voyage ID reused");
        }
    }else{
        for(auto child:plan.child_ids){
            std::size_t matched=0;
            for(const auto& leg:legs)if(leg.child_id==child &&
                leg.transport_id==plan.transport_id && leg.status==1 &&
                leg.guardian_id==plan.guardian_id &&
                leg.co_guardian_id==plan.co_guardian_id &&
                leg.ferry==plan.ferry &&
                leg.origin==plan.origin && leg.destination==plan.destination)++matched;
            if(matched!=1)throw std::runtime_error("child is not aboard this ferry voyage");
        }
    }
    next.step();
    const std::vector<Event> delta(next.events().begin()+static_cast<std::ptrdiff_t>(first),
                                   next.events().end());
    if(delta.size()!=1 || delta.front().command_id!=plan.command_id ||
       delta.front().result!=Outcome::Applied)
        throw std::runtime_error("underlying physical ferry command did not execute");
    const auto after=next.npc_records().at(plan.guardian_id);
    if(plan.co_guardian_id){
        const auto co=next.npc_records().find(plan.co_guardian_id);
        if(co==next.npc_records().end() ||
           co->second.location!=(plan.boarding?plan.ferry:plan.destination) ||
           co->second.transport_id!=(plan.boarding?plan.transport_id:0))
            throw std::runtime_error("second parent not in committed physical custody");
    }
    const auto voy=next.voyages_.find(plan.transport_id);
    bool resumed_rescue=false;
    if(!plan.boarding && voy!=next.voyages_.end() &&
       voy->second.origin!=plan.origin){
        for(const auto& leg:legs){
            if(leg.child_id==plan.child_ids.front() &&
               leg.transport_id==plan.transport_id &&
               leg.transfer_from_transport && leg.ferry==plan.ferry){
                const auto parent=world_.voyages_.find(leg.transfer_from_transport);
                resumed_rescue=parent!=world_.voyages_.end() &&
                    parent->second.ferry==voy->second.origin &&
                    parent->second.origin==plan.origin &&
                    parent->second.rescue_transport_id==plan.transport_id;
            }
        }
    }
    if(voy==next.voyages_.end() || voy->second.ferry!=plan.ferry ||
       (voy->second.origin!=plan.origin && !resumed_rescue) ||
       (plan.boarding ? (voy->second.status!=1 || after.location!=plan.ferry ||
         after.transport_id!=plan.transport_id) :
        (voy->second.status!=2 || voy->second.destination!=plan.destination ||
         after.location!=plan.destination || after.transport_id!=0)))
        throw std::runtime_error("guardian/voyage custody did not transition");
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(first_activity),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t gen){
        if(plan.co_guardian_id){
            Statement owner(db,"SELECT owner,phase,generation FROM "
                "sg_regional_mode_authority WHERE scope=?");
            owner.text(1,plan.region_scope);
            if(!owner.row() || sqlite3_column_int(owner.get(),0)!=0 ||
               sqlite3_column_int(owner.get(),1)!=0 ||
               sqlite3_column_int64(owner.get(),2)!=
                  static_cast<std::int64_t>(plan.expected_owner_generation) || owner.row())
                throw std::runtime_error("regional ownership changed during paired-family world commit");
        }
        run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_travel ("
            "family_scope TEXT NOT NULL,child_id TEXT NOT NULL,transport_id TEXT NOT NULL,"
            "guardian_id TEXT NOT NULL,ferry TEXT NOT NULL,origin TEXT NOT NULL,"
            "destination TEXT NOT NULL,board_command TEXT NOT NULL,"
            "arrival_command TEXT,board_generation INTEGER NOT NULL,"
            "arrival_generation INTEGER,board_tick INTEGER NOT NULL,arrival_tick INTEGER,"
            "status INTEGER NOT NULL CHECK(status IN (1,2)),"
            "PRIMARY KEY(family_scope,child_id,transport_id))");
        if(plan.co_guardian_id){
            bool has_parent=false;
            {Statement cols(db,"PRAGMA table_info(sg_dependent_travel)");
             while(cols.row())if(sqlite_str(cols.get(),1)=="co_guardian_id")has_parent=true;}
            if(!has_parent)run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                    "co_guardian_id TEXT NOT NULL DEFAULT '0'");
        }
        // 05q6e5b3b: a family's very first child boarding binds its region.
        // Never retroactively stamp an old, unscoped in-flight voyage with a
        // new region; legacy child custody stays under conservative global fence.
        if(plan.boarding && !plan.region_scope.empty()){
            run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_region_binding ("
                "family_scope TEXT PRIMARY KEY,region_scope TEXT NOT NULL,"
                "first_generation INTEGER NOT NULL,first_command TEXT NOT NULL)");
            run(db,"CREATE TRIGGER IF NOT EXISTS sg_dependent_region_binding_immutable_update "
                "BEFORE UPDATE ON sg_dependent_region_binding "
                "BEGIN SELECT RAISE(ABORT,'child region binding immutable'); END");
            run(db,"CREATE TRIGGER IF NOT EXISTS sg_dependent_region_binding_immutable_delete "
                "BEFORE DELETE ON sg_dependent_region_binding "
                "BEGIN SELECT RAISE(ABORT,'child region binding immutable'); END");
            bool bound=false;
            {Statement prior(db,"SELECT region_scope FROM sg_dependent_region_binding "
                "WHERE family_scope=?");prior.text(1,plan.family_scope);
             if(prior.row()){
                bound=true;
                if(sqlite_str(prior.get(),0)!=plan.region_scope||prior.row())
                    throw std::runtime_error("family region binding cannot change mid-voyage");
             }}
            if(!bound){
                Statement old(db,"SELECT 1 FROM sg_dependent_travel WHERE family_scope=? LIMIT 1");
                old.text(1,plan.family_scope);
                if(old.row())throw std::runtime_error("cannot backfill region for legacy child voyages");
                bool has_anchor=false;
                {Statement cols(db,"PRAGMA table_info(world_meta)");
                 while(cols.row())if(sqlite_str(cols.get(),1)=="dependent_region_binding_count")
                    has_anchor=true;}
                if(!has_anchor)run(db,"ALTER TABLE world_meta ADD COLUMN "
                    "dependent_region_binding_count INTEGER NOT NULL DEFAULT 0 "
                    "CHECK(dependent_region_binding_count>=0)");
                // The ALTER above runs only for the first binding in this DB.
                Statement insert(db,"INSERT INTO sg_dependent_region_binding VALUES(?,?,?,?)");
                insert.text(1,plan.family_scope);insert.text(2,plan.region_scope);
                insert.i64(3,static_cast<std::int64_t>(gen));
                insert.text(4,std::to_string(plan.command_id));insert.execute();
                run(db,"UPDATE world_meta SET dependent_region_binding_count="
                    "dependent_region_binding_count+1 WHERE singleton=1");
            }
        }
        run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_transport_batch ("
            "family_scope TEXT NOT NULL,command_id TEXT NOT NULL PRIMARY KEY,"
            "transport_id TEXT NOT NULL,phase INTEGER NOT NULL,"
            "member_count INTEGER NOT NULL,world_generation INTEGER NOT NULL)");
        run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_transport_anchor "
               "(id INTEGER PRIMARY KEY CHECK(id=1),first_generation INTEGER NOT NULL)");
        Statement activation(db,"INSERT OR IGNORE INTO sg_dependent_transport_anchor "
                       "VALUES(1,?)");
        activation.i64(1,static_cast<std::int64_t>(gen));activation.execute();
        Statement batch(db,"INSERT INTO sg_dependent_transport_batch VALUES(?,?,?,?,?,?)");
        batch.text(1,plan.family_scope);
        batch.text(2,std::to_string(plan.command_id));
        batch.text(3,std::to_string(plan.transport_id));
        batch.i64(4,plan.boarding?1:2);
        batch.i64(5,static_cast<std::int64_t>(plan.child_ids.size()));
        batch.i64(6,static_cast<std::int64_t>(gen));
        batch.execute();
        for(auto child:plan.child_ids){
            if(plan.boarding){
                Statement insert(db,"INSERT INTO sg_dependent_travel "
                    "(family_scope,child_id,transport_id,guardian_id,ferry,origin,destination,"
                    "board_command,arrival_command,board_generation,arrival_generation,"
                    "board_tick,arrival_tick,status) VALUES "
                    "(?,?,?,?,?,?,?,?,NULL,?,NULL,?,NULL,1)");
                insert.text(1,plan.family_scope);
                insert.text(2,std::to_string(child));
                insert.text(3,std::to_string(plan.transport_id));
                insert.text(4,std::to_string(plan.guardian_id));
                insert.text(5,std::to_string(plan.ferry));
                insert.text(6,std::to_string(plan.origin));
                insert.text(7,std::to_string(plan.destination));
                insert.text(8,std::to_string(plan.command_id));
                insert.i64(9,static_cast<std::int64_t>(gen));
                insert.i64(10,static_cast<std::int64_t>(next.tick()));
                insert.execute();
                if(plan.co_guardian_id){
                    Statement paired(db,"UPDATE sg_dependent_travel SET co_guardian_id=? "
                        "WHERE family_scope=? AND child_id=? AND transport_id=?");
                    paired.text(1,std::to_string(plan.co_guardian_id));
                    paired.text(2,plan.family_scope);
                    paired.text(3,std::to_string(child));
                    paired.text(4,std::to_string(plan.transport_id));
                    paired.execute();
                    if(sqlite3_changes(db)!=1)
                        throw std::runtime_error("co-parent custody receipt not saved");
                }
            }else{
                Statement update(db,"UPDATE sg_dependent_travel SET status=2,"
                    "arrival_command=?,arrival_generation=?,arrival_tick=? "
                    "WHERE family_scope=? AND child_id=? AND transport_id=? AND "
                    "guardian_id=? AND ferry=? AND origin=? AND destination=? AND status=1");
                update.text(1,std::to_string(plan.command_id));
                update.i64(2,static_cast<std::int64_t>(gen));
                update.i64(3,static_cast<std::int64_t>(next.tick()));
                update.text(4,plan.family_scope);
                update.text(5,std::to_string(child));
                update.text(6,std::to_string(plan.transport_id));
                update.text(7,std::to_string(plan.guardian_id));
                update.text(8,std::to_string(plan.ferry));
                update.text(9,std::to_string(plan.origin));
                update.text(10,std::to_string(plan.destination));
                update.execute();
                if(sqlite3_changes(db)!=1)
                    throw std::runtime_error("dependent arrival CAS failed");
            }
        }
        Statement count(db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? AND status=1");
        count.text(1,std::to_string(plan.ferry));
        if(!count.row() || sqlite3_column_int64(count.get(),0)>16)
            throw std::runtime_error("physical child berth overbooking");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,delta,due,
                   activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

void PersistentWorld::step_with_dependent_rescue(
    const DependentRescuePlan& plan,FailPoint fail){
    if(plan.expected_world_generation!=generation_ || !plan.command_id ||
       !plan.original_transport || !plan.rescue_transport ||
       plan.original_transport==plan.rescue_transport ||
       !plan.guardian_id || !plan.original_ferry || !plan.rescue_ferry ||
       plan.original_ferry==plan.rescue_ferry || !plan.origin || !plan.destination ||
       plan.origin==plan.destination || !plan.family_seed ||
       plan.family_scope.empty() || plan.child_ids.empty() ||
       plan.child_ids.size()>15 || !std::is_sorted(plan.child_ids.begin(),plan.child_ids.end()) ||
       std::adjacent_find(plan.child_ids.begin(),plan.child_ids.end())!=plan.child_ids.end() ||
       (plan.co_guardian_id && (plan.co_guardian_id==plan.guardian_id ||
          plan.region_scope.empty() || !plan.expected_owner_generation)))
        throw std::invalid_argument("stale or invalid child rescue plan");
    auto next=world_;
    const auto before=next.events().size(),before_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1 || !due.count(plan.command_id))
        throw std::runtime_error("child rescue requires single scheduled host incident");
    const auto& command=due.at(plan.command_id);
    if(command.action!=Action::HostEvacuatePassengers ||
       command.issuer!=Simulation::kServerAuthority ||
       command.actor!=plan.original_ferry || command.target!=plan.rescue_ferry ||
       command.transport_id!=plan.original_transport ||
       command.amount!=static_cast<std::int64_t>(plan.rescue_transport) ||
       std::find(command.passenger_ids.begin(),command.passenger_ids.end(),plan.guardian_id)==
           command.passenger_ids.end() ||
       (plan.co_guardian_id &&
        std::find(command.passenger_ids.begin(),command.passenger_ids.end(),
                  plan.co_guardian_id)==command.passenger_ids.end()))
        throw std::runtime_error("child rescue incident not bound to guardian manifest");
    const auto original_voyage=world_.voyages_.find(plan.original_transport);
    if(original_voyage==world_.voyages_.end() ||
       original_voyage->second.ferry!=plan.original_ferry ||
       original_voyage->second.origin!=plan.origin ||
       (original_voyage->second.status!=1 && original_voyage->second.status!=3))
        throw std::runtime_error("invalid actual source child voyage");
    NpcAppearanceStore family(store_->sqlite_path(),plan.family_scope,plan.family_seed,
        [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
    const auto legs=store_->dependent_travel(plan.family_scope);
    if(store_->active_dependent_berths(plan.original_ferry)!=
       static_cast<std::int64_t>(plan.child_ids.size()))
        throw std::runtime_error("rescue must enumerate all children on source ferry");
    for(auto child:plan.child_ids){
        std::size_t found=0;
        for(const auto& leg:legs)if(leg.child_id==child &&
            leg.transport_id==plan.original_transport && leg.status==1 &&
            leg.guardian_id==plan.guardian_id &&
            leg.co_guardian_id==plan.co_guardian_id &&
            leg.ferry==plan.original_ferry && leg.origin==plan.origin &&
            leg.destination==plan.destination)++found;
        if(found!=1 || family.current_residence(child)!=plan.origin)
            throw std::runtime_error("rescue child missing or in contradictory custody");
    }
    for(auto adult:{plan.guardian_id,plan.co_guardian_id}){
        if(!adult)continue;
        const auto n=world_.npc_records().find(adult);
        if(n==world_.npc_records().end() || n->second.location!=plan.original_ferry ||
           n->second.transport_id!=plan.original_transport)
            throw std::runtime_error("rescue guardian is not aboard stranded ferry");
    }
    if(plan.co_guardian_id){
        regional_events::CheckpointStore region(store_->sqlite_path(),plan.region_scope);
        regional_events::RegionalModeAuthority auth(region);
        const auto owner=auth.state();
        if(owner.owner!=regional_events::SimulationOwner::Cold ||
           owner.phase!=regional_events::TransitionPhase::Stable ||
           owner.generation!=plan.expected_owner_generation)
            throw std::runtime_error("rescue region ownership is stale");
    }
    const auto occupied=store_->active_dependent_berths(plan.rescue_ferry);
    if(occupied<0 || next.passengers_aboard(plan.rescue_ferry)+
       static_cast<std::uint64_t>(occupied)+plan.child_ids.size()>16)
        throw std::runtime_error("receiving child ferry overbooked");
    next.step();
    const std::vector<Event> changes(next.events().begin()+
        static_cast<std::ptrdiff_t>(before),next.events().end());
    if(changes.size()!=1 || changes[0].command_id!=plan.command_id ||
       changes[0].result!=Outcome::Applied ||
       next.voyage_status(plan.original_transport)!=5 ||
       next.voyage_status(plan.rescue_transport)!=1)
        throw std::runtime_error("underlying world rescue not applied");
    for(auto adult:{plan.guardian_id,plan.co_guardian_id}){
        if(!adult)continue;
        const auto& n=next.npc_records().at(adult);
        if(n.location!=plan.rescue_ferry || n.transport_id!=plan.rescue_transport)
            throw std::runtime_error("rescue parent not aboard receiving ferry");
    }
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(before_activity),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t gen){
        if(plan.co_guardian_id){
            Statement owner(db,"SELECT owner,phase,generation FROM sg_regional_mode_authority "
                               "WHERE scope=?");
            owner.text(1,plan.region_scope);
            if(!owner.row() || sqlite3_column_int(owner.get(),0)!=0 ||
               sqlite3_column_int(owner.get(),1)!=0 ||
               sqlite3_column_int64(owner.get(),2)!=
                    static_cast<std::int64_t>(plan.expected_owner_generation) || owner.row())
                throw std::runtime_error("rescue region changed during transaction");
        }
        bool has_transfer=false,has_coguardian=false;
        {Statement columns(db,"PRAGMA table_info(sg_dependent_travel)");
         while(columns.row()){
             if(sqlite_str(columns.get(),1)=="transfer_from_transport")has_transfer=true;
             if(sqlite_str(columns.get(),1)=="co_guardian_id")has_coguardian=true;
         }}
        if(!has_coguardian)
            run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                   "co_guardian_id TEXT NOT NULL DEFAULT '0'");
        if(!has_transfer){
            run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                   "transfer_from_transport TEXT NOT NULL DEFAULT '0'");
            run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                   "termination_kind INTEGER NOT NULL DEFAULT 0 CHECK(termination_kind IN (0,1))");
        }
        run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_transport_anchor "
               "(id INTEGER PRIMARY KEY CHECK(id=1),first_generation INTEGER NOT NULL)");
        Statement activation(db,"INSERT OR IGNORE INTO sg_dependent_transport_anchor "
                       "VALUES(1,?)");
        activation.i64(1,static_cast<std::int64_t>(gen));activation.execute();
        {Statement binding(db,"SELECT region_scope FROM sg_dependent_region_binding "
                "WHERE family_scope=?");
         binding.text(1,plan.family_scope);
         if(!binding.row()||sqlite_str(binding.get(),0)!=plan.region_scope||binding.row())
            throw std::runtime_error("rescue requires original family-region custody binding");}
        Statement batch(db,"INSERT INTO sg_dependent_transport_batch VALUES(?,?,?,?,?,?)");
        batch.text(1,plan.family_scope);batch.text(2,std::to_string(plan.command_id));
        batch.text(3,std::to_string(plan.rescue_transport));
        batch.i64(4,3);batch.i64(5,static_cast<std::int64_t>(plan.child_ids.size()));
        batch.i64(6,static_cast<std::int64_t>(gen));batch.execute();
        for(auto child:plan.child_ids){
            Statement close(db,"UPDATE sg_dependent_travel SET status=2,"
                "arrival_command=?,arrival_generation=?,arrival_tick=?,termination_kind=1 "
                "WHERE family_scope=? AND child_id=? AND transport_id=? AND status=1");
            close.text(1,std::to_string(plan.command_id));
            close.i64(2,static_cast<std::int64_t>(gen));
            close.i64(3,static_cast<std::int64_t>(next.tick()));
            close.text(4,plan.family_scope);close.text(5,std::to_string(child));
            close.text(6,std::to_string(plan.original_transport));close.execute();
            if(sqlite3_changes(db)!=1)throw std::runtime_error("child old berth not closed");
            Statement fresh(db,"INSERT INTO sg_dependent_travel (family_scope,child_id,"
                "transport_id,guardian_id,ferry,origin,destination,board_command,"
                "board_generation,board_tick,status,co_guardian_id,transfer_from_transport) "
                "VALUES(?,?,?,?,?,?,?,?,?, ?,1,?,?)");
            fresh.text(1,plan.family_scope);fresh.text(2,std::to_string(child));
            fresh.text(3,std::to_string(plan.rescue_transport));
            fresh.text(4,std::to_string(plan.guardian_id));
            fresh.text(5,std::to_string(plan.rescue_ferry));
            fresh.text(6,std::to_string(plan.origin));
            fresh.text(7,std::to_string(plan.destination));
            fresh.text(8,std::to_string(plan.command_id));
            fresh.i64(9,static_cast<std::int64_t>(gen));
            fresh.i64(10,static_cast<std::int64_t>(next.tick()));
            fresh.text(11,std::to_string(plan.co_guardian_id));
            fresh.text(12,std::to_string(plan.original_transport));fresh.execute();
        }
        Statement capacity(db,"SELECT COUNT(*) FROM sg_dependent_travel "
                              "WHERE ferry=? AND status=1");
        capacity.text(1,std::to_string(plan.rescue_ferry));
        if(!capacity.row() ||
           next.passengers_aboard(plan.rescue_ferry)+
            static_cast<std::uint64_t>(sqlite3_column_int64(capacity.get(),0))>16)
            throw std::runtime_error("receiving child occupancy over physical capacity");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   changes,due,activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

// DEMOG-05q6e5b4b: one world step and one SQL transaction for EVERY family
// on a real adult ferry. Family-scoped receipts share the true command ID.
namespace {
void multi_batch_schema(sqlite3* db){
    bool exists=false,composite=false;
    {Statement found(db,"SELECT 1 FROM sqlite_master WHERE type='table' "
                        "AND name='sg_dependent_transport_batch'");exists=found.row();}
    if(exists){
        int family_pk=0,command_pk=0;
        Statement cols(db,"PRAGMA table_info(sg_dependent_transport_batch)");
        while(cols.row()){
            const auto name=sqlite_str(cols.get(),1);
            if(name=="family_scope")family_pk=sqlite3_column_int(cols.get(),5);
            if(name=="command_id")command_pk=sqlite3_column_int(cols.get(),5);
        }
        composite=family_pk==1&&command_pk==2;
        if(!composite){
            run(db,"ALTER TABLE sg_dependent_transport_batch RENAME TO "
                   "sg_dependent_transport_batch_legacy_upgrade");
        }
    }
    if(!composite){
        run(db,"CREATE TABLE sg_dependent_transport_batch ("
               "family_scope TEXT NOT NULL,command_id TEXT NOT NULL,"
               "transport_id TEXT NOT NULL,phase INTEGER NOT NULL,"
               "member_count INTEGER NOT NULL,world_generation INTEGER NOT NULL,"
               "PRIMARY KEY(family_scope,command_id))");
        if(exists){
            run(db,"INSERT INTO sg_dependent_transport_batch SELECT * "
                   "FROM sg_dependent_transport_batch_legacy_upgrade");
            run(db,"DROP TABLE sg_dependent_transport_batch_legacy_upgrade");
        }
    }
}
void multi_schema(sqlite3* db){
    run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_travel ("
        "family_scope TEXT NOT NULL,child_id TEXT NOT NULL,transport_id TEXT NOT NULL,"
        "guardian_id TEXT NOT NULL,ferry TEXT NOT NULL,origin TEXT NOT NULL,"
        "destination TEXT NOT NULL,board_command TEXT NOT NULL,arrival_command TEXT,"
        "board_generation INTEGER NOT NULL,arrival_generation INTEGER,"
        "board_tick INTEGER NOT NULL,arrival_tick INTEGER,"
        "status INTEGER NOT NULL CHECK(status IN (1,2)),"
        "PRIMARY KEY(family_scope,child_id,transport_id))");
    bool co=false,from=false,kind=false;
    {Statement cols(db,"PRAGMA table_info(sg_dependent_travel)");
     while(cols.row()){
        const auto name=sqlite_str(cols.get(),1);
        if(name=="co_guardian_id")co=true;
        if(name=="transfer_from_transport")from=true;
        if(name=="termination_kind")kind=true;
     }}
    if(!co)run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
               "co_guardian_id TEXT NOT NULL DEFAULT '0'");
    if(!from)run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                 "transfer_from_transport TEXT NOT NULL DEFAULT '0'");
    if(!kind)run(db,"ALTER TABLE sg_dependent_travel ADD COLUMN "
                 "termination_kind INTEGER NOT NULL DEFAULT 0 CHECK(termination_kind IN (0,1))");
    multi_batch_schema(db);
    run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_transport_anchor "
           "(id INTEGER PRIMARY KEY CHECK(id=1),first_generation INTEGER NOT NULL)");
    run(db,"CREATE TABLE IF NOT EXISTS sg_dependent_region_binding ("
           "family_scope TEXT PRIMARY KEY,region_scope TEXT NOT NULL,"
           "first_generation INTEGER NOT NULL,first_command TEXT NOT NULL)");
    run(db,"CREATE TRIGGER IF NOT EXISTS sg_dependent_region_binding_immutable_update "
           "BEFORE UPDATE ON sg_dependent_region_binding "
           "BEGIN SELECT RAISE(ABORT,'child region binding immutable'); END");
    run(db,"CREATE TRIGGER IF NOT EXISTS sg_dependent_region_binding_immutable_delete "
           "BEFORE DELETE ON sg_dependent_region_binding "
           "BEGIN SELECT RAISE(ABORT,'child region binding immutable'); END");
    bool anchor=false;
    {Statement cols(db,"PRAGMA table_info(world_meta)");
     while(cols.row())if(sqlite_str(cols.get(),1)=="dependent_region_binding_count")anchor=true;}
    if(!anchor)run(db,"ALTER TABLE world_meta ADD COLUMN "
                   "dependent_region_binding_count INTEGER NOT NULL DEFAULT 0 "
                   "CHECK(dependent_region_binding_count>=0)");
}
void multi_region_bind(sqlite3* db,const DependentFamilyManifest& family,
                       std::uint64_t command,std::uint64_t gen,bool boarding){
    Statement exists(db,"SELECT region_scope FROM sg_dependent_region_binding "
                        "WHERE family_scope=?");exists.text(1,family.family_scope);
    if(exists.row()){
        if(sqlite_str(exists.get(),0)!=family.region_scope||exists.row())
            throw std::runtime_error("family region authority binding mismatch");
        return;
    }
    if(!boarding)throw std::runtime_error("unbound multi-family rescue region");
    Statement legacy(db,"SELECT 1 FROM sg_dependent_travel WHERE family_scope=? LIMIT 1");
    legacy.text(1,family.family_scope);
    if(legacy.row())throw std::runtime_error("cannot add regional provenance to old child travel");
    Statement insert(db,"INSERT INTO sg_dependent_region_binding VALUES(?,?,?,?)");
    insert.text(1,family.family_scope);insert.text(2,family.region_scope);
    insert.i64(3,static_cast<std::int64_t>(gen));
    insert.text(4,std::to_string(command));insert.execute();
    run(db,"UPDATE world_meta SET dependent_region_binding_count="
           "dependent_region_binding_count+1 WHERE singleton=1");
}
void multi_receipt(sqlite3* db,const DependentFamilyManifest& family,
                   std::uint64_t command,std::uint64_t voyage,unsigned phase,
                   std::uint64_t gen){
    Statement batch(db,"INSERT INTO sg_dependent_transport_batch VALUES(?,?,?,?,?,?)");
    batch.text(1,family.family_scope);batch.text(2,std::to_string(command));
    batch.text(3,std::to_string(voyage));batch.i64(4,phase);
    batch.i64(5,static_cast<std::int64_t>(family.child_ids.size()));
    batch.i64(6,static_cast<std::int64_t>(gen));batch.execute();
}
void multi_owner(sqlite3* db,const DependentFamilyManifest& family){
    Statement owner(db,"SELECT owner,phase,generation FROM sg_regional_mode_authority "
                       "WHERE scope=?");owner.text(1,family.region_scope);
    if(!owner.row() || sqlite3_column_int(owner.get(),0)!=0 ||
       sqlite3_column_int(owner.get(),1)!=0 ||
       sqlite3_column_int64(owner.get(),2)!=
           static_cast<std::int64_t>(family.expected_owner_generation) || owner.row())
        throw std::runtime_error("multi-family region custody owner changed");
}
} // namespace

void PersistentWorld::step_with_multi_dependent_transport(
        const MultiDependentTransportPlan& p,FailPoint fail){
    if(p.expected_world_generation!=generation_||!p.command_id||!p.transport_id||
       !p.ferry||!p.origin||!p.destination||p.origin==p.destination||
       p.families.size()<2||p.families.size()>8)
        throw std::invalid_argument("invalid multi-family boarding/arrival plan");
    auto next=world_;
    const auto first=next.events().size(),activities=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1||!due.count(p.command_id))
        throw std::runtime_error("multi-family command must be only due world action");
    const auto& cmd=due.at(p.command_id);
    if(cmd.issuer!=Simulation::kServerAuthority||
       cmd.action!=(p.boarding?Action::HostEmbarkPassengers:Action::HostDisembarkPassengers)||
       cmd.actor!=p.ferry||cmd.target!=(p.boarding?p.origin:p.destination)||
       cmd.transport_id!=p.transport_id)
        throw std::runtime_error("invalid real multi-family ferry command");
    std::size_t total_children=0;
    std::set<std::uint64_t> all_children,all_adults;
    std::string previous_scope;
    for(const auto& f:p.families){
        if(f.family_scope.empty()||f.region_scope.empty()||
           f.family_scope<=previous_scope||!f.family_seed||!f.guardian_id||
           !f.expected_owner_generation ||
           (f.co_guardian_id&&f.co_guardian_id==f.guardian_id)||
           f.child_ids.empty()||f.child_ids.size()>15||
           !std::is_sorted(f.child_ids.begin(),f.child_ids.end())||
           std::adjacent_find(f.child_ids.begin(),f.child_ids.end())!=f.child_ids.end())
            throw std::invalid_argument("noncanonical family manifest");
        previous_scope=f.family_scope;
        for(const auto adult:{f.guardian_id,f.co_guardian_id}){
            if(!adult)continue;
            if(!all_adults.insert(adult).second ||
               std::find(cmd.passenger_ids.begin(),cmd.passenger_ids.end(),adult)==
                   cmd.passenger_ids.end())
                throw std::runtime_error("guardian omitted or duplicated across families");
            const auto n=world_.npc_records().find(adult);
            if(n==world_.npc_records().end()||n->second.home_ship!=p.origin||
               n->second.location!=(p.boarding?p.origin:p.ferry)||
               n->second.transport_id!=(p.boarding?0:p.transport_id))
                throw std::runtime_error("multi-family parent has wrong current custody");
        }
        NpcAppearanceStore family(store_->sqlite_path(),f.family_scope,f.family_seed,
            [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
        if(family.current_residence(f.guardian_id)!=p.origin||
           (f.co_guardian_id&&family.current_residence(f.co_guardian_id)!=p.origin))
            throw std::runtime_error("multi-family guardian not enrolled at original home");
        sqlite3* verify=nullptr;
        if(sqlite3_open_v2(store_->sqlite_path().c_str(),&verify,
             SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
            if(verify)sqlite3_close(verify);
            throw std::runtime_error("family birth DB unavailable");
        }
        try{
            std::vector<std::uint64_t> expected;
            Statement births(verify,"SELECT child_id,parent_a,parent_b FROM npc_family_births "
                "WHERE scope=? AND (parent_a=? OR parent_b=?) ORDER BY child_id");
            births.text(1,f.family_scope);births.text(2,std::to_string(f.guardian_id));
            births.text(3,std::to_string(f.guardian_id));
            while(births.row()){
                const auto child=parse_u64(sqlite3_column_text(births.get(),0));
                const auto a=parse_u64(sqlite3_column_text(births.get(),1));
                const auto b=parse_u64(sqlite3_column_text(births.get(),2));
                if(f.co_guardian_id&&!((a==f.guardian_id&&b==f.co_guardian_id)||
                   (a==f.co_guardian_id&&b==f.guardian_id)))
                    throw std::runtime_error("wrong multi-family biological parents");
                if(family.current_residence(child)==p.origin)expected.push_back(child);
            }
            if(expected!=f.child_ids)throw std::runtime_error("incomplete multi-family child manifest");
        }catch(...){sqlite3_close(verify);throw;}
        sqlite3_close(verify);
        for(const auto child:f.child_ids){
            if(child<(1ULL<<62)||world_.npc_records().count(child)||
               !all_children.insert(child).second)
                throw std::runtime_error("reused, duplicated or playable dependent ID");
            const auto legs=store_->dependent_travel(f.family_scope);
            if(p.boarding){
                for(const auto& leg:legs)if(leg.child_id==child&&
                    (leg.status==1||leg.transport_id==p.transport_id))
                    throw std::runtime_error("minor already traveling");
            }else{
                std::size_t count=0;
                for(const auto& leg:legs)if(leg.child_id==child&&leg.status==1&&
                    leg.transport_id==p.transport_id&&leg.ferry==p.ferry&&
                    leg.guardian_id==f.guardian_id&&leg.co_guardian_id==f.co_guardian_id&&
                    leg.origin==p.origin&&leg.destination==p.destination)++count;
                if(count!=1)throw std::runtime_error("minor missing from destination arrival");
            }
        }
        regional_events::CheckpointStore r(store_->sqlite_path(),f.region_scope);
        regional_events::RegionalModeAuthority authority(r);
        const auto owner=authority.state();
        if(owner.owner!=regional_events::SimulationOwner::Cold||
           owner.phase!=regional_events::TransitionPhase::Stable||
           owner.generation!=f.expected_owner_generation)
            throw std::runtime_error("multi-family owner not stable cold");
        total_children+=f.child_ids.size();
    }
    if(total_children+cmd.passenger_ids.size()>16)
        throw std::runtime_error("adult/child manifest exceeds real ferry capacity");
    if(p.boarding){
        if(store_->active_dependent_berths(p.ferry)!=0||
           next.passengers_aboard(p.ferry)+cmd.passenger_ids.size()+total_children>16)
            throw std::runtime_error("multi-family boarding exceeds occupied ferry berths");
    }else if(store_->active_dependent_berths(p.ferry)!=
              static_cast<std::int64_t>(total_children))
        throw std::runtime_error("unmanifested child aboard receiving ferry");
    next.step();
    const std::vector<Event> delta(next.events().begin()+static_cast<std::ptrdiff_t>(first),
                                   next.events().end());
    if(delta.size()!=1||delta[0].command_id!=p.command_id||
       delta[0].result!=Outcome::Applied||
       next.voyage_status(p.transport_id)!=(p.boarding?1U:2U))
        throw std::runtime_error("physical multi-family ferry command not applied");
    for(const auto adult:all_adults){
        const auto& n=next.npc_records().at(adult);
        if(n.location!=(p.boarding?p.ferry:p.destination)||
           n.transport_id!=(p.boarding?p.transport_id:0))
            throw std::runtime_error("family adult missing from committed ferry state");
    }
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(activities),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t gen){
        multi_schema(db);
        for(const auto& f:p.families){
            multi_owner(db,f);
            multi_region_bind(db,f,p.command_id,gen,p.boarding);
            multi_receipt(db,f,p.command_id,p.transport_id,p.boarding?1U:2U,gen);
            for(const auto child:f.child_ids){
                if(p.boarding){
                    Statement ins(db,"INSERT INTO sg_dependent_travel (family_scope,child_id,"
                        "transport_id,guardian_id,ferry,origin,destination,board_command,"
                        "board_generation,board_tick,status,co_guardian_id) "
                        "VALUES(?,?,?,?,?,?,?,?,?,?,1,?)");
                    ins.text(1,f.family_scope);ins.text(2,std::to_string(child));
                    ins.text(3,std::to_string(p.transport_id));
                    ins.text(4,std::to_string(f.guardian_id));
                    ins.text(5,std::to_string(p.ferry));
                    ins.text(6,std::to_string(p.origin));
                    ins.text(7,std::to_string(p.destination));
                    ins.text(8,std::to_string(p.command_id));
                    ins.i64(9,static_cast<std::int64_t>(gen));
                    ins.i64(10,static_cast<std::int64_t>(next.tick()));
                    ins.text(11,std::to_string(f.co_guardian_id));ins.execute();
                }else{
                    Statement update(db,"UPDATE sg_dependent_travel SET status=2,"
                        "arrival_command=?,arrival_generation=?,arrival_tick=? WHERE "
                        "family_scope=? AND child_id=? AND transport_id=? AND status=1");
                    update.text(1,std::to_string(p.command_id));
                    update.i64(2,static_cast<std::int64_t>(gen));
                    update.i64(3,static_cast<std::int64_t>(next.tick()));
                    update.text(4,f.family_scope);update.text(5,std::to_string(child));
                    update.text(6,std::to_string(p.transport_id));update.execute();
                    if(sqlite3_changes(db)!=1)throw std::runtime_error("failed multi-family minor arrival CAS");
                }
            }
        }
        Statement anchor(db,"INSERT OR IGNORE INTO sg_dependent_transport_anchor "
                         "VALUES(1,?)");anchor.i64(1,static_cast<std::int64_t>(gen));anchor.execute();
        Statement capacity(db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? AND status=1");
        capacity.text(1,std::to_string(p.ferry));
        if(!capacity.row()||next.passengers_aboard(p.ferry)+
           static_cast<std::uint64_t>(sqlite3_column_int64(capacity.get(),0))>16)
            throw std::runtime_error("multi-family occupied seats overbooked");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,delta,due,
                   activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

void PersistentWorld::step_with_multi_dependent_rescue(
        const MultiDependentRescuePlan& p,FailPoint fail){
    if(p.expected_world_generation!=generation_||!p.command_id||
       !p.original_transport||!p.rescue_transport||
       p.original_transport==p.rescue_transport||!p.original_ferry||!p.rescue_ferry||
       p.original_ferry==p.rescue_ferry||!p.origin||!p.destination||
       p.origin==p.destination||p.families.size()<2||p.families.size()>8)
        throw std::invalid_argument("invalid multi-family rescue authority");
    auto next=world_;
    const auto first=next.events().size(),activities=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1||!due.count(p.command_id))
        throw std::runtime_error("multi-family rescue requires single actual incident");
    const auto& cmd=due.at(p.command_id);
    if(cmd.action!=Action::HostEvacuatePassengers||
       cmd.issuer!=Simulation::kServerAuthority||
       cmd.actor!=p.original_ferry||cmd.target!=p.rescue_ferry||
       cmd.transport_id!=p.original_transport||
       cmd.amount!=static_cast<std::int64_t>(p.rescue_transport))
        throw std::runtime_error("real adult evacuation does not match rescue plan");
    const auto original=world_.voyages_.find(p.original_transport);
    if(original==world_.voyages_.end()||original->second.ferry!=p.original_ferry||
       original->second.origin!=p.origin||
       (original->second.status!=1&&original->second.status!=3))
        throw std::runtime_error("original real stranded voyage unavailable");
    std::set<std::uint64_t> children,adults;
    std::string last;
    for(const auto& f:p.families){
        if(f.family_scope.empty()||f.family_scope<=last||f.region_scope.empty()||
           !f.family_seed||!f.guardian_id||!f.expected_owner_generation||
           (f.co_guardian_id&&f.co_guardian_id==f.guardian_id)||
           f.child_ids.empty()||f.child_ids.size()>15||
           !std::is_sorted(f.child_ids.begin(),f.child_ids.end())||
           std::adjacent_find(f.child_ids.begin(),f.child_ids.end())!=f.child_ids.end())
            throw std::invalid_argument("noncanonical multi-family rescue list");
        last=f.family_scope;
        NpcAppearanceStore family(store_->sqlite_path(),f.family_scope,f.family_seed,
            [&](std::uint64_t id){return world_.npc_records().count(id)!=0;});
        for(const auto adult:{f.guardian_id,f.co_guardian_id}){
            if(!adult)continue;
            if(!adults.insert(adult).second||
               std::find(cmd.passenger_ids.begin(),cmd.passenger_ids.end(),adult)==
                   cmd.passenger_ids.end())
                throw std::runtime_error("rescue guardian omitted or duplicated");
            const auto n=world_.npc_records().find(adult);
            if(n==world_.npc_records().end()||n->second.location!=p.original_ferry||
               n->second.transport_id!=p.original_transport||
               family.current_residence(adult)!=p.origin)
                throw std::runtime_error("guardian missing from stranded physical ferry");
        }
        const auto legs=store_->dependent_travel(f.family_scope);
        for(const auto child:f.child_ids){
            if(!children.insert(child).second||family.current_residence(child)!=p.origin)
                throw std::runtime_error("duplicate or absent mixed-family dependent");
            std::size_t valid=0;
            for(const auto& leg:legs)if(leg.child_id==child&&
               leg.status==1&&leg.transport_id==p.original_transport&&
               leg.ferry==p.original_ferry&&leg.guardian_id==f.guardian_id&&
               leg.co_guardian_id==f.co_guardian_id&&
               leg.origin==p.origin&&leg.destination==p.destination)++valid;
            if(valid!=1)throw std::runtime_error("unmanifested rescue child berth");
        }
        regional_events::CheckpointStore region(store_->sqlite_path(),f.region_scope);
        regional_events::RegionalModeAuthority authority(region);
        const auto owner=authority.state();
        if(owner.owner!=regional_events::SimulationOwner::Cold||
           owner.phase!=regional_events::TransitionPhase::Stable||
           owner.generation!=f.expected_owner_generation)
            throw std::runtime_error("multi-family rescue region ownership not stable");
    }
    if(store_->active_dependent_berths(p.original_ferry)!=
        static_cast<std::int64_t>(children.size()))
        throw std::runtime_error("multi-family evacuation omitted an active minor");
    if(children.size()+cmd.passenger_ids.size()>16||
       next.passengers_aboard(p.rescue_ferry)+cmd.passenger_ids.size()+children.size()>16)
        throw std::runtime_error("receiving rescue ferry berths exceeded");
    next.step();
    const std::vector<Event> delta(next.events().begin()+static_cast<std::ptrdiff_t>(first),
                                   next.events().end());
    if(delta.size()!=1||delta[0].command_id!=p.command_id||
       delta[0].result!=Outcome::Applied||
       next.voyage_status(p.original_transport)!=5||
       next.voyage_status(p.rescue_transport)!=1)
        throw std::runtime_error("actual evacuation was not applied");
    for(const auto adult:adults){
        const auto n=next.npc_records().find(adult);
        if(n==next.npc_records().end()||n->second.location!=p.rescue_ferry||
           n->second.transport_id!=p.rescue_transport)
            throw std::runtime_error("actual receiving adult custody missing");
    }
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(activities),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t gen){
        multi_schema(db);
        for(const auto& f:p.families){
            multi_owner(db,f);
            multi_region_bind(db,f,p.command_id,gen,false);
            multi_receipt(db,f,p.command_id,p.rescue_transport,3,gen);
            for(const auto child:f.child_ids){
                Statement close(db,"UPDATE sg_dependent_travel SET status=2,"
                    "arrival_command=?,arrival_generation=?,arrival_tick=?,termination_kind=1 "
                    "WHERE family_scope=? AND child_id=? AND transport_id=? AND status=1");
                close.text(1,std::to_string(p.command_id));
                close.i64(2,static_cast<std::int64_t>(gen));
                close.i64(3,static_cast<std::int64_t>(next.tick()));
                close.text(4,f.family_scope);close.text(5,std::to_string(child));
                close.text(6,std::to_string(p.original_transport));close.execute();
                if(sqlite3_changes(db)!=1)
                    throw std::runtime_error("original child berth not closed on rescue");
                Statement receiving(db,"INSERT INTO sg_dependent_travel (family_scope,child_id,"
                    "transport_id,guardian_id,ferry,origin,destination,board_command,"
                    "board_generation,board_tick,status,co_guardian_id,transfer_from_transport) "
                    "VALUES(?,?,?,?,?,?,?,?,?,?,1,?,?)");
                receiving.text(1,f.family_scope);receiving.text(2,std::to_string(child));
                receiving.text(3,std::to_string(p.rescue_transport));
                receiving.text(4,std::to_string(f.guardian_id));
                receiving.text(5,std::to_string(p.rescue_ferry));
                receiving.text(6,std::to_string(p.origin));
                receiving.text(7,std::to_string(p.destination));
                receiving.text(8,std::to_string(p.command_id));
                receiving.i64(9,static_cast<std::int64_t>(gen));
                receiving.i64(10,static_cast<std::int64_t>(next.tick()));
                receiving.text(11,std::to_string(f.co_guardian_id));
                receiving.text(12,std::to_string(p.original_transport));receiving.execute();
            }
        }
        Statement capacity(db,"SELECT COUNT(*) FROM sg_dependent_travel "
                              "WHERE ferry=? AND status=1");
        capacity.text(1,std::to_string(p.rescue_ferry));
        if(!capacity.row()||next.passengers_aboard(p.rescue_ferry)+
           static_cast<std::uint64_t>(sqlite3_column_int64(capacity.get(),0))>16)
            throw std::runtime_error("multi-family receiving ferry physical capacity exceeded");
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,delta,due,
                   activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

// v48: submission persists one scheduled, fully specified physical command but
// does NOT move a passenger. The following step is the sole execution path.
bool PersistentWorld::submit_host_atomic_split_rescue(
        const SplitRescueIntentPlan& plan,std::uint64_t command_id,FailPoint fail,
        int operating_crew_to_first){
    if(operating_crew_to_first<0)return false;
    if(!command_id||plan.receivers.size()!=2||plan.intent_id.empty()||
       plan.receivers[0].ferry>=plan.receivers[1].ferry||
       plan.expected_world_generation>generation_)
        return false;
    (void)verify_split_rescue_intent(plan.intent_id);
    Command command{};
    command.id=command_id;command.sequence=command_id;
    command.execute_tick=world_.tick()+1;
    command.issuer=Simulation::kServerAuthority;
    command.action=Action::HostSplitEvacuatePassengers;
    command.thrust_y=operating_crew_to_first; // v49, 0 retains exact v48 behavior
    command.actor=plan.source_ferry;
    command.target=plan.receivers[0].ferry;
    command.secondary_receiver=plan.receivers[1].ferry;
    command.amount=static_cast<std::int64_t>(plan.receivers[0].new_transport_id);
    command.secondary_transport=plan.receivers[1].new_transport_id;
    command.transport_id=plan.source_transport;
    std::set<std::uint64_t> used;
    for(std::size_t i=0;i<2;++i){
        std::vector<std::uint64_t> passengers;
        for(const auto& family:plan.receivers[i].families){
            if(!family.guardian_id||!family.co_guardian_id||
               family.guardian_id==family.co_guardian_id)return false;
            if(!used.insert(family.guardian_id).second||
               !used.insert(family.co_guardian_id).second)return false;
            passengers.push_back(family.guardian_id);
            passengers.push_back(family.co_guardian_id);
        }
        std::sort(passengers.begin(),passengers.end());
        command.passenger_ids.insert(command.passenger_ids.end(),
                                     passengers.begin(),passengers.end());
        if(i==0)command.thrust_x=static_cast<int>(passengers.size());
    }
    auto next=world_;
    if(!next.submit(command))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   &command,{},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}

void PersistentWorld::step_with_atomic_split_rescue(
        const SplitRescueIntentPlan& plan,std::uint64_t command_id,FailPoint fail){
    if(!command_id||plan.intent_id.empty()||plan.receivers.size()!=2||
       plan.expected_world_generation>generation_||
       plan.receivers[0].ferry>=plan.receivers[1].ferry)
        throw std::invalid_argument("split rescue not tied to an admitted two-receiver plan");
    (void)verify_split_rescue_intent(plan.intent_id);
    auto next=world_;
    const auto first=next.events().size(),prior_activity=next.activity_events().size();
    std::map<std::uint64_t,Command> due;
    if(auto it=next.commands_.find(next.tick()+1);it!=next.commands_.end())
        for(const auto& c:it->second)due.emplace(c.id,c);
    if(due.size()!=1||!due.count(command_id))
        throw std::runtime_error("split rescue needs one authenticated host command");
    const auto& cmd=due.at(command_id);
    if(cmd.action!=Action::HostSplitEvacuatePassengers||
       cmd.issuer!=Simulation::kServerAuthority||cmd.actor!=plan.source_ferry||
       cmd.transport_id!=plan.source_transport||
       cmd.target!=plan.receivers[0].ferry||
       cmd.secondary_receiver!=plan.receivers[1].ferry||
       cmd.amount!=static_cast<std::int64_t>(plan.receivers[0].new_transport_id)||
       cmd.secondary_transport!=plan.receivers[1].new_transport_id||
       plan.origin==plan.destination||!plan.origin||!plan.destination)
        throw std::runtime_error("actual split command does not match allocated receiving voyages");
    std::set<std::uint64_t> expected_adults;
    std::set<std::pair<std::string,std::uint64_t>> expected_children;
    std::vector<std::vector<std::uint64_t>> receiver_adults;
    std::string last_scope;
    for(const auto& receiver:plan.receivers){
        std::vector<std::uint64_t> adults;
        if(receiver.families.empty())throw std::runtime_error("empty receiver allocation");
        for(const auto& family:receiver.families){
            if(family.family_scope.empty()||!family.family_seed||
               !family.guardian_id||!family.co_guardian_id||
               family.guardian_id==family.co_guardian_id||family.child_ids.empty()||
               !std::is_sorted(family.child_ids.begin(),family.child_ids.end())||
               std::adjacent_find(family.child_ids.begin(),family.child_ids.end())!=
                   family.child_ids.end())
                throw std::runtime_error("noncanonical family on split rescue");
            for(const auto adult:{family.guardian_id,family.co_guardian_id}){
                if(!expected_adults.insert(adult).second)
                    throw std::runtime_error("adult assigned twice to two rescue ferries");
                adults.push_back(adult);
                const auto npc=world_.npc_records().find(adult);
                if(npc==world_.npc_records().end()||
                   npc->second.location!=plan.source_ferry||
                   npc->second.transport_id!=plan.source_transport)
                    throw std::runtime_error("split rescue source adult missing");
            }
            for(const auto child:family.child_ids){
                if(!expected_children.emplace(family.family_scope,child).second)
                    throw std::runtime_error("dependent assigned twice to two rescue ferries");
                std::uint64_t active=0;
                for(const auto& leg:store_->dependent_travel(family.family_scope))
                    if(leg.child_id==child&&leg.ferry==plan.source_ferry&&
                       leg.transport_id==plan.source_transport&&leg.status==1&&
                       leg.guardian_id==family.guardian_id&&
                       leg.co_guardian_id==family.co_guardian_id)++active;
                if(active!=1)throw std::runtime_error("dependent not actively aboard original ferry");
            }
        }
        std::sort(adults.begin(),adults.end());
        receiver_adults.push_back(std::move(adults));
    }
    // v49: operating crew are a physically distinct named-NPC partition.
    // They are not added to the pre-existing passenger manifest and retain
    // legal home and assigned job while in-transit on the receiving ferry.
    std::vector<std::uint64_t> native_crew;
    if(cmd.thrust_y>0){
        for(const auto& [id,n]:world_.npc_records())
            if(n.home_ship==plan.source_ferry && n.role==NpcRole::Ship &&
               n.assigned_site==plan.source_ferry){
                if(n.location!=plan.source_ferry||n.transport_id)
                    throw std::runtime_error("split crew not physically available on source ferry");
                native_crew.push_back(id);
            }
        const auto* source=world_.colony_ship(plan.source_ferry);
        if(!source||native_crew.size()!=static_cast<std::size_t>(source->crew)||
           native_crew.size()<2||
           static_cast<std::size_t>(cmd.thrust_y)>=native_crew.size())
            throw std::runtime_error("split crew roster incomplete or invalid partition");
    }
    const auto crew_primary=static_cast<std::size_t>(cmd.thrust_y);
    std::vector<std::vector<std::uint64_t>> receiver_crew(2);
    receiver_crew[0].assign(native_crew.begin(),native_crew.begin()+crew_primary);
    receiver_crew[1].assign(native_crew.begin()+crew_primary,native_crew.end());
    std::vector<std::uint64_t> manifest=receiver_adults[0];
    manifest.insert(manifest.end(),receiver_adults[1].begin(),receiver_adults[1].end());
    if(manifest!=cmd.passenger_ids||
       static_cast<std::size_t>(cmd.thrust_x)!=receiver_adults[0].size()||
       store_->active_dependent_berths(plan.source_ferry)!=
           static_cast<std::int64_t>(expected_children.size())||
       world_.passengers_aboard(plan.source_ferry)!=expected_adults.size())
        throw std::runtime_error("incomplete source passenger/census split manifest");
    for(std::size_t i=0;i<2;++i){
        const auto& receiver=plan.receivers[i];
        std::size_t child_count=0;
        for(const auto& family:receiver.families)child_count+=family.child_ids.size();
        if(store_->active_dependent_berths(receiver.ferry)!=0||
           world_.passengers_aboard(receiver.ferry)!=0||
           child_count+receiver_adults[i].size()+receiver_crew[i].size()>16)
            throw std::runtime_error("split rescue destination is full or occupied");
    }
    next.step();
    const std::vector<Event> events(next.events().begin()+
         static_cast<std::ptrdiff_t>(first),next.events().end());
    if(events.size()!=1||events[0].command_id!=command_id||
       events[0].result!=Outcome::Applied||
       next.voyage_status(plan.source_transport)!=5)
        throw std::runtime_error("single atomic split world event was not applied");
    for(std::size_t i=0;i<2;++i){
        const auto& receiver=plan.receivers[i];
        if(next.voyage_status(receiver.new_transport_id)!=1||
           next.passengers_aboard(receiver.ferry)!=
               receiver_adults[i].size()+receiver_crew[i].size())
            throw std::runtime_error("split receiver physical adult manifest incomplete");
        for(const auto adult:receiver_adults[i]){
            const auto npc=next.npc_records().find(adult);
            if(npc==next.npc_records().end()||npc->second.location!=receiver.ferry||
               npc->second.transport_id!=receiver.new_transport_id)
                throw std::runtime_error("split adult not physically aboard allocated vessel");
        }
        for(const auto crew:receiver_crew[i]){
            const auto before=world_.npc_records().find(crew);
            const auto after=next.npc_records().find(crew);
            if(before==world_.npc_records().end()||after==next.npc_records().end()||
               before->second.home_ship!=plan.source_ferry||
               before->second.assigned_site!=plan.source_ferry||
               after->second.home_ship!=before->second.home_ship||
               after->second.assigned_site!=before->second.assigned_site||
               after->second.role!=NpcRole::Ship||
               after->second.location!=receiver.ferry||
               after->second.transport_id!=receiver.new_transport_id)
                throw std::runtime_error("operating crew identity, job, or rescue custody incorrect");
        }
    }
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+
        static_cast<std::ptrdiff_t>(prior_activity),next.activity_events().end());
    const auto effect=[&](sqlite3* db,std::uint64_t gen){
        assert_split_execution_intent(db,plan,gen);
        multi_schema(db);
        for(std::size_t i=0;i<2;++i){
            const auto& receiver=plan.receivers[i];
            for(const auto& family:receiver.families){
                multi_owner(db,family); // CAS inside this same IMMEDIATE transaction
                multi_region_bind(db,family,command_id,gen,false);
                multi_receipt(db,family,command_id,receiver.new_transport_id,3,gen);
                for(const auto child:family.child_ids){
                    Statement close(db,"UPDATE sg_dependent_travel SET status=2,"
                        "arrival_command=?,arrival_generation=?,arrival_tick=?,termination_kind=1 "
                        "WHERE family_scope=? AND child_id=? AND transport_id=? AND "
                        "ferry=? AND status=1");
                    close.text(1,std::to_string(command_id));
                    close.i64(2,static_cast<std::int64_t>(gen));
                    close.i64(3,static_cast<std::int64_t>(next.tick()));
                    close.text(4,family.family_scope);close.text(5,std::to_string(child));
                    close.text(6,std::to_string(plan.source_transport));
                    close.text(7,std::to_string(plan.source_ferry));close.execute();
                    if(sqlite3_changes(db)!=1)
                        throw std::runtime_error("split child original berth not closed");
                    Statement berth(db,"INSERT INTO sg_dependent_travel (family_scope,child_id,"
                        "transport_id,guardian_id,ferry,origin,destination,board_command,"
                        "board_generation,board_tick,status,co_guardian_id,transfer_from_transport) "
                        "VALUES(?,?,?,?,?,?,?,?,?,?,1,?,?)");
                    berth.text(1,family.family_scope);berth.text(2,std::to_string(child));
                    berth.text(3,std::to_string(receiver.new_transport_id));
                    berth.text(4,std::to_string(family.guardian_id));
                    berth.text(5,std::to_string(receiver.ferry));
                    berth.text(6,std::to_string(plan.origin));
                    berth.text(7,std::to_string(plan.destination));
                    berth.text(8,std::to_string(command_id));
                    berth.i64(9,static_cast<std::int64_t>(gen));
                    berth.i64(10,static_cast<std::int64_t>(next.tick()));
                    berth.text(11,std::to_string(family.co_guardian_id));
                    berth.text(12,std::to_string(plan.source_transport));berth.execute();
                }
            }
            Statement seats(db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? AND status=1");
            seats.text(1,std::to_string(receiver.ferry));
            if(!seats.row()||next.passengers_aboard(receiver.ferry)+
               static_cast<std::uint64_t>(sqlite3_column_int64(seats.get(),0))>16)
                throw std::runtime_error("split receiver berth count overbooked");
            if(i==0&&fail==FailPoint::AfterFirstSplitReceiver)
                throw std::runtime_error("injected failure after first physical rescue receiver");
        }
        Statement left(db,"SELECT COUNT(*) FROM sg_dependent_travel WHERE ferry=? "
                          "AND transport_id=? AND status=1");
        left.text(1,std::to_string(plan.source_ferry));
        left.text(2,std::to_string(plan.source_transport));
        if(!left.row()||sqlite3_column_int64(left.get(),0)!=0)
            throw std::runtime_error("split left child behind on source ferry");
        if(!native_crew.empty())
            record_split_crew_execution(db,plan,command_id,gen,native_crew,crew_primary);
        record_split_execution(db,plan,command_id,gen,next.tick());
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,nullptr,
                   events,due,activity,fail,nullptr,nullptr,effect);
    world_.swap(next);++generation_;
}

void PersistentWorld::settle_constructed_stock(const Command& command,
    const std::function<void(sqlite3*,std::uint64_t)>& effect,FailPoint fail){
    if((command.action!=Action::HostEscrowConstructedStock &&
        command.action!=Action::HostReturnConstructedStock)||
       command.issuer!=Simulation::kServerAuthority||
       command.execute_tick!=world_.tick()+1||!effect)
        throw std::runtime_error("invalid host warehouse debit authority");
    const auto scheduled=world_.commands_.find(command.execute_tick);
    if(scheduled!=world_.commands_.end()&&!scheduled->second.empty())
        throw std::runtime_error("warehouse debit requires exclusive scheduled world step");
    auto next=world_;
    if(!next.submit(command))throw std::runtime_error("world rejected warehouse debit");
    const auto e=next.events().size(),a=next.activity_events().size();
    next.step();
    if(next.events().size()!=e+1||next.events().back().command_id!=command.id||
       next.events().back().result!=Outcome::Applied)
        throw std::runtime_error("physical storage withdrawal not applied");
    const std::vector<Event> events(next.events().begin()+static_cast<std::ptrdiff_t>(e),next.events().end());
    const std::vector<ActivityEvent> activity(next.activity_events().begin()+static_cast<std::ptrdiff_t>(a),next.activity_events().end());
    const std::map<std::uint64_t,Command> due{{command.id,command}};
    const auto enforced_effect=[&](sqlite3* db,std::uint64_t gen){
        effect(db,gen);
        if(command.action==Action::HostReturnConstructedStock){
            Statement witness(db,"SELECT COUNT(*) FROM sg_v53_world_return r "
                "JOIN sg_v51_event e ON e.scope=r.scope AND e.event_id=r.event_id "
                "WHERE r.command_id=?1 AND r.site=?2 AND r.source_ship=?3 "
                "AND r.stock_kg=?4 AND r.world_generation=?5 AND e.kind='world-return' "
                "AND e.world_generation=?5 AND e.after_payload=r.after_payload");
            witness.i64(1,static_cast<std::int64_t>(command.id));
            witness.i64(2,static_cast<std::int64_t>(command.target));
            witness.i64(3,static_cast<std::int64_t>(command.actor));
            witness.i64(4,command.amount);
            witness.i64(5,static_cast<std::int64_t>(gen));
            if(!witness.row()||sqlite3_column_int64(witness.get(),0)!=1)
                throw std::runtime_error("world return requires committed factory debit witness");
        }
    };
    store_->commit(next,generation_,resource_origin_,credit_origin_,&command,events,due,activity,
                   fail,nullptr,nullptr,enforced_effect);
    world_.swap(next);++generation_;
}

void PersistentWorld::enable_native_3d_physics(FailPoint fail) {
    if(world_.native_3d_physics())return;
    auto next=world_;
    next.enable_native_3d_physics();
    // Refuse to publish a nonroundtripping or physically lossy migration.
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash() || next.total_ore()!=resource_origin_ ||
       next.total_credits()!=credit_origin_)
        throw std::runtime_error("SPACE-04B native 3D migration not lossless");
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,{},{},{},fail);
    world_.swap(next);
    ++generation_;
}

void PersistentWorld::enable_native_galactic_physics(
        const std::string& native_sqlite,FailPoint fail) {
    if(world_.native_galactic_physics())return;
    if(native_sqlite.empty())
        throw std::invalid_argument("original galaxy SQLite path required");
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(native_sqlite.c_str(),&db,
        SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
        if(db)sqlite3_close_v2(db);
        throw std::runtime_error("cannot read real original star origins");
    }
    std::map<std::uint32_t,galactic::Point> origins;
    try{
        if(!galactic::snapshot_match(db,*this))
            throw std::runtime_error("stale native galaxy source during v120 upgrade");
        galactic::Query q(db,"SELECT system,sector_x,mm_x,sector_y,mm_y,"
            "sector_z,mm_z FROM sg_galaxy_system_origin ORDER BY system");
        while(q.row()){
            const auto system=q.n(0);
            if(system<=0 ||
               system>std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("untrusted system registry key");
            const galactic::Point star{{q.n(1),q.n(2)},
                {q.n(3),q.n(4)},{q.n(5),q.n(6)}};
            if(!star.valid() ||
               !origins.emplace(static_cast<std::uint32_t>(system),star).second)
                throw std::runtime_error("invalid immutable galaxy registry");
        }
        sqlite3_close_v2(db);db=nullptr;
    }catch(...){if(db)sqlite3_close_v2(db);throw;}
    auto next=world_;
    next.enable_native_galactic_physics(origins);
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash() || next.total_ore()!=resource_origin_ ||
       next.total_credits()!=credit_origin_)
        throw std::runtime_error("v120 galaxy migration not lossless");
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,{},{},{},fail);
    world_.swap(next);
    ++generation_;
}

void PersistentWorld::enable_native_3d_probes(FailPoint fail){
    if(world_.native_3d_probes())return;
    auto next=world_;
    next.enable_native_3d_probes();
    if(Simulation::from_snapshot(next.canonical_state()).state_hash()!=
       next.state_hash() || next.total_ore()!=resource_origin_ ||
       next.total_credits()!=credit_origin_)
        throw std::runtime_error("v121 3D probes migration not lossless");
    store_->commit(next,generation_,resource_origin_,credit_origin_,
                   nullptr,{},{},{},fail);
    world_.swap(next);
    ++generation_;
}

void PersistentWorld::step(FailPoint fail) {
    step_with_cell_worker(nullptr,fail);
}
void PersistentWorld::step_as_cell_worker(
        const NativeCellWorkerTick& token,FailPoint fail) {
    step_with_cell_worker(&token,fail);
}
void PersistentWorld::step_with_cell_worker(
        const NativeCellWorkerTick* token,FailPoint fail) {
    // SPACE-05: no active-worker authorization can make an ordinary host
    // tick into a distributed writer. One original SQLite writer remains.
    if(token){
        if(token->canonical_cell_scope.empty() || token->trusted_worker.empty() ||
           token->canonical_cell_scope.size()>256 ||
           token->trusted_worker.size()>128 || !token->worker_epoch ||
           !token->region_generation || !token->checkpoint_revision)
            throw std::runtime_error("invalid native cell worker admission");
        bool occupied=false;
        for(const auto& [id,e]:world_.entities()){
            if(e.type!=EntityType::Ship)continue;
            const auto cell=spatial::ship_cell(world_,id);
            if(!cell)
                throw std::runtime_error("missing physical native ship residency");
            if(spatial::scope(*cell)==token->canonical_cell_scope)
                occupied=true;
        }
        if(!occupied)
            throw std::runtime_error("worker dispatch has no occupied physical cell");
        // The original Simulation::step still advances ALL entities once.
        // This token elects its serialized writer, not per-cell physics.
    }
    auto next = world_;
    const auto before = next.events().size();
    const auto before_activity = next.activity_events().size();
    std::map<std::uint64_t, Command> due;
    // One logical step processes precisely the commands scheduled for tick+1.
    if (auto it = next.commands_.find(next.tick() + 1); it != next.commands_.end()) {
        for (const auto& c : it->second) due.emplace(c.id, c);
    }
    // Never allow the ordinary worker tick to publish a residence change
    // without its ART02/ECON03 settlement in the same SQLite transaction.
    for(const auto& [id,c]:due){(void)id;
        if(c.action==Action::HostEscrowConstructedStock ||
           c.action==Action::HostReturnConstructedStock)
            throw std::runtime_error("constructed warehouse transfer requires atomic network effect");
        if(c.action==Action::HostSettleMigrant)
            throw std::runtime_error("migration needs host atomic society step");
        if(c.action==Action::HostSplitEvacuatePassengers)
            throw std::runtime_error("split evacuation requires atomic two-receiver child custody step");
        if(c.action==Action::HostColonyDeparture)
            throw std::runtime_error("colony departure requires atomic full-population host step");
        if(c.action==Action::HostRegisterDependent)
            throw std::runtime_error("physical child requires birth-authorized atomic host step");
    }
    for(const auto& [id,c]:due){(void)id;
        if((c.action==Action::HostDisembarkPassengers ||
            c.action==Action::HostDestroyPassengerFerry ||
            c.action==Action::HostEvacuatePassengers ||
            c.action==Action::HostResolveMixedFerryLoss) &&
           store_->active_dependent_berths(c.actor)>0)
            throw std::runtime_error("dependent travelers need atomic child custody step");
    }
    next.step();
    // Capture precisely those new native commands created INSIDE this
    // simulation tick, not already accepted host/player commands.
    std::vector<Command> generated_native_commands;
    if(const auto at=next.commands_.find(next.tick()+1);
       at!=next.commands_.end()){
        for(const auto& c:at->second)
            if(!world_.submitted_ids_.count(c.id) &&
               (c.id & (std::uint64_t{1}<<61)) &&
               !(c.id & (std::uint64_t{1}<<62)))
                generated_native_commands.push_back(c);
    }
    const std::vector<Event> new_events(next.events().begin() + static_cast<std::ptrdiff_t>(before), next.events().end());
    const std::vector<ActivityEvent> new_activity(
        next.activity_events().begin() + static_cast<std::ptrdiff_t>(before_activity), next.activity_events().end());
    // Validation occurs under the original Store::commit BEGIN IMMEDIATE
    // *after* the exact new world/spatial index is inserted and before COMMIT.
    // Worker takeover, region handoff or stale world changes are serialized
    // with this same lock. A refusal rolls back the entire simulated tick.
    // Store::commit is the final admission authority for both worker
    // ticks and EVERY other world mutation, under the original SQLite lock.
    store_->commit(next, generation_, resource_origin_, credit_origin_, nullptr,
                   new_events, due, new_activity, fail,nullptr,nullptr,{},token,
                   generated_native_commands);
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
