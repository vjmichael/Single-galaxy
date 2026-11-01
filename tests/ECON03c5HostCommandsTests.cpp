#include "galaxy/RegionalHostCommands.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("ECON03c5 check failed: ")+ #c+" @"+std::to_string(__LINE__));}while(false)
namespace {
template<class F> bool denied(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
Scheduler seed(){Scheduler s;cold_colony::State a,b;a.depot.food_kg=90;a.depot.water_kg=70;a.depot.oxygen_kg=50;
 s.add_colony("A",a);s.add_colony("B",b);return s;}
}
int main(){
 const auto db=(std::filesystem::temp_directory_path()/("sg_econ03c5_"+
  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 try{
    CheckpointStore store(db,"sector-a");CHECK(store.save(seed(),0)==1);
    // Pretend this adapter is installed strictly inside the trusted host;
    // physical route proof is checked by host policy, not made up by scheduler.
    HostCommands host(store,[](const HostCommand& c){
        return c.actor=="colony-owner" &&
          (c.op!=HostOp::ScheduleShipment || c.carrier_receipt=="host-verified-route-7") &&
          (c.op!=HostOp::ScheduleHabitatDelivery || c.carrier_receipt=="host-verified-worker-8");
    });
    HostCommand ship;ship.command_id="ship-1";ship.actor="colony-owner";
    ship.op=HostOp::ScheduleShipment;ship.origin="A";ship.destination="B";
    ship.cargo={20,10,5};ship.at_tick=10;ship.travel_ticks=20;
    ship.carrier_receipt="host-verified-route-7";
    auto invalid=ship;invalid.actor="intruder";
    CHECK(denied([&]{host.execute(invalid,1);}));CHECK(store.load()->revision==1);
    invalid=ship;invalid.carrier_receipt="unverified-carrier";
    CHECK(denied([&]{host.execute(invalid,1);}));CHECK(store.load()->revision==1);
    const auto r1=host.execute(ship,1);
    CHECK(!r1.replayed && r1.revision==2 && r1.outcome==1);
    CHECK(store.load()->scheduler.colony("A").depot.food_kg==70);
    CHECK(store.load()->scheduler.custody().food_kg==20);
    // Replay after restart and despite deliberately stale expected revision.
    {
      CheckpointStore reloaded(db,"sector-a");HostCommands again(reloaded,[](const HostCommand& c){return c.actor=="colony-owner";});
      const auto retry=again.execute(ship,1);
      CHECK(retry.replayed && retry.outcome==r1.outcome && retry.revision==r1.revision);
      CHECK(reloaded.load()->revision==2);
    }
    invalid=ship;invalid.cargo={21,10,5};
    CHECK(denied([&]{host.execute(invalid,2);})); // command-ID collision
    CHECK(denied([&]{host.execute(ship,0);})); // zero expected revision is not a valid mutation
    CHECK(store.load()->revision==2);
    HostCommand impossible=ship;impossible.command_id="ship-invalid";impossible.cargo={1000,0,0};
    CHECK(denied([&]{host.execute(impossible,2);}));CHECK(store.load()->revision==2);
    HostCommand tick;tick.actor="colony-owner";tick.command_id="advance-30";
    tick.op=HostOp::Advance;tick.at_tick=30;
    CHECK(denied([&]{host.execute(tick,1);})); // CAS stale
    CHECK(host.execute(tick,2).revision==3);
    CHECK(store.load()->scheduler.colony("B").depot.food_kg==20);
    CHECK(store.load()->scheduler.shipment(1).stage==ShipmentStage::Delivered);
    CHECK(store.load()->scheduler.custody().food_kg==0);
    HostCommand ack;ack.actor="colony-owner";ack.command_id="ack-outbox-1";
    ack.op=HostOp::AcknowledgeNotices;ack.expected_notice_sequence=store.load()->scheduler.pending_notices().front().sequence;
    const auto before=store.load()->scheduler.pending_notices().size();CHECK(before>1);
    const auto accepted=host.execute(ack,3);CHECK(accepted.outcome==ack.expected_notice_sequence && accepted.revision==4);
    CHECK(store.load()->scheduler.pending_notices().size()==before-1);
    HostCommand wrong=ack;wrong.command_id="wrong-ack";wrong.expected_notice_sequence+=500;
    CHECK(denied([&]{host.execute(wrong,4);})); // can never ack by count or wrong sequence
    CHECK(host.execute(ack,3).replayed); // retry does not drop second notice
    CHECK(denied([&]{store.save(store.load()->scheduler,4);})); // old save pathway is fenced off
    CHECK(store.load()->scheduler.pending_notices().size()==before-1);
    {
      CheckpointStore other(db,"sector-b");CHECK(other.save(seed(),0)==1);
      HostCommands h(other,[](const HostCommand&){return true;});
      CHECK(h.execute(ship,1).outcome==1); // same key valid in disjoint scope
      CHECK(store.load()->revision==4);
    }
    // Never commit callback side effects on failure: revision and ledger unchanged.
    CHECK(denied([&]{store.commit_command("fail", "colony-owner","exception",4,
                [](Scheduler&)->std::uint64_t{throw std::runtime_error("injected failure");});}));
    CHECK(store.load()->revision==4);
    const auto same=store.commit_command("fail", "colony-owner","exception",4,
                [](Scheduler&)->std::uint64_t{return 42;});
    CHECK(!same.replayed && same.outcome==42 && same.revision==5);
    std::filesystem::remove(db);std::filesystem::remove(db+"-wal");std::filesystem::remove(db+"-shm");
    std::cout<<"PASS ECON03c5 host-policy gate, atomic receipts, retry safety, conservation, rollback, restart, scope isolation\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
