#include "galaxy/WO5PhysicalResupply.hpp"
#include "galaxy/WO5IntercolonyBooking.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    "WO5 native route guard line "+std::to_string(__LINE__)+": " #x);}while(0)

int main() {
 try {
    const auto folder=fs::temp_directory_path()/
        ("wo5_guard_"+std::to_string(std::chrono::steady_clock::now().
                                    time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path d;~Cleanup(){
        std::error_code ec;fs::remove_all(d,ec);
    }} cleanup{folder};
    Simulation seed(94105);
    const auto first=seed.add_colony_ship(7,{0,0},100,8);
    seed.provision_colony_genesis(first,1000,3000,1000,1000,1000);
    const auto second=seed.add_colony_ship(7,{0,0},100,8);
    seed.provision_colony_genesis(second,100,100,100,300,400);
    const auto foreign=seed.add_colony_ship(8,{0,0},100,8);
    seed.provision_colony_genesis(foreign,1000,3000,1000,1000,1000);
    seed.enable_individual_workforce();
    const auto mass=seed.total_ore();
    const auto db=(folder/"world.sqlite").string();
    PersistentWorld w(db,std::move(seed));
    // A shortage is real but it does NOT give an imaginary planet, shuttle,
    // ship frame, pilot, cargo, remote route or trusted host approval.
    const auto board=economy04::observe_regional_distribution(
        w.world(),7,economy04::Policy{32,1});
    CHECK(board && board->still_current(w.world()));
    CHECK(!board->shortages.empty());
    CHECK(w.world().total_ore()==mass);
    economy04::CarrierRequest fake{};
    fake.source.source_ship=first;
    fake.source.physical_location=first;
    fake.source.kind=economy04::CompartmentKind::ColonyShip;
    fake.destination.kind=economy04::CompartmentKind::PlanetarySurface;
    fake.destination.source_ship=first;
    fake.destination.physical_location=99999;
    fake.destination.planetary_key=PlanetarySiteKey{100,3,4,1};
    fake.vital=economy04::Vital::Food;
    fake.priority=economy04::Priority::VitalCurrentWindow;
    fake.proposed_kg=10;
    fake.observed_tick=w.world().tick();
    fake.observed_state_hash=w.world().state_hash();
    CHECK(!logistics05::inspect_planetary_resupply(
        w.world(),7,fake,10));
    CHECK(!logistics05::inspect_planetary_resupply(
        w.world(),8,fake,10));
    CHECK(!logistics05::inspect_planetary_resupply(
        w.world(),7,fake,1001));
    const auto original=w.world().state_hash();
    logistics05::ShuttleResupplyOffer unapproved{};
    unapproved.owner=7;
    unapproved.shortage=fake;
    unapproved.source_ship=first;
    unapproved.site=fake.destination.planetary_key;
    unapproved.cargo=ShuttleCargo::Food;
    unapproved.kilograms=10;
    unapproved.observed_tick=w.world().tick();
    unapproved.observed_state_hash=original;
    CHECK(logistics05::commit_approved_planetary_load(
        w,unapproved,false).result==logistics05::PhysicalResult::Refused);
    CHECK(logistics05::commit_approved_planetary_load(
        w,unapproved,true).result==logistics05::PhysicalResult::Refused);
    CHECK(logistics05::commit_approved_landed_unload(
        w,7,first,fake.destination.planetary_key,ShuttleCargo::Food,
        10,111,true).result==logistics05::PhysicalResult::Refused);
    // Even a plausible 3-ship intercolony request has no real surface,
    // real scanner-equipped freighter, original site pickup or cargo.
    fake.source.kind=economy04::CompartmentKind::PlanetarySurface;
    fake.source.physical_location=99999;
    fake.source.planetary_key=fake.destination.planetary_key;
    fake.source.source_ship=first;
    fake.destination.kind=economy04::CompartmentKind::ColonyShip;
    fake.destination.source_ship=second;
    fake.destination.physical_location=second;
    CHECK(!logistics05::inspect_intercolony_booking(
        w.world(),7,fake,foreign,10001,10));
    CHECK(!logistics05::inspect_intercolony_booking(
        w.world(),7,fake,first,10001,10));
    CHECK(!logistics05::inspect_intercolony_booking(
        w.world(),7,fake,foreign,10001,10,economy04::Policy{32,1}));
    logistics05::IntercolonyBookingOffer fake_booking{};
    fake_booking.owner=7;fake_booking.demand=fake;
    fake_booking.origin_ship=first;fake_booking.carrier_ship=foreign;
    fake_booking.destination_ship=second;
    fake_booking.pickup_site=fake.source.planetary_key;
    fake_booking.cargo=ShuttleCargo::Food;
    fake_booking.kilograms=10;fake_booking.native_local_order_id=10001;
    fake_booking.observed_tick=w.world().tick();
    fake_booking.observed_state_hash=original;
    CHECK(logistics05::book_approved_intercolony_pickup(
        w,fake_booking,true).status==logistics05::BookingState::Refused);
    CHECK(w.world().local_delivery_board().empty());
    CHECK(w.world().intercolony_freight().empty());
    CHECK(w.world().state_hash()==original &&
          w.world().total_ore()==mass);
    const auto copy=(folder/"copy.sqlite").string();
    PersistentWorld::recover_copy(db,copy);
    PersistentWorld restored(copy,Simulation(94106));
    CHECK(restored.world().state_hash()==w.world().state_hash());
    CHECK(restored.world().local_delivery_board().empty());
    CHECK(restored.world().intercolony_freight().empty());
    std::cout<<"WO5 original route/cargo/owner/host fail-closed guards: OK\n";
    return 0;
 }catch(const std::exception& e) {
    std::cerr<<"WO5 guard failure: "<<e.what()<<'\n';
    return 1;
 }
}
