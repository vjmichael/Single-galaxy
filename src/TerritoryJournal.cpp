#include "galaxy/TerritoryJournal.hpp"
#include "galaxy/ColonyDefense.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <utility>

namespace galaxy::colony_disposition {
namespace {
void check(int code,sqlite3* db,const char* context) {
    if(code!=SQLITE_OK && code!=SQLITE_DONE && code!=SQLITE_ROW)
        throw std::runtime_error(std::string(context)+": "+sqlite3_errmsg(db));
}
void exec(sqlite3* db,const char* sql) {
    char* err=nullptr;
    const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){
        std::string message=err?err:"SQLite error";
        sqlite3_free(err);throw std::runtime_error(message);
    }
}
struct Statement {
    sqlite3_stmt* p=nullptr;
    sqlite3* db=nullptr;
    Statement(sqlite3* d,const char* sql):db(d){
        check(sqlite3_prepare_v2(d,sql,-1,&p,nullptr),d,"prepare");
    }
    ~Statement(){sqlite3_finalize(p);}
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
};
std::string digest(const TerritoryEvent& e){
    std::ostringstream s;
    s<<e.previous_digest<<'|'<<e.sequence<<'|'<<e.event_id<<'|'
     <<static_cast<int>(e.kind)<<'|'<<e.colony<<'|'<<e.owner<<'|'
     <<e.visitor_owner<<'|'<<e.visitor_ship<<'|'<<e.observer_npc<<'|'
     <<e.world_tick<<'|'<<e.world_generation<<'|'<<e.center_x<<'|'
     <<e.center_y<<'|'<<e.radius_mm<<'|'<<e.deadline_tick<<'|'
     <<e.related_event;
    std::uint64_t hash=14695981039346656037ULL;
    for(const unsigned char c:s.str()){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    return out.str();
}
bool in_range(const Entity& ship,std::int64_t x,std::int64_t y,
              std::int64_t radius){
    // Unsigned subtraction avoids signed overflow on MSVC and GCC.
    const auto ux=static_cast<std::uint64_t>(ship.position.x);
    const auto uy=static_cast<std::uint64_t>(ship.position.y);
    const auto cx=static_cast<std::uint64_t>(x);
    const auto cy=static_cast<std::uint64_t>(y);
    const auto dx=ship.position.x>=x?ux-cx:cx-ux;
    const auto dy=ship.position.y>=y?uy-cy:cy-uy;
    if(dx>static_cast<std::uint64_t>(radius) ||
       dy>static_cast<std::uint64_t>(radius))return false;
    return dx*dx+dy*dy<=static_cast<std::uint64_t>(radius*radius);
}
bool owner_of(const PersistentWorld& world,PlayerId owner,EntityId colony){
    const auto* e=world.world().find(colony);
    return owner && owner!=Simulation::kServerAuthority && e &&
           e->type==EntityType::Ship && e->owner_id==owner &&
           world.world().colony_ship(colony)!=nullptr;
}
bool currently_witnessed(const PersistentWorld& world,EntityId colony,
                          EntityId target,std::uint64_t npc){
    if(!npc || !world.world().passenger_needs_enabled())return false;
    const auto it=world.world().npc_records().find(npc);
    if(it==world.world().npc_records().end() ||
       it->second.home_ship!=colony || it->second.location!=colony ||
       it->second.transport_id ||
       !world.world().npc_available_for_duty(npc))return false;
    const auto observed=world.local_colony_watch_contacts(colony,npc);
    return std::any_of(observed.begin(),observed.end(),
        [&](const colony_defense::Contact& c){
            return c.foreign_ship==target &&
                   c.world_tick==world.world().tick();
        });
}
} // namespace
struct TerritoryJournal::State {
    sqlite3* db=nullptr;
};
TerritoryJournal::TerritoryJournal(std::string db_path):state_(new State) {
    try {
        if(sqlite3_open_v2(db_path.c_str(),&state_->db,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,
                nullptr)!=SQLITE_OK)throw std::runtime_error("territory DB open");
        sqlite3_busy_timeout(state_->db,10000);
        exec(state_->db,"PRAGMA foreign_keys=ON");
        exec(state_->db,
            "CREATE TABLE IF NOT EXISTS sg_disposition02 ("
            "sequence INTEGER PRIMARY KEY,event_id INTEGER NOT NULL UNIQUE,"
            "kind INTEGER NOT NULL,colony INTEGER NOT NULL,owner INTEGER NOT NULL,"
            "visitor_owner INTEGER NOT NULL,visitor_ship INTEGER NOT NULL,"
            "observer INTEGER NOT NULL,world_tick INTEGER NOT NULL,"
            "world_generation INTEGER NOT NULL,center_x INTEGER NOT NULL,"
            "center_y INTEGER NOT NULL,radius INTEGER NOT NULL,"
            "deadline INTEGER NOT NULL,related INTEGER NOT NULL,"
            "prev_digest TEXT NOT NULL,digest TEXT NOT NULL)");
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_disp02_no_update "
            "BEFORE UPDATE ON sg_disposition02 "
            "BEGIN SELECT RAISE(ABORT,'immutable territory history'); END");
        exec(state_->db,
            "CREATE TRIGGER IF NOT EXISTS sg_disp02_no_delete "
            "BEFORE DELETE ON sg_disposition02 "
            "BEGIN SELECT RAISE(ABORT,'immutable territory history'); END");
        verify();
    }catch(...){if(state_->db)sqlite3_close(state_->db);delete state_;throw;}
}
TerritoryJournal::~TerritoryJournal(){
    if(state_){if(state_->db)sqlite3_close(state_->db);delete state_;}
}
std::vector<TerritoryEvent> TerritoryJournal::history() const {
    Statement stmt(state_->db,
        "SELECT sequence,event_id,kind,colony,owner,visitor_owner,visitor_ship,"
        "observer,world_tick,world_generation,center_x,center_y,radius,deadline,"
        "related,prev_digest,digest FROM sg_disposition02 ORDER BY sequence");
    std::vector<TerritoryEvent> rows;
    int rc=SQLITE_OK;
    while((rc=sqlite3_step(stmt.p))==SQLITE_ROW){
        auto v=[&](int col)->std::uint64_t{
            const auto n=sqlite3_column_int64(stmt.p,col);
            if(n<0)throw std::runtime_error("negative territorial identity");
            return static_cast<std::uint64_t>(n);
        };
        TerritoryEvent e;
        e.sequence=v(0);e.event_id=v(1);
        e.kind=static_cast<TerritoryEventKind>(v(2));
        e.colony=v(3);e.owner=v(4);e.visitor_owner=v(5);
        e.visitor_ship=v(6);e.observer_npc=v(7);
        e.world_tick=v(8);e.world_generation=v(9);
        e.center_x=sqlite3_column_int64(stmt.p,10);
        e.center_y=sqlite3_column_int64(stmt.p,11);
        e.radius_mm=sqlite3_column_int64(stmt.p,12);
        e.deadline_tick=v(13);e.related_event=v(14);
        if(sqlite3_column_type(stmt.p,15)!=SQLITE_TEXT ||
           sqlite3_column_type(stmt.p,16)!=SQLITE_TEXT)
            throw std::runtime_error("missing territorial evidence digests");
        e.previous_digest=reinterpret_cast<const char*>(sqlite3_column_text(stmt.p,15));
        e.digest=reinterpret_cast<const char*>(sqlite3_column_text(stmt.p,16));
        rows.push_back(std::move(e));
    }
    check(rc,state_->db,"territory history");
    return rows;
}
void TerritoryJournal::verify() const {
    auto rows=history();
    std::uint64_t sequence=0,previous_tick=0,previous_generation=0;
    std::string previous="genesis";
    for(const auto& e:rows){
        if(e.sequence!=++sequence || !e.event_id || !e.colony || !e.owner ||
           e.owner==Simulation::kServerAuthority ||
           static_cast<int>(e.kind)<1 || static_cast<int>(e.kind)>6 ||
           (e.kind==TerritoryEventKind::Claim &&
             (e.radius_mm<=0 || e.radius_mm>5000 || e.visitor_owner ||
              e.visitor_ship || e.observer_npc || e.related_event)) ||
           (e.kind!=TerritoryEventKind::Claim &&
             (e.visitor_owner==0 || e.radius_mm!=0)) ||
           (e.kind==TerritoryEventKind::Warning &&
             (!e.visitor_ship || !e.observer_npc || !e.related_event ||
              e.deadline_tick<=e.world_tick)) ||
           (e.kind==TerritoryEventKind::Trespass &&
             (!e.visitor_ship || !e.observer_npc || !e.related_event ||
              e.world_tick<e.deadline_tick)) ||
           (e.kind==TerritoryEventKind::Expulsion &&
             (!e.visitor_ship || !e.observer_npc || !e.related_event)) ||
           (e.previous_digest!=previous || e.digest!=digest(e)) ||
           (sequence>1 && (e.world_tick<previous_tick ||
                            e.world_generation<previous_generation)))
            throw std::runtime_error("invalid or rewritten territory evidence chain");
        previous=e.digest;previous_tick=e.world_tick;
        previous_generation=e.world_generation;
    }
}
bool TerritoryJournal::append(const TerritoryEvent& supplied) {
    // Validation precedes publication; this is a separate durable journal,
    // not a modification of world identity or simulated materials.
    verify();
    auto rows=history();
    for(const auto& e:rows)if(e.event_id==supplied.event_id)return false;
    TerritoryEvent e=supplied;
    e.sequence=rows.size()+1;
    e.previous_digest=rows.empty()?"genesis":rows.back().digest;
    e.digest=digest(e);
    if(!rows.empty() && (e.world_tick<rows.back().world_tick ||
                         e.world_generation<rows.back().world_generation))
        return false;
    exec(state_->db,"BEGIN IMMEDIATE");
    try {
        Statement st(state_->db,
          "INSERT INTO sg_disposition02 VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        auto bind=[&](int i,std::uint64_t n){
            if(n>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw std::overflow_error("territory ID exceeds SQLite");
            check(sqlite3_bind_int64(st.p,i,static_cast<sqlite3_int64>(n)),
                  state_->db,"bind");
        };
        bind(1,e.sequence);bind(2,e.event_id);bind(3,static_cast<int>(e.kind));
        bind(4,e.colony);bind(5,e.owner);bind(6,e.visitor_owner);
        bind(7,e.visitor_ship);bind(8,e.observer_npc);
        bind(9,e.world_tick);bind(10,e.world_generation);
        check(sqlite3_bind_int64(st.p,11,e.center_x),state_->db,"center x");
        check(sqlite3_bind_int64(st.p,12,e.center_y),state_->db,"center y");
        check(sqlite3_bind_int64(st.p,13,e.radius_mm),state_->db,"radius");
        bind(14,e.deadline_tick);bind(15,e.related_event);
        check(sqlite3_bind_text(st.p,16,e.previous_digest.c_str(),-1,SQLITE_TRANSIENT),state_->db,"prior");
        check(sqlite3_bind_text(st.p,17,e.digest.c_str(),-1,SQLITE_TRANSIENT),state_->db,"digest");
        check(sqlite3_step(st.p),state_->db,"insert territory");
        exec(state_->db,"COMMIT");
    }catch(...){sqlite3_exec(state_->db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    verify();
    return true;
}
bool TerritoryJournal::claim(const PersistentWorld& w,PlayerId owner,
                             EntityId colony,std::uint64_t id,std::int64_t radius){
    if(!id || radius<=0 || radius>5000 || !owner_of(w,owner,colony))
        return false;
    for(const auto& e:history())
        if(e.kind==TerritoryEventKind::Claim && e.colony==colony)return false;
    const auto* ship=w.world().find(colony);
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::Claim;
    e.colony=colony;e.owner=owner;e.radius_mm=radius;
    e.center_x=ship->position.x;e.center_y=ship->position.y;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    return append(e);
}
bool TerritoryJournal::invite(const PersistentWorld& w,PlayerId owner,
        EntityId colony,PlayerId visitor,std::uint64_t id){
    if(!id || !owner_of(w,owner,colony) || !visitor ||
       visitor==owner || visitor==Simulation::kServerAuthority)return false;
    bool has_claim=false;for(const auto& e:history())
        if(e.colony==colony && e.kind==TerritoryEventKind::Claim)
            has_claim=true;
    if(!has_claim)return false;
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::Invite;
    e.colony=colony;e.owner=owner;e.visitor_owner=visitor;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    return append(e);
}
bool TerritoryJournal::revoke_invitation(const PersistentWorld& w,PlayerId owner,
        EntityId colony,PlayerId visitor,std::uint64_t id){
    if(!id || !owner_of(w,owner,colony) || !visitor ||
       visitor==owner)return false;
    bool invited=false;
    for(const auto& e:history())if(e.colony==colony &&
        e.visitor_owner==visitor) {
        if(e.kind==TerritoryEventKind::Invite){
            invited=true;
            // An explicit later invitation can lift an expulsion; the
            // original trespass remains in the historical ledger.
            state.standing=npc_response::VisitorStanding::Unknown;
        }
        if(e.kind==TerritoryEventKind::RevokeInvitation)invited=false;
    }
    if(!invited)return false;
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::RevokeInvitation;
    e.colony=colony;e.owner=owner;e.visitor_owner=visitor;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    return append(e);
}
TerritorialStanding TerritoryJournal::standing(const PersistentWorld& w,
        EntityId colony,EntityId visitor) const {
    verify();
    TerritorialStanding state{};
    const auto* source=w.world().find(colony);
    const auto* target=w.world().find(visitor);
    if(!source || !target || source->type!=EntityType::Ship ||
       target->type!=EntityType::Ship || !target->owner_id ||
       target->owner_id==source->owner_id ||
       !w.world().same_local_frame(colony,visitor))return state;
    TerritoryEvent claim{};
    bool exists=false;
    for(const auto& e:history())
        if(e.kind==TerritoryEventKind::Claim && e.colony==colony){
            claim=e;exists=true;break;
        }
    if(!exists)return state;
    state.claim_event=claim.event_id;
    state.in_claimed_region=in_range(*target,claim.center_x,
                                   claim.center_y,claim.radius_mm);
    if(!state.in_claimed_region)return state;
    std::uint64_t warning=0,trespass=0,grace=0;
    bool invited=false;
    for(const auto& e:history()){
        if(e.colony!=colony || e.visitor_owner!=target->owner_id)continue;
        if(e.kind==TerritoryEventKind::Invite)invited=true;
        if(e.kind==TerritoryEventKind::RevokeInvitation)invited=false;
        if(e.visitor_ship==visitor && e.kind==TerritoryEventKind::Warning){
            warning=e.event_id;grace=e.deadline_tick;
        }
        if(e.visitor_ship==visitor && e.kind==TerritoryEventKind::Trespass)
            trespass=e.event_id;
        if(e.visitor_ship==visitor && e.kind==TerritoryEventKind::Expulsion)
            state.standing=npc_response::VisitorStanding::Expelled;
    }
    state.invited=invited;state.warning_event=warning;
    state.trespass_event=trespass;state.grace_until=grace;
    state.warned=warning!=0;
    state.trespass_recorded=trespass!=0;
    if(state.standing!=npc_response::VisitorStanding::Expelled){
        if(invited)state.standing=npc_response::VisitorStanding::Invited;
        else if(trespass)state.standing=npc_response::VisitorStanding::Uninvited;
        else if(warning && w.world().tick()>=grace)
            state.standing=npc_response::VisitorStanding::Uninvited;
        else state.standing=npc_response::VisitorStanding::Unknown;
    }
    return state;
}
bool TerritoryJournal::warn(const PersistentWorld& w,PlayerId owner,
       EntityId colony,EntityId foreign,std::uint64_t npc,
       std::uint64_t id,std::uint64_t grace){
    if(!id || !owner_of(w,owner,colony) || grace<2 || grace>10000 ||
       w.world().tick()>std::numeric_limits<std::uint64_t>::max()-grace)
        return false;
    const auto* target=w.world().find(foreign);
    const auto state=standing(w,colony,foreign);
    if(!target || !state.in_claimed_region || state.invited ||
       state.warned || !currently_witnessed(w,colony,foreign,npc))return false;
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::Warning;
    e.colony=colony;e.owner=owner;e.visitor_owner=target->owner_id;
    e.visitor_ship=foreign;e.observer_npc=npc;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    e.deadline_tick=e.world_tick+grace;e.related_event=state.claim_event;
    return append(e);
}
bool TerritoryJournal::record_trespass(const PersistentWorld& w,
        PlayerId owner,EntityId colony,EntityId foreign,
        std::uint64_t npc,std::uint64_t id) {
    if(!id || !owner_of(w,owner,colony))return false;
    const auto* target=w.world().find(foreign);
    const auto s=standing(w,colony,foreign);
    if(!target || !s.in_claimed_region || s.invited || !s.warned ||
       s.trespass_recorded || w.world().tick()<s.grace_until ||
       !currently_witnessed(w,colony,foreign,npc))return false;
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::Trespass;
    e.colony=colony;e.owner=owner;e.visitor_owner=target->owner_id;
    e.visitor_ship=foreign;e.observer_npc=npc;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    e.deadline_tick=s.grace_until;e.related_event=s.warning_event;
    return append(e);
}
bool TerritoryJournal::expel(const PersistentWorld& w,PlayerId owner,
        EntityId colony,EntityId foreign,std::uint64_t npc,std::uint64_t id) {
    if(!id || !owner_of(w,owner,colony))return false;
    const auto* target=w.world().find(foreign);
    const auto s=standing(w,colony,foreign);
    if(!target || !s.trespass_recorded || !s.in_claimed_region ||
       s.standing==npc_response::VisitorStanding::Expelled ||
       !currently_witnessed(w,colony,foreign,npc))return false;
    TerritoryEvent e{};e.event_id=id;e.kind=TerritoryEventKind::Expulsion;
    e.colony=colony;e.owner=owner;e.visitor_owner=target->owner_id;
    e.visitor_ship=foreign;e.observer_npc=npc;
    e.world_tick=w.world().tick();e.world_generation=w.generation();
    e.related_event=s.trespass_event;
    return append(e);
}
} // namespace galaxy::colony_disposition
