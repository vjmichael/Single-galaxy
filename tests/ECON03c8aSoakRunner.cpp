// ECON-03c8a: opt-in local sustained simulation / persistence soak harness.
// This exercises the actual PersistentWorld + isolated cold scheduler, but it
// does NOT claim reconciled live-to-cold material handoff or a 3D game client.
#include "galaxy/Persistence.hpp"
#include "galaxy/RegionalPersistentWorldBridge.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include <sqlite3.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <algorithm>

using namespace galaxy;
using namespace galaxy::regional_events;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
#define VERIFY(c) do { if(!(c))throw std::runtime_error(std::string("VERIFY FAILED ")+ #c +" at line "+std::to_string(__LINE__)); }while(false)

static std::uint64_t size_or_zero(const fs::path& p){std::error_code ec;return fs::exists(p,ec)?fs::file_size(p,ec):0;}
static std::uint64_t rss_kib(){
    std::ifstream f("/proc/self/status");std::string key;std::uint64_t n=0;
    while(f>>key){if(key=="VmRSS:"){f>>n;return n;}std::string rest;std::getline(f,rest);}
    return 0;
}
static void check_integrity(const std::string& dbPath){
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(dbPath.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)
        throw std::runtime_error("cannot open SQLite integrity check");
    sqlite3_stmt* q=nullptr;
    if(sqlite3_prepare_v2(db,"PRAGMA quick_check",-1,&q,nullptr)!=SQLITE_OK){sqlite3_close(db);throw std::runtime_error("quick_check prepare");}
    const int rc=sqlite3_step(q);
    std::string answer;
    if(rc==SQLITE_ROW){auto* text=sqlite3_column_text(q,0);if(text)answer=reinterpret_cast<const char*>(text);}
    sqlite3_finalize(q);sqlite3_close(db);
    if(answer!="ok")throw std::runtime_error("SQLite quick_check: "+answer);
}

int main(int argc, char** argv){
    try{
        const unsigned duration=argc>1?static_cast<unsigned>(std::stoul(argv[1])):1800;
        const unsigned hz=argc>2?static_cast<unsigned>(std::stoul(argv[2])):5;
        const fs::path directory=argc>3?fs::path(argv[3]):fs::temp_directory_path()/"single_galaxy_soak";
        const bool accelerated = argc>4 && std::string(argv[4])=="--accelerated";
        if(!duration||duration>3600||!hz||hz>20)throw std::invalid_argument("duration/hz outside safe ranges");
        fs::create_directories(directory);
        const auto liveDb=(directory/"real_world.sqlite").string();
        const auto coldDb=(directory/"cold_region.sqlite").string();
        if(fs::exists(liveDb)||fs::exists(coldDb))throw std::runtime_error("refusing to overwrite existing soak DB");
        auto starting=Clock::now();
        Simulation seeded(20261101);
        const auto ship=seeded.add_colony_ship(7,{0,0},100000,6);
        const auto nearRock=seeded.add_asteroid({1000,0},50000);
        for(int i=0;i<128;i++)seeded.add_asteroid({10000+3000*i,10000+2000*(i%17)},800);
        const auto originalOre=seeded.total_ore();
        auto world=std::make_unique<PersistentWorld>(liveDb,std::move(seeded));
        auto bridge=std::make_unique<PersistentWorldBridge>(liveDb,"world-primary",*world);
        VERIFY(bridge->enroll().owner==SimulationOwner::Cold);
        bool fenced=false;try{world->step();}catch(...){fenced=true;}
        VERIFY(fenced);
        VERIFY(bridge->promote("soak-initial-promotion").owner==SimulationOwner::Detailed);

        CheckpointStore cold(coldDb,"cold-sim");
        Scheduler aggregate;
        for(int i=0;i<512;i++){
            cold_colony::State cohort;
            cohort.population=1'300'000'000'000ULL;
            aggregate.add_colony("cohort-"+std::to_string(i),cohort);
        }
        cold_colony::State cargoSource,cargoDest;
        cargoSource.population=2;
        cargoSource.depot.food_kg=100000;cargoSource.depot.water_kg=100000;cargoSource.depot.oxygen_kg=100000;
        cargoDest.population=2;
        aggregate.add_colony("supply",cargoSource);
        aggregate.add_colony("receiving",cargoDest);
        VERIFY(aggregate.schedule_shipment("supply","receiving",{200,200,200},20,60)==1);
        VERIFY(aggregate.schedule_shipment("supply","receiving",{300,300,300},510,60)==2);
        auto coldRev=cold.save(aggregate,0);

        std::uint64_t simTicks=0, commands=0, restarts=0, coldSaves=1;
        std::uint64_t fullChecks=0, expectedCargo=0, highestRss=0;
        double maxTickMs=0;
        std::uint64_t lastEventBoundary=0;
        const auto tickPeriod=std::chrono::nanoseconds(1000000000ULL/hz);
        auto nextTick=Clock::now()+tickPeriod;
        std::cout<<"START duration_s="<<duration<<" tick_hz="<<hz<<" simulated_ticks_target="
                 <<static_cast<std::uint64_t>(duration)*hz
                 <<" mode="<<(accelerated?"accelerated_logical":"wall_clock_realtime")
                 <<" world_ore="<<originalOre<<" cohorts=512 unique_population=665600000000000"
                 <<std::endl;
        for(;;){
            auto now=Clock::now();
            if(accelerated){
                if(simTicks >= static_cast<std::uint64_t>(duration)*hz)break;
            }else{
                if(now-starting>=std::chrono::seconds(duration))break;
                if(now<nextTick){std::this_thread::sleep_until(nextTick);now=Clock::now();}
                if(now-starting>=std::chrono::seconds(duration))break;
                nextTick+=tickPeriod;
                if(nextTick<now)nextTick=now+tickPeriod; // never do runaway catch-up
            }

            if(simTicks%300==0){
                Command cmd{};
                cmd.id=100000+commands;cmd.sequence=cmd.id;
                cmd.execute_tick=world->world().tick()+1;
                cmd.issuer=7;cmd.actor=ship;cmd.target=nearRock;
                cmd.action=Action::Mine;cmd.amount=10;
                VERIFY(world->submit(cmd));
                commands++;expectedCargo+=10;
            }
            auto t0=Clock::now();
            world->step();
            double tickMs=std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
            maxTickMs=std::max(maxTickMs,tickMs);
            simTicks++;
            VERIFY(world->world().total_ore()==originalOre);
            VERIFY(world->world().find(ship)!=nullptr);
            VERIFY(world->world().find(ship)->cargo==static_cast<std::int64_t>(expectedCargo));
            if(simTicks%150==0){
                const auto reparse=Simulation::from_snapshot(world->world().canonical_state());
                VERIFY(reparse.state_hash()==world->world().state_hash());
                fullChecks++;
            }
            if(simTicks%300==0){
                const auto desiredTick=aggregate.tick()+300;
                auto stats=aggregate.advance_to(desiredTick);
                (void)stats;
                coldRev=cold.save(aggregate,coldRev);coldSaves++;
                auto loaded=cold.load();
                VERIFY(loaded && loaded->revision==coldRev);
                VERIFY(loaded->scheduler.tick()==aggregate.tick());
                VERIFY(loaded->scheduler.shipment(1).stage==ShipmentStage::Delivered);
                VERIFY(loaded->scheduler.shipment(2).stage==ShipmentStage::Reserved ||
                       loaded->scheduler.shipment(2).stage==ShipmentStage::InTransit ||
                       loaded->scheduler.shipment(2).stage==ShipmentStage::Delivered);
                check_integrity(coldDb);check_integrity(liveDb);
                highestRss=std::max(highestRss,rss_kib());
                std::cout<<"SAMPLE elapsed_s="<<std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-starting).count()
                         <<" ticks="<<simTicks<<" commands="<<commands
                         <<" cargo="<<expectedCargo<<" state_hash="<<world->world().state_hash()
                         <<" cold_tick="<<aggregate.tick()<<" cold_notices="<<aggregate.pending_notices().size()
                         <<" rss_kib="<<rss_kib()<<" live_db_bytes="<<size_or_zero(liveDb)
                         <<" live_wal_bytes="<<size_or_zero(liveDb+"-wal")
                         <<" cold_db_bytes="<<size_or_zero(coldDb)<<" max_tick_ms="<<maxTickMs
                         <<std::endl;
            }
            if(simTicks%1500==0){
                const auto hash=world->world().state_hash();const auto tick=world->world().tick();
                bridge.reset();world.reset();
                world=std::make_unique<PersistentWorld>(liveDb,Simulation(999));
                VERIFY(world->world().state_hash()==hash && world->world().tick()==tick);
                bridge=std::make_unique<PersistentWorldBridge>(liveDb,"world-primary",*world);
                VERIFY(bridge->enroll().owner==SimulationOwner::Detailed);
                VERIFY(world->world().total_ore()==originalOre);
                restarts++;
                std::cout<<"RESTART tick="<<tick<<" hash="<<hash<<std::endl;
                if(restarts==1){
                    bool failedClosed=false;
                    try {bridge->demote_if_unchanged("must-refuse-changed-world");}
                    catch(...){failedClosed=true;}
                    VERIFY(failedClosed && bridge->state().owner==SimulationOwner::Detailed);
                    std::cout<<"EXPECTED_DEMOTION_REFUSAL real_detailed_world_has_changed=true"<<std::endl;
                }
            }
        }
        VERIFY(world->world().total_ore()==originalOre);
        VERIFY(world->world().find(ship)->cargo==static_cast<std::int64_t>(expectedCargo));
        const auto finalHash=world->world().state_hash();const auto finalTick=world->world().tick();
        bridge.reset();world.reset();
        PersistentWorld reopened(liveDb,Simulation(999));
        VERIFY(reopened.world().state_hash()==finalHash && reopened.world().tick()==finalTick);
        VERIFY(reopened.world().total_ore()==originalOre);
        VERIFY(reopened.world().find(ship)->cargo==static_cast<std::int64_t>(expectedCargo));
        check_integrity(coldDb);check_integrity(liveDb);
        const auto elapsed=std::chrono::duration<double>(Clock::now()-starting).count();
        std::cout<<"PASS mode="<<(accelerated?"accelerated_logical":"wall_clock_realtime")
                 <<" wall_s="<<elapsed<<" ticks="<<simTicks<<" expected_ticks="
                 <<static_cast<std::uint64_t>(duration)*hz<<" mining_commands="<<commands
                 <<" mining_cargo="<<expectedCargo<<" restarts="<<restarts
                 <<" cold_saves="<<coldSaves<<" snapshot_reparses="<<fullChecks
                 <<" max_rss_kib="<<highestRss<<" max_tick_ms="<<maxTickMs
                 <<" world_hash="<<finalHash<<" world_db_bytes="<<size_or_zero(liveDb)
                 <<" cold_db_bytes="<<size_or_zero(coldDb)<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}