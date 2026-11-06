#include "galaxy/Persistence.hpp"
#include "galaxy/Navigation.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error( \
    std::string("FLIGHT-02 line ")+std::to_string(__LINE__)+": "+#x); }while(false)

static std::string forged_local_arrival(std::string world,std::uint64_t id,
                                         std::uint64_t tick){
    const auto marker="npcflight|"+std::to_string(id)+"|";
    const auto begin=world.find(marker);
    CHECK(begin!=std::string::npos);
    const auto end=world.find('\n',begin);
    CHECK(end!=std::string::npos);
    std::istringstream in(world.substr(begin,end-begin));
    std::vector<std::string> f;
    std::string entry;
    while(std::getline(in,entry,'|'))f.push_back(entry);
    CHECK(f.size()==16);
    CHECK(f[12]=="1"); // the original typed local docking mode
    f[8]="2"; // fake completion, without original native Docked receipt
    f[11]=std::to_string(tick);
    std::string replacement;
    for(const auto& x:f){if(!replacement.empty())replacement+='|';replacement+=x;}
    world.replace(begin,end-begin,replacement);
    return world;
}
static void run(Vector2 dock_target){
    Simulation seed(2402);
    const auto home=seed.add_station({0,0});
    const auto destination=seed.add_station(dock_target);
    const auto ship=seed.add_colony_ship(7,{0,0},120,4);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    CHECK(seed.npc_records().size()==4);
    const auto chosen=seed.npc_records().begin()->first;
    const auto initial_ore=seed.total_ore();
    const auto dir=fs::temp_directory_path()/
        ("single_galaxy_flight02_"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove(dir);
    std::string final_state;
    try{
        {
            PersistentWorld w(dir.string(),std::move(seed));
            CHECK(!w.queue_npc_station_docking(
                8,ship,0,destination,96001));
            CHECK(!w.queue_npc_station_docking(
                7,ship,999999,destination,96001));
            CHECK(!w.queue_npc_station_docking(
                7,ship,0,ship,96001));
            const auto before=w.world().canonical_state();
            bool failed=false;
            try{(void)w.queue_npc_station_docking(
                7,ship,0,destination,96001,NpcFlightPurpose::Freight,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){failed=true;}
            CHECK(failed && w.world().canonical_state()==before);
            CHECK(w.queue_npc_station_docking(
                7,ship,0,destination,96001,NpcFlightPurpose::Freight));
            CHECK(!w.queue_npc_station_docking(
                7,ship,chosen,destination,96002));
            CHECK(w.world().npc_flight_orders().at(96001).pilot_npc==chosen);
            CHECK(w.world().npc_flight_orders().at(96001).mode==
                  NpcFlightMode::LocalDock);
            CHECK(!w.world().npc_available_for_duty(chosen));
            CHECK(!w.cancel_npc_flight(8,96001));
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v96|",0)==0);
            CHECK(Simulation::from_snapshot(w.world().canonical_state())
                  .canonical_state()==w.world().canonical_state());

            Command manual{};
            manual.id=9991;manual.sequence=manual.id;
            manual.execute_tick=w.world().tick()+1;
            manual.issuer=7;manual.actor=ship;
            manual.action=Action::Thrust;manual.thrust_x=1;
            CHECK(!w.submit(manual)); // pilot owns physical control
            manual.action=Action::RequestDock;manual.target=destination;
            CHECK(!w.submit(manual));
            const auto admitted=w.world().canonical_state();
            bool crashed=false;
            try{w.step(FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){crashed=true;}
            CHECK(crashed);
            CHECK(w.world().canonical_state()==admitted);
            w.steps(3);
            CHECK(w.world().npc_flight_orders().at(96001).phase==
                  NpcFlightPhase::InTransit);
            CHECK(w.world().npc_flight_orders().at(96001).local_commands>0);
            CHECK(!w.cancel_npc_flight(7,96001));
            const auto underway=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(underway).canonical_state()==underway);
            bool fake_rejected=false;
            try{(void)Simulation::from_snapshot(
                forged_local_arrival(underway,96001,w.world().tick()));}
            catch(const std::exception&){fake_rejected=true;}
            CHECK(fake_rejected);
            bool arrived=false;
            for(int i=0;i<160;i++){
                w.step();
                if(w.world().npc_flight_orders().at(96001).phase==
                   NpcFlightPhase::Arrived){arrived=true;break;}
            }
            CHECK(arrived);
            const auto* vessel=w.world().find(ship);
            CHECK(vessel->dock_state==DockState::Docked);
            CHECK(vessel->dock_station==destination);
            CHECK(vessel->velocity.x==0 && vessel->velocity.y==0);
            CHECK(w.world().npc_available_for_duty(chosen));
            CHECK(w.world().total_ore()==initial_ore);
            CHECK(w.world().npc_records().size()==4);
            CHECK(w.world().life_support(ship)!=nullptr);
            const auto completed=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(completed).canonical_state()==completed);

            // The SAME generic NPC navigator now undocks and returns home,
            // reusing the original 2-tick dock transition, thrust and brake.
            CHECK(w.queue_npc_station_docking(
                7,ship,chosen,home,96002,NpcFlightPurpose::Rescue));
            bool departed=false,returned=false;
            for(int i=0;i<180;i++){
                w.step();
                const auto* state=w.world().find(ship);
                if(state->dock_state==DockState::InSpace)departed=true;
                if(w.world().npc_flight_orders().at(96002).phase==
                   NpcFlightPhase::Arrived){returned=true;break;}
            }
            CHECK(departed && returned);
            CHECK(w.world().find(ship)->dock_station==home);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(w.world().npc_flight_orders().at(96002).last_local_command!=0);
            CHECK(w.world().total_ore()==initial_ore);
            CHECK(w.world().npc_records().size()==4);
            CHECK(!w.queue_npc_station_docking(
                7,ship,chosen,home,96003)); // already at destination
            final_state=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(final_state).canonical_state()==final_state);
        }
        {
            PersistentWorld reopened(dir.string(),Simulation(1));
            CHECK(reopened.world().canonical_state()==final_state);
            CHECK(reopened.world().npc_flight_orders().at(96001).phase==
                  NpcFlightPhase::Arrived);
            CHECK(reopened.world().npc_flight_orders().at(96002).phase==
                  NpcFlightPhase::Arrived);
            CHECK(reopened.world().find(ship)->dock_station==home);
            CHECK(reopened.world().npc_records().size()==4);
            CHECK(reopened.world().total_ore()==initial_ore);
            // The original human ship controls are released after
            // real arrival. No second simulator owns the vessel.
            Command manual{};
            manual.id=9992;manual.sequence=manual.id;
            manual.execute_tick=reopened.world().tick()+1;
            manual.issuer=7;manual.actor=ship;manual.target=home;
            manual.action=Action::RequestUndock;
            CHECK(reopened.submit(manual));
            reopened.steps(3);
            CHECK(reopened.world().find(ship)->dock_state==DockState::InSpace);
        }
        fs::remove(dir);
    }catch(...){std::error_code ec;fs::remove(dir,ec);throw;}
}
int main(){
    try{
        run({12000,7000});
        run({0,14000});
        run({-10000,-8000});
        std::cout<<"PASS FLIGHT-02: one real NPC flight navigator reuses original "
            "two-axis braking, dock/undock command receipts, native SQLite "
            "commit/rollback, v95 compatibility and v96 restart\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL FLIGHT-02: "<<e.what()<<'\n';return 1;
    }
}
