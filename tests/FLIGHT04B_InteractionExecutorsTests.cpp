#include "galaxy/ShipInteractionExecutors.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::ship_interaction;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/("sg_flight04b_"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Simulation seed(2404);
        const auto carrier=seed.add_ship(7,{0,0},100);
        const auto receiver=seed.add_ship(7,{1500,0},100);
        const auto other=seed.add_ship(8,{1800,0},100);
        PersistentWorld world(dir.string(),std::move(seed));
        const auto before=world.world().canonical_state();
        // Physical proximity does NOT prove an approved mission.
        CHECK(rendezvous_preflight(world.world(),7,carrier,receiver,11)==
              Readiness::MissingRendezvous);
        CHECK(!deliver_freight_after_rendezvous(world,7,100,11));
        Command c{};
        c.id=100;c.issuer=Simulation::kServerAuthority;
        c.action=Action::HostEvacuatePassengers;
        c.actor=receiver;c.target=carrier;c.passenger_ids={1};
        CHECK(!submit_passenger_rescue_after_rendezvous(
            world,7,carrier,receiver,11,c));
        DependentRescuePlan rescue{};
        rescue.command_id=100;rescue.original_transport=10;
        rescue.rescue_transport=11;
        rescue.original_ferry=other;rescue.rescue_ferry=carrier;
        rescue.child_ids={1};
        CHECK(!execute_dependent_rescue_after_rendezvous(world,7,11,rescue));
        DependentTravelPlan arrival{};
        arrival.boarding=false;arrival.transport_id=15;
        arrival.ferry=carrier;arrival.destination=receiver;
        arrival.child_ids={1};
        CHECK(!execute_dependent_arrival_after_rendezvous(
            world,7,receiver,11,arrival));
        CHECK(world.world().canonical_state()==before);
        CHECK(world.world().events().empty());
        CHECK(world.world().activity_events().empty());
        fs::remove_all(dir);
        std::cout<<"PASS FLIGHT-04B: no rendezvous cannot debit freight or move named passengers\n";
        return 0;
    }catch(const std::exception& e){
        std::error_code ec;fs::remove_all(dir,ec);
        std::cerr<<"FAIL FLIGHT-04B: "<<e.what()<<'\n';return 1;
    }
}
