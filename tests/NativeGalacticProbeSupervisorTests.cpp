#include "galaxy/Persistence.hpp"
#include "galaxy/SeamlessCellSupervisor.hpp"
#include "galaxy/NativeCellProcessRunner.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(  "SPACE-04C/04D/05B "+std::to_string(__LINE__)+": " #x);}while(false)
template<class F>static void denied(F&& fun){
    bool blocked=false;try{fun();}catch(const std::exception&){blocked=true;}
    CHECK(blocked);
}
static Command command(std::uint64_t id,EntityId ship,std::uint64_t tick,
                       Action action,EntityId target=0,int x=0,int y=0,
                       std::int64_t amount=0){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=tick;
    c.issuer=7;c.actor=ship;c.target=target;c.action=action;
    c.thrust_x=x;c.thrust_y=y;c.amount=amount;return c;
}
static Outcome run(PersistentWorld& w,Command c){
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    return w.world().events().back().result;
}
static void galaxy_and_probes(const fs::path& root){
    const auto db=(root/"v121.sqlite").string();
    Simulation genesis(4401);
    const auto ship=genesis.add_colony_ship(7,{0,0},60,24);
    genesis.enable_individual_workforce();
    genesis.provision_starter_exploration_scanner(ship);
    genesis.provision_starter_probe_kit(ship);
    const auto rock=genesis.add_asteroid({0,0},100);
    const auto ore=genesis.total_ore();
    PersistentWorld w(db,std::move(genesis));
    w.enable_native_3d_physics();
    constexpr auto edge=galactic::kSectorEdgeMm;
    const galactic::Point original_star{{123,edge-100},{-42,12345},{15,222}};
    CHECK(galactic::register_original_system(db,w,1,original_star,
          "space-04c-stellar-genesis",true));
    const auto before=w.generation();
    denied([&]{w.enable_native_galactic_physics(db,
                              FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==before && !w.world().native_galactic_physics());
    w.enable_native_galactic_physics(db);
    CHECK(w.world().native_galactic_physics());
    CHECK(w.world().canonical_state().find("galaxy-kernel-v120|")==0);
    CHECK(w.world().find(ship)->physical_galactic.x.sector==123);
    CHECK(w.world().total_ore()==ore);
    w.enable_native_3d_probes();
    CHECK(w.world().native_3d_probes());
    CHECK(w.world().canonical_state().find("galaxy-kernel-v121|")==0);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).state_hash()
          ==w.world().state_hash());
    CHECK(run(w,command(10001,ship,w.world().tick()+1,
            Action::DeploySurveyProbe))==Outcome::Applied);
    CHECK(w.world().survey_probes().at(10001).galactic_authoritative);
    CHECK(w.world().survey_probes().at(10001).galactic_position==
          w.world().find(ship)->physical_galactic);
    CHECK(run(w,command(10002,ship,w.world().tick()+1,
            Action::ScanSurveyProbe,10001))==Outcome::Applied);
    CHECK(w.world().pending_probe_report_count()==1);
    const auto snapshot=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
    const auto pre=w.generation();
    auto broken=snapshot;
    const auto at=broken.find("surveyprobe|10001|");
    CHECK(at!=std::string::npos);
    const auto end=broken.find('\n',at);
    CHECK(end!=std::string::npos);
    const auto pipe=broken.rfind('|',end);
    CHECK(pipe!=std::string::npos && pipe>at);
    broken.replace(pipe+1,end-pipe-1,std::to_string(edge));
    denied([&]{(void)Simulation::from_snapshot(broken);});
    const auto clone=(root/"v121-copy.sqlite").string();
    PersistentWorld::recover_copy(db,clone);
    PersistentWorld reopened(clone,Simulation(1));
    CHECK(reopened.world().canonical_state()==snapshot &&
          reopened.generation()==pre);
    // One 250mm displacement crosses the 1e15mm sector edge; the ship's
    // identity, crew, ore and original native generation remain continuous.
    CHECK(run(w,command(10003,ship,w.world().tick()+1,
            Action::Thrust,0,1,0))==Outcome::Applied);
    const auto after=w.world().find(ship)->physical_galactic.x;
    CHECK(after.sector==124 && after.millimeters==150);
    const auto cell=spatial::ship_cell(w.world(),ship);
    CHECK(cell && cell->hierarchical && cell->sector_x==124 && cell->x==0);
    CHECK(w.world().total_ore()==ore);
    CHECK(galactic::original_ship_galactic_position(db,w,ship)==
          std::optional<galactic::Point>(w.world().find(ship)->physical_galactic));
    CHECK(w.world().find(rock)->physical_galactic.x.sector==123);
    // Delayed observation survives a physical sector crossing.
    for(int i=0;i<30;++i)w.step();
    CHECK(w.world().discoveries().count({7,rock})==1);
    CHECK(w.world().total_ore()==ore);

    // Identical X/Y must not produce a false 3D probe contact if the
    // carrier launched its probe more than 50 meters above the rock.
    Simulation vertical(4402);
    const auto flyer=vertical.add_colony_ship(7,{0,0},60,24);
    vertical.enable_individual_workforce();
    vertical.provision_starter_exploration_scanner(flyer);
    vertical.provision_starter_probe_kit(flyer);
    vertical.add_asteroid({0,0},100);
    vertical.enable_native_3d_physics();
    vertical.enable_native_galactic_physics({{1,original_star}});
    vertical.enable_native_3d_probes();
    for(std::uint64_t i=0;i<21;++i){
        CHECK(vertical.submit(command(20000+i,flyer,vertical.tick()+1,
                           Action::Thrust,0,0,0,1)));
        vertical.step();
    }
    CHECK(vertical.find(flyer)->position_z_mm>
          Simulation::kProbeScanRangeMm);
    const auto probe=command(20030,flyer,vertical.tick()+1,
                              Action::DeploySurveyProbe);
    CHECK(vertical.submit(probe));vertical.step();
    CHECK(vertical.events().back().result==Outcome::Applied);
    const auto scan=command(20031,flyer,vertical.tick()+1,
                             Action::ScanSurveyProbe,20030);
    CHECK(vertical.submit(scan));vertical.step();
    CHECK(vertical.events().back().result==Outcome::Applied);
    CHECK(vertical.pending_probe_report_count()==0);
}
static void anchored_nonships_and_swap(){
    // One original world has two vastly separated, real star systems.
    // Body-local resources must take their own celestial anchor position;
    // a system-two outpost must not inherit system-one's star coordinates.
    Simulation original(4403);
    original.add_system(2);
    const auto ship=original.add_colony_ship(7,{0,0},100,8);
    original.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
    original.add_asteroid({1000,0},50);
    original.enable_native_3d_physics();
    const galactic::Point star1{{99,777},{0,444},{6,321}};
    const galactic::Point star2{{-1000,700},{2000,800},{900,1000}};
    original.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto far_station=original.add_station_in_system(2,{123,456});
    const auto far_rock=original.add_asteroid_in_system(2,{-500,1000},22);
    const auto expected_station=galactic::advance(star2,{123,456,0});
    const auto expected_rock=galactic::advance(star2,{-500,1000,0});
    CHECK(expected_station && expected_rock);
    CHECK(original.find(far_station)->physical_galactic==*expected_station);
    CHECK(original.find(far_rock)->physical_galactic==*expected_rock);
    const auto& bodies=original.physical_celestial_systems().at(1);
    CHECK(!bodies.empty());
    const auto body=bodies.front();
    original.seed_celestial_local_region(body.id);
    const auto generated=original.generated_local_regions().at(body.id);
    CHECK(generated[0] && generated[1]);
    const auto frame=galactic::advance(star1,
        {body.position.x_mm,body.position.y_mm,body.position.z_mm});
    CHECK(frame);
    for(const auto id:generated){
        const auto* rock=original.find(id);
        CHECK(rock && rock->type==EntityType::Asteroid &&
              original.local_entity_anchors().at(id)==body.id);
        const auto location=galactic::advance(*frame,
            {rock->position.x,rock->position.y,rock->position_z_mm});
        CHECK(location && rock->physical_galactic==*location);
    }
    const auto original_bytes=original.canonical_state();
    CHECK(Simulation::from_snapshot(original_bytes).canonical_state()==original_bytes);
    Simulation spare(9944);
    original.swap(spare);
    CHECK(spare.native_galactic_physics() &&
          spare.galactic_system_origins().at(1)==star1 &&
          spare.galactic_system_origins().at(2)==star2 &&
          spare.canonical_state()==original_bytes);
}
static void multi_cell_supervision(const fs::path& root,
    const std::string& worker_binary,const std::string& hang_binary){
    const auto db=(root/"multi.sqlite").string();
    Simulation initial(77112);
    const auto ship1=initial.add_colony_ship(7,{0,0},100,8);
    const auto ship2=initial.add_colony_ship(8,{300000000,0},100,8);
    initial.provision_colony_genesis(ship1,1000,1000,1000,1000,1000);
    initial.provision_colony_genesis(ship2,1000,1000,1000,1000,1000);
    const auto ore=initial.total_ore();
    PersistentWorld w(db,std::move(initial));
    w.enable_native_3d_physics();
    const galactic::Point origin{{0,1000000},{0,1000000},{0,1000000}};
    CHECK(galactic::register_original_system(
        db,w,1,origin,"space-05b-original-star",true));
    w.enable_native_galactic_physics(db);
    w.enable_native_3d_probes();
    const auto cell1=spatial::ship_cell(w.world(),ship1);
    const auto cell2=spatial::ship_cell(w.world(),ship2);
    CHECK(cell1 && cell2 && !(*cell1==*cell2));
    auto register_cell=[&](const spatial::Cell& cell,
                           const std::string& id){
        const auto scope=spatial::scope(cell);
        regional_events::CheckpointStore checkpoint(db,scope);
        regional_events::Scheduler scheduler;
        scheduler.add_colony("physical-"+id,cold_colony::State{});
        CHECK(checkpoint.save(scheduler,0)==1);
        regional_events::RegionalModeAuthority authority(checkpoint);
        const auto region=authority.enroll();
        return spatial::transfer_cell_worker(db,cell,id,0,
            region.generation,region.checkpoint_revision,
            "lease-"+id,true);
    };
    const auto l1=register_cell(*cell1,"worker-A");
    const auto l2=register_cell(*cell2,"worker-B");
    // Both real physical cells have independently fenced worker leases.
    // A hung OS child cannot commit the original world or evade watchdog.
    spatial::NativeMultiCellSupervisor stalled(db,w);
    stalled.register_worker(l1);stalled.register_worker(l2);
    stalled.use_native_process_workers(hang_binary);
    const auto baseline=w.generation();
    denied([&]{stalled.run_next_tick("hung-workers");});
    CHECK(stalled.last_failed_cells().size()==2 &&
          w.generation()==baseline);
    spatial::NativeMultiCellSupervisor supervisor(db,w);
    supervisor.register_worker(l1);supervisor.register_worker(l2);
    supervisor.use_native_process_workers(worker_binary);
    CHECK(supervisor.worker_count()==2);
    const auto start=w.generation(),tick=w.world().tick();
    denied([&]{w.step();});
    denied([&]{w.step_as_cell_worker(NativeCellWorkerTick{
         spatial::scope(*cell1),l1.worker,l1.epoch,
         l1.region_generation,l1.checkpoint_revision});});
    CHECK(w.generation()==start && w.world().tick()==tick);
    const auto first=supervisor.run_next_tick("supervised-1");
    CHECK(w.generation()==start+1 && w.world().tick()==tick+1 &&
          first.world_generation==w.generation());
    const auto unchanged=w.world().canonical_state();
    const auto generation=w.generation();
    denied([&]{supervisor.run_next_tick("supervised-2",
                                      FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==generation && w.world().canonical_state()==unchanged);
    const auto retry=supervisor.run_next_tick("supervised-2");
    CHECK(retry.world_generation==generation+1 &&
          w.world().total_ore()==ore);
    const auto stable=w.world().canonical_state();
    const auto before=w.generation();
    denied([&]{supervisor.run_next_tick("abandoned-3",
                                      FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==before && w.world().canonical_state()==stable);
    const auto next_cell=(first.scope==spatial::scope(*cell1))?*cell1:*cell2;
    // Two successful selections rotate through equal-priority cells.
    const auto successor=supervisor.recover_worker(
        next_cell,"worker-C","durable-takeover-3",true);
    CHECK(successor.epoch>0);
    const auto completed=supervisor.run_next_tick("replacement-3");
    CHECK(completed.worker=="worker-C" &&
          completed.world_generation==before+1 &&
          w.world().total_ore()==ore && w.world().tick()==tick+3);
    denied([&]{w.step_as_cell_worker(NativeCellWorkerTick{
        spatial::scope(next_cell),first.worker,successor.epoch-1,
        successor.region_generation,successor.checkpoint_revision,
        "abandoned-3"});});
    // An orphaned selected writer cannot be stolen while its original
    // epoch is live. After a real durable lease CAS revokes that epoch,
    // a healthy worker on ANOTHER actual occupied cell may take over.
    const auto stranded=spatial::claim_original_writer(
        db,w,successor,"crosscell-stranded",true);
    const auto cross_gen=w.generation(),cross_tick=w.world().tick();
    const auto other=(next_cell==*cell1)?l2:l1;
    denied([&]{(void)spatial::claim_original_writer(
        db,w,other,"crosscell-too-early",true);});
    CHECK(w.generation()==cross_gen && w.world().tick()==cross_tick);
    const auto retired=spatial::transfer_cell_worker(db,next_cell,
        "worker-D",successor.epoch,successor.region_generation,
        successor.checkpoint_revision,"crosscell-retire",true);
    CHECK(retired.epoch>successor.epoch);
    spatial::NativeMultiCellSupervisor surviving(db,w);
    surviving.register_worker(other);
    surviving.use_native_process_workers(worker_binary);
    const auto takeover=surviving.run_next_tick("crosscell-takeover");
    CHECK(takeover.scope==spatial::scope(other.cell) &&
          w.generation()==cross_gen+1 &&
          w.world().tick()==cross_tick+1 &&
          w.world().total_ore()==ore);
    denied([&]{w.step_as_cell_worker(stranded);});
}
int main(int argc,char** argv){
    const auto root=fs::temp_directory_path()/
        ("sg_space_04c_05b_"+std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup{fs::path path;~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
    try{
        CHECK(argc==3 && argv[1] && argv[2]);
        const spatial::CellProcessTask task{
            "spatial/v2/s1/q0,0,0/c0,0,0","worker-A",1,1234567,2,1};
        CHECK(spatial::run_cell_process(argv[1],task).passed);
        CHECK(!spatial::run_cell_process(argv[2],task,
            std::chrono::milliseconds(50)).passed);
        galaxy_and_probes(root);
        anchored_nonships_and_swap();
        multi_cell_supervision(root,argv[1],argv[2]);
        std::cout<<"PASS exact original galactic 3D, probe persistence and "
                    "durable multi-cell single-writer supervision\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL SPACE-04C/04D/05B: "<<e.what()<<'\n';return 1;
    }
}
