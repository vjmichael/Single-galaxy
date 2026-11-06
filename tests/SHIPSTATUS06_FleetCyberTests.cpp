#include "galaxy/NpcCyberSupervisor.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" @ "+std::to_string(__LINE__));}while(false)

static Command order(const PersistentWorld& w,std::uint64_t id,
                     EntityId ship,Action action,std::int64_t amount=0,
                     EntityId target=0){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=8;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
    return c;
}
static void apply(PersistentWorld& w,const Command& c){
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    CHECK(w.world().events().back().result==Outcome::Applied);
}
int main(){
 const auto dir=fs::temp_directory_path()/
    ("sg_ship_status06_"+std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto db=(dir/"world.sqlite").string();
 const auto copy=(dir/"copy.sqlite").string();
 try{
    fs::create_directories(dir);
    EntityId player=0,predator=0;
    std::uint64_t original_material=0,flight_id=0;
    std::string pending;
    {
        Simulation genesis(91006);
        predator=genesis.add_colony_ship(8,{0,0},100,6);
        player=genesis.add_colony_ship(7,{4200,0},100,6);
        const auto rock=genesis.add_asteroid({400,0},18);
        genesis.provision_starter_exploration_scanner(player);
        for(const auto ship:{predator,player})
            genesis.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        PersistentWorld world(db,std::move(genesis));
        std::uint64_t pilot=0;
        for(const auto& [id,n]:world.world().npc_records())
            if(n.home_ship==predator && n.location==predator &&
               n.role==NpcRole::Ship &&
               world.world().npc_available_for_duty(id)){
                pilot=id;break;
            }
        CHECK(pilot);
        world.establish_colony_conflict_agenda(
            94001,predator,pilot,colony_conflict::Agenda::Predatory);
        apply(world,order(world,4001,predator,Action::Mine,2,rock));
        apply(world,order(world,4002,predator,Action::RefineOre,2));
        apply(world,order(world,4003,predator,Action::FabricateModule,1));
        apply(world,order(world,4004,predator,Action::ConfigureCyberModule,3));
        CHECK(world.world().tick()==4);
        original_material=world.world().total_material_kg();
        world.step(); // real tick five, still outside the close cyber envelope
        const auto pulse=npc_cyber::supervise(world,7);
        CHECK(pulse.tick==5 && pulse.queued==0 && pulse.approaches==1);
        CHECK(pulse.last_approaching_ship==predator &&
              pulse.last_approach_target==player);
        CHECK(pulse.last_flight>=npc_cyber::kTacticalFlightIdBase &&
              pulse.last_flight<(std::uint64_t{1}<<40));
        flight_id=pulse.last_flight;
        const auto& f=world.world().npc_flight_orders().at(flight_id);
        CHECK(f.mode==NpcFlightMode::LocalRendezvous &&
              f.purpose==NpcFlightPurpose::Patrol &&
              f.owner==8 && f.ship==predator &&
              f.station==player && f.pilot_npc==pilot);
        const auto repeat=npc_cyber::supervise(world,7);
        CHECK(repeat.queued==0 && repeat.approaches==0);
        CHECK(world.world().npc_flight_orders().size()==1);
        CHECK(world.world().status_effects().records().empty());
        CHECK(world.world().total_material_kg()==original_material);
        pending=world.world().canonical_state();
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld world(copy,Simulation(1));
        CHECK(world.world().canonical_state()==pending);
        CHECK(world.world().npc_flight_orders().at(flight_id).phase==
              NpcFlightPhase::Planned);
        CHECK(npc_cyber::supervise(world,7).queued==0);
        bool arrived=false;
        for(int i=0;i<400;i++){
            world.step();
            if(world.world().npc_flight_orders().at(flight_id).phase==
               NpcFlightPhase::Arrived){arrived=true;break;}
        }
        CHECK(arrived);
        const auto& flight=world.world().npc_flight_orders().at(flight_id);
        CHECK(flight.last_local_command!=0);
        bool physical_receipt=false;
        for(const auto& ev:world.world().activity_events())
            if(ev.kind==ActivityKind::NpcRendezvous &&
               ev.actor==predator && ev.target==player &&
               ev.start_command_id==flight.last_local_command &&
               ev.result==Outcome::Applied)physical_receipt=true;
        CHECK(physical_receipt);
        CHECK(world.world().status_effects().records().empty());
        while(world.world().tick()%npc_cyber::kSupervisorCadenceTicks)
            world.step();
        const auto pulse=npc_cyber::supervise(world,7);
        CHECK(pulse.approaches==0 && pulse.queued==1);
        CHECK(pulse.last_attacking_ship==predator &&
              pulse.last_target_ship==player);
        CHECK(world.world().npc_cyber_operators().count(pulse.last_command)==1);
        CHECK(npc_cyber::supervise(world,7).queued==0);
        world.step();
        CHECK(world.world().events().back().command_id==pulse.last_command);
        CHECK(world.world().events().back().result==Outcome::Applied);
        CHECK(world.world().status_effects().records().size()==1);
        CHECK(world.world().find(player)->hull==100);
        CHECK(world.world().total_material_kg()==original_material);
        const auto saved=world.world().canonical_state();
        CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
        PersistentWorld::recover_copy(copy,db);
        PersistentWorld independent(db,Simulation(2));
        CHECK(independent.world().canonical_state()==saved);
        CHECK(independent.world().npc_flight_orders().at(flight_id).phase==
              NpcFlightPhase::Arrived);
        CHECK(independent.world().status_effects().records().size()==1);
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-06: original observed patrol flight, physical rendezvous receipt, governed attack, finite matter and SQLite restart\n";
    return 0;
 }catch(const std::exception& error){
    std::cerr<<"FAIL SHIP-STATUS-06 "<<error.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
