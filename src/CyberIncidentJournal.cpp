#include "galaxy/CyberIncidentJournal.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy::colony_disposition {
namespace {
void exec(sqlite3* db,const char* sql){
    char* error=nullptr;
    const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&error);
    if(rc!=SQLITE_OK){
        std::string message=error?error:sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("cyber casebook SQLite: "+message);
    }
}
struct Statement {
    sqlite3* db=nullptr;
    sqlite3_stmt* stmt=nullptr;
    Statement(sqlite3* d,const char* sql):db(d){
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error(
                std::string("cyber casebook prepare: ")+sqlite3_errmsg(db));
    }
    ~Statement(){sqlite3_finalize(stmt);}
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
    void number(int slot,std::uint64_t value){
        if(value>static_cast<std::uint64_t>(
                 std::numeric_limits<sqlite3_int64>::max()) ||
           sqlite3_bind_int64(stmt,slot,static_cast<sqlite3_int64>(value))
                !=SQLITE_OK)
            throw std::runtime_error("cyber casebook numeric binding invalid");
    }
    void string(int slot,const std::string& value){
        if(sqlite3_bind_text(stmt,slot,value.c_str(),
               static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("cyber casebook text binding invalid");
    }
};
std::uint64_t number(sqlite3_stmt* row,int column){
    if(sqlite3_column_type(row,column)!=SQLITE_INTEGER ||
       sqlite3_column_int64(row,column)<0)
        throw std::runtime_error("cyber casebook corrupt numeric field");
    return static_cast<std::uint64_t>(sqlite3_column_int64(row,column));
}
std::string string(sqlite3_stmt* row,int column){
    if(sqlite3_column_type(row,column)!=SQLITE_TEXT)
        throw std::runtime_error("cyber casebook corrupt digest field");
    const auto* value=sqlite3_column_text(row,column);
    return value?reinterpret_cast<const char*>(value):std::string{};
}
// A non-cryptographic integrity checksum, matching other original-game
// journals. Trigger-enforced immutability and world-command provenance
// provide the constraints; never call this a secure digital signature.
std::string digest(const CyberIncidentCase& c){
    std::ostringstream fields;
    fields<<c.previous_digest<<'|'<<c.sequence<<'|'<<c.case_id<<'|'
          <<c.command_id<<'|'<<c.world_tick<<'|'<<c.world_generation
          <<'|'<<c.observer_npc<<'|'<<c.jurisdiction_colony
          <<'|'<<c.attacking_ship<<'|'<<c.victim_ship
          <<'|'<<c.attacker_owner<<'|'<<c.victim_owner<<'|'
          <<static_cast<unsigned>(c.confusion_applied);
    if(c.record_version>=2)
        fields<<'|'<<static_cast<unsigned>(c.record_version)<<'|'
              <<static_cast<unsigned>(c.sensor_detail)<<'|'
              <<c.observed_description;
    std::uint64_t value=14695981039346656037ULL;
    for(const unsigned char letter:fields.str()){
        value^=letter;
        value*=1099511628211ULL;
    }
    std::ostringstream out;
    out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;
    return out.str();
}
} // namespace

struct CyberIncidentJournal::State {
    sqlite3* db=nullptr;
};
CyberIncidentJournal::CyberIncidentJournal(
    std::string original_world_db_path):state_(new State){
    try {
        // A security casebook cannot create a second pretend game/world DB.
        // The existing PersistentWorld must already own this SQLite save.
        if(sqlite3_open_v2(original_world_db_path.c_str(),&state_->db,
              SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,
              nullptr)!=SQLITE_OK)
            throw std::runtime_error("cyber casebook failed to open existing world SQLite");
        sqlite3_busy_timeout(state_->db,10000);
        {
            Statement schema(state_->db,
                "SELECT 1 FROM sqlite_master "
                "WHERE type='table' AND name='snapshots'");
            if(sqlite3_step(schema.stmt)!=SQLITE_ROW)
                throw std::runtime_error("cyber casebook requires original world snapshots");
        }
        exec(state_->db,"PRAGMA foreign_keys=ON");
        exec(state_->db,
            "CREATE TABLE IF NOT EXISTS sg_ship_status07_incidents ("
            "sequence INTEGER PRIMARY KEY,case_id INTEGER NOT NULL UNIQUE,"
            "command_id INTEGER NOT NULL,world_tick INTEGER NOT NULL,"
            "world_generation INTEGER NOT NULL,observer INTEGER NOT NULL,"
            "colony INTEGER NOT NULL,attacker_ship INTEGER NOT NULL,"
            "victim_ship INTEGER NOT NULL,attacker_owner INTEGER NOT NULL,"
            "victim_owner INTEGER NOT NULL,applied INTEGER NOT NULL,"
            "prev_digest TEXT NOT NULL,digest TEXT NOT NULL,"
            "observed_description TEXT NOT NULL DEFAULT '',"
            "sensor_detail INTEGER NOT NULL DEFAULT 0,"
            "record_version INTEGER NOT NULL DEFAULT 1,"
            "UNIQUE(colony,command_id),CHECK(applied IN (0,1)))");
        // Additive migration of the earlier SHIP-STATUS-07 draft schema.
        // Old case ancestry remains readable under digest version 1.
        {
            Statement fields(state_->db,
                "PRAGMA table_info(sg_ship_status07_incidents)");
            bool description=false,detail=false,version=false;
            int rc=SQLITE_OK;
            while((rc=sqlite3_step(fields.stmt))==SQLITE_ROW){
                const auto* name=sqlite3_column_text(fields.stmt,1);
                const std::string field=name?
                    reinterpret_cast<const char*>(name):"";
                if(field=="observed_description")description=true;
                if(field=="sensor_detail")detail=true;
                if(field=="record_version")version=true;
            }
            if(rc!=SQLITE_DONE)throw std::runtime_error(
                "cyber casebook cannot inspect original schema");
            if(!description)exec(state_->db,
                "ALTER TABLE sg_ship_status07_incidents ADD COLUMN "
                "observed_description TEXT NOT NULL DEFAULT ''");
            if(!detail)exec(state_->db,
                "ALTER TABLE sg_ship_status07_incidents ADD COLUMN "
                "sensor_detail INTEGER NOT NULL DEFAULT 0");
            if(!version)exec(state_->db,
                "ALTER TABLE sg_ship_status07_incidents ADD COLUMN "
                "record_version INTEGER NOT NULL DEFAULT 1");
        }
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_status07_immutable_update "
            "BEFORE UPDATE ON sg_ship_status07_incidents "
            "BEGIN SELECT RAISE(ABORT,'immutable cyber case'); END");
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_status07_immutable_delete "
            "BEFORE DELETE ON sg_ship_status07_incidents "
            "BEGIN SELECT RAISE(ABORT,'immutable cyber case'); END");
        verify();
    }catch(...){
        if(state_->db)sqlite3_close(state_->db);
        delete state_;throw;
    }
}
CyberIncidentJournal::~CyberIncidentJournal(){
    if(state_){
        if(state_->db)sqlite3_close(state_->db);
        delete state_;
    }
}

std::vector<CyberIncidentCase> CyberIncidentJournal::history() const {
    Statement sql(state_->db,
        "SELECT sequence,case_id,command_id,world_tick,world_generation,"
        "observer,colony,attacker_ship,victim_ship,attacker_owner,victim_owner,"
        "applied,prev_digest,digest,observed_description,sensor_detail,"
        "record_version FROM sg_ship_status07_incidents ORDER BY sequence");
    std::vector<CyberIncidentCase> cases;
    int rc=SQLITE_OK;
    while((rc=sqlite3_step(sql.stmt))==SQLITE_ROW){
        CyberIncidentCase c{};
        c.sequence=number(sql.stmt,0);
        c.case_id=number(sql.stmt,1);
        c.command_id=number(sql.stmt,2);
        c.world_tick=number(sql.stmt,3);
        c.world_generation=number(sql.stmt,4);
        c.observer_npc=number(sql.stmt,5);
        c.jurisdiction_colony=number(sql.stmt,6);
        c.attacking_ship=number(sql.stmt,7);
        c.victim_ship=number(sql.stmt,8);
        c.attacker_owner=number(sql.stmt,9);
        c.victim_owner=number(sql.stmt,10);
        const auto applied=number(sql.stmt,11);
        if(applied>1)throw std::runtime_error("invalid cyber case effect");
        c.confusion_applied=applied==1;
        c.previous_digest=string(sql.stmt,12);
        c.digest=string(sql.stmt,13);
        c.observed_description=string(sql.stmt,14);
        const auto detail=number(sql.stmt,15),version=number(sql.stmt,16);
        if(detail>255 || version<1 || version>2)
            throw std::runtime_error("unsupported cyber evidence version");
        c.sensor_detail=static_cast<std::uint8_t>(detail);
        c.record_version=static_cast<std::uint8_t>(version);
        cases.push_back(std::move(c));
        if(cases.size()>10000)
            throw std::runtime_error("cyber casebook exceeds bounded history");
    }
    if(rc!=SQLITE_DONE)
        throw std::runtime_error(
            std::string("cyber casebook read: ")+sqlite3_errmsg(state_->db));
    return cases;
}
void CyberIncidentJournal::verify() const {
    const auto cases=history();
    std::uint64_t previous_tick=0,previous_generation=0,sequence=0;
    std::string previous_digest="genesis";
    std::set<std::uint64_t> case_ids;
    std::set<std::pair<EntityId,std::uint64_t>> original_commands;
    for(const auto& c:cases) {
        if(c.sequence!=++sequence || !c.case_id || !c.command_id ||
           c.case_id==c.command_id || !c.world_tick ||
           !c.world_generation || !c.observer_npc ||
           !c.jurisdiction_colony || !c.attacking_ship || !c.victim_ship ||
           !c.victim_owner ||
           (c.record_version==1 && !c.attacker_owner) ||
           (c.record_version==2 &&
             (c.attacker_owner || c.sensor_detail<1 || c.sensor_detail>3 ||
              c.observed_description.empty() ||
              c.observed_description.size()>180 ||
              !std::all_of(c.observed_description.begin(),
                           c.observed_description.end(),
                [](unsigned char ch){return (ch>='A'&&ch<='Z') ||
                    (ch>='0'&&ch<='9') || ch=='_' || ch=='|';}))) ||
           (c.attacker_owner && c.attacker_owner==c.victim_owner) ||
           c.attacking_ship==c.victim_ship ||
           c.world_tick<previous_tick ||
           c.world_generation<previous_generation ||
           c.previous_digest!=previous_digest ||
           c.digest!=digest(c) ||
           !case_ids.insert(c.case_id).second ||
           !original_commands.emplace(c.jurisdiction_colony,
                                      c.command_id).second)
            throw std::runtime_error("cyber casebook ancestry invalid");
        previous_tick=c.world_tick;
        previous_generation=c.world_generation;
        previous_digest=c.digest;
    }
}
bool CyberIncidentJournal::intake(
    const PersistentWorld& world,const TerritoryJournal& territory,
    EntityId colony,std::uint64_t observer_npc,
    std::uint64_t original_attack_command,std::uint64_t case_id,
    CyberIntakeFailPoint fail) {
    if(!colony || !observer_npc || !original_attack_command || !case_id ||
       case_id==original_attack_command ||
       case_id>=(std::uint64_t{1}<<62) ||
       world.generation()==0)
        return false;
    // Critical: validate against the actual original world first. The
    // witness must be present on the SAME committed tick as the native
    // HackConfuse outcome; an old command/rumor or stale ship sighting fails.
    const auto original=contemporaneous_cyber_evidence(
        world,territory,colony,observer_npc,original_attack_command);
    if(!original.attempted || !original.world_tick ||
       original.world_tick!=world.world().tick() ||
       original.legal_crime_finding || original.arrest_authorized ||
       original.damage_authorized || original.seizure_authorized)
        return false;
    bool transaction=false;
    try {
        exec(state_->db,"BEGIN IMMEDIATE");
        transaction=true;
        verify();
        const auto prior=history();
        if(prior.size()>=10000)return (exec(state_->db,"ROLLBACK"),false);
        for(const auto& c:prior)
            if(c.case_id==case_id ||
               (c.command_id==original_attack_command &&
                c.jurisdiction_colony==colony)){
                exec(state_->db,"ROLLBACK");return false;
            }
        if(!prior.empty() &&
           (original.world_tick<prior.back().world_tick ||
            world.generation()<prior.back().world_generation)){
            exec(state_->db,"ROLLBACK");return false;
        }
        CyberIncidentCase case_record{};
        case_record.sequence=prior.size()+1;
        case_record.case_id=case_id;
        case_record.command_id=original_attack_command;
        case_record.world_tick=original.world_tick;
        case_record.world_generation=world.generation();
        case_record.observer_npc=original.observer_npc;
        case_record.jurisdiction_colony=colony;
        case_record.attacking_ship=original.attacking_ship;
        case_record.victim_ship=original.victim_ship;
        case_record.attacker_owner=0;
        case_record.victim_owner=original.victim_owner;
        case_record.sensor_detail=original.sensor_detail;
        case_record.observed_description=original.observed_description;
        case_record.record_version=2;
        case_record.confusion_applied=original.confusion_applied;
        case_record.previous_digest=prior.empty()?"genesis":prior.back().digest;
        case_record.digest=digest(case_record);
        Statement sql(state_->db,
            "INSERT INTO sg_ship_status07_incidents "
            "(sequence,case_id,command_id,world_tick,world_generation,"
            "observer,colony,attacker_ship,victim_ship,attacker_owner,victim_owner,"
            "applied,prev_digest,digest,observed_description,sensor_detail,"
            "record_version) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        sql.number(1,case_record.sequence);
        sql.number(2,case_record.case_id);
        sql.number(3,case_record.command_id);
        sql.number(4,case_record.world_tick);
        sql.number(5,case_record.world_generation);
        sql.number(6,case_record.observer_npc);
        sql.number(7,case_record.jurisdiction_colony);
        sql.number(8,case_record.attacking_ship);
        sql.number(9,case_record.victim_ship);
        sql.number(10,case_record.attacker_owner);
        sql.number(11,case_record.victim_owner);
        sql.number(12,case_record.confusion_applied?1:0);
        sql.string(13,case_record.previous_digest);
        sql.string(14,case_record.digest);
        sql.string(15,case_record.observed_description);
        sql.number(16,case_record.sensor_detail);
        sql.number(17,case_record.record_version);
        if(sqlite3_step(sql.stmt)!=SQLITE_DONE)
            throw std::runtime_error(
                std::string("cyber casebook insert: ")+
                    sqlite3_errmsg(state_->db));
        if(fail==CyberIntakeFailPoint::AfterInsert)
            throw std::runtime_error("cyber casebook injected crash before COMMIT");
        exec(state_->db,"COMMIT");
        transaction=false;
        return true;
    }catch(...){
        if(transaction)sqlite3_exec(state_->db,"ROLLBACK",nullptr,nullptr,nullptr);
        throw;
    }
}
std::vector<CyberIncidentSummary> CyberIncidentJournal::for_colony_owner(
    const PersistentWorld& world,PlayerId authenticated_owner,
    EntityId colony) const {
    std::vector<CyberIncidentSummary> permitted;
    const auto* site=world.world().find(colony);
    if(!authenticated_owner ||
       authenticated_owner==Simulation::kServerAuthority ||
       !site || site->type!=EntityType::Ship ||
       site->owner_id!=authenticated_owner ||
       !world.world().colony_ship(colony))
        return permitted;
    for(auto& item:summaries())
        if(item.evidence.jurisdiction_colony==colony)
            permitted.push_back(std::move(item));
    return permitted;
}
std::vector<CyberIncidentSummary> CyberIncidentJournal::summaries() const {
    verify();
    std::vector<CyberIncidentSummary> output;
    for(const auto& c:history()) {
        CyberIncidentSummary s{};
        s.evidence=c;
        s.witnessed_attempt=true;
        s.disrupted_modules=c.confusion_applied;
        s.independent_law_review_required=true;
        output.push_back(std::move(s));
    }
    return output;
}
} // namespace galaxy::colony_disposition
