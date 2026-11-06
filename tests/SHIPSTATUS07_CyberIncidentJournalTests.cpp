#include "galaxy/CyberIncidentJournal.hpp"
#include "galaxy/CyberSecurityResponse.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
using namespace galaxy::colony_disposition;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)

static Command command(const PersistentWorld& world,std::uint64_t id,
    EntityId ship,Action action,std::int64_t amount=0,
    EntityId target=0,PlayerId issuer=7){
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=world.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
    return c;
}
static Outcome execute(PersistentWorld& world,const Command& c){
    CHECK(world.submit(c));
    world.step();
    CHECK(world.world().events().back().command_id==c.id);
    return world.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
    ("sg_ship_status07_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto db=(dir/"original.sqlite").string();
 const auto copy=(dir/"recovered.sqlite").string();
 try{
    fs::create_directories(dir);
    // Intake cannot fabricate a second original world from an unknown path.
    bool nonexistent_denied=false;
    try {
        CyberIncidentJournal counterfeit((dir/"no_original_world.sqlite").string());
    }catch(const std::exception&){nonexistent_denied=true;}
    CHECK(nonexistent_denied);
    CHECK(!fs::exists(dir/"no_original_world.sqlite"));
    EntityId attacker=0,victim=0;
    constexpr std::uint64_t attack_id=97001,case_id=97002;
    std::uint64_t defender_guard=0;
    std::string original_snapshot;
    {
        Simulation genesis(97007);
        attacker=genesis.add_colony_ship(7,{0,0},100,6);
        victim=genesis.add_colony_ship(8,{1000,0},100,6);
        const auto rock=genesis.add_asteroid({400,0},16);
        genesis.provision_starter_exploration_scanner(victim);
        genesis.provision_colony_genesis(attacker,2000,2000,2000,2000,2000);
        genesis.provision_colony_genesis(victim,2000,2000,2000,2000,2000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        PersistentWorld world(db,std::move(genesis));
        TerritoryJournal territory(db);
        CyberIncidentJournal cases(db);
        for(const auto& [id,n]:world.world().npc_records())
            if(n.home_ship==victim && n.location==victim &&
               n.role==NpcRole::Ship &&
               world.world().npc_available_for_duty(id)){
                defender_guard=id;break;
            }
        CHECK(defender_guard);
        CHECK(cases.history().empty());
        CHECK(!cases.intake(world,territory,victim,defender_guard,
                            attack_id,case_id));
        CHECK(territory.claim(world,8,victim,96001,3000));
        CHECK(execute(world,command(world,94001,attacker,Action::Mine,2,rock))==
              Outcome::Applied);
        CHECK(execute(world,command(world,94002,attacker,Action::RefineOre,2))==
              Outcome::Applied);
        CHECK(execute(world,command(world,94003,attacker,Action::FabricateModule,1))==
              Outcome::Applied);
        CHECK(execute(world,command(world,94004,attacker,Action::ConfigureCyberModule,3))==
              Outcome::Applied);
        CHECK(world.world().find(attacker)->fitting.intrusion_emitter());
        CHECK(!cases.intake(world,territory,victim,defender_guard,
                            attack_id,case_id));
        CHECK(execute(world,command(world,attack_id,attacker,Action::HackConfuse,
                                    0,victim))==Outcome::Applied);
        CHECK(world.world().status_effects().records().size()==1);
        const auto original=contemporaneous_cyber_evidence(
            world,territory,victim,defender_guard,attack_id);
        CHECK(original.attempted && original.confusion_applied);
        CHECK(original.attacker_owner==0);
        CHECK(original.sensor_detail>=1 &&
              !original.observed_description.empty());
        CHECK(!original.legal_crime_finding && !original.arrest_authorized &&
              !original.damage_authorized && !original.seizure_authorized);
        CHECK(!cases.intake(world,territory,attacker,defender_guard,
                            attack_id,case_id));
        CHECK(!cases.intake(world,territory,victim,defender_guard+100000,
                            attack_id,case_id));
        const auto before=world.world().canonical_state();
        bool failed=false;
        try{
            (void)cases.intake(world,territory,victim,defender_guard,
                 attack_id,case_id,CyberIntakeFailPoint::AfterInsert);
        }catch(const std::runtime_error&){failed=true;}
        CHECK(failed);
        CHECK(cases.history().empty());
        CHECK(world.world().canonical_state()==before);
        {CyberIncidentJournal independent(db);
         CHECK(independent.history().empty());}
        CHECK(cases.intake(world,territory,victim,defender_guard,
                           attack_id,case_id));
        CHECK(!cases.intake(world,territory,victim,defender_guard,
                            attack_id,case_id));
        CHECK(!cases.intake(world,territory,victim,defender_guard,
                            attack_id,case_id+1));
        cases.verify();
        const auto reports=cases.summaries();
        CHECK(reports.size()==1 && reports[0].witnessed_attempt &&
              reports[0].disrupted_modules &&
              reports[0].independent_law_review_required);
        CHECK(!reports[0].later_identified_controller &&
              !reports[0].identity_hail_event &&
              !reports[0].evidence.attacking_ship);
        CHECK(cases.hail_history().empty());
        CHECK(cases.watch_known_descriptions(
              world,8,victim,defender_guard).size()==1);
        CHECK(cases.watch_known_descriptions(
              world,7,victim,defender_guard).empty());
        const auto weak=security_matches(world,cases,8,victim,defender_guard);
        CHECK(weak.size()==1 && weak.front().case_id==case_id &&
              weak.front().sighted_ship==attacker);
        // The victim's scanner is temporarily confused OFF by this hack.
        // A weak watch may record a matching description but cannot
        // mobilize a security patrol from a suspicious silhouette alone.
        CHECK(weak.front().response==CyberSecurityLevel::RecordOnly);
        CHECK(!weak.front().identified_culprit &&
              !weak.front().crime_finding &&
              !weak.front().arrest_authorized &&
              !weak.front().damage_authorized);
        CHECK(request_signature_patrol(world,cases,8,victim,defender_guard,
                 attacker,case_id,96010)==npc_response::Status::NoAuthority);
        CHECK(!reports[0].legal_crime_finding &&
              !reports[0].arrest_authorized &&
              !reports[0].damage_authorized &&
              !reports[0].seizure_authorized);
        CHECK(cases.for_colony_owner(world,8,victim).size()==1);
        CHECK(cases.for_colony_owner(world,7,victim).empty());
        CHECK(cases.for_colony_owner(world,8,attacker).empty());
        CHECK(cases.for_colony_owner(world,0,victim).empty());
        const auto& c=reports[0].evidence;
        CHECK(c.command_id==attack_id && c.case_id==case_id &&
              c.attacker_owner==0 && c.victim_owner==8 &&
              c.attacking_ship==0 && c.victim_ship==victim &&
              c.jurisdiction_colony==victim &&
              c.observer_npc==defender_guard);
        CHECK(c.world_tick==world.world().tick() &&
              c.world_generation==world.generation() &&
              c.previous_digest=="genesis" && c.digest.size()==16);
        CHECK(c.record_version==2 && c.sensor_detail>=1 &&
              c.observed_description==original.observed_description);
        CHECK(cases.history().front().attacking_ship==attacker);
        CHECK(cases.history().front().attacker_owner==0);
        // Hail acknowledgement is an independent original-world receipt.
        CHECK(!cases.link_authenticated_hail(
            world,territory,victim,defender_guard,attacker,7,96003));
        CHECK(territory.warn(world,8,victim,attacker,defender_guard,
                             96002,4));
        CHECK(!cases.link_authenticated_hail(
            world,territory,victim,defender_guard,attacker,7,96002));
        CHECK(territory.respond_to_notice(world,7,victim,attacker,
                                           96002,96003,false));
        CHECK(!cases.link_authenticated_hail(
            world,territory,victim,defender_guard,attacker,8,96003));
        CHECK(cases.link_authenticated_hail(
            world,territory,victim,defender_guard,attacker,7,96003));
        CHECK(!cases.link_authenticated_hail(
            world,territory,victim,defender_guard,attacker,7,96003));
        CHECK(cases.hail_history().size()==1);
        const auto linked=cases.summaries().front();
        CHECK(linked.later_identified_controller==7 &&
              linked.identity_hail_event==96003 &&
              linked.evidence.attacker_owner==0 &&
              linked.evidence.attacking_ship==attacker &&
              !linked.legal_crime_finding && !linked.arrest_authorized);
        // Security capability must be truly installed, not derived from
        // a police label or hostile diplomatic agenda.
        CHECK(execute(world,command(world,94011,victim,Action::Mine,
                                    2,rock,8))==Outcome::Applied);
        CHECK(execute(world,command(world,94012,victim,Action::RefineOre,
                                    2,0,8))==Outcome::Applied);
        CHECK(execute(world,command(world,94013,victim,Action::FabricateModule,
                                    1,0,8))==Outcome::Applied);
        CHECK(execute(world,command(world,94014,victim,Action::ConfigureCyberModule,
                                    6,0,8))==Outcome::Applied);
        CHECK(world.world().find(victim)->fitting.counter_ecm_array());
        CHECK(security_capability(world.world(),victim)==
              CyberSecurityLevel::PatrolEligible);
        const auto strong=security_matches(world,cases,8,victim,defender_guard);
        CHECK(strong.size()==1 && strong.front().response==
              CyberSecurityLevel::PatrolEligible);
        CHECK(!strong.front().identified_culprit &&
              !strong.front().crime_finding);
        CHECK(request_signature_patrol(world,cases,8,victim,defender_guard,
              attacker,case_id,96010)==npc_response::Status::Accepted);
        CHECK(world.world().npc_flight_orders().at(96010).mode==
              NpcFlightMode::LocalRendezvous);
        CHECK(world.world().npc_flight_orders().at(96010).purpose==
              NpcFlightPurpose::Patrol);
        CHECK(!linked.damage_authorized && !linked.seizure_authorized);
        original_snapshot=world.world().canonical_state();
        // Even same-process tampering through SQLite is refused by the
        // original-world casebook's append-only database triggers.
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "UPDATE sg_ship_status07_incidents SET applied=0 WHERE case_id=97002",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "DELETE FROM sg_ship_status07_incidents WHERE case_id=97002",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "UPDATE sg_ship_status07_hail_links SET controller=8 WHERE case_id=97002",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "DELETE FROM sg_ship_status07_hail_links WHERE case_id=97002",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_close(raw)==SQLITE_OK);
        cases.verify();
        PersistentWorld::recover_copy(db,copy);
        // The sidecar case index is copied along with the original world
        // SQLite image, but is never allowed to modify that world state.
        CHECK(world.world().canonical_state()==original_snapshot);
        world.step();
        CHECK(!cases.intake(world,territory,victim,defender_guard,
                            attack_id,case_id+2));
    }
    {
        PersistentWorld recovered(copy,Simulation(1));
        CyberIncidentJournal cases(copy);
        TerritoryJournal territory(copy);
        CHECK(recovered.world().canonical_state()==original_snapshot);
        cases.verify();
        CHECK(cases.history().size()==1);
        CHECK(cases.hail_history().size()==1);
        const auto summary=cases.summaries().front();
        CHECK(summary.evidence.case_id==case_id &&
              summary.evidence.command_id==attack_id &&
              summary.witnessed_attempt && summary.disrupted_modules &&
              summary.later_identified_controller==7 &&
              summary.identity_hail_event==96003);
        CHECK(!summary.legal_crime_finding && !summary.arrest_authorized &&
              !summary.damage_authorized && !summary.seizure_authorized);
        recovered.step();
        CHECK(!cases.intake(recovered,territory,victim,defender_guard,
                            attack_id,case_id+3));
        CHECK(cases.history().size()==1);
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07: genuine local native cyber evidence, immutable SQLite case intake, crash rollback, no repeat or forged witness, no invented verdict, copy/restart recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-07 "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
