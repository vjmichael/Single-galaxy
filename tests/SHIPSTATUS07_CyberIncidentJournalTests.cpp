#include "galaxy/CyberIncidentJournal.hpp"
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
    EntityId ship,Action action,std::int64_t amount=0,EntityId target=0){
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=world.world().tick()+1;
    c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
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
        CHECK(!reports[0].legal_crime_finding &&
              !reports[0].arrest_authorized &&
              !reports[0].damage_authorized &&
              !reports[0].seizure_authorized);
        const auto& c=reports[0].evidence;
        CHECK(c.command_id==attack_id && c.case_id==case_id &&
              c.attacker_owner==7 && c.victim_owner==8 &&
              c.attacking_ship==attacker && c.victim_ship==victim &&
              c.jurisdiction_colony==victim &&
              c.observer_npc==defender_guard);
        CHECK(c.world_tick==world.world().tick() &&
              c.world_generation==world.generation() &&
              c.previous_digest=="genesis" && c.digest.size()==16);
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
        const auto summary=cases.summaries().front();
        CHECK(summary.evidence.case_id==case_id &&
              summary.evidence.command_id==attack_id &&
              summary.witnessed_attempt && summary.disrupted_modules);
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
