#include "galaxy/Persistence.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static Outcome perform(PersistentWorld& world,PlayerId owner,EntityId ship,
    Action action,std::int64_t amount,std::uint64_t id,EntityId target=0){
    Command command{};
    command.id=id;command.sequence=id;
    command.execute_tick=world.world().tick()+1;
    command.issuer=owner;command.actor=ship;
    command.action=action;command.amount=amount;command.target=target;
    CHECK(world.submit(command));
    world.step();
    CHECK(world.world().events().back().command_id==id);
    return world.world().events().back().result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
   ("sg_status07_sensor_"+std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"game.sqlite").string();
    const auto copy=(dir/"restart.sqlite").string();
    EntityId intruder=0,defender=0;
    std::uint64_t guard=0;
    std::string saved;
    {
        Simulation seed(71717);
        intruder=seed.add_colony_ship(7,{2500,0},100,6);
        defender=seed.add_colony_ship(8,{0,0},100,6);
        const auto asteroid=seed.add_asteroid({2850,0},16);
        for(const auto ship:{intruder,defender})
            seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        seed.enable_individual_workforce();
        seed.upgrade_assignment_snapshot_format();
        seed.enable_passenger_life_support();
        PersistentWorld world(db,std::move(seed));
        for(const auto& [id,npc]:world.world().npc_records())
            if(npc.home_ship==defender && npc.location==defender &&
               npc.role==NpcRole::Ship &&
               world.world().npc_available_for_duty(id)){
                guard=id;break;
            }
        CHECK(guard);
        const auto initial=world.local_colony_watch_contacts(defender,guard);
        CHECK(initial.size()==1 && initial.front().foreign_ship==intruder);
        CHECK(initial.front().sensor_detail==ship_sensors::kOpticalClass);
        CHECK(!initial.front().authenticated_hail);
        CHECK(perform(world,7,intruder,Action::Mine,2,7001,asteroid)==Outcome::Applied);
        CHECK(perform(world,7,intruder,Action::RefineOre,2,7002)==Outcome::Applied);
        CHECK(perform(world,7,intruder,Action::FabricateModule,1,7003)==Outcome::Applied);
        CHECK(perform(world,7,intruder,Action::ConfigureCyberModule,5,7004)==Outcome::Applied);
        CHECK(world.world().find(intruder)->fitting.stealth_baffle());
        CHECK(world.world().canonical_state().rfind("galaxy-kernel-v103|",0)==0);
        CHECK(world.local_colony_watch_contacts(defender,guard).empty());
        // Even with a GUESSED original entity ID, a real emitter cannot
        // attack a stealth target beyond the native sensor envelope.
        CHECK(perform(world,8,defender,Action::Mine,2,8011,asteroid)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::RefineOre,2,8012)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::FabricateModule,1,8013)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::ConfigureCyberModule,3,8014)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::HackConfuse,0,8015,intruder)==
              Outcome::OutOfRange);
        CHECK(world.world().status_effects().records().empty());
        // Counter-ECM is physically constructed from the defender's own
        // mined/refined modules, not a free or omniscient sensor buff.
        CHECK(perform(world,8,defender,Action::Mine,2,8001,asteroid)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::RefineOre,2,8002)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::FabricateModule,1,8003)==Outcome::Applied);
        CHECK(perform(world,8,defender,Action::ConfigureCyberModule,6,8004)==Outcome::Applied);
        const auto strong=world.local_colony_watch_contacts(defender,guard);
        CHECK(strong.size()==1 && strong.front().foreign_ship==intruder);
        CHECK(strong.front().sensor_detail==ship_sensors::kHighResolutionClass);
        CHECK(strong.front().observed_description.find("SUPPORT_")!=std::string::npos);
        CHECK(!strong.front().authenticated_hail);
        // Once strong sensors actually acquire the same stealth vessel,
        // the native emission reaches its executor, but the target has no
        // exposed hardpoints and the original status engine resists.
        CHECK(perform(world,8,defender,Action::HackConfuse,0,8016,intruder)==
              Outcome::AttackResisted);
        CHECK(world.world().status_effects().records().empty());
        CHECK(perform(world,8,defender,Action::ConfigureCyberModule,-6,8005)==Outcome::Applied);
        CHECK(world.local_colony_watch_contacts(defender,guard).empty());
        // A regular powered survey scanner can penetrate some stealth, but
        // only within 3 km; turning the hardpoint OFF loses this detection.
        CHECK(perform(world,8,defender,Action::ConfigureExplorationScanner,1,8006)==Outcome::Applied);
        CHECK(world.local_colony_watch_contacts(defender,guard).size()==1);
        CHECK(perform(world,8,defender,Action::SetHardpointPower,0,8007)==Outcome::Applied);
        CHECK(world.local_colony_watch_contacts(defender,guard).empty());
        CHECK(perform(world,8,defender,Action::SetHardpointPower,1,8008)==Outcome::Applied);
        const auto seen=world.local_colony_watch_contacts(defender,guard);
        CHECK(seen.size()==1 && seen.front().sensor_detail==ship_sensors::kDetailedClass);
        CHECK(seen.front().observed_description.find("HP_")!=std::string::npos);
        CHECK(!seen.front().authenticated_hail);
        saved=world.world().canonical_state();
        CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld world(copy,Simulation(6));
        CHECK(world.world().canonical_state()==saved);
        CHECK(world.world().find(intruder)->fitting.stealth_baffle());
        CHECK(world.world().find(defender)->fitting.exploration_scanner());
        const auto seen=world.local_colony_watch_contacts(defender,guard);
        CHECK(seen.size()==1 && seen.front().sensor_detail==
              ship_sensors::kDetailedClass);
        CHECK(!seen.front().authenticated_hail);
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07: real material-backed stealth/baffle, weak vs powered/strong sensors, anonymous hull observations, v103 snapshot and SQLite restart\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-07 sensors: "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
