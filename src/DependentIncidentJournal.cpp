#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

namespace galaxy {
namespace {
struct Database {
    sqlite3* db=nullptr;
    explicit Database(const std::string& path){
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
            const std::string what=db?sqlite3_errmsg(db):"open failure";
            sqlite3_close(db);throw std::runtime_error("incident database: "+what);
        }
        sqlite3_busy_timeout(db,5000);
        sqlite3_exec(db,"PRAGMA foreign_keys=ON",nullptr,nullptr,nullptr);
    }
    ~Database(){sqlite3_close(db);}
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
};
void sql(sqlite3* db,const char* statement){
    char* err=nullptr;
    const auto rc=sqlite3_exec(db,statement,nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){const std::string text=err?err:sqlite3_errmsg(db);
        sqlite3_free(err);throw std::runtime_error("incident SQL: "+text);}
}
struct Query {
    sqlite3* db;
    sqlite3_stmt* stmt=nullptr;
    Query(sqlite3* database,const char* statement):db(database){
        if(sqlite3_prepare_v2(db,statement,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error(std::string("incident query: ")+sqlite3_errmsg(db));
    }
    ~Query(){sqlite3_finalize(stmt);}
    void s(int i,const std::string& v){
        if(sqlite3_bind_text(stmt,i,v.c_str(),static_cast<int>(v.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("incident bind text failed");
    }
    void n(int i,std::uint64_t v){
        if(v>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::invalid_argument("incident generation overflow");
        if(sqlite3_bind_int64(stmt,i,static_cast<sqlite3_int64>(v))!=SQLITE_OK)
            throw std::runtime_error("incident bind integer failed");
    }
    bool row(){const int rc=sqlite3_step(stmt);
        if(rc==SQLITE_ROW)return true;
        if(rc==SQLITE_DONE)return false;
        throw std::runtime_error(std::string("incident read: ")+sqlite3_errmsg(db));}
    void execute(){if(sqlite3_step(stmt)!=SQLITE_DONE)
        throw std::runtime_error(std::string("incident write: ")+sqlite3_errmsg(db));}
    std::string str(int i)const{
        const auto* raw=sqlite3_column_text(stmt,i);
        if(!raw)throw std::runtime_error("incident missing text");
        return reinterpret_cast<const char*>(raw);
    }
    std::uint64_t uid(int i)const{
        const auto text=str(i);
        if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("incident noncanonical ID");
        std::size_t consumed=0;
        const auto result=std::stoull(text,&consumed);
        if(consumed!=text.size()||std::to_string(result)!=text)
            throw std::runtime_error("incident noncanonical ID");
        return result;
    }
    std::uint64_t number(int i)const{
        const auto result=sqlite3_column_int64(stmt,i);
        if(result<0)throw std::runtime_error("incident negative stored generation");
        return static_cast<std::uint64_t>(result);
    }
};
bool table(sqlite3* db,const char* name){
    Query q(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
    q.s(1,name);return q.row();
}
bool case_counter_column(sqlite3* db){
    Query q(db,"PRAGMA table_info(world_meta)");
    while(q.row())if(q.str(1)=="dependent_case_count")return true;
    return false;
}
std::uint64_t current_generation(sqlite3* db){
    Query q(db,"SELECT COALESCE(MAX(generation),0) FROM snapshots");
    if(!q.row())throw std::runtime_error("incident missing world generation");
    return q.number(0);
}
void canonical(const std::vector<std::uint64_t>& ids,const char* what){
    if(ids.empty()||ids.size()>16||
       !std::is_sorted(ids.begin(),ids.end())||ids.front()==0||
       std::adjacent_find(ids.begin(),ids.end())!=ids.end())
        throw std::invalid_argument(std::string("incident requires complete ordered ")+what);
}
std::vector<std::uint64_t> adults_aboard(const Simulation& state,
                                             EntityId ferry,std::uint64_t transport){
    std::vector<std::uint64_t> ids;
    for(const auto& [id,n]:state.npc_records())
        if(n.location==ferry && n.transport_id==transport)ids.push_back(id);
    std::sort(ids.begin(),ids.end());return ids;
}
// Applied, independently journaled physical event, not a user assertion that
// the ferry is damaged. The normal world restore checks the command digest.
void validate_cause(sqlite3* db,std::uint64_t command_id,EntityId ferry,
                    std::uint64_t transport,std::uint64_t generation){
    Query q(db,"SELECT generation,action,outcome,actor_id,command FROM historical_events "
               "WHERE command_id=?");
    q.s(1,std::to_string(command_id));
    if(!q.row()||q.number(0)>generation||
       q.number(1)!=static_cast<std::uint64_t>(Action::HostStrandFerry)||
       q.number(2)!=static_cast<std::uint64_t>(Outcome::Applied)||
       q.uid(3)!=ferry)
        throw std::runtime_error("incident has no applied, matching ferry stranding event");
    // The original audited command's T31 envelope binds its transport ID.
    const auto record=q.str(4);
    std::istringstream in(record);
    std::uint64_t id=0,tick=0,sequence=0,issuer=0,actor=0,target=0;
    int action=-1;
    double vx=0,vy=0;
    std::int64_t amount=0;
    std::string envelope;
    std::uint64_t voyage=0;
    if(!(in>>id>>tick>>sequence>>issuer>>actor>>target>>action>>vx>>vy>>amount>>envelope>>voyage)||
       id!=command_id||actor!=ferry||voyage!=transport||envelope!="T31"||
       issuer!=Simulation::kServerAuthority||action!=static_cast<int>(Action::HostStrandFerry))
        throw std::runtime_error("incident stranding command envelope inconsistent");
    if(q.row())throw std::runtime_error("duplicated stranding command record");
}
struct CaseEvidence {
    std::vector<std::uint64_t> adults,children;
    std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> guardians;
    std::map<std::uint64_t,std::uint64_t> board_generations;
    std::uint64_t tick=0;
};
CaseEvidence snapshot_evidence(sqlite3* db,const std::string& family,
    std::uint64_t gen,std::uint64_t current_transport,EntityId ferry){
    Query state(db,"SELECT state,checksum FROM snapshots WHERE generation=?");state.n(1,gen);
    if(!state.row()||sqlite3_column_type(state.stmt,0)!=SQLITE_BLOB)
        throw std::runtime_error("incident requires retained historical world snapshot");
    const auto* bytes=static_cast<const char*>(sqlite3_column_blob(state.stmt,0));
    const int n=sqlite3_column_bytes(state.stmt,0);
    if(!bytes||n<=0||n>16*1024*1024)
        throw std::runtime_error("incident historical snapshot unreadable");
    const auto simulation=Simulation::from_snapshot(std::string(bytes,static_cast<std::size_t>(n)));
    if(state.str(1)!=std::to_string(simulation.state_hash()))
        throw std::runtime_error("incident pinned snapshot checksum mismatch");
    Query logged(db,"SELECT state_hash,tick FROM operation_log WHERE generation=?");
    logged.n(1,gen);
    if(!logged.row()||logged.str(0)!=state.str(1)||logged.number(1)!=simulation.tick())
        throw std::runtime_error("incident pinned snapshot not in canonical operation journal");
    if(simulation.voyage_status(current_transport)!=3)
        throw std::runtime_error("incident does not describe an actually stranded voyage");
    CaseEvidence proof{};proof.tick=simulation.tick();
    proof.adults=adults_aboard(simulation,ferry,current_transport);
    Query children(db,"SELECT family_scope,child_id,guardian_id,co_guardian_id,board_generation "
        "FROM sg_dependent_travel WHERE ferry=? AND transport_id=? AND "
        "board_generation<=? AND (arrival_generation IS NULL OR arrival_generation>?) "
        "ORDER BY child_id");
    children.s(1,std::to_string(ferry));children.s(2,std::to_string(current_transport));
    children.n(3,gen);children.n(4,gen);
    while(children.row()){
        if(children.str(0)!=family)
            throw std::runtime_error("incident excludes a separate family aboard ferry");
        const auto child=children.uid(1),guardian=children.uid(2),other=children.uid(3);
        if(!child || !guardian || (other && other==guardian)||
           !proof.guardians.emplace(child,std::make_pair(guardian,other)).second)
            throw std::runtime_error("incident duplicate or missing child guardian");
        proof.children.push_back(child);
        proof.board_generations.emplace(child,children.number(4));
    }
    if(proof.adults.empty() || proof.children.empty() ||
       proof.adults.size()+proof.children.size()>16)
        throw std::runtime_error("incident has invalid actual occupied ferry berths");
    for(const auto& [child,parents]:proof.guardians){(void)child;
        for(const auto adult:{parents.first,parents.second})
            if(adult && !std::binary_search(proof.adults.begin(),proof.adults.end(),adult))
                throw std::runtime_error("incident child guardian not aboard real ferry");
    }
    return proof;
}
// Chain the current rescue transport back to the child's original boarding.
// A forged original transport number must never make a case appear valid.
void verify_child_origins(sqlite3* db,const std::string& family,
                          std::uint64_t original,std::uint64_t current,
                          const std::vector<std::uint64_t>& children){
    bool has_transfer=false;
    {Query columns(db,"PRAGMA table_info(sg_dependent_travel)");
     while(columns.row())if(columns.str(1)=="transfer_from_transport")has_transfer=true;}
    for(const auto child:children){
        std::set<std::uint64_t> visited;
        auto transport=current;
        for(;;){
            if(!visited.insert(transport).second || visited.size()>32)
                throw std::runtime_error("incident rescue chain is cyclic or excessive");
            const std::string query=std::string("SELECT ")+
                (has_transfer?"transfer_from_transport":"'0'")+
                " FROM sg_dependent_travel WHERE family_scope=? AND child_id=? AND transport_id=?";
            Query leg(db,query.c_str());
            leg.s(1,family);leg.s(2,std::to_string(child));
            leg.s(3,std::to_string(transport));
            if(!leg.row())throw std::runtime_error("incident missing historical rescue leg");
            const auto prior=leg.uid(0);
            if(leg.row())throw std::runtime_error("incident duplicate rescue predecessor");
            if(transport==original){
                if(prior!=0)throw std::runtime_error("incident original transport is not first leg");
                break;
            }
            if(!prior)throw std::runtime_error("incident false original transport");
            transport=prior;
        }
    }
}
void verify_database(sqlite3* db,std::uint64_t max_generation){
    const bool has_counter=case_counter_column(db);
    const bool has_cases=table(db,"sg_dependent_incident_case");
    const bool has_members=table(db,"sg_dependent_incident_case_member");
    if(!has_counter){
        if(has_cases||has_members)throw std::runtime_error("orphaned incident journal schema");
        return; // historical worlds without incident cases remain valid
    }
    if(!has_cases||!has_members)
        throw std::runtime_error("activated incident journal tables are missing");
    for(const auto* t:{"sg_dependent_incident_case","sg_dependent_incident_case_member"}){
        for(const auto* suffix:{"_immutable_update","_immutable_delete"}){
            Query trigger(db,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?");
            trigger.s(1,std::string(t)+suffix);
            if(!trigger.row())throw std::runtime_error("incident immutability guard missing");
        }
    }
    Query metadata(db,"SELECT dependent_case_count FROM world_meta WHERE singleton=1");
    if(!metadata.row())throw std::runtime_error("incident journal missing world metadata");
    const auto expected_count=metadata.number(0);
    Query cases(db,"SELECT case_id,family_scope,original_transport,current_transport,ferry,"
        "stranding_command,observed_generation,observed_tick,adult_count,child_count,observation "
        "FROM sg_dependent_incident_case ORDER BY case_id");
    std::uint64_t count=0;
    while(cases.row()){
        ++count;
        const auto id=cases.str(0),family=cases.str(1);
        const auto original=cases.uid(2),transport=cases.uid(3),ferry=cases.uid(4);
        const auto command=cases.uid(5),gen=cases.number(6),tick=cases.number(7);
        const auto adult_count=cases.number(8),child_count=cases.number(9);
        if(id.empty()||family.empty()||!original||!transport||!ferry||!command||
           !gen||gen>max_generation||cases.number(10)!=1)
            throw std::runtime_error("incident case contains forbidden outcome or generation");
        const auto proof=snapshot_evidence(db,family,gen,transport,ferry);
        verify_child_origins(db,family,original,transport,proof.children);
        if(proof.tick!=tick || proof.adults.size()!=adult_count ||
           proof.children.size()!=child_count)
            throw std::runtime_error("incident roster count or tick does not match world");
        validate_cause(db,command,ferry,transport,gen);
        Query members(db,"SELECT kind,member_id,guardian_id,co_guardian_id,board_generation "
                          "FROM sg_dependent_incident_case_member WHERE case_id=? "
                          "ORDER BY kind,member_id");
        members.s(1,id);
        std::set<std::uint64_t> adults,children;
        while(members.row()){
            const auto kind=members.number(0),member=members.uid(1);
            if(kind==1){
                if(members.uid(2)||members.uid(3)||members.number(4)||
                   !adults.insert(member).second)
                    throw std::runtime_error("incident forged adult member");
            }else if(kind==2){
                const auto found=proof.guardians.find(member);
                if(found==proof.guardians.end() ||
                   found->second!=std::make_pair(members.uid(2),members.uid(3)) ||
                   proof.board_generations.at(member)!=members.number(4) ||
                   !children.insert(member).second)
                    throw std::runtime_error("incident forged child lineage or berth");
            }else throw std::runtime_error("incident invalid passenger role");
        }
        if(std::vector<std::uint64_t>(adults.begin(),adults.end())!=proof.adults ||
           std::vector<std::uint64_t>(children.begin(),children.end())!=proof.children)
            throw std::runtime_error("incident missing or surplus manifest member");
    }
    if(count!=expected_count)
        throw std::runtime_error("incident journal count disagrees with immutable cases");
    Query orphan(db,"SELECT COUNT(*) FROM sg_dependent_incident_case_member m "
        "LEFT JOIN sg_dependent_incident_case c ON c.case_id=m.case_id WHERE c.case_id IS NULL");
    if(!orphan.row()||orphan.number(0)!=0)throw std::runtime_error("orphaned incident members");
}
// q6e5b2: a stranding case's *nonfatal* rescue must be separately proved.
// Case evidence and world receipts cannot be replaced by a free-form report.
bool rescue_outcome_counter_column(sqlite3* db){
    Query q(db,"PRAGMA table_info(world_meta)");
    while(q.row())if(q.str(1)=="dependent_rescue_outcome_count")return true;
    return false;
}
struct RescueProof {
    std::string family;
    std::uint64_t original_transport=0,source_transport=0;
    EntityId source_ferry=0;
    std::vector<std::uint64_t> adults,children;
    std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> parents;
};
RescueProof rescue_case_proof(sqlite3* db,const std::string& case_id){
    Query q(db,"SELECT family_scope,original_transport,current_transport,ferry,observed_generation "
               "FROM sg_dependent_incident_case WHERE case_id=?");
    q.s(1,case_id);
    if(!q.row())throw std::runtime_error("rescue references unknown stranding case");
    RescueProof r{};r.family=q.str(0);r.original_transport=q.uid(1);
    r.source_transport=q.uid(2);r.source_ferry=q.uid(3);
    const auto gen=q.number(4);
    if(q.row())throw std::runtime_error("duplicate stranding case identity");
    const auto snapshot=snapshot_evidence(db,r.family,gen,r.source_transport,r.source_ferry);
    r.adults=snapshot.adults;r.children=snapshot.children;r.parents=snapshot.guardians;
    verify_child_origins(db,r.family,r.original_transport,r.source_transport,r.children);
    return r;
}
struct OutcomeFields {
    std::string outcome_id,case_id,family;
    std::uint64_t source=0,rescue=0,rescue_command=0,guardian=0;
    EntityId receiving_ferry=0;
    std::uint64_t generation=0,tick=0,member_count=0;
};
void validate_rescue_outcome(sqlite3* db,const OutcomeFields& v,
                            std::uint64_t max_generation){
    if(v.outcome_id.empty()||v.case_id.empty()||v.family.empty()||
       !v.source||!v.rescue||v.source==v.rescue||!v.rescue_command||
       !v.guardian||!v.receiving_ferry||!v.generation||
       v.generation>max_generation||v.member_count==0||v.member_count>15)
        throw std::runtime_error("rescue outcome contains invalid identity/generation");
    const auto prior=rescue_case_proof(db,v.case_id);
    if(prior.family!=v.family||prior.source_transport!=v.source||
       prior.children.size()!=v.member_count || prior.source_ferry==v.receiving_ferry ||
       !std::binary_search(prior.adults.begin(),prior.adults.end(),v.guardian))
        throw std::runtime_error("rescue outcome is not linked to exact stranded roster");
    Query event(db,"SELECT generation,action,outcome,actor_id,target_id,command "
                   "FROM historical_events WHERE command_id=?");
    event.s(1,std::to_string(v.rescue_command));
    if(!event.row()||event.number(0)!=v.generation||
       event.number(1)!=static_cast<std::uint64_t>(Action::HostEvacuatePassengers)||
       event.number(2)!=static_cast<std::uint64_t>(Outcome::Applied)||
       event.uid(3)!=prior.source_ferry||event.uid(4)!=v.receiving_ferry)
        throw std::runtime_error("rescue outcome lacks actual applied evacuation event");
    std::istringstream in(event.str(5));
    std::uint64_t command_id=0,tick=0,sequence=0,issuer=0,actor=0,target=0;
    int action=-1;double vx=0,vy=0;std::int64_t amount=0;
    std::string envelope;std::uint64_t transport=0;
    if(!(in>>command_id>>tick>>sequence>>issuer>>actor>>target>>action>>vx>>vy>>
          amount>>envelope>>transport)||
       command_id!=v.rescue_command||issuer!=Simulation::kServerAuthority||
       actor!=prior.source_ferry||target!=v.receiving_ferry||
       action!=static_cast<int>(Action::HostEvacuatePassengers)||
       envelope!="T31"||transport!=v.source||amount<0||
       static_cast<std::uint64_t>(amount)!=v.rescue)
        throw std::runtime_error("forged evacuation transport command envelope");
    if(event.row())throw std::runtime_error("duplicate evacuation historical event");
    Query state(db,"SELECT state,checksum FROM snapshots WHERE generation=?");
    state.n(1,v.generation);
    if(!state.row()||sqlite3_column_type(state.stmt,0)!=SQLITE_BLOB)
        throw std::runtime_error("rescue outcome lacks pinned world snapshot");
    const auto* bytes=static_cast<const char*>(sqlite3_column_blob(state.stmt,0));
    const int length=sqlite3_column_bytes(state.stmt,0);
    if(!bytes||length<=0||length>16*1024*1024)
        throw std::runtime_error("rescue outcome snapshot unreadable");
    const auto simulation=Simulation::from_snapshot(
        std::string(bytes,static_cast<std::size_t>(length)));
    const auto hash=state.str(1);
    if(hash!=std::to_string(simulation.state_hash())||simulation.tick()!=v.tick||
       simulation.voyage_status(v.source)!=5||
       simulation.voyage_status(v.rescue)!=1)
        throw std::runtime_error("rescue outcome snapshot is not actual evacuation");
    Query op(db,"SELECT state_hash,tick,kind FROM operation_log WHERE generation=?");
    op.n(1,v.generation);
    if(!op.row()||op.str(0)!=hash||op.number(1)!=v.tick||op.number(2)!=2||op.row())
        throw std::runtime_error("rescue outcome lacks durable world operation step");
    if(adults_aboard(simulation,v.receiving_ferry,v.rescue)!=prior.adults)
        throw std::runtime_error("rescue did not place complete adults on receiving ferry");
    Query members(db,"SELECT child_id,guardian_id,co_guardian_id,board_generation "
        "FROM sg_dependent_rescue_outcome_member WHERE outcome_id=? ORDER BY child_id");
    members.s(1,v.outcome_id);
    std::vector<std::uint64_t> observed;
    while(members.row()){
        const auto child=members.uid(0),guardian=members.uid(1),other=members.uid(2);
        const auto board_gen=members.number(3);
        const auto found=prior.parents.find(child);
        if(found==prior.parents.end()||found->second!=std::make_pair(guardian,other)||
           board_gen!=v.generation || (v.guardian!=guardian && v.guardian!=other))
            throw std::runtime_error("receiving guardian differs from actual parent or case roster");
        observed.push_back(child);
        Query prior_leg(db,"SELECT status,termination_kind,arrival_command,arrival_generation "
            "FROM sg_dependent_travel WHERE family_scope=? AND child_id=? AND transport_id=?");
        prior_leg.s(1,v.family);prior_leg.s(2,std::to_string(child));
        prior_leg.s(3,std::to_string(v.source));
        if(!prior_leg.row()||prior_leg.number(0)!=2||prior_leg.number(1)!=1||
           prior_leg.uid(2)!=v.rescue_command||prior_leg.number(3)!=v.generation||
           prior_leg.row())
            throw std::runtime_error("child's original ferry custody was not closed by rescue");
        Query received(db,"SELECT guardian_id,co_guardian_id,transfer_from_transport,"
            "board_command,board_generation,ferry FROM sg_dependent_travel "
            "WHERE family_scope=? AND child_id=? AND transport_id=?");
        received.s(1,v.family);received.s(2,std::to_string(child));
        received.s(3,std::to_string(v.rescue));
        if(!received.row()||received.uid(0)!=guardian||received.uid(1)!=other||
           received.uid(2)!=v.source||received.uid(3)!=v.rescue_command||
           received.number(4)!=v.generation||received.uid(5)!=v.receiving_ferry||
           received.row())
            throw std::runtime_error("child lacks matching actual receiving-ferry berth");
    }
    if(observed!=prior.children || observed.size()!=v.member_count)
        throw std::runtime_error("rescue outcome contains incomplete/duplicate child roster");
}
void verify_rescue_outcomes(sqlite3* db,std::uint64_t max_generation){
    const bool counter=rescue_outcome_counter_column(db);
    const bool headers=table(db,"sg_dependent_rescue_outcome");
    const bool members=table(db,"sg_dependent_rescue_outcome_member");
    if(!counter){
        if(headers||members)throw std::runtime_error("orphaned rescue outcome schema");
        return; // v45b1 worlds remain readable without outcomes
    }
    if(!headers||!members)throw std::runtime_error("activated rescue outcome ledger missing");
    for(const auto* t:{"sg_dependent_rescue_outcome","sg_dependent_rescue_outcome_member"})
        for(const auto* suffix:{"_immutable_update","_immutable_delete"}){
            Query trigger(db,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?");
            trigger.s(1,std::string(t)+suffix);
            if(!trigger.row())throw std::runtime_error("rescue immutability guard missing");
        }
    Query expected(db,"SELECT dependent_rescue_outcome_count FROM world_meta WHERE singleton=1");
    if(!expected.row())throw std::runtime_error("rescue outcome count anchor missing");
    const auto count_expected=expected.number(0);
    Query rows(db,"SELECT outcome_id,case_id,family_scope,source_transport,rescue_transport,"
        "receiving_ferry,rescue_command,receiving_guardian,rescue_generation,rescue_tick,member_count "
        "FROM sg_dependent_rescue_outcome ORDER BY outcome_id");
    std::uint64_t count=0;
    while(rows.row()){
        ++count;OutcomeFields o{};
        o.outcome_id=rows.str(0);o.case_id=rows.str(1);o.family=rows.str(2);
        o.source=rows.uid(3);o.rescue=rows.uid(4);o.receiving_ferry=rows.uid(5);
        o.rescue_command=rows.uid(6);o.guardian=rows.uid(7);
        o.generation=rows.number(8);o.tick=rows.number(9);o.member_count=rows.number(10);
        validate_rescue_outcome(db,o,max_generation);
    }
    if(count!=count_expected)throw std::runtime_error("rescue outcome anchor disagrees with receipts");
    Query orphan(db,"SELECT COUNT(*) FROM sg_dependent_rescue_outcome_member m LEFT JOIN "
        "sg_dependent_rescue_outcome o ON o.outcome_id=m.outcome_id WHERE o.outcome_id IS NULL");
    if(!orphan.row()||orphan.number(0)!=0)
        throw std::runtime_error("unowned rescue child member receipt");
}

} // namespace

// New guarded ledger is verified only after its underlying rescue outcome.
void verify_nonparent_guardianship_ledger(const std::string& path,
                                          std::uint64_t max_generation);
void verify_dependent_region_bindings(const std::string& path,
                                      std::uint64_t max_generation);

void verify_dependent_stranding_cases(const std::string& path,std::uint64_t max_generation){
    Database db(path);
    verify_database(db.db,max_generation);
    verify_rescue_outcomes(db.db,max_generation);
    verify_nonparent_guardianship_ledger(path,max_generation);
    verify_dependent_region_bindings(path,max_generation);
}

void PersistentWorld::record_dependent_stranding_case(
    const DependentStrandingCasePlan& plan,FailPoint fail){
    if(plan.case_id.empty()||plan.case_id.size()>128||
       plan.case_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=
           std::string::npos||plan.family_scope.empty()||plan.family_scope.size()>128||
       !plan.expected_world_generation||plan.expected_world_generation!=generation()||
       !plan.original_transport||!plan.current_transport||!plan.stranding_command_id||
       !plan.ferry)throw std::invalid_argument("invalid/stale host incident case");
    canonical(plan.adult_ids,"adult roster");canonical(plan.child_ids,"child roster");
    if(plan.adult_ids.size()+plan.child_ids.size()>16)
        throw std::invalid_argument("incident manifest exceeds physical berth capacity");
    const auto audit=audit_dependent_incident(plan.family_scope,plan.original_transport,
                                              plan.child_ids);
    for(const auto& member:audit.members)
        if(member.current_transport!=plan.current_transport||member.current_ferry!=plan.ferry||
           (member.custody!=DependentVerifiedCustody::Stranded &&
            member.custody!=DependentVerifiedCustody::RescuedStranded))
            throw std::runtime_error("incident outcome not a verified stranded dependent");
    if(adults_aboard(world(),plan.ferry,plan.current_transport)!=plan.adult_ids ||
       world().voyage_status(plan.current_transport)!=3)
        throw std::runtime_error("incident adult manifest/ship state mismatches reality");
    Database db(db_path_);
    sql(db.db,"BEGIN IMMEDIATE");
    try{
        if(current_generation(db.db)!=plan.expected_world_generation)
            throw std::runtime_error("incident committed world changed during preflight");
        const auto proof=snapshot_evidence(db.db,plan.family_scope,generation(),
                                           plan.current_transport,plan.ferry);
        if(proof.adults!=plan.adult_ids||proof.children!=plan.child_ids)
            throw std::runtime_error("incident manifest changed before SQLite transaction");
        validate_cause(db.db,plan.stranding_command_id,plan.ferry,
                       plan.current_transport,generation());
        if(!case_counter_column(db.db))
            sql(db.db,"ALTER TABLE world_meta ADD COLUMN dependent_case_count INTEGER "
                      "NOT NULL DEFAULT 0 CHECK(dependent_case_count>=0)");
        sql(db.db,"CREATE TABLE IF NOT EXISTS sg_dependent_incident_case ("
              "case_id TEXT PRIMARY KEY,family_scope TEXT NOT NULL,"
              "original_transport TEXT NOT NULL,current_transport TEXT NOT NULL,"
              "ferry TEXT NOT NULL,stranding_command TEXT NOT NULL UNIQUE,"
              "observed_generation INTEGER NOT NULL,observed_tick INTEGER NOT NULL,"
              "adult_count INTEGER NOT NULL CHECK(adult_count BETWEEN 1 AND 16),"
              "child_count INTEGER NOT NULL CHECK(child_count BETWEEN 1 AND 15),"
              "observation INTEGER NOT NULL CHECK(observation=1))");
        sql(db.db,"CREATE TABLE IF NOT EXISTS sg_dependent_incident_case_member ("
              "case_id TEXT NOT NULL REFERENCES sg_dependent_incident_case(case_id),"
              "kind INTEGER NOT NULL CHECK(kind IN (1,2)),member_id TEXT NOT NULL,"
              "guardian_id TEXT NOT NULL,co_guardian_id TEXT NOT NULL,"
              "board_generation INTEGER NOT NULL CHECK(board_generation>=0),"
              "PRIMARY KEY(case_id,kind,member_id))");
        for(const auto* t:{"sg_dependent_incident_case","sg_dependent_incident_case_member"}){
            const std::string prefix=t;
            const std::string update="CREATE TRIGGER IF NOT EXISTS "+prefix+"_immutable_update "
                "BEFORE UPDATE ON "+prefix+" BEGIN SELECT RAISE(ABORT,'incident receipt immutable'); END";
            const std::string remove="CREATE TRIGGER IF NOT EXISTS "+prefix+"_immutable_delete "
                "BEFORE DELETE ON "+prefix+" BEGIN SELECT RAISE(ABORT,'incident receipt immutable'); END";
            sql(db.db,update.c_str());sql(db.db,remove.c_str());
        }
        Query add(db.db,"INSERT INTO sg_dependent_incident_case VALUES(?,?,?,?,?,?,?,?,?,?,1)");
        add.s(1,plan.case_id);add.s(2,plan.family_scope);
        add.s(3,std::to_string(plan.original_transport));
        add.s(4,std::to_string(plan.current_transport));add.s(5,std::to_string(plan.ferry));
        add.s(6,std::to_string(plan.stranding_command_id));add.n(7,generation());
        add.n(8,proof.tick);add.n(9,plan.adult_ids.size());add.n(10,plan.child_ids.size());
        add.execute();
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected incident header write failure");
        for(const auto adult:plan.adult_ids){
            Query add_member(db.db,"INSERT INTO sg_dependent_incident_case_member "
                "VALUES(?,1,?,'0','0',0)");
            add_member.s(1,plan.case_id);add_member.s(2,std::to_string(adult));add_member.execute();
        }
        for(const auto child:plan.child_ids){
            const auto parents=proof.guardians.at(child);
            Query add_member(db.db,"INSERT INTO sg_dependent_incident_case_member "
                "VALUES(?,2,?,?,?,?)");
            add_member.s(1,plan.case_id);add_member.s(2,std::to_string(child));
            add_member.s(3,std::to_string(parents.first));
            add_member.s(4,std::to_string(parents.second));
            add_member.n(5,proof.board_generations.at(child));add_member.execute();
        }
        Query increase(db.db,"UPDATE world_meta SET dependent_case_count=dependent_case_count+1 "
                             "WHERE singleton=1");increase.execute();
        if(sqlite3_changes(db.db)!=1)
            throw std::runtime_error("missing world case count anchor");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected incident member write failure");
        verify_database(db.db,generation());
        sql(db.db,"COMMIT");
    }catch(...){sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void PersistentWorld::record_dependent_rescue_outcome(
    const DependentRescueOutcomePlan& plan,FailPoint fail){
    const auto valid_name=[](const std::string& name){
        return !name.empty()&&name.size()<=128&&
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==
                std::string::npos;
    };
    if(!valid_name(plan.outcome_id)||!valid_name(plan.case_id)||
       plan.family_scope.empty()||plan.family_scope.size()>128||
       !plan.expected_world_generation || plan.expected_world_generation!=generation()||
       !plan.rescue_command_id||!plan.source_transport||!plan.rescue_transport||
       plan.source_transport==plan.rescue_transport||!plan.receiving_ferry||
       !plan.receiving_guardian_id)
        throw std::invalid_argument("invalid/stale dependent rescue outcome");
    canonical(plan.child_ids,"rescue outcome children");
    Database db(db_path_);
    sql(db.db,"BEGIN IMMEDIATE");
    try{
        if(current_generation(db.db)!=plan.expected_world_generation)
            throw std::runtime_error("rescue outcome world generation changed");
        verify_database(db.db,generation());
        verify_rescue_outcomes(db.db,generation());
        const auto prior=rescue_case_proof(db.db,plan.case_id);
        if(prior.family!=plan.family_scope||prior.source_transport!=plan.source_transport||
           prior.children!=plan.child_ids)
            throw std::runtime_error("rescue outcome manifest is not the whole stranded case");
        if(!rescue_outcome_counter_column(db.db))
            sql(db.db,"ALTER TABLE world_meta ADD COLUMN dependent_rescue_outcome_count "
                      "INTEGER NOT NULL DEFAULT 0 CHECK(dependent_rescue_outcome_count>=0)");
        sql(db.db,"CREATE TABLE IF NOT EXISTS sg_dependent_rescue_outcome ("
            "outcome_id TEXT PRIMARY KEY,case_id TEXT NOT NULL UNIQUE "
            "REFERENCES sg_dependent_incident_case(case_id),family_scope TEXT NOT NULL,"
            "source_transport TEXT NOT NULL,rescue_transport TEXT NOT NULL,"
            "receiving_ferry TEXT NOT NULL,rescue_command TEXT NOT NULL UNIQUE,"
            "receiving_guardian TEXT NOT NULL,rescue_generation INTEGER NOT NULL,"
            "rescue_tick INTEGER NOT NULL,member_count INTEGER NOT NULL "
            "CHECK(member_count BETWEEN 1 AND 15))");
        sql(db.db,"CREATE TABLE IF NOT EXISTS sg_dependent_rescue_outcome_member ("
            "outcome_id TEXT NOT NULL REFERENCES sg_dependent_rescue_outcome(outcome_id),"
            "child_id TEXT NOT NULL,guardian_id TEXT NOT NULL,"
            "co_guardian_id TEXT NOT NULL,board_generation INTEGER NOT NULL,"
            "PRIMARY KEY(outcome_id,child_id))");
        for(const auto* t:{"sg_dependent_rescue_outcome",
                            "sg_dependent_rescue_outcome_member"}){
            const std::string base=t;
            sql(db.db,("CREATE TRIGGER IF NOT EXISTS "+base+"_immutable_update BEFORE "
                "UPDATE ON "+base+" BEGIN SELECT RAISE(ABORT,'rescue receipt immutable'); END")
                .c_str());
            sql(db.db,("CREATE TRIGGER IF NOT EXISTS "+base+"_immutable_delete BEFORE "
                "DELETE ON "+base+" BEGIN SELECT RAISE(ABORT,'rescue receipt immutable'); END")
                .c_str());
        }
        Query current(db.db,"SELECT tick FROM operation_log WHERE generation=?");
        current.n(1,generation());
        if(!current.row())throw std::runtime_error("rescue step lacks world journal");
        const auto tick=current.number(0);
        Query add(db.db,"INSERT INTO sg_dependent_rescue_outcome VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        add.s(1,plan.outcome_id);add.s(2,plan.case_id);add.s(3,plan.family_scope);
        add.s(4,std::to_string(plan.source_transport));
        add.s(5,std::to_string(plan.rescue_transport));
        add.s(6,std::to_string(plan.receiving_ferry));
        add.s(7,std::to_string(plan.rescue_command_id));
        add.s(8,std::to_string(plan.receiving_guardian_id));
        add.n(9,generation());add.n(10,tick);add.n(11,plan.child_ids.size());
        add.execute();
        if(fail==FailPoint::AfterReceiptWrite)
            throw std::runtime_error("injected rescue outcome header failure");
        for(const auto child:plan.child_ids){
            const auto it=prior.parents.find(child);
            if(it==prior.parents.end())throw std::runtime_error("unregistered child rescue outcome");
            Query member(db.db,"INSERT INTO sg_dependent_rescue_outcome_member VALUES(?,?,?,?,?)");
            member.s(1,plan.outcome_id);member.s(2,std::to_string(child));
            member.s(3,std::to_string(it->second.first));
            member.s(4,std::to_string(it->second.second));
            member.n(5,generation());member.execute();
        }
        sql(db.db,"UPDATE world_meta SET dependent_rescue_outcome_count="
                  "dependent_rescue_outcome_count+1 WHERE singleton=1");
        if(fail==FailPoint::AfterSnapshotWrite)
            throw std::runtime_error("injected rescue outcome member failure");
        verify_rescue_outcomes(db.db,generation());
        sql(db.db,"COMMIT");
    }catch(...){sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}

DependentRescueOutcome PersistentWorld::verify_dependent_rescue_outcome(
    const std::string& outcome_id) const{
    if(outcome_id.empty())throw std::invalid_argument("empty rescue outcome ID");
    // Re-read evidence, not merely the cached in-memory simulation.
    verify_dependent_stranding_cases(db_path_,generation());
    Database db(db_path_);
    Query row(db.db,"SELECT case_id,family_scope,rescue_transport,receiving_ferry,"
        "receiving_guardian,rescue_generation FROM sg_dependent_rescue_outcome "
        "WHERE outcome_id=?");
    row.s(1,outcome_id);
    if(!row.row())throw std::runtime_error("unknown verified rescue outcome");
    DependentRescueOutcome r{};
    r.outcome_id=outcome_id;r.case_id=row.str(0);const auto family=row.str(1);
    r.receiving_transport=row.uid(2);r.receiving_ferry=row.uid(3);
    r.receiving_guardian_id=row.uid(4);r.rescue_generation=row.number(5);
    const auto prior=rescue_case_proof(db.db,r.case_id);
    r.child_ids=prior.children;
    r.current_custody=audit_dependent_incident(family,prior.original_transport,r.child_ids);
    for(const auto& member:r.current_custody.members)
        if(!member.ever_rescued)
            throw std::runtime_error("rescue outcome child does not have a rescue chain");
    return r;
}

} // namespace galaxy
