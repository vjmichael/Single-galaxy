#include "galaxy/Persistence.hpp"
#include "galaxy/SeamlessCellWorker.hpp"
#include "galaxy/Native3DFloatingProjection.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(\
  "SPACE-04B/05 line "+std::to_string(__LINE__)+": " #x); } while(0)
template<class F>void refused(F&& f) {
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}
    CHECK(failed);
}
static Command thrust(std::uint64_t id,EntityId ship,std::uint64_t tick,
                      int x,int y,std::int64_t z){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=tick;
    c.issuer=7;c.actor=ship;c.action=Action::Thrust;
    c.thrust_x=x;c.thrust_y=y;c.amount=z;return c;
}
int main(){
 try{
    const auto root=fs::temp_directory_path()/
        ("sg_native3d_"+std::to_string(std::chrono::steady_clock::now().
                                       time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{root};
    auto db=(root/"native.sqlite").string();
    Simulation sim(999);
    const auto ship=sim.add_colony_ship(7,
        {Simulation::kWorldBoundMm-100,0},100,8);
    sim.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
    const auto original_mass=sim.total_ore();
    const auto original_crew=sim.colony_ship(ship)->crew;
    PersistentWorld w(db,std::move(sim));
    // v119 is an explicit original SQLite generation, not a sidecar.
    const auto prior=w.generation();
    refused([&]{w.enable_native_3d_physics(FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==prior && !w.world().native_3d_physics());
    w.enable_native_3d_physics();
    CHECK(w.world().native_3d_physics());
    CHECK(w.world().canonical_state().find("galaxy-kernel-v119|")==0);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).state_hash()
          ==w.world().state_hash());
    CHECK(w.submit(thrust(1001,ship,w.world().tick()+1,0,0,1)));
    w.step();
    CHECK(w.world().find(ship)->position_z_mm==250);
    CHECK(w.world().find(ship)->velocity_z_mm_per_tick==250);
    CHECK(w.submit(thrust(1002,ship,w.world().tick()+1,1,0,0)));
    w.step();
    const auto* physical=w.world().find(ship);
    CHECK(physical && physical->position.x>Simulation::kWorldBoundMm &&
          physical->position_z_mm==500 && physical->velocity.x==250);
    CHECK(w.world().total_ore()==original_mass &&
          w.world().colony_ship(ship)->crew==original_crew);
    // The near-field 3D projection consumes the SAME persistent ship and
    // host-approved immutable original star origin, never a new entity.
    const galactic::Point star{{946073000,17},{-946073000,19},{12,23}};
    CHECK(galactic::register_original_system(
          db,w,1,star,"native-3d-original-star",true));
    const auto global=galactic::original_ship_galactic_position(db,w,ship);
    CHECK(global);
    const galactic::FloatingFrame frame{*global,1000000000LL};
    const auto projected=galactic::original_ship_in_floating_frame(
        db,w,ship,frame);
    CHECK(projected && projected->original_generation==w.generation()
          && (projected->relative==galactic::Local3{0,0,0}));
    const auto displaced=galactic::rebase(frame,star);
    CHECK(displaced && !galactic::original_ship_in_floating_frame(
          db,w,ship,*displaced)); // beyond local radius, not deleted

    const auto cell=spatial::ship_cell(w.world(),ship);
    CHECK(cell && cell->z==0 && cell->x==10);
    const auto persisted=w.world().canonical_state();
    const auto gen=w.generation();
    const auto clone=(root/"recovered.sqlite").string();
    PersistentWorld::recover_copy(db,clone);
    PersistentWorld reopened(clone,Simulation(999));
    CHECK(reopened.world().canonical_state()==persisted &&
          reopened.generation()==gen &&
          spatial::ship_cell(reopened.world(),ship)==cell);
    // Enroll the original region owner and assign one durable native worker.
    const auto scope=spatial::scope(*cell);
    regional_events::CheckpointStore checkpoint(db,scope);
    regional_events::Scheduler macro;
    macro.add_colony("worker-evidence",cold_colony::State{});
    CHECK(checkpoint.save(macro,0)==1);
    regional_events::RegionalModeAuthority authority(checkpoint);
    const auto epoch=authority.enroll();
    const auto lease=spatial::transfer_cell_worker(db,*cell,
        "worker-a",0,epoch.generation,epoch.checkpoint_revision,
        "test-lease-a",true);
    NativeCellWorkerTick token{scope,"worker-a",lease.epoch,
                               epoch.generation,epoch.checkpoint_revision};
    const auto unchanged=w.world().canonical_state();
    const auto before=w.generation();
    // All ORIGINAL mutation entrypoints must now enforce the lease, not
    // just a worker-driven physics tick. A refused submit cannot retain
    // a phantom pending command, receipt, cargo or tick.
    const auto attempted=thrust(1003,ship,w.world().tick()+1,0,0,0);
    refused([&]{(void)w.submit(attempted);});
    refused([&]{w.configure_probe_report_tick_ms(700);});
    refused([&]{w.step();});
    CHECK(!w.world().has_command_id(attempted.id));
    CHECK(w.world().canonical_state()==unchanged && w.generation()==before);
    CHECK(w.submit_as_cell_worker(attempted,token));
    CHECK(w.world().has_command_id(attempted.id) &&
          w.generation()==before+1);
    w.step_as_cell_worker(token);
    CHECK(w.generation()==before+2 && w.world().tick()==3);
    // Stale worker epoch, even under the same region owner, cannot advance
    // the original physics or create another cargo/simulation tick.
    const auto replacement=spatial::transfer_cell_worker(db,*cell,
        "worker-b",lease.epoch,epoch.generation,epoch.checkpoint_revision,
        "test-lease-b",true);
    const auto stable=w.world().canonical_state();
    const auto before_replay=w.generation();
    refused([&]{w.step_as_cell_worker(token);});
    refused([&]{(void)w.submit_as_cell_worker(
        thrust(1004,ship,w.world().tick()+1,0,0,0),token);});
    CHECK(!w.world().has_command_id(1004));
    CHECK(w.generation()==before_replay &&
          w.world().canonical_state()==stable);
    NativeCellWorkerTick successor{scope,"worker-b",replacement.epoch,
                                   epoch.generation,epoch.checkpoint_revision};
    refused([&]{w.step_as_cell_worker(successor,FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==before_replay &&
          w.world().canonical_state()==stable);
    w.step_as_cell_worker(successor);
    CHECK(w.generation()==before_replay+1 &&
          w.world().total_ore()==original_mass &&
          w.world().colony_ship(ship)->crew==original_crew);
    std::cout<<"PASS v119 original 3D ship motion, rollback, recovery and "
                "SQLite-enforced worker tick handoff\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SPACE-04B/05: "<<e.what()<<'\n';return 1;
 }
}
