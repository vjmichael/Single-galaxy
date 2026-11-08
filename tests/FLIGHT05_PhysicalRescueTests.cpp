#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include "galaxy/ShipEmergencyRendezvous.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace galaxy;
#define CHECK(v) do{if(!(v))throw std::runtime_error(std::string("AUTO04D3E: ")+#v+" line "+std::to_string(__LINE__));}while(false)
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
      ("sg_flight05_rescue_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    std::int64_t mass=0;EntityId source=0,origin=0,receiver=0,understocked=0;
    std::set<std::uint64_t> original_npc_ids;
    {
        Simulation initial(700433);
        source=initial.add_colony_ship(7,{0,0},100,4);
        origin=initial.add_colony_ship(8,{0,0},100,4);
        receiver=initial.add_colony_ship(9,{0,0},100,4);
        understocked=initial.add_colony_ship(10,{0,0},100,16);
        CHECK(source!=origin && origin!=receiver);
        initial.provision_colony_genesis(source,100,100,100,200,500);
        initial.provision_colony_genesis(origin,1000,1000,1000,1000,1000);
        initial.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        initial.provision_colony_genesis(understocked,100,100,100,200,500);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        initial.enable_passenger_life_support();
        CHECK(initial.npc_records().size()==28);
        for(const auto& item:initial.npc_records())
            original_npc_ids.insert(item.first);
        CHECK(original_npc_ids.size()==28);
        mass=initial.total_material_kg();
        PersistentWorld w(path,std::move(initial));
        Command board{};
        board.id=1001;board.transport_id=1001;board.sequence=1;
        board.issuer=Simulation::kServerAuthority;
        board.execute_tick=w.world().tick()+1;
        board.action=Action::HostEmbarkPassengers;
        board.actor=source;board.target=origin;
        board.passenger_ids={5,6,7,8};
        CHECK(w.submit_host_passenger_action(board));
        w.step();
        CHECK(w.world().voyage_status(1001)==1);
        CHECK(w.world().passengers_aboard(source)==4);
        CHECK(w.world().passengers_aboard(receiver)==0);
        Command rescue{};
        rescue.id=1002;rescue.sequence=2;rescue.issuer=Simulation::kServerAuthority;
        rescue.execute_tick=w.world().tick()+1;
        rescue.action=Action::HostEvacuatePassengers;
        rescue.actor=source;rescue.target=receiver;
        rescue.transport_id=1001;rescue.amount=1003;
        rescue.passenger_ids={5,6,7,8};
        // Healthy source ferry does not justify autonomous emergency authority.
        CHECK(!w.submit_host_emergency_ferry_rescue(rescue));
        w.steps(220-w.world().tick());
        CHECK(colony_safety::assess(w.world(),source,source).risk==colony_safety::Risk::Critical);
        rescue.execute_tick=w.world().tick()+1;
        auto starved_receiver=rescue;starved_receiver.target=understocked;
        CHECK(!w.submit_host_emergency_ferry_rescue(starved_receiver));
        auto incomplete=rescue;incomplete.passenger_ids={5,6,7};
        CHECK(!w.submit_host_emergency_ferry_rescue(incomplete));
        auto forged=rescue;forged.issuer=7;
        CHECK(!w.submit_host_emergency_ferry_rescue(forged));
        CHECK(!ship_interaction::submit_emergency_rescue_after_rendezvous(
            w,9,97051,rescue));
        const auto before_flight=w.world().canonical_state();
        bool posting_failed=false;
        try{(void)w.queue_npc_ship_rendezvous(
            9,receiver,0,source,97051,NpcFlightPurpose::Rescue,
            FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){posting_failed=true;}
        CHECK(posting_failed && w.world().canonical_state()==before_flight);
        CHECK(!w.queue_npc_ship_rendezvous(
            7,receiver,0,source,97051,NpcFlightPurpose::Rescue));
        CHECK(w.queue_npc_ship_rendezvous(
            9,receiver,0,source,97051,NpcFlightPurpose::Rescue));
        bool rendezvoused=false;
        for(int i=0;i<300;i++){
            w.step();
            if(w.world().npc_flight_orders().at(97051).phase==
               NpcFlightPhase::Arrived){rendezvoused=true;break;}
        }
        CHECK(rendezvoused);
        CHECK(ship_interaction::rendezvous_preflight(
            w.world(),9,receiver,source,97051)==
            ship_interaction::Readiness::Ready);
        CHECK(w.world().voyage_status(1001)==1);
        CHECK(w.world().passengers_aboard(source)==4);
        rescue.execute_tick=w.world().tick()+1;
        CHECK(!ship_interaction::submit_emergency_rescue_after_rendezvous(
            w,8,97051,rescue));
        auto bad_roster=rescue;bad_roster.passenger_ids={5,6,7};
        CHECK(!ship_interaction::submit_emergency_rescue_after_rendezvous(
            w,9,97051,bad_roster));
        const auto before_submit=w.world().canonical_state();
        bool rescue_fault=false;
        try{(void)ship_interaction::submit_emergency_rescue_after_rendezvous(
            w,9,97051,rescue,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){rescue_fault=true;}
        CHECK(rescue_fault && w.world().canonical_state()==before_submit);
        CHECK(ship_interaction::submit_emergency_rescue_after_rendezvous(
            w,9,97051,rescue));
        CHECK(!ship_interaction::submit_emergency_rescue_after_rendezvous(w,9,97051,rescue)); // no double reservation
        // Admission is NOT rescue; source still physically owns passengers.
        CHECK(w.world().voyage_status(1001)==1);
        CHECK(w.world().passengers_aboard(source)==4);
        w.step(); // actual native ferry rescue (physical custody)
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().voyage_status(1001)==5);
        CHECK(w.world().voyage_status(1003)==1);
        CHECK(w.world().passengers_aboard(source)==0);
        CHECK(w.world().passengers_aboard(receiver)==4);
        CHECK(w.world().npc_records().size()==28);
        // An unchanged count cannot conceal a replaced or cloned passenger.
        std::set<std::uint64_t> actual_npc_ids;
        for(const auto& item:w.world().npc_records())
            actual_npc_ids.insert(item.first);
        CHECK(actual_npc_ids==original_npc_ids);
        CHECK(w.world().colony_ship(source)->crew==4); // native ship crew NOT falsely rescued
        CHECK(w.world().total_material_kg()==mass);
        const auto receiver_food=w.world().life_support(receiver)->food_kg;
        const auto source_food=w.world().life_support(source)->food_kg;
        const auto origin_food=w.world().life_support(origin)->food_kg;
        w.steps(20); // allow actual life-support consumption
        CHECK(w.world().life_support(receiver)->food_kg<=receiver_food);
        CHECK(w.world().life_support(source)->food_kg<=source_food);
        CHECK(w.world().life_support(origin)->food_kg<=origin_food);
        // All four original crew are physically away. Home employment is
        // persistent, but absent people do not consume a second supply ration.
        CHECK(w.world().total_material_kg()==mass);
    }
    {
        PersistentWorld recovered(path,Simulation(98));
        CHECK(recovered.world().voyage_status(1001)==5);
        CHECK(recovered.world().voyage_status(1003)==1);
        CHECK(recovered.world().passengers_aboard(source)==0);
        CHECK(recovered.world().passengers_aboard(receiver)==4);
        CHECK(recovered.world().colony_ship(source)->crew==4);
        CHECK(recovered.world().npc_records().size()==28);
        std::set<std::uint64_t> recovered_npc_ids;
        for(const auto& item:recovered.world().npc_records())
            recovered_npc_ids.insert(item.first);
        CHECK(recovered_npc_ids==original_npc_ids);
        CHECK(recovered.world().total_material_kg()==mass);
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path+"-wal");
    std::filesystem::remove(path+"-shm");
    std::cout<<"PASS stocked rescue ferry admitted ONLY for critical source, actual passenger custody transferred and recovered\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
