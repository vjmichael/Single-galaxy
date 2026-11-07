#include "galaxy/SeamlessCellWorker.hpp"
#include "galaxy/GalacticCoordinates.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(expr) do {if(!(expr))throw std::runtime_error( \
  "SPACE-01 native C++ line "+std::to_string(__LINE__)+": " #expr);}while(0)
template<class F>void denied(F&& f) {
    bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}
    CHECK(thrown);
}
struct Db {
    sqlite3* db=nullptr;
    explicit Db(const std::string& path){
        CHECK(sqlite3_open_v2(path.c_str(),&db,
             SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    }
    ~Db(){sqlite3_close(db);}
    std::int64_t count(const char* q) {
        sqlite3_stmt* stmt=nullptr;
        CHECK(sqlite3_prepare_v2(db,q,-1,&stmt,nullptr)==SQLITE_OK);
        CHECK(sqlite3_step(stmt)==SQLITE_ROW);
        const auto n=sqlite3_column_int64(stmt,0);
        sqlite3_finalize(stmt);
        return n;
    }
};
int main(){
 try{
    using spatial::Cell;
    constexpr auto side=spatial::kCellEdgeMm;
    CHECK(spatial::floor_cell(-1)==-1);
    CHECK(spatial::floor_cell(-side)==-1);
    CHECK(spatial::floor_cell(-side-1)==-2);
    CHECK(spatial::floor_cell(0)==0);
    CHECK(spatial::floor_cell(std::numeric_limits<std::int64_t>::min())<
          -1000000);
    const Cell xyz=spatial::of({-1,side*3+1,-side*4},7);
    CHECK(xyz.system==7 && xyz.x==-1 && xyz.y==3 && xyz.z==-4);
    CHECK(spatial::parse_scope(spatial::scope(xyz))==xyz);
    CHECK(!spatial::parse_scope("spatial/v1/s07/x-1/y3/z-4"));
    CHECK(!spatial::parse_scope("spatial/v1/s7/x-0/y3/z-4"));
    CHECK(!spatial::parse_scope("spatial/v1/s7/x-1/y3/z-4/extra"));
    CHECK(!spatial::parse_scope("spatial/v1/s0/x0/y0/z0"));
    // SPACE-04A: real galaxy-scale precision math. Sector boundaries
    // normalize without stopping speed, changing galaxy identity or
    // converting a physical 3D coordinate to a legacy 2D projection.
    constexpr auto sector=galactic::kSectorEdgeMm;
    const galactic::Point huge{
        {946073000,sector-5},{-946073000,0},{1234567,sector-1}};
    CHECK(huge.valid()); // hundreds of millions of sectors: galactic scale
    const auto moved=galactic::advance(
        huge,{10,-1,2});
    CHECK(moved && moved->x.sector==946073001 &&
          moved->x.millimeters==5 &&
          moved->y.sector==-946073001 &&
          moved->y.millimeters==sector-1 &&
          moved->z.sector==1234568 &&
          moved->z.millimeters==1);
    const auto first_cell=galactic::cell(huge);
    const auto next_cell=galactic::cell(*moved);
    CHECK(first_cell && next_cell && !(*first_cell==*next_cell) &&
          galactic::scope(*first_cell)!=galactic::scope(*next_cell));
    const auto relative=galactic::nearby_displacement(*moved,huge);
    CHECK(relative && relative->x_mm==10 &&
          relative->y_mm==-1 && relative->z_mm==2);
    const auto negative_overflow=galactic::normalize(
        {0,0},std::numeric_limits<std::int64_t>::min());
    CHECK(negative_overflow && negative_overflow->valid() &&
          negative_overflow->sector<-9000);
    const galactic::Point at_limit{
        {std::numeric_limits<std::int64_t>::max(),sector-1},
        {0,0},{0,0}};
    CHECK(!galactic::advance(at_limit,{1,0,0}));
    const auto origin=galactic::at_system_origin(
        huge,{100,200,-300});
    CHECK(origin && origin->valid() &&
          origin->x.sector==946073001 &&
          origin->x.millimeters==95 &&
          origin->z.sector==1234567 &&
          origin->z.millimeters==sector-301);
    const auto directory=fs::temp_directory_path()/
        ("sg_spatial_"+std::to_string(std::chrono::steady_clock::now().
                                    time_since_epoch().count()));
    fs::create_directories(directory);
    struct Remove {fs::path path;~Remove(){std::error_code ec;
        fs::remove_all(path,ec);}} cleanup{directory};
    const auto path=(directory/"native.sqlite").string();
    Simulation simulation(91011);
    simulation.add_system(2); // real original system ID, not a synthetic star
    const auto ship=simulation.add_colony_ship(7,{side-2,0},100,8);
    simulation.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
    const auto other=simulation.add_colony_ship(7,{-1,0},100,8);
    simulation.provision_colony_genesis(other,1000,1000,1000,1000,1000);
    const auto initial_mass=simulation.total_ore();
    const auto original_crew=simulation.colony_ship(ship)->crew;
    CHECK(spatial::desired_detail(simulation,{1,10,0,0})==
          spatial::Detail::Dormant);
    CHECK(spatial::desired_detail(simulation,{1,0,0,0})==
          spatial::Detail::Cold);
    CHECK(spatial::desired_detail(simulation,{1,0,0,0},Cell{1,0,0,0})==
          spatial::Detail::Detailed);
    PersistentWorld w(path,std::move(simulation));
    // SPACE-04A.2: real host-registered original star origins. Registries
    // describe astronomical geometry, not new star systems, items or ships.
    const galactic::Point star_1{
        {946073000,10},{-946073000,20},{1000,30}};
    const galactic::Point star_2{
        {946073100,11},{-946073050,21},{1002,31}};
    CHECK(!galactic::register_original_system(
        path,w,1,star_1,"survey-system-one",false));
    CHECK(!galactic::register_original_system(
        path,w,999,star_1,"ghost-system",true));
    CHECK(galactic::register_original_system(
        path,w,1,star_1,"survey-system-one",true));
    CHECK(galactic::register_original_system(
        path,w,1,star_1,"survey-system-one",true)); // exact replay only
    denied([&]{(void)galactic::register_original_system(
        path,w,1,star_2,"forged-replacement",true);});
    CHECK(galactic::register_original_system(
        path,w,2,star_2,"survey-system-two",true));
    const auto origin_before=galactic::original_ship_galactic_position(
        path,w,ship);
    const auto native_before=spatial::cosmic_ship(w.world(),ship);
    CHECK(origin_before && native_before &&
          origin_before==galactic::at_system_origin(
              star_1,*native_before));
    const auto start=spatial::ship_cell(w.world(),ship);
    const auto before=spatial::ship_cell(w.world(),other);
    CHECK(start && before && start->x==0 && before->x==-1);
    {Db db(path);CHECK(spatial::verify_native_index(
         db.db,w.world(),w.generation()));}
    Command thrust{};
    thrust.id=911;thrust.sequence=911;
    thrust.execute_tick=w.world().tick()+1;
    thrust.issuer=7;thrust.actor=ship;
    thrust.action=Action::Thrust;thrust.thrust_x=1;
    CHECK(w.submit(thrust));
    w.step();
    const auto after=spatial::ship_cell(w.world(),ship);
    const auto origin_after=galactic::original_ship_galactic_position(
        path,w,ship);
    const auto native_after=spatial::cosmic_ship(w.world(),ship);
    CHECK(origin_after && native_after &&
          origin_after==galactic::at_system_origin(
              star_1,*native_after) &&
          !(*origin_after==*origin_before));
    CHECK(after && after->x==1 && after->y==0 &&
          w.world().find(ship)->velocity.x>0 &&
          w.world().find(ship)->owner_id==7 &&
          w.world().colony_ship(ship)->crew==original_crew &&
          w.world().total_ore()==initial_mass);
    const auto old_state=w.world().canonical_state();
    const auto old_generation=w.generation();
    {
        Db db(path);
        CHECK(spatial::verify_native_index(db.db,w.world(),w.generation()));
        CHECK(db.count("SELECT COUNT(*) FROM sg_spatial_native_ship")==2);
        CHECK(db.count("SELECT COUNT(*) FROM sg_spatial_native_transfer "
            "WHERE from_scope='spatial/v1/s1/x0/y0/z0' "
            "AND to_scope='spatial/v1/s1/x1/y0/z0'")==1);
        CHECK(db.count("SELECT COUNT(*) FROM sg_spatial_native_transfer")==3);
    }
    bool fault=false;
    try{w.step(FailPoint::AfterSnapshotWrite);}
    catch(const std::exception&){fault=true;}
    CHECK(fault && w.world().canonical_state()==old_state &&
          w.generation()==old_generation);
    {
        Db db(path);CHECK(spatial::verify_native_index(
            db.db,w.world(),w.generation()));
    }
    // A cell is a COMPUTE partition only, never a physical barrier.
    CHECK(spatial::touching(*after,Cell{1,2,0,0}));
    CHECK(!spatial::touching(*after,Cell{1,9,0,0}));
    const auto scoped=spatial::scope(*after);
    regional_events::CheckpointStore checkpoint(path,scoped);
    regional_events::Scheduler macro;
    macro.add_colony("spatial-only",cold_colony::State{});
    CHECK(checkpoint.save(macro,0)==1);
    regional_events::RegionalModeAuthority authority(checkpoint);
    const auto epoch=authority.enroll();
    CHECK(epoch.generation==1 && epoch.checkpoint_revision==1);
    denied([&]{(void)spatial::transfer_cell_worker(
        path,*after,"worker-a",0,1,1,"initial",false);});
    const auto lease=spatial::transfer_cell_worker(
        path,*after,"worker-a",0,1,1,"initial",true);
    CHECK(lease.epoch==1 && !lease.replayed &&
          lease.observed_original_generation==w.generation());
    const auto retry=spatial::transfer_cell_worker(
        path,*after,"worker-a",0,1,1,"initial",true);
    CHECK(retry.replayed && retry.epoch==lease.epoch);
    CHECK(spatial::worker_may_simulate_cell(path,*after,"worker-a",1,1,1));
    CHECK(!spatial::worker_may_simulate_cell(path,*after,"worker-b",1,1,1));
    denied([&]{(void)spatial::transfer_cell_worker(
        path,*after,"worker-b",0,1,1,"stale",true);});
    const auto to_detailed=authority.prepare("spatial-promote",
        regional_events::SimulationOwner::Detailed,1,1,0);
    CHECK(!spatial::worker_may_simulate_cell(path,*after,"worker-a",1,1,1));
    const auto warm=authority.commit("spatial-promote",to_detailed.generation,
        to_detailed.checkpoint_revision,
        [](sqlite3*,regional_events::Scheduler&){});
    CHECK(warm.generation==2 &&
          warm.owner==regional_events::SimulationOwner::Detailed &&
          !spatial::worker_may_simulate_cell(path,*after,"worker-a",1,1,1));
    // A byte-identical worker transfer cannot be replayed under a NEW
    // regional owner generation even though the previous worker still
    // has that old token: old authority is permanently stale.
    denied([&]{(void)spatial::transfer_cell_worker(
        path,*after,"worker-a",0,warm.generation,
        warm.checkpoint_revision,"initial",true);});
    const auto replacement=spatial::transfer_cell_worker(
        path,*after,"worker-b",1,warm.generation,
        warm.checkpoint_revision,"worker-handoff",true);
    CHECK(replacement.epoch==2 &&
          spatial::worker_may_simulate_cell(path,*after,"worker-b",2,2,1));
    CHECK(!spatial::worker_may_simulate_cell(path,*after,"worker-a",1,2,1));
    CHECK(w.world().canonical_state()==old_state &&
          w.world().total_ore()==initial_mass);
    const auto clone=(directory/"recovered.sqlite").string();
    PersistentWorld::recover_copy(path,clone);
    {
        PersistentWorld reopened(clone,Simulation(91011));
        CHECK(reopened.generation()==w.generation() &&
              reopened.world().canonical_state()==w.world().canonical_state());
        Db db(clone);
        CHECK(spatial::verify_native_index(
            db.db,reopened.world(),reopened.generation()));
        CHECK(galactic::original_ship_galactic_position(
            clone,reopened,ship)==origin_after);
        CHECK(spatial::worker_may_simulate_cell(
            clone,*after,"worker-b",2,warm.generation,
            warm.checkpoint_revision));
        CHECK(db.count("SELECT COUNT(*) FROM sg_spatial_native_transfer "
           "WHERE from_scope='spatial/v1/s1/x0/y0/z0' "
           "AND to_scope='spatial/v1/s1/x1/y0/z0'")==1);
    }
    // Invalid index-only edits must not provide a forged spatial owner:
    // the immutable receipt tip and original world generation still win.
    const auto tampered=(directory/"tampered.sqlite").string();
    PersistentWorld::recover_copy(path,tampered);
    sqlite3* unsafe=nullptr;
    CHECK(sqlite3_open_v2(tampered.c_str(),&unsafe,
          SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
    char* error=nullptr;
    CHECK(sqlite3_exec(unsafe,
          "UPDATE sg_spatial_native_ship SET scope='spatial/v1/s1/x999/y0/z0' "
          "WHERE scope='spatial/v1/s1/x1/y0/z0'",
          nullptr,nullptr,&error)==SQLITE_OK);
    sqlite3_free(error);error=nullptr;
    CHECK(sqlite3_exec(unsafe,
          "DELETE FROM sg_spatial_native_transfer",
          nullptr,nullptr,&error)==SQLITE_CONSTRAINT);
    sqlite3_free(error);sqlite3_close(unsafe);
    {
        Db db(tampered);
        CHECK(!spatial::verify_native_index(
              db.db,w.world(),w.generation()));
    }
    std::cout<<"PASS SPACE-01/02/03 native cell crossing, identity, atomic rollback, "
       "epoch-fenced computation, worker CAS and independent SQLite recovery\n";
    return 0;
 }catch(const std::exception& e) {
    std::cerr<<"FAIL SPACE: "<<e.what()<<'\n';
    return 1;
 }
}
