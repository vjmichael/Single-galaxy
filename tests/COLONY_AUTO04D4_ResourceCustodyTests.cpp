#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    const auto file=(std::filesystem::temp_directory_path()/
      ("sg_auto04d4_custody_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    EntityId ferry=0,home=0;
    std::int64_t material=0;
    {
        Simulation initial(7044);
        ferry=initial.add_colony_ship(7,{0,0},100,4);
        home=initial.add_colony_ship(8,{0,0},100,2);
        initial.provision_colony_genesis(ferry,100,100,100,200,500);
        initial.provision_colony_genesis(home,100,100,100,200,500);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_passenger_life_support();
        material=initial.total_material_kg();
        PersistentWorld w(file,std::move(initial));
        const auto before_ferry=colony_safety::assess(w.world(),ferry,ferry);
        CHECK(before_ferry.population==4);
        Command boarding{};
        boarding.id=1001;boarding.transport_id=1001;boarding.sequence=1;
        boarding.issuer=Simulation::kServerAuthority;boarding.execute_tick=1;
        boarding.actor=ferry;boarding.target=home;
        boarding.action=Action::HostEmbarkPassengers;
        boarding.passenger_ids={5,6};
        CHECK(w.submit_host_passenger_action(boarding));
        w.step();
        CHECK(w.world().passengers_aboard(ferry)==2);
        CHECK(colony_safety::assess(w.world(),ferry,ferry).population==6);
        CHECK(colony_safety::assess(w.world(),home,home).population==2);
        w.steps(20-w.world().tick());
        CHECK(w.world().life_support(ferry)->food_kg==94);
        CHECK(w.world().life_support(ferry)->water_kg==94);
        CHECK(w.world().life_support(ferry)->oxygen_kg==94);
        CHECK(w.world().life_support(home)->food_kg==98);
        CHECK(w.world().total_material_kg()==material);
        Command disembark{};
        disembark.id=1002;disembark.transport_id=1001;disembark.sequence=2;
        disembark.issuer=Simulation::kServerAuthority;
        disembark.execute_tick=w.world().tick()+1;
        disembark.actor=ferry;disembark.target=home;
        disembark.action=Action::HostDisembarkPassengers;
        disembark.passenger_ids={5,6};
        CHECK(w.submit_host_passenger_action(disembark));
        w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().passengers_aboard(ferry)==0);
        CHECK(colony_safety::assess(w.world(),ferry,ferry).population==4);
        CHECK(colony_safety::assess(w.world(),home,home).population==2);
        // The destination's returned crew remain its original 2 named ship crew;
        // passengers who disembarked are not a second copy of home crew.
        w.steps(40-w.world().tick());
        CHECK(w.world().life_support(ferry)->food_kg==90);
        CHECK(w.world().life_support(home)->food_kg==96);
        CHECK(w.world().total_material_kg()==material);
        const auto generation=w.generation(),tick=w.world().tick();
        bool rolled_back=false;
        try{w.step(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){rolled_back=true;}
        CHECK(rolled_back);
        CHECK(w.generation()==generation && w.world().tick()==tick);
    }
    {
        PersistentWorld w(file,Simulation(9999));
        CHECK(w.world().tick()==40);
        CHECK(w.world().passengers_aboard(ferry)==0);
        CHECK(w.world().life_support(ferry)->food_kg==90);
        CHECK(w.world().life_support(home)->food_kg==96);
        CHECK(w.world().npc_records().size()==6);
        CHECK(w.world().total_material_kg()==material);
    }
    std::filesystem::remove(file);
    std::filesystem::remove(file+"-wal");
    std::filesystem::remove(file+"-shm");
    std::cout<<"PASS AUTO04D4: real passenger life support, custody, disembark and restart\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
