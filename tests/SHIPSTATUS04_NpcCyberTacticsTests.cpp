#include "galaxy/TerritorialCyberEvidence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
using namespace galaxy::colony_disposition;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)

static Command order(const PersistentWorld& world,std::uint64_t id,
                     PlayerId issuer,EntityId ship,Action action,
                     std::int64_t amount=0,EntityId target=0){
    Command c{};
    c.id=id;c.execute_tick=world.world().tick()+1;c.sequence=id;
    c.issuer=issuer;c.actor=ship;c.action=action;c.target=target;
    c.amount=amount;
    return c;
}
static Outcome run(PersistentWorld& w,const Command& cmd){
    CHECK(w.submit(cmd));
    w.step();
    CHECK(w.world().events().back().command_id==cmd.id);
    return w.world().events().back().result;
}
int main() {
 const auto dir=fs::temp_directory_path()/
    ("sg_npc_cyber_"+std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto db=(dir/"world.sqlite").string(),copy=(dir/"copy.sqlite").string();
 try {
  fs::create_directories(dir);
  EntityId attacker=0,victim=0,far=0;
  std::uint64_t pilot=0,defender_watch=0;
  std::string saved;
  std::uint64_t attack_id=91009;
  {
    Simulation genesis(91004);
    attacker=genesis.add_colony_ship(7,{0,0},100,6);
    victim=genesis.add_colony_ship(8,{1000,0},100,6);
    far=genesis.add_ship(9,{40000,0},100);
    const auto rock=genesis.add_asteroid({400,0},16);
    genesis.provision_starter_exploration_scanner(victim);
    genesis.provision_colony_genesis(attacker,2000,2000,2000,2000,2000);
    genesis.provision_colony_genesis(victim,2000,2000,2000,2000,2000);
    genesis.enable_individual_workforce();
    genesis.upgrade_assignment_snapshot_format();
    genesis.enable_passenger_life_support();
    const auto original=genesis.total_material_kg();
    PersistentWorld w(db,std::move(genesis));
    TerritoryJournal journal(db);
    for(const auto& [id,n]:w.world().npc_records()){
        if(n.role!=NpcRole::Ship || !w.world().npc_available_for_duty(id))
            continue;
        if(n.home_ship==attacker && n.location==attacker)pilot=id;
        if(n.home_ship==victim && n.location==victim)defender_watch=id;
    }
    CHECK(pilot && defender_watch);
    CHECK(journal.claim(w,8,victim,91001,3000));
    CHECK(run(w,order(w,400,7,attacker,Action::Mine,2,rock))==
          Outcome::Applied);
    CHECK(run(w,order(w,401,7,attacker,Action::RefineOre,2))==
          Outcome::Applied);
    CHECK(run(w,order(w,402,7,attacker,Action::FabricateModule,1))==
          Outcome::Applied);
    CHECK(run(w,order(w,403,7,attacker,Action::ConfigureCyberModule,3))==
          Outcome::Applied);
    CHECK(w.world().find(attacker)->fitting.intrusion_emitter());
    CHECK(w.world().total_material_kg()==original);

    CHECK(!w.submit_npc_cyber_attack(8,attacker,pilot,pilot,victim,91002));
    CHECK(!w.submit_npc_cyber_attack(7,attacker,defender_watch,pilot,victim,91003));
    CHECK(!w.submit_npc_cyber_attack(7,attacker,pilot,defender_watch,victim,91004));
    CHECK(!w.submit_npc_cyber_attack(7,attacker,pilot,pilot,far,91005));
    CHECK(!w.submit_npc_cyber_attack(7,attacker,pilot,pilot,attacker,91006));
    CHECK(w.world().status_effects().records().empty());

    const auto previous=w.world().canonical_state();
    const auto generation=w.generation();
    bool crashed=false;
    try{
        (void)w.submit_npc_cyber_attack(7,attacker,pilot,pilot,victim,attack_id,
                                       FailPoint::AfterSnapshotWrite);
    }catch(const std::runtime_error&){crashed=true;}
    CHECK(crashed);
    CHECK(w.generation()==generation);
    CHECK(w.world().canonical_state()==previous);
    {PersistentWorld independently_open(db,Simulation(11));
        CHECK(independently_open.world().canonical_state()==previous);}
    CHECK(w.submit_npc_cyber_attack(7,attacker,pilot,pilot,victim,attack_id));
    CHECK(!w.submit_npc_cyber_attack(7,attacker,pilot,pilot,victim,attack_id));
    CHECK(w.world().status_effects().records().empty());
    saved=w.world().canonical_state();
  }
  {
    PersistentWorld w(db,Simulation(12));
    TerritoryJournal journal(db);
    CHECK(w.world().canonical_state()==saved);
    CHECK(w.world().status_effects().records().empty());
    w.step();
    CHECK(w.world().events().back().command_id==attack_id);
    CHECK(w.world().events().back().result==Outcome::Applied);
    CHECK(w.world().status_effects().records().size()==1);
    CHECK(!w.world().hardpoint_effective_on(victim,0));
    const auto evidence=contemporaneous_cyber_evidence(
       w,journal,victim,defender_watch,attack_id);
    CHECK(evidence.attempted && evidence.confusion_applied);
    CHECK(evidence.attacking_ship==attacker && evidence.victim_ship==victim);
    CHECK(evidence.attacker_owner==7 && evidence.victim_owner==8);
    CHECK(!evidence.legal_crime_finding && !evidence.arrest_authorized &&
          !evidence.damage_authorized && !evidence.seizure_authorized);
    CHECK(!contemporaneous_cyber_evidence(w,journal,victim,pilot,attack_id).attempted);
    CHECK(!contemporaneous_cyber_evidence(w,journal,victim,defender_watch,403).attempted);
    // The same original scanner executor is disabled regardless of operator.
    CHECK(run(w,order(w,905,8,victim,Action::SurveyLocal))==
          Outcome::ModuleOffline);
    CHECK(!contemporaneous_cyber_evidence(
        w,journal,victim,defender_watch,attack_id).attempted);
    PersistentWorld::recover_copy(db,copy);
    saved=w.world().canonical_state();
  }
  {
    PersistentWorld recovered(copy,Simulation(13));
    CHECK(recovered.world().canonical_state()==saved);
    CHECK(recovered.world().status_effects().records().size()==1);
    CHECK(!recovered.world().hardpoint_effective_on(victim,0));
    recovered.steps(12);
    CHECK(recovered.world().hardpoint_effective_on(victim,0));
  }
  fs::remove_all(dir);
  std::cout<<"PASS SHIP-STATUS-04: named NPC current observation, real native attack, original SQLite rollback/restart and no fabricated crime verdict\n";
  return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-04 "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
