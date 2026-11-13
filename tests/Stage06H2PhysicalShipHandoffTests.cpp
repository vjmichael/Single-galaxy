// S6-H2. Original native 250mm galactic sector crossing; genuine original
// SQLite ship movement and atomic leased worker handoff, crash/restart, anti-
// replay and no fabricated NPC/ship/ore ownership. No test-only world writer.
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/SeamlessCellSupervisor.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <set>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error("S6-H2 line "+std::to_string(__LINE__)+": "#x);}while(false)
template<class F>static void refused(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
static std::uint64_t count(sqlite3* db,const char* sql){
    sqlite3_stmt* stmt=nullptr;
    CHECK(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(stmt)==SQLITE_ROW);
    auto n=sqlite3_column_int64(stmt,0);sqlite3_finalize(stmt);
    CHECK(n>=0);return static_cast<std::uint64_t>(n);
}
static spatial::WorkerLease enroll(const std::string& db,const spatial::Cell& cell,
    const std::string& worker){
    regional_events::CheckpointStore checkpoint(db,spatial::scope(cell));
    regional_events::Scheduler scheduler;
    scheduler.add_colony("original-h2-"+worker,cold_colony::State{});
    CHECK(checkpoint.save(scheduler,0)==1);
    regional_events::RegionalModeAuthority authority(checkpoint);
    const auto state=authority.enroll();
    return spatial::transfer_cell_worker(db,cell,worker,0,
        state.generation,state.checkpoint_revision,"h2-lease-"+worker,true);
}
static std::pair<std::uint64_t,std::uint64_t> enroll_region(
    const std::string& db,const spatial::Cell& cell){
    regional_events::CheckpointStore checkpoint(db,spatial::scope(cell));
    regional_events::Scheduler scheduler;
    scheduler.add_colony("original-h2-arrival",cold_colony::State{});
    CHECK(checkpoint.save(scheduler,0)==1);
    regional_events::RegionalModeAuthority authority(checkpoint);
    const auto state=authority.enroll();
    return {state.generation,state.checkpoint_revision};
}
static void retained_source_worker(const fs::path& root,const char* executable){
    const auto db=(root/"source-retained.sqlite").string();
    Simulation initial(77116);
    const auto moving=initial.add_colony_ship(7,{0,0},100,8);
    const auto resident=initial.add_colony_ship(8,{-50000,0},100,8);
    initial.provision_colony_genesis(moving,10000,10000,10000,10000,10000);
    initial.provision_colony_genesis(resident,10000,10000,10000,10000,10000);
    PersistentWorld w(db,std::move(initial));
    w.enable_native_3d_physics();
    constexpr auto edge=galactic::kSectorEdgeMm;
    CHECK(galactic::register_original_system(db,w,1,
        galactic::Point{{12,edge-100},{34,1000},{56,1000}},
        "h2-retain-source",true));
    w.enable_native_galactic_physics(db);
    w.enable_native_3d_probes();
    const auto source_cell=spatial::ship_cell(w.world(),moving);
    CHECK(source_cell && spatial::ship_cell(w.world(),resident)==source_cell);
    const auto source=enroll(db,*source_cell,"h2-still-occupied-worker");
    spatial::NativeMultiCellSupervisor host(db,w);
    host.register_worker(source);
    host.use_native_process_workers(executable);
    Command motion{};motion.id=920001;motion.sequence=920001;
    motion.execute_tick=w.world().tick()+1;
    motion.issuer=7;motion.actor=moving;motion.action=Action::Thrust;
    motion.thrust_x=1;
    CHECK(host.submit_original_command(motion,"h2-retained-source-thrust"));
    host.run_next_tick("h2-retained-source-motion");
    const auto destination=spatial::ship_cell(w.world(),moving);
    CHECK(destination && !(*destination==*source_cell));
    CHECK(spatial::ship_cell(w.world(),resident)==source_cell);
    const auto region=enroll_region(db,*destination);
    const auto proof=host.reconcile_ship_handoff(moving,*source_cell,*destination,
        "h2-new-cell-worker",0,region.first,region.second,
        "h2-occupied-source-transfer",true);
    CHECK(!proof.source_retired && host.worker_count()==2);
    CHECK(spatial::worker_may_simulate_cell(db,*source_cell,
          source.worker,source.epoch,source.region_generation,
          source.checkpoint_revision));
    CHECK(spatial::worker_may_simulate_cell(db,*destination,
          proof.destination.worker,proof.destination.epoch,
          proof.destination.region_generation,proof.destination.checkpoint_revision));
    const auto ore=w.world().total_ore();
    host.run_next_tick("h2-two-original-cells-continue");
    CHECK(w.world().find(moving) && w.world().find(resident) &&
          w.world().total_ore()==ore);
    const auto copy=(root/"source-retained-copy.sqlite").string();
    PersistentWorld::recover_copy(db,copy);
    PersistentWorld reopened(copy,Simulation(1));
    CHECK(reopened.world().canonical_state()==w.world().canonical_state());
    CHECK(spatial::worker_may_simulate_cell(copy,*source_cell,
          source.worker,source.epoch,source.region_generation,
          source.checkpoint_revision));
}
static void real_cross_system_arrival(const fs::path& root,const char* executable){
    const auto db=(root/"crosssystem.sqlite").string();
    Simulation initial(77115);
    initial.add_system(2);
    const auto ship=initial.add_colony_ship(7,{0,0},100,8);
    initial.provision_colony_genesis(ship,10000,10000,10000,10000,10000);
    const auto gate=initial.add_gate(1,{0,0},2,{0,0});
    PersistentWorld w(db,std::move(initial));
    w.enable_native_3d_physics();
    const galactic::Point star1{{0,1000000},{0,1000000},{0,1000000}};
    const galactic::Point star2{{0,1001000},{0,1000000},{0,1000000}};
    CHECK(galactic::register_original_system(db,w,1,star1,"h2-cross-system-one",true));
    CHECK(galactic::register_original_system(db,w,2,star2,"h2-cross-system-two",true));
    w.enable_native_galactic_physics(db);
    w.enable_native_3d_probes();
    const auto initial_cell=spatial::ship_cell(w.world(),ship);
    CHECK(initial_cell && initial_cell->system==1);
    const auto ore=w.world().total_ore();
    const auto entities=w.world().entities().size();
    std::set<std::uint64_t> people;
    for(const auto& item:w.world().npc_records())people.insert(item.first);
    const auto source=enroll(db,*initial_cell,"h2-cross-system-worker-1");
    spatial::NativeMultiCellSupervisor supervisor(db,w);
    supervisor.register_worker(source);
    supervisor.use_native_process_workers(executable);
    Command request{};request.id=910001;request.sequence=910001;
    request.execute_tick=w.world().tick()+1;
    request.issuer=7;request.actor=ship;request.target=gate;
    request.action=Action::RequestJump;
    CHECK(supervisor.submit_original_command(request,"h2-original-jump-authorized"));
    bool arrived=false;
    for(int i=0;i<16;++i){
        const auto tick=supervisor.run_next_tick("h2-gate-transit-"+std::to_string(i));
        CHECK(tick.world_generation==w.generation());
        if(w.world().find(ship)->system_id==2){arrived=true;break;}
    }
    CHECK(arrived);
    const auto destination=spatial::ship_cell(w.world(),ship);
    CHECK(destination && destination->system==2 &&
          !(*destination==*initial_cell) &&
          w.world().find(ship)->physical_galactic_authoritative);
    CHECK(galactic::original_ship_galactic_position(db,w,ship));
    const auto region=enroll_region(db,*destination);
    CHECK(w.world().total_ore()==ore && w.world().entities().size()==entities);
    // This is explicitly the existing authorized gate jump, not an invented
    // seamless interstellar warp. H2 only reassigns original worker custody.
    const auto proof=supervisor.reconcile_ship_handoff(ship,*initial_cell,*destination,
        "h2-cross-system-worker-2",0,region.first,region.second,
        "h2-authorized-gate-transfer",true);
    CHECK(proof.source_retired && proof.destination.epoch==1 &&
          !spatial::worker_may_simulate_cell(db,*initial_cell,source.worker,
              source.epoch,source.region_generation,source.checkpoint_revision));
    CHECK(supervisor.worker_count()==1);
    for(int i=0;i<8 && w.world().find(ship)->jump_state!=ShipJumpState::Ready;++i)
        supervisor.run_next_tick("h2-destination-clearance-"+std::to_string(i));
    CHECK(w.world().find(ship)->jump_state==ShipJumpState::Ready);
    CHECK(w.world().total_ore()==ore && w.world().entities().size()==entities &&
          w.world().npc_records().size()==people.size());
    const auto recovered=(root/"crosssystem-recovered.sqlite").string();
    PersistentWorld::recover_copy(db,recovered);
    PersistentWorld clone(recovered,Simulation(1));
    CHECK(clone.world().canonical_state()==w.world().canonical_state());
    CHECK(clone.world().find(ship)->system_id==2 &&
          spatial::ship_cell(clone.world(),ship)==destination);
    CHECK(spatial::worker_may_simulate_cell(recovered,*destination,
          proof.destination.worker,proof.destination.epoch,
          proof.destination.region_generation,proof.destination.checkpoint_revision));
}

int main(int argc,char** argv){
    const auto root=fs::temp_directory_path()/
        ("stage06h2-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Clean{fs::path p;~Clean(){std::error_code ec;fs::remove_all(p,ec);}} clean{root};
    try{
        CHECK(argc==2 && argv[1]);
        const auto db=(root/"original.sqlite").string();
        Simulation initial(77114);
        const auto ship=initial.add_colony_ship(7,{0,0},100,8);
        initial.provision_colony_genesis(ship,10000,10000,10000,10000,10000);
        initial.add_asteroid({1000000,0},300);
        PersistentWorld w(db,std::move(initial));
        w.enable_native_3d_physics();
        constexpr auto edge=galactic::kSectorEdgeMm;
        CHECK(galactic::register_original_system(db,w,1,
             galactic::Point{{12,edge-100},{34,1000},{56,1000}},
             "h2-genuine-continuous-sector",true));
        w.enable_native_galactic_physics(db);
        w.enable_native_3d_probes();
        const auto original=spatial::ship_cell(w.world(),ship);
        CHECK(original && original->sector_x==12 &&
              original->x==galactic::kCellsPerSector-1);
        const auto ore=w.world().total_ore();
        std::set<std::uint64_t> npc;
        for(const auto& item:w.world().npc_records())npc.insert(item.first);
        const auto original_entity_count=w.world().entities().size();
        const auto source=enroll(db,*original,"h2-worker-original");
        spatial::NativeMultiCellSupervisor supervisor(db,w);
        supervisor.use_native_process_workers(argv[1]);
        supervisor.register_worker(source);
        Command thrust{};
        thrust.id=900001;thrust.sequence=900001;
        thrust.execute_tick=w.world().tick()+1;
        thrust.issuer=7;thrust.actor=ship;thrust.action=Action::Thrust;
        thrust.thrust_x=1;
        CHECK(supervisor.submit_original_command(thrust,"h2-real-thrust"));
        const auto step=supervisor.run_next_tick("h2-physical-crossing");
        CHECK(step.world_generation==w.generation());
        const auto arrival=spatial::ship_cell(w.world(),ship);
        CHECK(arrival && !(*arrival==*original) &&
              arrival->sector_x==13 && arrival->x==0);
        CHECK(w.world().find(ship)->physical_galactic.x.sector==13 &&
              w.world().find(ship)->physical_galactic.x.millimeters==150);
        CHECK(w.world().total_ore()==ore &&
              w.world().npc_records().size()==npc.size() &&
              w.world().entities().size()==original_entity_count);
        refused([&]{(void)spatial::claim_original_writer(db,w,source,
            "h2-old-cell-still-writer",true);}); // old cell physically vacant
        const auto enrolled=enroll_region(db,*arrival);
        refused([&]{(void)spatial::reconcile_committed_ship_handoff(
            db,w,ship,source,*original,"h2-worker-next",0,
            enrolled.first,enrolled.second,"h2-wrong-destination",true);});
        refused([&]{(void)spatial::reconcile_committed_ship_handoff(
            db,w,ship,source,*arrival,"h2-worker-next",0,
            enrolled.first,enrolled.second,"h2-unauthorized",false);});
        refused([&]{(void)supervisor.reconcile_ship_handoff(ship,*original,*arrival,
            "h2-worker-next",0,enrolled.first,enrolled.second,
            "h2-crossing-1",true,FailPoint::AfterSnapshotWrite);});
        CHECK(spatial::worker_may_simulate_cell(db,*original,source.worker,
              source.epoch,source.region_generation,source.checkpoint_revision));
        CHECK(!spatial::worker_may_simulate_cell(db,*arrival,"h2-worker-next",1,
              enrolled.first,enrolled.second));
        sqlite3* sql=nullptr;
        CHECK(sqlite3_open_v2(db.c_str(),&sql,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
        CHECK(count(sql,"SELECT COUNT(*) FROM sqlite_master WHERE name='sg_spatial_ship_handoff'")==0);
        sqlite3_close(sql);
        // An OS crash between movement and worker handoff restarts against
        // the COMMITTED ship, not a second movement or a new test-only world.
        PersistentWorld reopened(db,Simulation(123));
        CHECK(reopened.world().canonical_state()==w.world().canonical_state());
        spatial::NativeMultiCellSupervisor recovered(db,reopened);
        recovered.use_native_process_workers(argv[1]);
        recovered.register_worker(source);
        const auto done=recovered.reconcile_ship_handoff(ship,*original,*arrival,
            "h2-worker-next",0,enrolled.first,enrolled.second,
            "h2-crossing-1",true);
        CHECK(done.source_retired && !done.replayed &&
              done.destination.epoch==1 &&
              done.physical_migration_generation<=reopened.generation());
        CHECK(!spatial::worker_may_simulate_cell(db,*original,source.worker,
              source.epoch,source.region_generation,source.checkpoint_revision));
        CHECK(spatial::worker_may_simulate_cell(db,*arrival,done.destination.worker,
              done.destination.epoch,enrolled.first,enrolled.second));
        CHECK(recovered.worker_count()==1);
        refused([&]{(void)spatial::claim_original_writer(db,reopened,source,
            "h2-stale-original-epoch",true);});
        const auto replay=spatial::reconcile_committed_ship_handoff(
            db,reopened,ship,source,*arrival,"h2-worker-next",0,
            enrolled.first,enrolled.second,"h2-crossing-1",true);
        CHECK(replay.replayed && replay.source_retired &&
              replay.destination.epoch==1);
        refused([&]{(void)spatial::reconcile_committed_ship_handoff(
            db,reopened,ship,source,*arrival,"h2-worker-next",0,
            enrolled.first,enrolled.second,"h2-crossing-2",true);});
        const auto generation=reopened.generation();
        CHECK(recovered.run_next_tick("h2-after-restart").world_generation==generation+1);
        CHECK(reopened.world().total_ore()==ore &&
              reopened.world().npc_records().size()==npc.size() &&
              reopened.world().entities().size()==original_entity_count);
        const auto copy=(root/"copy.sqlite").string();
        PersistentWorld::recover_copy(db,copy);
        PersistentWorld audit(copy,Simulation(222));
        CHECK(audit.world().canonical_state()==reopened.world().canonical_state());
        CHECK(audit.generation()==reopened.generation());
        sqlite3* check=nullptr;
        CHECK(sqlite3_open_v2(copy.c_str(),&check,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
        CHECK(count(check,"SELECT COUNT(*) FROM sg_spatial_ship_handoff")==1);
        CHECK(count(check,"SELECT COUNT(*) FROM sg_spatial_worker_lease")==1);
        CHECK(count(check,"SELECT COUNT(*) FROM sg_spatial_native_ship")==1);
        CHECK(count(check,"SELECT COUNT(*) FROM sg_spatial_native_transfer WHERE from_scope<>''")>=1);
        sqlite3_close(check);
        retained_source_worker(root,argv[1]);
        real_cross_system_arrival(root,argv[1]);
        std::cout<<"S6-H2 PASS native 250mm sector-crossing, original-world "
                 "immutable migration, crash-rollback, one-lease handoff, "
                 "independent SQLite recovery, stale/replay denial\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"S6-H2 FAIL "<<e.what()<<'\n';return 1;
    }
}
