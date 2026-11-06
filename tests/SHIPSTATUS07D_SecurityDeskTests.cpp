#include "galaxy/SecurityDeskHost.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
using namespace galaxy::colony_disposition;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
static Outcome committed(PersistentWorld& world,PlayerId owner,
    EntityId ship,Action action,std::int64_t amount,std::uint64_t id,
    EntityId target=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=owner;c.actor=ship;
    c.execute_tick=world.world().tick()+1;c.action=action;
    c.amount=amount;c.target=target;
    CHECK(world.submit(c));world.step();
    CHECK(world.world().events().back().command_id==id);
    return world.world().events().back().result;
}
static bool contains(const std::vector<std::string>& messages,
                     const std::string& fragment){
    for(const auto& s:messages)if(s.find(fragment)!=std::string::npos)return true;
    return false;
}
int main(){
 const auto dir=fs::temp_directory_path()/
     ("sg_status07d_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto copy=(dir/"recover.sqlite").string();
    EntityId aggressor=0,home=0;
    std::string canonical;
    std::uint64_t native_case_id=0,voluntary_hail_id=0;
    {
        Simulation genesis(7777);
        aggressor=genesis.add_colony_ship(7,{0,0},100,6);
        home=genesis.add_colony_ship(8,{1000,0},100,6);
        const auto asteroid=genesis.add_asteroid({400,0},16);
        for(const auto ship:{aggressor,home})
            genesis.provision_colony_genesis(
                ship,2000,2000,2000,2000,2000);
        // The target's real powered scanner is a legitimate exposed
        // hardpoint: an applied Confusion must not be fabricated from an
        // empty ship fitting.
        genesis.provision_starter_exploration_scanner(home);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        PersistentWorld world(db,std::move(genesis));
        TerritoryJournal land(db);
        CyberIncidentJournal cases(db);
        std::vector<std::string> messages;
        auto emit=[&](const std::string& frame){
            messages.push_back(frame);
        };
        std::uint64_t nonce8=920000000001ULL,nonce7=930000000001ULL;
        CHECK(security_desk::execute({"SECURITY_CLAIM","3000"},
            world,land,cases,8,home,home,nonce8,emit));
        CHECK(contains(messages,"SECURITY_RESULT|SECURITY_CLAIM|ACCEPTED|"));
        CHECK(contains(messages,"SECURITY_STATUS|"+std::to_string(home)+"|"));
        messages.clear();
        // No cross-owner session may use somebody else's home/pilot.
        CHECK(security_desk::execute({"SECURITY_CLAIM","3000"},
            world,land,cases,7,home,home,nonce7,emit));
        CHECK(contains(messages,"SECURITY_RESULT|DENIED|0"));
        CHECK(land.history().size()==1);
        CHECK(committed(world,7,aggressor,Action::Mine,2,7001,asteroid)==
              Outcome::Applied);
        CHECK(committed(world,7,aggressor,Action::RefineOre,2,7002)==
              Outcome::Applied);
        CHECK(committed(world,7,aggressor,Action::FabricateModule,1,7003)==
              Outcome::Applied);
        CHECK(committed(world,7,aggressor,Action::ConfigureCyberModule,3,7004)==
              Outcome::Applied);
        CHECK(committed(world,7,aggressor,Action::HackConfuse,0,7005,home)==
              Outcome::Applied);
        messages.clear();
        CHECK(security_desk::capture_current_tick(
            world,land,cases,8,emit)==1);
        CHECK(contains(messages,"SECURITY_CASE_FILED|8|"));
        CHECK(security_desk::capture_current_tick(
            world,land,cases,8,emit)==0);
        const auto index=cases.history();
        CHECK(index.size()==1);
        CHECK(index.front().attacker_owner==0);
        CHECK(index.front().attacking_ship==aggressor);
        native_case_id=index.front().case_id;
        messages.clear();
        security_desk::snapshot(world,land,cases,8,home,home,emit);
        CHECK(contains(messages,"SECURITY_CASE|"+
            std::to_string(native_case_id)+"|"));
        CHECK(!contains(messages,"SECURITY_CASE|"+std::to_string(
            native_case_id)+"|"+std::to_string(index.front().world_tick)+
            "|1|1|7|")); // no owner injected as a case field
        CHECK(!cases.summaries().front().later_identified_controller);
        messages.clear();
        // This native authenticated event does NOT require a warning and
        // does NOT autonomously acknowledge any notice or presume guilt.
        CHECK(security_desk::execute({"SECURITY_HAIL",std::to_string(home)},
            world,land,cases,7,aggressor,aggressor,nonce7,emit));
        CHECK(contains(messages,"SECURITY_RESULT|SECURITY_HAIL|ACCEPTED|"));
        const auto published=land.history();
        CHECK(published.back().kind==TerritoryEventKind::VoluntaryHail);
        CHECK(published.back().visitor_ship==aggressor);
        CHECK(published.back().visitor_owner==7);
        voluntary_hail_id=published.back().event_id;
        CHECK(cases.hail_history().size()==1);
        CHECK(cases.hail_history().front().hail_event==voluntary_hail_id);
        CHECK(cases.summaries().front().later_identified_controller==7);
        CHECK(cases.summaries().front().evidence.attacker_owner==0);
        CHECK(!cases.summaries().front().legal_crime_finding);
        CHECK(!cases.summaries().front().arrest_authorized);
        messages.clear();
        CHECK(security_desk::execute({"SECURITY_WARN",
               std::to_string(aggressor)},world,land,cases,
               8,home,home,nonce8,emit));
        CHECK(contains(messages,"SECURITY_RESULT|SECURITY_WARN|ACCEPTED|"));
        const auto warnings=land.pending_notices(world,7);
        CHECK(warnings.size()==1);
        messages.clear();
        security_desk::snapshot(world,land,cases,7,aggressor,aggressor,emit);
        CHECK(contains(messages,"SECURITY_NOTICE|"+
             std::to_string(home)+"|"+std::to_string(aggressor)));
        CHECK(!contains(messages,"SECURITY_CASE|"+std::to_string(native_case_id)));
        messages.clear();
        CHECK(security_desk::execute({"SECURITY_REPLY",
              std::to_string(home),std::to_string(warnings.front().event_id),
              "ACK"},world,land,cases,7,aggressor,aggressor,nonce7,emit));
        CHECK(contains(messages,"SECURITY_RESULT|SECURITY_REPLY|ACCEPTED|"));
        CHECK(land.pending_notices(world,7).empty());
        CHECK(cases.hail_history().size()==2);
        // A visual match is never a criminal conviction; weak infrastructure
        // cannot use the incident as a force/dispatch permission.
        messages.clear();
        CHECK(security_desk::execute({"SECURITY_PATROL",
              std::to_string(native_case_id),std::to_string(aggressor)},
              world,land,cases,8,home,home,nonce8,emit));
        CHECK(contains(messages,"SECURITY_RESULT|SECURITY_PATROL|DENIED|"));
        CHECK(world.world().npc_flight_orders().empty());
        CHECK(cases.history().size()==1);
        land.verify();cases.verify();
        canonical=world.world().canonical_state();
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld world(copy,Simulation(7));
        TerritoryJournal land(copy);
        CyberIncidentJournal cases(copy);
        CHECK(world.world().canonical_state()==canonical);
        CHECK(cases.history().size()==1 &&
              cases.history().front().case_id==native_case_id);
        CHECK(cases.hail_history().size()==2);
        CHECK(cases.hail_history().front().hail_event==voluntary_hail_id);
        CHECK(cases.summaries().front().later_identified_controller==7);
        CHECK(land.pending_notices(world,7).empty());
        land.verify();cases.verify();
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07D: original claimed security cases, authenticated voluntary hail and notice reply, scope isolation, no automatic force and durable restart\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-07D "<<e.what()<<'\n';
    std::error_code ignore;fs::remove_all(dir,ignore);return 1;
 }
}
