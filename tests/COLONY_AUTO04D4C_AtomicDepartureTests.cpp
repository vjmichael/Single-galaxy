#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4C: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
       ("sg_departure_v58_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    EntityId source=0,receiver=0,underfunded=0,foreign=0;
    std::int64_t starting_material=0;
    std::uint64_t accepted_generation=0;
    {
        Simulation genesis(7204);
        source=genesis.add_colony_ship(7,{0,0},100,4);
        receiver=genesis.add_colony_ship(7,{0,0},100,4);
        underfunded=genesis.add_colony_ship(7,{0,0},100,60);
        foreign=genesis.add_colony_ship(8,{0,0},100,4);
        genesis.provision_colony_genesis(source,100,100,100,200,500);
        genesis.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        genesis.provision_colony_genesis(underfunded,100,100,100,200,500);
        genesis.provision_colony_genesis(foreign,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        starting_material=genesis.total_material_kg();
        CHECK(genesis.npc_records().size()==72);
        PersistentWorld w(path,std::move(genesis));
        Command c{};
        c.id=2001;c.sequence=2001;c.execute_tick=w.world().tick()+1;
        c.issuer=Simulation::kServerAuthority;c.actor=source;c.target=receiver;
        c.action=Action::HostColonyDeparture;c.passenger_ids={1,2,3,4};
        CHECK(!w.submit_host_colony_departure(c)); // healthy source cannot be emptied
        w.steps(480-w.world().tick());
        CHECK(colony_safety::assess(w.world(),source,source).risk==colony_safety::Risk::Critical);
        c.execute_tick=w.world().tick()+1;
        auto bad=c;bad.passenger_ids={1,2,3};
        CHECK(!w.submit_host_colony_departure(bad));
        bad=c;bad.passenger_ids={1,2,3,4,4};
        CHECK(!w.submit_host_colony_departure(bad));
        bad=c;bad.issuer=7;
        CHECK(!w.submit_host_colony_departure(bad));
        bad=c;bad.target=underfunded;
        CHECK(!w.submit_host_colony_departure(bad));
        bad=c;bad.target=foreign;
        CHECK(!w.submit_host_colony_departure(bad)); // receiver belongs to another player
        CHECK(w.world().colony_ship(source)->crew==4);
        CHECK(w.world().colony_ship(receiver)->crew==4);
        CHECK(w.submit_host_colony_departure(c));
        CHECK(!w.submit_host_colony_departure(c)); // exact command replay denied
        accepted_generation=w.generation();
        // A generic step cannot skip the special all-or-nothing custody gate.
        bool blocked=false;
        try{w.step();}catch(const std::runtime_error&){blocked=true;}
        CHECK(blocked);
        CHECK(w.generation()==accepted_generation);
        bool rolledback=false;
        try{w.step_with_colony_departure(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){rolledback=true;}
        CHECK(rolledback);
        CHECK(w.generation()==accepted_generation);
        CHECK(w.world().colony_ship(source)->crew==4);
        CHECK(w.world().colony_departures().empty());
        w.step_with_colony_departure();
        CHECK(w.world().tick()==481);
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().events().back().transferred==0); // material ledger remains independent of person custody
        CHECK(w.world().colony_ship(source)->crew==0);
        CHECK(w.world().colony_ship(receiver)->crew==8);
        CHECK(w.world().people_physically_aboard(source)==0);
        CHECK(w.world().people_physically_aboard(receiver)==8);
        CHECK(w.world().colony_departures().count(source)==1);
        const auto& receipt=w.world().colony_departures().at(source);
        CHECK(receipt.receiver==receiver && receipt.command_id==c.id);
        CHECK(receipt.relocated_npcs==c.passenger_ids);
        CHECK(w.world().find(source)->type==EntityType::Ship); // intact, not fictitious wreck
        for(auto id:c.passenger_ids){
            const auto& npc=w.world().npc_records().at(id);
            CHECK(npc.location==receiver && npc.home_ship==receiver);
            CHECK(npc.assigned_site==receiver && npc.role==NpcRole::Ship);
            CHECK(!npc.transport_id);
        }
        // No player can command the abandoned crewless vessel.
        Command thrust{};
        thrust.id=2202;thrust.sequence=2202;thrust.actor=source;
        thrust.issuer=7;thrust.action=Action::Thrust;thrust.thrust_x=1;
        thrust.execute_tick=w.world().tick()+1;
        CHECK(!w.submit(thrust));
        CHECK(w.world().total_material_kg()==starting_material);
        const auto bytes=w.world().canonical_state();
        CHECK(bytes.rfind("galaxy-kernel-v58|",0)==0);
        CHECK(Simulation::from_snapshot(bytes).canonical_state()==bytes);
        const auto saved_receiver_food=w.world().life_support(receiver)->food_kg;
        w.steps(500-w.world().tick());
        CHECK(w.world().life_support(receiver)->food_kg==saved_receiver_food-8);
        CHECK(w.world().total_material_kg()==starting_material);
    }
    {
        PersistentWorld restart(path,Simulation(77));
        CHECK(restart.world().colony_ship(source)->crew==0);
        CHECK(restart.world().colony_ship(receiver)->crew==8);
        CHECK(restart.world().colony_departures().at(source).relocated_npcs==
              std::vector<std::uint64_t>({1,2,3,4}));
        CHECK(restart.world().npc_records().size()==72);
        CHECK(restart.world().total_material_kg()==starting_material);
    }
    const auto replay_path=path+".replayed";
    PersistentWorld::recover_copy(path,replay_path);
    {
        PersistentWorld replayed(replay_path,Simulation(81));
        CHECK(replayed.world().colony_ship(source)->crew==0);
        CHECK(replayed.world().colony_ship(receiver)->crew==8);
        CHECK(replayed.world().colony_departures().count(source)==1);
        CHECK(replayed.world().total_material_kg()==starting_material);
    }
    for(auto base:{path,replay_path}){
        std::filesystem::remove(base);
        std::filesystem::remove(base+"-wal");
        std::filesystem::remove(base+"-shm");
    }
    std::cout<<"PASS v58 atomic entire adult crew departure, real finite stock, independent replay and rollback\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
