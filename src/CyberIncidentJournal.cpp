#include "galaxy/CyberIncidentJournal.hpp"
#include "galaxy/ShipNotificationOutbox.hpp"
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
std::string link_digest(const CyberHailIdentityLink& l){
    std::ostringstream fields;
    fields<<l.previous_digest<<'|'<<l.sequence<<'|'<<l.case_id<<'|'
          <<l.hail_event<<'|'<<l.world_tick<<'|'<<l.world_generation
          <<'|'<<l.authenticated_controller<<'|'<<l.verified_ship;
    std::uint64_t hash=14695981039346656037ULL;
    for(const unsigned char c:fields.str()){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream out;
    out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
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
        ship_notifications::Outbox::ensure_schema(state_->db);
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
        // Identity is a SEPARATE later verified event. Never rewrite an
        // anonymous incident merely because a player eventually hails.
        exec(state_->db,
            "CREATE TABLE IF NOT EXISTS sg_ship_status07_hail_links ("
            "sequence INTEGER PRIMARY KEY,case_id INTEGER NOT NULL,"
            "hail_event INTEGER NOT NULL,world_tick INTEGER NOT NULL,"
            "world_generation INTEGER NOT NULL,"
            "controller INTEGER NOT NULL,verified_ship INTEGER NOT NULL,"
            "prev_digest TEXT NOT NULL,digest TEXT NOT NULL,"
            "UNIQUE(case_id,hail_event))");
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_status07_hail_no_update "
            "BEFORE UPDATE ON sg_ship_status07_hail_links "
            "BEGIN SELECT RAISE(ABORT,'immutable cyber hail link'); END");
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_status07_hail_no_delete "
            "BEFORE DELETE ON sg_ship_status07_hail_links "
            "BEGIN SELECT RAISE(ABORT,'immutable cyber hail link'); END");
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
std::vector<CyberHailIdentityLink> CyberIncidentJournal::hail_history() const {
    Statement stmt(state_->db,
        "SELECT sequence,case_id,hail_event,world_tick,world_generation,"
        "controller,verified_ship,prev_digest,digest "
        "FROM sg_ship_status07_hail_links ORDER BY sequence");
    std::vector<CyberHailIdentityLink> links;
    int rc=SQLITE_OK;
    while((rc=sqlite3_step(stmt.stmt))==SQLITE_ROW){
        CyberHailIdentityLink link{};
        link.sequence=number(stmt.stmt,0);
        link.case_id=number(stmt.stmt,1);
        link.hail_event=number(stmt.stmt,2);
        link.world_tick=number(stmt.stmt,3);
        link.world_generation=number(stmt.stmt,4);
        link.authenticated_controller=number(stmt.stmt,5);
        link.verified_ship=number(stmt.stmt,6);
        link.previous_digest=string(stmt.stmt,7);
        link.digest=string(stmt.stmt,8);
        links.push_back(std::move(link));
        if(links.size()>10000)
            throw std::runtime_error("cyber hail history is unbounded");
    }
    if(rc!=SQLITE_DONE)
        throw std::runtime_error("cyber hail history SQLite read failed");
    return links;
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
    std::uint64_t last_tick=0,last_generation=0,link_sequence=0;
    std::string last_digest="genesis";
    std::set<std::pair<std::uint64_t,std::uint64_t>> unique_links;
    for(const auto& link:hail_history()){
        const auto original=std::find_if(cases.begin(),cases.end(),
            [&](const CyberIncidentCase& c){return c.case_id==link.case_id;});
        if(original==cases.end() ||
           link.sequence!=++link_sequence || !link.hail_event ||
           !link.authenticated_controller ||
           link.authenticated_controller==Simulation::kServerAuthority ||
           link.verified_ship!=original->attacking_ship ||
           link.world_tick<original->world_tick ||
           link.world_generation<original->world_generation ||
           link.world_tick<last_tick || link.world_generation<last_generation ||
           link.previous_digest!=last_digest ||
           link.digest!=link_digest(link) ||
           !unique_links.emplace(link.case_id,link.hail_event).second)
            throw std::runtime_error("cyber hail identity link ancestry invalid");
        last_tick=link.world_tick;
        last_generation=link.world_generation;
        last_digest=link.digest;
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
        const auto* jurisdiction=world.world().find(colony);
        if(!jurisdiction || !jurisdiction->owner_id)
            throw std::runtime_error("missing original jurisdiction owner");
        // Neither an anonymous case nor an observed signature reveals who
        // controlled the attacking ship. Original case + both actual
        // recipient notices are committed/rolled back as one SQLite unit.
        ship_notifications::Outbox::record_cyber_case(state_->db,
            jurisdiction->owner_id,colony,case_record.victim_owner,
            case_record.victim_ship,case_record.case_id,
            case_record.command_id,case_record.world_tick);
        exec(state_->db,"COMMIT");
        transaction=false;
        return true;
    }catch(...){
        if(transaction)sqlite3_exec(state_->db,"ROLLBACK",nullptr,nullptr,nullptr);
        throw;
    }
}
bool CyberIncidentJournal::link_authenticated_hail(
    const PersistentWorld& world,const TerritoryJournal& territory,
    EntityId jurisdiction_colony,std::uint64_t observer_npc,
    EntityId visitor_ship,PlayerId authenticated_owner,
    std::uint64_t actual_hail_reply_event) {
    if(!jurisdiction_colony || !observer_npc || !visitor_ship ||
       !actual_hail_reply_event || !authenticated_owner ||
       authenticated_owner==Simulation::kServerAuthority)return false;
    const auto* visitor=world.world().find(visitor_ship);
    const auto* colony=world.world().find(jurisdiction_colony);
    if(!visitor || !colony ||
       visitor->type!=EntityType::Ship ||
       colony->type!=EntityType::Ship ||
       visitor->owner_id!=authenticated_owner ||
       colony->owner_id==authenticated_owner ||
       !world.world().colony_ship(jurisdiction_colony))
        return false;
    // A remote/anonymous radio ping is not an identity credential:
    // require an ORIGINAL authenticated response or voluntary hail in the
    // same jurisdiction and on this exact physical hull.
    const auto notices=territory.history();
    const auto hail=std::find_if(notices.begin(),notices.end(),
        [&](const TerritoryEvent& row){
            return row.event_id==actual_hail_reply_event &&
              (row.kind==TerritoryEventKind::NoticeAcknowledged ||
               row.kind==TerritoryEventKind::NoticeRefused ||
               row.kind==TerritoryEventKind::VoluntaryHail) &&
              row.colony==jurisdiction_colony &&
              row.visitor_ship==visitor_ship &&
              row.visitor_owner==authenticated_owner &&
              row.world_tick==world.world().tick() &&
              row.world_generation==world.generation();
        });
    if(hail==notices.end() || !hail->related_event)return false;
    const bool self_hail=hail->kind==TerritoryEventKind::VoluntaryHail;
    const auto parent=std::find_if(notices.begin(),notices.end(),
        [&](const TerritoryEvent& row){
            return row.event_id==hail->related_event &&
                   row.colony==jurisdiction_colony &&
                   (self_hail ?
                      row.kind==TerritoryEventKind::Claim :
                      (row.kind==TerritoryEventKind::Warning &&
                       row.visitor_owner==authenticated_owner &&
                       row.visitor_ship==visitor_ship)) &&
                   row.world_tick<=hail->world_tick;
        });
    if(parent==notices.end())return false;
    if(self_hail && hail->observer_npc!=observer_npc)return false;
    const auto standing=territory.standing(
        world,jurisdiction_colony,visitor_ship);
    if(!standing.in_claimed_region ||
       (!self_hail && (standing.invited || !standing.notice_received ||
          standing.notice_reply_event!=actual_hail_reply_event)))
        return false;
    const auto observed=world.local_colony_watch_contacts(
        jurisdiction_colony,observer_npc);
    if(std::none_of(observed.begin(),observed.end(),
        [&](const colony_defense::Contact& contact){
            return contact.foreign_ship==visitor_ship &&
                   contact.source==jurisdiction_colony &&
                   contact.world_tick==world.world().tick();
        }))return false;
    bool committed=false;
    try {
        exec(state_->db,"BEGIN IMMEDIATE");
        verify();
        const auto cases=history();
        const auto prior=hail_history();
        std::vector<CyberIncidentCase> to_link;
        for(const auto& c:cases){
            if(c.jurisdiction_colony!=jurisdiction_colony ||
               c.attacking_ship!=visitor_ship ||
               c.world_tick>world.world().tick())continue;
            const bool duplicate=std::any_of(prior.begin(),prior.end(),
                [&](const CyberHailIdentityLink& link){
                    return link.case_id==c.case_id &&
                           link.hail_event==actual_hail_reply_event;
                });
            if(!duplicate)to_link.push_back(c);
        }
        if(to_link.empty() || prior.size()+to_link.size()>10000){
            exec(state_->db,"ROLLBACK");
            return false;
        }
        std::string previous=prior.empty()?"genesis":prior.back().digest;
        std::uint64_t sequence=prior.size();
        for(const auto& c:to_link){
            CyberHailIdentityLink link{};
            link.sequence=++sequence;
            link.case_id=c.case_id;
            link.hail_event=actual_hail_reply_event;
            link.world_tick=world.world().tick();
            link.world_generation=world.generation();
            link.authenticated_controller=authenticated_owner;
            link.verified_ship=visitor_ship;
            link.previous_digest=previous;
            link.digest=link_digest(link);
            Statement insert(state_->db,
                "INSERT INTO sg_ship_status07_hail_links "
                "(sequence,case_id,hail_event,world_tick,world_generation,"
                "controller,verified_ship,prev_digest,digest) "
                "VALUES (?,?,?,?,?,?,?,?,?)");
            insert.number(1,link.sequence);
            insert.number(2,link.case_id);
            insert.number(3,link.hail_event);
            insert.number(4,link.world_tick);
            insert.number(5,link.world_generation);
            insert.number(6,link.authenticated_controller);
            insert.number(7,link.verified_ship);
            insert.string(8,link.previous_digest);
            insert.string(9,link.digest);
            if(sqlite3_step(insert.stmt)!=SQLITE_DONE)
                throw std::runtime_error("native cyber hail link insert failed");
            previous=link.digest;
        }
        // There may be several earlier original cases linked by a single
        // later authorized hail. Notify the genuine jurisdiction once and
        // leave each separate immutable link in the source casebook.
        const auto* source=world.world().find(jurisdiction_colony);
        if(!source || !source->owner_id || to_link.empty())
            throw std::runtime_error("later hail link has no jurisdiction");
        ship_notifications::Outbox::record_cyber_link(state_->db,
            source->owner_id,actual_hail_reply_event,visitor_ship,
            authenticated_owner,to_link.front().case_id,world.world().tick());
        exec(state_->db,"COMMIT");
        committed=true;
        return true;
    }catch(...){
        if(!committed)
            sqlite3_exec(state_->db,"ROLLBACK",nullptr,nullptr,nullptr);
        throw;
    }
}
std::vector<CyberSignatureAlert> CyberIncidentJournal::watch_known_descriptions(
    const PersistentWorld& world,PlayerId colony_owner,EntityId colony,
    std::uint64_t observer_npc) const {
    std::vector<CyberSignatureAlert> alerts;
    const auto* site=world.world().find(colony);
    if(!colony_owner || colony_owner==Simulation::kServerAuthority ||
       !site || site->owner_id!=colony_owner ||
       !world.world().colony_ship(colony))return alerts;
    // Original NPC watch must prove the ship was actually detected with
    // sufficient fitted optics. Reconstruct cases by observed features,
    // *not* by private persistent entity ID or true owner.
    const auto seen=world.local_colony_watch_contacts(colony,observer_npc);
    const auto cases=history();
    for(const auto& contact:seen){
        for(const auto& c:cases){
            if(c.jurisdiction_colony!=colony ||
               c.record_version<2 || !c.sensor_detail ||
               c.sensor_detail>contact.sensor_detail ||
               c.world_tick>world.world().tick())
                continue;
            if(ship_sensors::description_at_detail(
                 world.world(),contact.foreign_ship,c.sensor_detail)!=
               c.observed_description)continue;
            alerts.push_back({c.case_id,c.observed_description,
                              c.sensor_detail,contact.world_tick,
                              true,false,false});
            if(alerts.size()>=64)return alerts;
        }
    }
    return alerts;
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
    const auto links=hail_history();
    for(const auto& c:history()) {
        CyberIncidentSummary s{};
        s.evidence=c;
        // Public reports must not publish privileged native ship/owner IDs
        // merely because the C++ command executor knows the source actor.
        s.evidence.attacker_owner=0;
        s.evidence.attacking_ship=0;
        s.witnessed_attempt=true;
        s.disrupted_modules=c.confusion_applied;
        s.independent_law_review_required=true;
        for(const auto& link:links)
            if(link.case_id==c.case_id){
                s.later_identified_controller=link.authenticated_controller;
                s.identity_hail_event=link.hail_event;
                s.evidence.attacking_ship=link.verified_ship;
                // It proves the LATER authenticated controller/hull pair,
                // not who physically piloted the hull during the offense.
            }
        output.push_back(std::move(s));
    }
    return output;
}
} // namespace galaxy::colony_disposition
