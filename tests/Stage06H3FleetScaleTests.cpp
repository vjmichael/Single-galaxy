// S6-H3. One REAL original SQLite game world. 64 genuine ships across four
// physical cells, each tick preflighted through real bounded native OS worker
// processes. Global writer remains the original PersistentWorld journal;
// no second fleet ledger, fake NPC state, hidden gate jump or installed GPU.
#include "galaxy/SeamlessCellSupervisor.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(expr) do {if(!(expr)) throw std::runtime_error(\
    "S6-H3 line "+std::to_string(__LINE__)+": " #expr);}while(false)
template<typename F>void denied(F&& f){
    bool no=false;try{f();}catch(const std::exception&){no=true;}CHECK(no);
}
struct InventoryIdentity {
    PlayerId owner=0;
    std::uint32_t system=0;
    std::int64_t cargo=0;
    spatial::Cell cell{};
};
static spatial::WorkerLease lease_for(const std::string& db,
    spatial::Cell cell,const std::string& worker){
    regional_events::CheckpointStore checkpoint(db,spatial::scope(cell));
    regional_events::Scheduler scheduler;
    scheduler.add_colony("fleet-source-"+worker,cold_colony::State{});
    CHECK(checkpoint.save(scheduler,0)==1);
    regional_events::RegionalModeAuthority mode(checkpoint);
    const auto rev=mode.enroll();
    return spatial::transfer_cell_worker(db,cell,worker,0,rev.generation,
        rev.checkpoint_revision,"fleet-original-lease-"+worker,true);
}
static std::set<std::uint64_t> people(const Simulation& game){
    std::set<std::uint64_t> ids;
    for(const auto& it:game.npc_records())ids.insert(it.first);
    return ids;
}
static void fleet(const fs::path& root,const std::string& worker,int ticks){
    const auto db=(root/"fleet-original.sqlite").string();
    Simulation genesis(7703001);
    constexpr std::size_t kShips=64,kCells=4;
    std::map<EntityId,InventoryIdentity> original;
    std::vector<EntityId> ships;
    for(std::size_t cell=0;cell<kCells;++cell){
        for(std::size_t i=0;i<kShips/kCells;++i){
            const auto x=static_cast<std::int64_t>(cell)*200000000+
                         static_cast<std::int64_t>(i)*3000;
            const auto id=genesis.add_colony_ship(
                static_cast<PlayerId>(100+cell*16+i),{x,0},100,8);
            genesis.provision_colony_genesis(id,100000,100000,100000,100000,100000);
            ships.push_back(id);
        }
        genesis.add_asteroid({static_cast<std::int64_t>(cell)*200000000+90000,0},500);
    }
    genesis.enable_individual_workforce();
    PersistentWorld w(db,std::move(genesis));
    w.enable_native_3d_physics();
    CHECK(galactic::register_original_system(db,w,1,
        galactic::Point{{12,1234},{34,5678},{56,9012}},
        "s6-h3-original-fleet-star",true));
    w.enable_native_galactic_physics(db);
    w.enable_native_3d_probes();
    const auto first_generation=w.generation();
    const auto ore=w.world().total_ore();
    const auto entity_count=w.world().entities().size();
    const auto names=people(w.world());
    CHECK(names.size()>=kShips); // actual physical workforce, not counts only
    std::vector<spatial::WorkerLease> leases;
    for(std::size_t i=0;i<kCells;++i){
        const auto c=spatial::ship_cell(w.world(),ships[i*16]);
        CHECK(c && std::none_of(leases.begin(),leases.end(),
            [&](const auto& l){return l.cell==*c;}));
        leases.push_back(lease_for(db,*c,"fleet-worker-"+std::to_string(i)));
    }
    for(std::size_t i=0;i<ships.size();++i){
        const auto* ship=w.world().find(ships[i]);
        CHECK(ship && ship->type==EntityType::Ship);
        const auto c=spatial::ship_cell(w.world(),ships[i]);
        CHECK(c && *c==leases[i/16].cell);
        original[ships[i]]={ship->owner_id,ship->system_id,ship->cargo,*c};
    }
    denied([&]{spatial::NativeMultiCellSupervisor invalid(db,w,4,0);});
    denied([&]{spatial::NativeMultiCellSupervisor invalid(db,w,4,5);});
    std::unique_ptr<spatial::NativeMultiCellSupervisor> host;
    auto renew=[&]{
        host=std::make_unique<spatial::NativeMultiCellSupervisor>(db,w,4,2);
        host->use_native_process_workers(worker);
        for(const auto& l:leases)host->register_worker(l);
        CHECK(host->worker_count()==4);
    };
    renew();
    denied([&]{
        spatial::WorkerLease fake=leases[0];fake.cell.x+=1;
        host->register_worker(fake);
    });
    std::map<std::string,std::size_t> selections;
    std::size_t restart=0,rollback=0,copy_count=0;
    std::vector<double> latency;
    latency.reserve(static_cast<std::size_t>(ticks));
    for(int i=0;i<ticks;++i){
        if(i && i%32==0){host.reset();renew();++restart;}
        const auto before_generation=w.generation(),before_tick=w.world().tick();
        const auto previous=w.world().canonical_state();
        const std::string id="fleet-dispatch-"+std::to_string(i);
        const auto start=std::chrono::steady_clock::now();
        spatial::NativeMultiCellSupervisor::Committed committed;
        if(i%37==17){
            denied([&]{(void)host->run_next_tick(id,FailPoint::AfterSnapshotWrite);});
            CHECK(w.generation()==before_generation && w.world().tick()==before_tick &&
                  w.world().canonical_state()==previous);
            host.reset();renew();++restart;
            const auto resumed=host->resume_pending_tick();
            CHECK(resumed && resumed->dispatch_id==id &&
                  resumed->world_generation==before_generation+1 &&
                  !host->resume_pending_tick());
            committed=*resumed;++rollback;
        }else{
            committed=host->run_next_tick(id);
            const auto& stats=host->last_schedule();
            CHECK(stats.world_generation==before_generation &&
                  stats.committed_tick==w.world().tick() &&
                  stats.cells_examined==kCells &&
                  stats.eligible_cells==kCells &&
                  stats.failed_cells==0 && stats.preflight_waves==2 &&
                  stats.maximum_parallel_preflights==2 && stats.elapsed_ms>=0);
        }
        latency.push_back(std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count());
        ++selections[committed.scope];
        CHECK(w.generation()==before_generation+1 &&
              w.world().tick()==before_tick+1 &&
              committed.world_generation==w.generation());
        CHECK(w.world().total_ore()==ore &&
              w.world().entities().size()==entity_count &&
              people(w.world())==names);
        for(const auto& [ship_id,identity]:original){
            const auto* ship=w.world().find(ship_id);
            CHECK(ship && ship->type==EntityType::Ship &&
                  ship->owner_id==identity.owner &&
                  ship->system_id==identity.system &&
                  ship->cargo==identity.cargo &&
                  spatial::ship_cell(w.world(),ship_id)==identity.cell);
        }
        if((i+1)%64==0){
            const auto copy=(root/("fleet-recovery-"+std::to_string(i)+".sqlite")).string();
            PersistentWorld::recover_copy(db,copy);++copy_count;
            PersistentWorld mirror(copy,Simulation(1));
            CHECK(mirror.generation()==w.generation() &&
                  mirror.world().canonical_state()==w.world().canonical_state() &&
                  people(mirror.world())==names &&
                  mirror.world().total_ore()==ore);
            for(const auto& l:leases)CHECK(spatial::worker_may_simulate_cell(
                copy,l.cell,l.worker,l.epoch,l.region_generation,
                l.checkpoint_revision));
        }
    }
    CHECK(selections.size()==4 && copy_count==static_cast<std::size_t>(ticks/64));
    for(const auto& selection:selections)CHECK(selection.second>0);
    CHECK(host->rejected_admissions()==0 && first_generation+ticks==w.generation());
    // A committed claim ID must not grant another tick on another generation.
    const auto sealed_state=w.world().canonical_state();
    const auto sealed_generation=w.generation();
    denied([&]{(void)host->run_next_tick(
        "fleet-dispatch-"+std::to_string(ticks-1));});
    CHECK(w.generation()==sealed_generation &&
          w.world().canonical_state()==sealed_state);
    std::sort(latency.begin(),latency.end());
    const auto ratio=[&](std::size_t pct){return latency[latency.size()*pct/100];};
    std::cout<<"S6H3_FLEET|ships="<<ships.size()<<"|people="<<names.size()
             <<"|cells="<<leases.size()<<"|ticks="<<ticks
             <<"|real_os_preflights="<<4*ticks+static_cast<int>(rollback)+4
             <<"|parallel_budget=2|waves_per_tick=2"
             <<"|p50_ms="<<ratio(50)<<"|p95_ms="<<ratio(95)
             <<"|max_ms="<<latency.back()
             <<"|supervisor_restarts="<<restart<<"|rollback_replays="<<rollback
             <<"|copy_recoveries="<<copy_count
             <<"|generation="<<w.generation()
             <<"|stale_dispatch_rejected=1|RESULT=PASS\n";
}
static void competing_dispatch(const fs::path& root,
    const std::string& worker,const std::string& hanging_worker){
    const auto db=(root/"busy-original.sqlite").string();
    Simulation initial(7703002);
    const auto ship=initial.add_colony_ship(7,{0,0},100,8);
    initial.provision_colony_genesis(ship,10000,10000,10000,10000,10000);
    PersistentWorld w(db,std::move(initial));
    w.enable_native_3d_physics();
    const auto cell=spatial::ship_cell(w.world(),ship);
    CHECK(cell);
    const auto lease=lease_for(db,*cell,"busy-original-worker");
    spatial::NativeMultiCellSupervisor supervisor(db,w,1,1);
    supervisor.register_worker(lease);
    supervisor.use_native_process_workers(hanging_worker);
    const auto generation=w.generation();
    std::atomic<bool> first_denied{false};
    std::thread original([&]{
        try{(void)supervisor.run_next_tick("blocked-original-preflight");}
        catch(const std::exception&){first_denied.store(true);}
    });
    try{
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!supervisor.busy() && std::chrono::steady_clock::now()<until)
            std::this_thread::yield();
        CHECK(supervisor.busy());
        const auto started=std::chrono::steady_clock::now();
        denied([&]{(void)supervisor.run_next_tick("forged-concurrent-tick");});
        denied([&]{supervisor.use_native_process_workers(worker);});
        denied([&]{supervisor.register_worker(lease);});
        CHECK(std::chrono::steady_clock::now()-started<
              std::chrono::milliseconds(400));
        CHECK(supervisor.rejected_admissions()==3);
    }catch(...){original.join();throw;}
    original.join();
    CHECK(first_denied.load() && !supervisor.busy() &&
          w.generation()==generation &&
          supervisor.last_failed_cells().size()==1);
    supervisor.use_native_process_workers(worker);
    const auto committed=supervisor.run_next_tick("retry-after-failed-readonly-worker");
    CHECK(committed.world_generation==generation+1 &&
          w.generation()==generation+1);
    std::cout<<"S6H3_OVERLOAD|rejected_concurrent_admissions=3"
             <<"|unmodified_world_before_retry=1"
             <<"|bounded_timeout_recovery=1|RESULT=PASS\n";
}
int main(int argc,char** argv){
    const auto root=fs::temp_directory_path()/
        ("stage06h3-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Clean{fs::path p;~Clean(){std::error_code ec;fs::remove_all(p,ec);}} clean{root};
    try{
        CHECK(argc==3 && argv[1] && argv[2]);
        competing_dispatch(root,argv[1],argv[2]);
        int ticks=256;
        if(const char* selected=std::getenv("SPACE_H3_FLEET_TICKS")){
            CHECK(std::string(selected)=="1024");ticks=1024;
        }
        fleet(root,argv[1],ticks);
        std::cout<<"S6-H3 original one-world fleet scheduling and crash recovery PASS\n";
    }catch(const std::exception& e){
        std::cerr<<"S6-H3 FAIL: "<<e.what()<<'\n';return 1;
    }
}
