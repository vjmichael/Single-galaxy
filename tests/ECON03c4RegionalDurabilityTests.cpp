#include "galaxy/RegionalCheckpointStore.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
using namespace galaxy;
using namespace galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("ECON03c4 check failed: ")+ #c+" @"+std::to_string(__LINE__));}while(false)
namespace {
std::int64_t mass(const ColonyLifeSupport& s){
    return s.food_kg+s.water_kg+s.oxygen_kg+s.organic_waste_kg+s.wastewater_kg+s.co2_kg;
}
std::int64_t accounted(const Scheduler& s){
    const auto& a=s.colony("Alpha");const auto& b=s.colony("Beta");
    std::int64_t sum=mass(a.depot)+mass(a.habitat)+mass(b.depot)+mass(b.habitat);
    const auto c=s.custody();
    sum+=c.food_kg+c.water_kg+c.oxygen_kg;
    return sum;
}
Scheduler setup(){
    Scheduler s;
    cold_colony::State a,b;
    a.population=2;a.depot.food_kg=200;a.depot.water_kg=200;a.depot.oxygen_kg=200;
    b.population=1;
    s.add_colony("Alpha",a);s.add_colony("Beta",b);
    CHECK(s.schedule_shipment("Alpha","Beta",{30,30,30},10,30)==1);
    CHECK(s.schedule_shipment("Alpha","Beta",{12,12,12},50,10)==2);
    s.schedule_habitat_delivery("Alpha",{15,15,15},20);
    s.schedule_population("Beta",3,70);
    return s;
}
}
int main(){
    const auto dbpath=(std::filesystem::temp_directory_path()/
      ("sg_econ03c4_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    try{
        auto baseline=setup();
        auto baseline_material=accounted(baseline);
        auto expected=baseline;
        expected.advance_to(100);
        {
            CheckpointStore store(dbpath,"outer-region");
            CHECK(!store.load().has_value());
            CHECK(store.save(baseline,0)==1);
            bool stale=false;
            try{store.save(baseline,0);}catch(const std::runtime_error&){stale=true;}
            CHECK(stale);
            CHECK(store.load()->revision==1);
            // Independent region keys must not accidentally alias the same world.
            CheckpointStore other(dbpath,"second-region");
            CHECK(!other.load().has_value());
            CHECK(other.save(Scheduler{},0)==1);
            CHECK(other.load()->scheduler.tick()==0);
        }
        {
            CheckpointStore store(dbpath,"outer-region");
            auto loaded=store.load();CHECK(loaded.has_value());
            CHECK(loaded->scheduler.pending_events()==6);
            CHECK(accounted(loaded->scheduler)==baseline_material);
            CHECK(loaded->scheduler.advance_to(19).executed_events==1);
            CHECK(loaded->scheduler.shipment(1).stage==ShipmentStage::InTransit);
            CHECK(loaded->scheduler.colony("Beta").depot.food_kg==0);
            CHECK(store.save(loaded->scheduler,loaded->revision)==2);
        }
        {
            CheckpointStore store(dbpath,"outer-region");
            auto loaded=store.load();CHECK(loaded->revision==2);
            CHECK(loaded->scheduler.shipment(1).stage==ShipmentStage::InTransit);
            CHECK(accounted(loaded->scheduler)==baseline_material);
            loaded->scheduler.advance_to(40);
            CHECK(loaded->scheduler.shipment(1).stage==ShipmentStage::Delivered);
            CHECK(loaded->scheduler.colony("Beta").depot.food_kg==30);
            CHECK(store.save(loaded->scheduler,loaded->revision)==3);
        }
        {
            CheckpointStore store(dbpath,"outer-region");
            auto loaded=store.load();CHECK(loaded->revision==3);
            CHECK(loaded->scheduler.shipment(1).stage==ShipmentStage::Delivered);
            CHECK(loaded->scheduler.colony("Beta").depot.food_kg==30); // no replayed arrival
            loaded->scheduler.advance_to(100);
            CHECK(accounted(loaded->scheduler)==baseline_material);
            CHECK(loaded->scheduler.colony("Beta").depot.food_kg==42);
            CHECK(loaded->scheduler.colony("Alpha").habitat.organic_waste_kg==
                  expected.colony("Alpha").habitat.organic_waste_kg);
            CHECK(loaded->scheduler.colony("Beta").habitat.shortages==
                  expected.colony("Beta").habitat.shortages);
            CHECK(loaded->scheduler.pending_notices().size()==expected.pending_notices().size());
            CHECK(store.save(loaded->scheduler,loaded->revision)==4);
        }
        std::size_t unacked=0;
        {
            CheckpointStore store(dbpath,"outer-region");
            auto state=store.load();CHECK(state->revision==4);
            unacked=state->scheduler.pending_notices().size();
            CHECK(unacked>3);
            // Observe without ack: reopening must still deliver the pending notices.
        }
        {
            CheckpointStore store(dbpath,"outer-region");
            auto state=store.load();
            CHECK(state->scheduler.pending_notices().size()==unacked);
            const auto ack=state->scheduler.take_notices(3);
            CHECK(ack.size()==3);
            const auto last=ack.back().sequence;
            CHECK(last!=0);
            CHECK(store.save(state->scheduler,state->revision)==5);
        }
        {
            CheckpointStore store(dbpath,"outer-region");
            auto state=store.load();
            CHECK(state->scheduler.pending_notices().size()==unacked-3);
            CHECK(state->scheduler.tick()==100);
            CHECK(state->scheduler.advance_to(100).executed_events==0);
            CHECK(state->scheduler.pending_notices().size()==unacked-3);
        }
        // Multiple process handles must not lose updates or reuse stale snapshots.
        {
            CheckpointStore first(dbpath,"outer-region"),second(dbpath,"outer-region");
            auto left=first.load(),right=second.load();
            CHECK(left->revision==right->revision);
            CHECK(first.save(left->scheduler,left->revision)==6);
            bool stale=false;
            try{second.save(right->scheduler,right->revision);}catch(const std::runtime_error&){stale=true;}
            CHECK(stale);
            CHECK(second.load()->revision==6);
        }
        // Full-size populations are stored as a number, not as individual NPC records.
        {
            CheckpointStore store(dbpath,"large-cohort");
            Scheduler huge;
            cold_colony::State s;s.population=1'300'000'000'000ULL;
            huge.add_colony("Trillion",s);
            huge.advance_to(1'000'000'000'000ULL);
            CHECK(store.save(huge,0)==1);
            const auto roundtrip=store.load();
            CHECK(roundtrip->scheduler.colony("Trillion").population==1'300'000'000'000ULL);
            CHECK(roundtrip->scheduler.colony("Trillion").habitat.shortages==50'000'000'000ULL);
            CHECK(roundtrip->scheduler.pending_notices().size()==1);
        }
        // Corrupted rows are rejected rather than replayed as physical resources.
        {
            sqlite3* raw=nullptr;
            CHECK(sqlite3_open(dbpath.c_str(),&raw)==SQLITE_OK);
            CHECK(sqlite3_exec(raw,"UPDATE sg_regional_checkpoint SET payload=x'010203' WHERE scope='large-cohort'",nullptr,nullptr,nullptr)==SQLITE_OK);
            sqlite3_close(raw);
            bool corrupt=false;
            try{CheckpointStore store(dbpath,"large-cohort");store.load();}
            catch(const std::runtime_error&){corrupt=true;}
            CHECK(corrupt);
            CheckpointStore store(dbpath,"outer-region");
            CHECK(store.load()->scheduler.colony("Beta").depot.food_kg==42);
        }
        std::filesystem::remove(dbpath);
        std::filesystem::remove(dbpath+"-wal");
        std::filesystem::remove(dbpath+"-shm");
        std::cout<<"PASS ECON03c4 SQLite atomic snapshots, escrow restart, once-only arrivals, revision CAS, durable notice ack, checksum rejection, trillion-cohort roundtrip\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
