#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" @"+std::to_string(__LINE__));}while(false)

static Command order(const PersistentWorld& w,std::uint64_t id,PlayerId owner,
                     EntityId ship,Action action,std::int64_t amount=0,
                     EntityId target=0) {
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
    return c;
}
static Outcome run(PersistentWorld& w,const Command& c){
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    return w.world().events().back().result;
}
static void fabricate(PersistentWorld& w,PlayerId owner,EntityId ship,
                      EntityId rock,std::uint64_t& id,std::int64_t count){
    CHECK(run(w,order(w,id++,owner,ship,Action::Mine,count*2,rock))==Outcome::Applied);
    CHECK(run(w,order(w,id++,owner,ship,Action::RefineOre,count*2))==Outcome::Applied);
    CHECK(run(w,order(w,id++,owner,ship,Action::FabricateModule,count))==Outcome::Applied);
}
int main(){
 const auto dir=fs::temp_directory_path()/("sg_status03_"+std::to_string(
     std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto db=(dir/"world.sqlite").string();
 const auto copy=(dir/"copy.sqlite").string();
 try{
  fs::create_directories(dir);
  EntityId attacker=0,victim=0,remote=0,own_ship=0;
  std::int64_t starting_mass=0;
  std::uint64_t next_id=110;
  std::string queued_state,recovered_state;
  {
    Simulation genesis(50303);
    attacker=genesis.add_colony_ship(7,{0,0},100,6);
    victim=genesis.add_colony_ship(8,{1000,0},100,6);
    remote=genesis.add_ship(9,{40000,0},100);
    own_ship=genesis.add_ship(7,{500,0},100);
    const auto rock=genesis.add_asteroid({400,0},20);
    genesis.provision_starter_exploration_scanner(victim);
    genesis.enable_individual_workforce();
    starting_mass=genesis.total_material_kg();
    PersistentWorld w(db,std::move(genesis));
    // Original finite ore -> refine -> fabricated modules -> fitted device.
    fabricate(w,7,attacker,rock,next_id,1);
    fabricate(w,8,victim,rock,next_id,1);
    CHECK(w.world().colony_ship(attacker)->modules==1);
    CHECK(w.world().colony_ship(victim)->modules==1);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::ConfigureCyberModule,3))==
          Outcome::Applied);
    CHECK(w.world().find(attacker)->fitting.intrusion_emitter());
    CHECK(w.world().colony_ship(attacker)->modules==0);
    CHECK(w.world().total_material_kg()==starting_mass);
    CHECK(run(w,order(w,next_id++,8,victim,Action::HackConfuse,0,attacker))==
          Outcome::NoResources);
    // Input authorization and physical distance: source owner cannot spoof.
    const auto unauthorized=order(w,next_id++,8,attacker,Action::HackConfuse,0,victim);
    if(w.submit(unauthorized)){
        w.step();CHECK(w.world().events().back().result==Outcome::Unauthorized);
    }
    CHECK(w.world().status_effects().records().empty());
    // An unfitted or manually powered-down transmitter cannot emit.
    CHECK(run(w,order(w,next_id++,7,attacker,Action::SetHardpointPower,0))==
          Outcome::Applied);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,victim))==
          Outcome::ModuleOffline);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::SetHardpointPower,1))==
          Outcome::Applied);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,remote))==
          Outcome::OutOfRange);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,own_ship))==
          Outcome::Unauthorized);
    CHECK(w.world().find(attacker)->last_hack_tick==0);
    // Failure during native attack's snapshot write cannot half-publish.
    const auto attack=order(w,next_id++,7,attacker,Action::HackConfuse,0,victim);
    CHECK(w.submit(attack));
    queued_state=w.world().canonical_state();
    bool failed=false;
    try{w.step(FailPoint::AfterSnapshotWrite);}
    catch(const std::runtime_error&){failed=true;}
    CHECK(failed);
    CHECK(w.world().canonical_state()==queued_state);
    {PersistentWorld independently_open(db,Simulation(1));
        CHECK(independently_open.world().canonical_state()==queued_state);}
    w.step();
    CHECK(w.world().events().back().command_id==attack.id);
    CHECK(w.world().events().back().result==Outcome::Applied);
    const auto records=w.world().status_effects().records();
    CHECK(records.size()==1);
    const auto& first=records.front();
    CHECK(first.id==(attack.id | (std::uint64_t{1}<<63)));
    CHECK(first.source_ship==attacker && first.target_ship==victim);
    CHECK(first.resistance==0 && first.expires_tick-first.started_tick==8);
    CHECK(first.targets.size()==1 && first.targets.front().hardware==1);
    CHECK(w.world().find(attacker)->last_hack_tick==w.world().tick());
    CHECK(!w.world().hardpoint_effective_on(victim,0));
    CHECK(w.world().total_material_kg()==starting_mass);
    CHECK(!w.submit(attack));
    // Real native scanner executor checks status on the command tick.
    CHECK(run(w,order(w,next_id++,8,victim,Action::SurveyLocal))==
          Outcome::ModuleOffline);
    CHECK(w.world().discoveries().empty());
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,victim))==
          Outcome::CooldownActive);
    // Manual control isolation immediately overrides the compromised
    // module path, without deleting historical attack evidence.
    CHECK(run(w,order(w,next_id++,8,victim,Action::SetCyberIsolation,1))==
          Outcome::Applied);
    CHECK(w.world().find(victim)->hardpoint_isolated[0]);
    CHECK(w.world().hardpoint_effective_on(victim,0));
    CHECK(run(w,order(w,next_id++,8,victim,Action::SurveyLocal))==Outcome::Applied);
    // Save and resume while real attack evidence is present.
    recovered_state=w.world().canonical_state();
    PersistentWorld::recover_copy(db,copy);
  }
  {
    PersistentWorld w(db,Simulation(9));
    CHECK(w.world().canonical_state()==recovered_state);
    CHECK(w.world().find(victim)->hardpoint_isolated[0]);
    CHECK(w.world().hardpoint_effective_on(victim,0));
    const auto first=w.world().status_effects().records().front();
    while(w.world().tick()<first.expires_tick)w.step();
    CHECK(w.world().hardpoint_effective_on(victim,0));
    // Isolated target defeats an attempted new intrusion. This attempt
    // still uses the physical emitter and incurs a cooldown.
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,victim))==
          Outcome::AttackResisted);
    CHECK(w.world().status_effects().records().size()==1);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,victim))==
          Outcome::CooldownActive);
    CHECK(run(w,order(w,next_id++,8,victim,Action::SetCyberIsolation,0))==
          Outcome::Applied);
    CHECK(run(w,order(w,next_id++,8,victim,Action::ConfigureCyberModule,4))==
          Outcome::Applied);
    CHECK(w.world().find(victim)->fitting.cyber_firewall());
    CHECK(w.world().colony_ship(victim)->modules==0);
    CHECK(w.world().total_material_kg()==starting_mass);
    while(w.world().tick()<w.world().find(attacker)->last_hack_tick+
                               Simulation::kHackCooldownTicks)w.step();
    CHECK(run(w,order(w,next_id++,7,attacker,Action::HackConfuse,0,victim))==
          Outcome::Applied);
    const auto effects=w.world().status_effects().records();
    CHECK(effects.size()==2);
    const auto& second=effects.back();
    CHECK(second.resistance==60);
    CHECK(second.expires_tick-second.started_tick==4); // defensive firewall
    CHECK(second.id & (std::uint64_t{1}<<63));
    const auto restored=Simulation::from_snapshot(w.world().canonical_state());
    CHECK(restored.canonical_state()==w.world().canonical_state());
    CHECK(restored.status_effects().records().size()==2);
    // The genuine physical target cannot disappear through status restore.
    CHECK(restored.find(victim)->fitting.cyber_firewall());
    CHECK(restored.find(attacker)->fitting.intrusion_emitter());
    CHECK(w.world().total_material_kg()==starting_mass);
    // Current effective state MUST follow actual tick not just UI intent.
    const auto active=w.world().hardpoint_effective_on(victim,0);
    CHECK(active==restored.hardpoint_effective_on(victim,0));
    // Active refit cannot launder a compromised scanner.
    CHECK(run(w,order(w,next_id++,8,victim,Action::ConfigureExplorationScanner,-1))==
          Outcome::InTransition);
    while(w.world().tick()<second.expires_tick)w.step();
    CHECK(w.world().hardpoint_effective_on(victim,0));
    // After expiry real equipment may legitimately be removed. Historical
    // effects remain auditable without making restart impossible.
    CHECK(run(w,order(w,next_id++,8,victim,Action::ConfigureExplorationScanner,-1))==
          Outcome::Applied);
    CHECK(!w.world().find(victim)->fitting.exploration_scanner());
    // Genuine hardware can be removed/installed without creating material
    // or clearing the original emission cooldown.
    const auto last_emission=w.world().find(attacker)->last_hack_tick;
    CHECK(run(w,order(w,next_id++,7,attacker,Action::ConfigureCyberModule,-3))==
          Outcome::Applied);
    CHECK(!w.world().find(attacker)->fitting.intrusion_emitter());
    CHECK(w.world().colony_ship(attacker)->modules==1);
    CHECK(w.world().find(attacker)->last_hack_tick==last_emission);
    CHECK(run(w,order(w,next_id++,7,attacker,Action::ConfigureCyberModule,3))==
          Outcome::Applied);
    CHECK(w.world().find(attacker)->fitting.intrusion_emitter());
    CHECK(w.world().colony_ship(attacker)->modules==0);
    CHECK(w.world().find(attacker)->last_hack_tick==last_emission);
    CHECK(run(w,order(w,next_id++,8,victim,Action::ConfigureCyberModule,-4))==
          Outcome::Applied);
    CHECK(!w.world().find(victim)->fitting.cyber_firewall());
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==
          w.world().canonical_state());
    CHECK(w.world().total_material_kg()==starting_mass);
  }
  {
    PersistentWorld w(copy,Simulation(99));
    CHECK(w.world().canonical_state()==recovered_state);
    CHECK(w.world().hardpoint_effective_on(victim,0));
    // Invalid high-bit receipt ancestry cannot be fabricated.
    auto saved=w.world().canonical_state();
    auto row=saved.find("shipstatus|");
    CHECK(row!=std::string::npos);
    const auto lineend=saved.find('\n',row);
    CHECK(lineend!=std::string::npos);
    auto record=saved.substr(row,lineend-row);
    const auto sep=record.find('|');
    const auto sep2=record.find('|',sep+1);
    CHECK(sep2!=std::string::npos);
    record.replace(sep+1,sep2-sep-1,std::to_string((std::uint64_t{1}<<63)|99991));
    saved.replace(row,lineend-row,record);
    bool refused=false;
    try{(void)Simulation::from_snapshot(saved);}
    catch(const std::exception&){refused=true;}
    CHECK(refused);
  }
  fs::remove_all(dir);
  std::cout<<"PASS SHIP-STATUS-03: physically fabricated attack, denial, cooldown, isolation, firewall, crash recovery, v101 ancestry and mass\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL SHIP-STATUS-03 "<<e.what()<<'\n';
  std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
