#include "galaxy/RegionalInterestManager.hpp"
#include "galaxy/RegionalCustodyLedger.hpp"
#include "galaxy/RegionalHostCommands.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
using namespace galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("ECON03c6 check failed: ")+ #c+" @"+std::to_string(__LINE__));}while(false)
namespace {
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
Scheduler seed(){Scheduler s;cold_colony::State a,b;a.depot.food_kg=90;a.depot.water_kg=70;a.depot.oxygen_kg=50;s.add_colony("A",a);s.add_colony("B",b);return s;}
void test_interest(){
    RegionalInterestManager m({2000,3000,10});RegionBounds r{"colony-sector",{0,0,0},500};
    CHECK(m.consider(r,{{9000,0,0}},0).desired==RegionMode::Cold);
    auto near=m.consider(r,{{1000,0,0}},0);
    CHECK(near.transition_requested&&near.nearest_surface_m==500);
    // The manager cannot switch authority itself. Host must acknowledge after
    // reconciling events/inventory and committing the handoff.
    CHECK(m.mode(r.id)==RegionMode::Cold);
    m.acknowledge(r.id,RegionMode::Detailed,0);
    CHECK(!m.consider(r,{{3100,0,0}},2).transition_requested); // hysteresis
    CHECK(!m.consider(r,{{5500,0,0}},5).transition_requested); // dwell
    CHECK(m.consider(r,{{5500,0,0}},10).desired==RegionMode::Cold);
    CHECK(m.mode(r.id)==RegionMode::Detailed);
    // Any local human keeps detail loaded even when another player is remote.
    CHECK(m.consider(r,{{9000,0,0},{1500,0,0}},11).desired==RegionMode::Detailed);
    m.acknowledge(r.id,RegionMode::Cold,11);
    CHECK(!m.consider(r,{{1400,0,0}},12).transition_requested); // dwell
    CHECK(m.consider(r,{{1400,0,0}},21).transition_requested);
    CHECK(rejects([&]{m.consider(r,{{NAN,0,0}},21);}));
    CHECK(rejects([&]{m.acknowledge(r.id,RegionMode::Detailed,10);}));
    CHECK(rejects([&]{RegionalInterestManager fail({3000,2000,0});}));
    // Empty active observer set allows unloading only after dwell and host ack.
    CHECK(m.consider(r,{},21).desired==RegionMode::Cold);
    CHECK(std::isinf(m.consider(r,{},21).nearest_surface_m));
}
void test_custody(){
    const auto db=(std::filesystem::temp_directory_path()/("sg_c6_"+
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    {
        CheckpointStore store(db,"region-1");CHECK(store.save(seed(),0)==1);
        CustodyLedger ledger(db,"region-1");
        auto initial=store.load();CHECK(initial);
        ledger.enable(*initial);CHECK(ledger.enabled());
        CHECK(rejects([&]{store.save(seed(),1);})); // bootstrap can't alter receipted region once enabled
        HostCommands old(store,[](const HostCommand&){return true;});
        HostCommand s;s.actor="host-operator";s.command_id="s-1";s.op=HostOp::ScheduleShipment;
        s.origin="A";s.destination="B";s.cargo={20,10,5};s.at_tick=10;s.travel_ticks=20;
        s.carrier_receipt="physical-load-1";
        CHECK(rejects([&]{old.execute(s,1);})); // no bypass after opt-in
        HostCommands host(store,[](const HostCommand& c){return c.actor=="host-operator";},&ledger);
        auto one=host.execute(s,1);CHECK(one.outcome==1&&one.revision==2);
        CHECK(store.load()->scheduler.colony("A").depot.food_kg==70);
        CHECK(store.load()->scheduler.custody().food_kg==20);
        CHECK(ledger.find(1)&&ledger.find(1)->stage==ShipmentStage::Reserved);
        auto replay=host.execute(s,1);CHECK(replay.replayed);
        CHECK(store.load()->revision==2);
        auto bad=s;bad.command_id="s-new";bad.carrier_receipt="physical-load-1";
        CHECK(rejects([&]{host.execute(bad,2);})); // no double reserve of same handoff ID
        CHECK(store.load()->revision==2);
        bad.carrier_receipt="physical-load-2";bad.cargo={10000,0,0};
        CHECK(rejects([&]{host.execute(bad,2);})); // cannot grant fabricated inventory
        CHECK(store.load()->revision==2);

        // Failed SQLite custody insert MUST roll back the scheduler checkpoint,
        // inventory reservation, revision, and receipt in the same transaction.
        sqlite3* faultdb=nullptr;
        CHECK(sqlite3_open(db.c_str(),&faultdb)==SQLITE_OK);
        CHECK(sqlite3_exec(faultdb,
          "CREATE TRIGGER sg_c6_test_reject BEFORE INSERT ON sg_regional_shipment_custody "
          "WHEN NEW.handoff_id='fault-handoff' "
          "BEGIN SELECT RAISE(ABORT,'injected custody failure'); END",nullptr,nullptr,nullptr)==SQLITE_OK);
        HostCommand faulty=s;faulty.command_id="injected-fault";faulty.carrier_receipt="fault-handoff";
        CHECK(rejects([&]{host.execute(faulty,2);}));
        CHECK(store.load()->revision==2);
        CHECK(store.load()->scheduler.colony("A").depot.food_kg==70);
        CHECK(!ledger.find(2));
        CHECK(sqlite3_exec(faultdb,"DROP TRIGGER sg_c6_test_reject",nullptr,nullptr,nullptr)==SQLITE_OK);
        sqlite3_close(faultdb);
        // Check a custody ledger on a DIFFERENT database cannot be swapped in
        // as if it belonged to this checkpoint transaction.
        const auto otherdb=db+".other";
        {CheckpointStore otherstore(otherdb,"region-1");CHECK(otherstore.save(seed(),0)==1);
         CustodyLedger foreign(otherdb,"region-1");foreign.enable(*otherstore.load());
         HostCommands incorrect(store,[](const HostCommand&){return true;},&foreign);
         CHECK(rejects([&]{incorrect.execute(faulty,2);}));
         CHECK(store.load()->revision==2);}
        std::filesystem::remove(otherdb);std::filesystem::remove(otherdb+"-wal");
        std::filesystem::remove(otherdb+"-shm");
        // Scope mismatch is rejected even when both scopes are present in one DB.
        CHECK(rejects([&]{HostCommands wrong(store,[](const HostCommand&){return true;},
          (CustodyLedger*)nullptr); // harmless baseline constructor
          CustodyLedger foreign(db,"region-foreign");
          HostCommands badscope(store,[](const HostCommand&){return true;},&foreign);}));
        HostCommand advance;advance.actor="host-operator";advance.command_id="t-10";
        advance.op=HostOp::Advance;advance.at_tick=10;
        CHECK(host.execute(advance,2).revision==3);
        CHECK(ledger.find(1)->stage==ShipmentStage::InTransit);
        CHECK(store.load()->scheduler.shipment(1).stage==ShipmentStage::InTransit);
        CHECK(store.load()->scheduler.colony("B").depot.food_kg==0);
        {
            CheckpointStore restart(db,"region-1");CustodyLedger recon(db,"region-1");
            HostCommands after(restart,[](const HostCommand&){return true;},&recon);
            CHECK(after.execute(advance,2).replayed);
            CHECK(recon.find(1)->stage==ShipmentStage::InTransit);
            HostCommand arrive=advance;arrive.command_id="t-30";arrive.at_tick=30;
            sqlite3* fault2=nullptr;CHECK(sqlite3_open(db.c_str(),&fault2)==SQLITE_OK);
            CHECK(sqlite3_exec(fault2,
              "CREATE TRIGGER sg_c6_test_stage_reject BEFORE UPDATE ON sg_regional_shipment_custody "
              "WHEN NEW.stage=2 BEGIN SELECT RAISE(ABORT,'injected arrival failure'); END",
              nullptr,nullptr,nullptr)==SQLITE_OK);
            CHECK(rejects([&]{after.execute(arrive,3);}));
            CHECK(restart.load()->revision==3);
            CHECK(restart.load()->scheduler.colony("B").depot.food_kg==0);
            CHECK(recon.find(1)->stage==ShipmentStage::InTransit);
            CHECK(sqlite3_exec(fault2,"DROP TRIGGER sg_c6_test_stage_reject",nullptr,nullptr,nullptr)==SQLITE_OK);
            sqlite3_close(fault2);
            CHECK(after.execute(arrive,3).revision==4);
            CHECK(recon.find(1)->stage==ShipmentStage::Delivered);
            CHECK(restart.load()->scheduler.colony("B").depot.food_kg==20);
            CHECK(restart.load()->scheduler.custody().food_kg==0);
            CHECK(after.execute(arrive,3).replayed);
        }
        CHECK(ledger.find(1)->stage==ShipmentStage::Delivered);
        CHECK(store.load()->revision==4);

        // A legacy untracked in-flight shipment cannot be retroactively
        // assigned physical custody just by turning the ledger on.
        {
            CheckpointStore legacy(db,"region-legacy");CHECK(legacy.save(seed(),0)==1);
            HostCommands oldhost(legacy,[](const HostCommand&){return true;});
            HostCommand legacy_ship=s;legacy_ship.command_id="legacy-ship";
            CHECK(oldhost.execute(legacy_ship,1).revision==2);
            CustodyLedger pending(db,"region-legacy");
            CHECK(rejects([&]{pending.enable(*legacy.load());}));
            CHECK(!pending.enabled());
        }
        // Refuse stale token and cross-scope custody use.
        CHECK(rejects([&]{host.execute(bad,3);}));
        {
            CheckpointStore other(db,"region-2");CHECK(other.save(seed(),0)==1);
            CustodyLedger otherledger(db,"region-2");otherledger.enable(*other.load());
            HostCommands oh(other,[](const HostCommand&){return true;},&otherledger);
            CHECK(oh.execute(s,1).revision==2);
            CHECK(otherledger.find(1).has_value());
            CHECK(store.load()->revision==4);
        }
    }
    std::filesystem::remove(db);std::filesystem::remove(db+"-wal");std::filesystem::remove(db+"-shm");
}
}
int main(){
 try{test_interest();test_custody();std::cout<<"PASS ECON03c6 hysteresis, multi-observer interest, atomic carrier custody, idempotency, restart, scope isolation\n";return 0;}
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
