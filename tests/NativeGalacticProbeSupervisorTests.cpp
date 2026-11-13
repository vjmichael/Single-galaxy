#include "galaxy/Persistence.hpp"
#include "galaxy/Navigation.hpp"
#include "galaxy/SeamlessCellSupervisor.hpp"
#include "galaxy/NativeCellProcessRunner.hpp"
#include "galaxy/GalacticSystemRegistry.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <map>
#include <vector>
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
        CHECK(original.same_local_frame(ship,id)); // no artificial view wall
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
static void cross_system_physical_contact(){
    // Historical system labels identify original star catalogues, not
    // walls. Two objects 250 mm apart stay near even when assigned to
    // different stars. Distant targets still fail real native range checks.
    Simulation physical(4812);
    physical.add_system(2);
    const auto ship=physical.add_colony_ship(7,{0,0},100,4);
    physical.add_asteroid({400000,0},9);
    physical.enable_native_3d_physics();
    const galactic::Point star1{{0,1000000},{0,1000000},{0,1000000}};
    const galactic::Point star2{{0,1001000},{0,1000000},{0,1000000}};
    physical.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto station=physical.add_station_in_system(2,{-750,0});
    const auto rock=physical.add_asteroid_in_system(2,{-750,0},23);
    const auto remote=physical.add_asteroid_in_system(2,{250000,0},23);
    const auto gate=physical.add_gate(2,{-750,0},1,{1000,0});
    CHECK(physical.find(ship)->system_id==1 &&
          physical.find(station)->system_id==2);
    CHECK(physical.same_local_frame(ship,station));
    // NPC navigation may use the authoritative galactic relative vector
    // even when a physically adjacent station has another catalogue label.
    CHECK(assist_to_object(*physical.find(ship),*physical.find(station),0).arrived);
    CHECK(!assist_to_object(*physical.find(ship),*physical.find(remote),0).arrived);
    // Exercise the public original command path; mine_status is intentionally
    // private and cannot be called by a native integration test.
    const auto mining_outcome=[&](EntityId target,std::uint64_t id){
        auto isolated=Simulation::from_snapshot(physical.canonical_state());
        CHECK(isolated.submit(command(id,ship,isolated.tick()+1,
            Action::Mine,target,0,0,1)));
        isolated.step();
        CHECK(!isolated.events().empty());
        CHECK(isolated.events().back().command_id==id);
        return isolated.events().back().result;
    };
    CHECK(mining_outcome(rock,50040)==Outcome::Applied);
    CHECK(mining_outcome(remote,50041)==Outcome::OutOfRange);
    CHECK(spatial::touching(
        spatial::Cell{1,0,0,0,true,0,0,0},
        spatial::Cell{2,1,0,0,true,0,0,0}));
    CHECK(!spatial::touching(
        spatial::Cell{1,0,0,0,true,0,0,0},
        spatial::Cell{2,3,0,0,true,0,0,0}));
    physical.enable_native_3d_probes();
    CHECK(physical.submit(command(50001,ship,1,Action::RequestDock,station)));
    physical.step();
    CHECK(physical.events().back().result==Outcome::Applied);
    CHECK(Simulation::from_snapshot(physical.canonical_state()).state_hash()
          ==physical.state_hash());
    for(int i=0;i<2;++i)physical.step();
    CHECK(physical.find(ship)->dock_state==DockState::Docked);
    CHECK(physical.find(ship)->physical_galactic==
          physical.find(station)->physical_galactic);
    CHECK(Simulation::from_snapshot(physical.canonical_state()).canonical_state()
          ==physical.canonical_state());
    // The same physical crossing applies to fold-gate admission.
    Simulation jumping(4813);
    jumping.add_system(2);
    const auto jumper=jumping.add_colony_ship(7,{0,0},100,4);
    jumping.enable_native_3d_physics();
    jumping.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto gate2=jumping.add_gate(2,{-750,0},1,{1000,0});
    CHECK(jumping.submit(command(50002,jumper,1,Action::RequestJump,gate2)));
    jumping.step();
    CHECK(jumping.events().back().result==Outcome::Applied);
    CHECK(Simulation::from_snapshot(jumping.canonical_state()).state_hash()
          ==jumping.state_hash());
    CHECK(physical.find(gate)->physical_galactic==
          jumping.find(gate2)->physical_galactic);

    // A genuine named-NPC pilot may start an original docking plan to
    // an adjacent foreign-label station. Older snapshots still use the
    // original same-system safety rule. No teleport/forced docking.
    Simulation npc_nav(4815);
    npc_nav.add_system(2);
    const auto captain=npc_nav.add_colony_ship(7,{0,0},120,4);
    npc_nav.provision_colony_genesis(captain,2000,2000,2000,2000,2000);
    npc_nav.enable_individual_workforce();
    CHECK(npc_nav.npc_records().size()==4);
    npc_nav.enable_native_3d_physics();
    npc_nav.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto foreign_station=npc_nav.add_station_in_system(2,{-750,0});
    CHECK(npc_nav.queue_npc_station_docking(
        7,captain,0,foreign_station,50012,NpcFlightPurpose::Rescue));
    CHECK(Simulation::from_snapshot(npc_nav.canonical_state()).canonical_state()
          ==npc_nav.canonical_state());
    for(int step=0;step<3;++step)npc_nav.step();
    CHECK(npc_nav.npc_flight_orders().at(50012).phase!=NpcFlightPhase::Cancelled);
    CHECK(npc_nav.npc_flight_orders().at(50012).local_commands>0);
    CHECK(Simulation::from_snapshot(npc_nav.canonical_state()).canonical_state()
          ==npc_nav.canonical_state());

    // The v121 probe has one immutable launcher custody but may discover a
    // mineral physically within scanning range beyond a catalogue boundary.
    Simulation survey(4814);
    survey.add_system(2);
    const auto explorer=survey.add_colony_ship(7,{0,0},60,24);
    survey.enable_individual_workforce();
    survey.provision_starter_exploration_scanner(explorer);
    survey.provision_starter_probe_kit(explorer);
    survey.enable_native_3d_physics();
    survey.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto foreign=survey.add_asteroid_in_system(2,{-750,0},31);
    survey.enable_native_3d_probes();
    CHECK(survey.submit(command(50020,explorer,1,Action::DeploySurveyProbe)));
    survey.step();
    CHECK(survey.events().back().result==Outcome::Applied);
    CHECK(survey.submit(command(50021,explorer,2,
        Action::ScanSurveyProbe,50020)));
    survey.step();
    CHECK(survey.events().back().result==Outcome::Applied);
    CHECK(survey.pending_probe_report_count()>=1);
    CHECK(Simulation::from_snapshot(survey.canonical_state()).state_hash()
          ==survey.state_hash());
    for(int tick=0;tick<30;++tick)survey.step();
    const auto observed=survey.discoveries().find({7,foreign});
    CHECK(observed!=survey.discoveries().end() &&
          observed->second.system==2 &&
          observed->second.galactic_authoritative &&
          observed->second.galactic_location==
              survey.find(foreign)->physical_galactic);
    CHECK(Simulation::from_snapshot(survey.canonical_state()).canonical_state()
          ==survey.canonical_state());
}
// SPACE-04I: exact physical outpost genesis is source-authoritative even
// when its local XY compatibility field crosses a galactic sector. The
// nearest station is checked in the real world, not a catalogue label.
// SPACE-05K: cross-sector physical interaction integrity. A catalogue
// grouping may never grant a remote, server-authorized ship destruction.
// The only durable wreck is the same physical entity, retaining cargo and
// owner; no second NPC/ship or free ore appears after snapshot recovery.
static void cross_system_ship_destruction_custody(){
    Simulation world(4817);
    world.add_system(2);
    const auto attacker=world.add_colony_ship(7,{0,0},100,4);
    world.provision_colony_genesis(attacker,1000,1000,1000,1000,1000);
    const auto near_drone=world.add_training_drone({2000,0});
    const auto remote_drone=world.add_training_drone({250000,0});
    world.enable_native_3d_physics();
    const galactic::Point first{{0,1000000},{0,1000000},{0,1000000}};
    const galactic::Point second{{0,1001000},{0,1000000},{0,1000000}};
    world.enable_native_galactic_physics({{1,first},{2,second}});
    // A genuine nearby resource is allowed to cross the neighbouring
    // star's catalogue; the unrelated remote hull is not.
    const auto foreign_rock=world.add_asteroid_in_system(2,{-750,0},12);
    CHECK(world.same_local_frame(attacker,foreign_rock));
    CHECK(world.find(attacker)->system_id!=world.find(foreign_rock)->system_id);
    // Physically cross an actual fold-gate: the attacker is now filed under
    // star TWO while the nearby drone is still filed under star ONE. Their
    // absolute 3D separation is within normal weapon range.
    const auto passage=world.add_gate(1,{0,0},2,{0,0});
    CHECK(world.submit(command(62000,attacker,world.tick()+1,
                               Action::RequestJump,passage)));
    world.step();
    CHECK(world.events().back().result==Outcome::Applied);
    bool completed_jump=false;
    for(int i=0;i<100;++i){
        const auto* vessel=world.find(attacker);
        if(vessel->system_id==2 && vessel->jump_state==ShipJumpState::Ready){
            completed_jump=true;break;
        }
        world.step();
    }
    CHECK(completed_jump);
    CHECK(world.find(attacker)->system_id==2 &&
          world.find(near_drone)->system_id==1 &&
          world.same_local_frame(attacker,near_drone));
    const auto separation=galactic::nearby_displacement(
        world.find(attacker)->physical_galactic,
        world.find(near_drone)->physical_galactic);
    if(!separation || std::llabs(separation->x_mm)>=12000 ||
       std::llabs(separation->y_mm)>=12000 ||
       std::llabs(separation->z_mm)>=12000){
        std::cerr<<"SPACE05K original post-gate separation diagnostic: "
                 <<"ship system="<<world.find(attacker)->system_id
                 <<" drone system="<<world.find(near_drone)->system_id
                 <<" separation_mm="<<(separation?std::to_string(separation->x_mm):"overflow")
                 <<","<<(separation?std::to_string(separation->y_mm):"overflow")
                 <<","<<(separation?std::to_string(separation->z_mm):"overflow")
                 <<" ship_local_x="<<world.find(attacker)->position.x
                 <<" drone_local_x="<<world.find(near_drone)->position.x<<"\n";
        CHECK(false && "cross-system physical separation not within weapon reach");
    }
    const auto ore=world.total_ore();
    const auto entities=world.entities().size();
    const auto npcs=world.npc_records().size();
    const auto id_before=world.find(near_drone)->id;
    CHECK(world.find(near_drone)->cargo==6 &&
          world.find(remote_drone)->cargo==6);
    const auto fire=[&](std::uint64_t id,EntityId target){
        Command c=command(id,attacker,world.tick()+1,
                          Action::HostDestroyShip,target);
        c.issuer=Simulation::kServerAuthority;
        CHECK(world.submit(c));
        world.step();
        CHECK(world.events().back().command_id==id);
        return world.events().back().result;
    };
    CHECK(fire(62010,remote_drone)==Outcome::OutOfRange);
    CHECK(world.find(remote_drone)->type==EntityType::Ship &&
          world.find(remote_drone)->cargo==6 &&
          world.entities().size()==entities && world.total_ore()==ore);
    CHECK(fire(62011,near_drone)==Outcome::Applied);
    CHECK(world.find(near_drone)->type==EntityType::Wreck &&
          world.find(near_drone)->id==id_before &&
          world.find(near_drone)->cargo==6 &&
          world.find(near_drone)->owner_id==8 &&
          world.total_ore()==ore &&
          world.entities().size()==entities &&
          world.npc_records().size()==npcs);
    CHECK(fire(62012,near_drone)==Outcome::InvalidTarget);
    CHECK(world.total_ore()==ore &&
          world.entities().size()==entities);
    const auto bytes=world.canonical_state();
    const auto replay=Simulation::from_snapshot(bytes);
    CHECK(replay.canonical_state()==bytes &&
          replay.find(near_drone)->type==EntityType::Wreck &&
          replay.find(near_drone)->cargo==6 &&
          replay.find(remote_drone)->type==EntityType::Ship &&
          replay.total_ore()==ore &&
          replay.entities().size()==entities &&
          replay.npc_records().size()==npcs);
}

static void cross_system_outpost_genesis(){
    Simulation genesis(4912);
    genesis.add_system(2);
    const auto ship=genesis.add_colony_ship(7,{0,0},60,24);
    const auto rock=genesis.add_asteroid({1800,0},40);
    genesis.enable_native_3d_physics();
    constexpr auto edge=galactic::kSectorEdgeMm;
    const galactic::Point star1{{55,edge-500},{10,1000},{12,1000}};
    const galactic::Point star2{{56,500},{10,1000},{12,1000}};
    genesis.enable_native_galactic_physics({{1,star1},{2,star2}});
    const auto genesis_state=genesis.canonical_state();
    auto prepare=[&](Simulation& simulation){
        auto run=[&](std::uint64_t id,Action action,
                     EntityId target,std::int64_t amount){
            CHECK(simulation.submit(command(id,ship,simulation.tick()+1,
                                            action,target,0,0,amount)));
            simulation.step();
            CHECK(simulation.events().back().result==Outcome::Applied);
        };
        run(61001,Action::Mine,rock,16);
        run(61002,Action::RefineOre,0,16);
        run(61003,Action::FabricateModule,0,4);
        CHECK(simulation.colony_ship(ship)->modules==4);
    };

    // Real star/system membership is seeded only at genesis, never after
    // gameplay begins. Competing environments replay identical ore work.
    Simulation blocked=Simulation::from_snapshot(genesis_state);
    const auto foreign=blocked.add_station_in_system(2,{250,0});
    CHECK(blocked.find(foreign)->physical_galactic.x.sector==56);
    prepare(blocked);
    const auto original_ore=blocked.total_ore();
    const auto original_crew=blocked.colony_ship(ship)->crew;
    CHECK(blocked.submit(command(61004,ship,blocked.tick()+1,
        Action::ConstructOutpost,0,0,0,1)));
    blocked.step();
    CHECK(blocked.events().back().result==Outcome::InvalidTarget);
    CHECK(blocked.built_outposts().empty() &&
          blocked.colony_ship(ship)->modules==4 &&
          blocked.colony_ship(ship)->crew==original_crew &&
          blocked.total_ore()==original_ore);
    CHECK(Simulation::from_snapshot(blocked.canonical_state())
          .canonical_state()==blocked.canonical_state());

    Simulation built=Simulation::from_snapshot(genesis_state);
    prepare(built);
    const auto ship_location=built.find(ship)->physical_galactic;
    const auto exact=galactic::advance(ship_location,{1000,0,0});
    CHECK(exact && exact->x.sector==56 && exact->x.millimeters==500);
    CHECK(built.submit(command(61005,ship,built.tick()+1,
        Action::ConstructOutpost,0,0,0,1)));
    built.step();
    CHECK(built.events().back().result==Outcome::Applied);
    CHECK(built.built_outposts().size()==1);
    const auto outpost=built.built_outposts().begin()->first;
    CHECK(built.find(outpost)->physical_galactic==*exact &&
          built.find(outpost)->system_id==1 &&
          built.colony_ship(ship)->modules==0 &&
          built.colony_ship(ship)->crew==original_crew &&
          built.total_ore()==original_ore);
    CHECK(Simulation::from_snapshot(built.canonical_state()).canonical_state()
          ==built.canonical_state());
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
    // SGDX-02 original-world integration: two REAL denied native workers
    // must produce two separate private records, never game-state changes.
    const fs::path diagnostic_dir=db+".diagnostics";
    CHECK(fs::is_directory(diagnostic_dir));
    std::size_t dx_failures=0;
    std::set<std::string> reported_cells;
    for(const auto& entry:fs::directory_iterator(diagnostic_dir)){
        if(!entry.is_regular_file() ||
           entry.path().filename().string().rfind("process-",0)!=0 ||
           entry.path().extension()!=".json")continue;
        std::ifstream evidence(entry.path(),std::ios::binary);
        CHECK(evidence.good());
        const std::string record((std::istreambuf_iterator<char>(evidence)),
                                 std::istreambuf_iterator<char>());
        CHECK(record.find("\"schema\":\"SGDX-02\"")!=std::string::npos &&
              record.find("\"kind\":\"worker_failure\"")!=std::string::npos &&
              record.find("\"role\":\"native_cell_worker\"")!=std::string::npos &&
              record.find("\"reason\":\"preflight_failed\"")!=std::string::npos &&
              record.find("\"last_world_generation\":"+
                          std::to_string(baseline))!=std::string::npos);
        if(record.find(spatial::scope(*cell1))!=std::string::npos)
            reported_cells.insert(spatial::scope(*cell1));
        if(record.find(spatial::scope(*cell2))!=std::string::npos)
            reported_cells.insert(spatial::scope(*cell2));
        ++dx_failures;
    }
    CHECK(dx_failures==2 && reported_cells.size()==2);
    CHECK(w.generation()==baseline); // DX never becomes another writer
    spatial::NativeMultiCellSupervisor supervisor(db,w);
    supervisor.register_worker(l1);supervisor.register_worker(l2);
    supervisor.use_native_process_workers(worker_binary);
    CHECK(supervisor.worker_count()==2);
    // Command admission is also globally leased, not merely the tick path.
    // An invalid stale command cannot strand a speculative writer claim.
    const auto unmodified=w.generation();
    auto stale_command=command(8001,ship2,w.world().tick(),
                               Action::Thrust,0,0,0,1);
    CHECK(!supervisor.submit_original_command(
        stale_command,"rejected-command"));
    CHECK(w.generation()==unmodified &&
          !w.world().has_command_id(8001));
    CHECK(supervisor.submit_original_command(
        command(8002,ship1,w.world().tick()+1,Action::Thrust,0,0,0,1),
        "accepted-command"));
    CHECK(w.generation()==unmodified+1 &&
          w.world().has_command_id(8002));
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
    // A new supervisor instance reuses the exact durable failed claim
    // rather than inventing another tick or taking another worker's turn.
    spatial::NativeMultiCellSupervisor restarted(db,w);
    restarted.register_worker(l1);restarted.register_worker(l2);
    restarted.use_native_process_workers(worker_binary);
    const auto retry=restarted.resume_pending_tick();
    CHECK(retry && retry->dispatch_id=="supervised-2" &&
          retry->world_generation==generation+1 &&
          w.world().total_ore()==ore &&
          !restarted.resume_pending_tick());
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
// SPACE-05G: sustained *original-world* four-cell dispatch. This test
// exercises the existing immutable-authority OS workers, one SQLite writer,
// failpoint/recovery and repeated supervisor restarts. It must not describe
// itself as four independent parallel physical simulation writers.
static void sustained_multi_cell_endurance(const fs::path& root,
    const std::string& native_worker, int endurance_ticks){
    CHECK(endurance_ticks==256 || endurance_ticks==1024);
    const auto db=(root/"endurance.sqlite").string();
    Simulation simulation(77113);
    std::vector<EntityId> ships;
    for(int i=0;i<4;++i){
        const auto ship=simulation.add_colony_ship(
            static_cast<PlayerId>(20+i),
            {static_cast<std::int64_t>(i)*200000000,0},100,8);
        // Prove finite ore and NPC identities through 256 or 1024 ticks;
        // provide enough ordinary life support that this regression tests
        // worker conservation, not intentionally induced starvation.
        simulation.provision_colony_genesis(
            ship,100000,100000,100000,100000,100000);
        simulation.add_asteroid(
            {static_cast<std::int64_t>(i)*200000000+1000,0},100);
        ships.push_back(ship);
    }
    PersistentWorld w(db,std::move(simulation));
    w.enable_native_3d_physics();
    CHECK(galactic::register_original_system(db,w,1,
        galactic::Point{{12,1234},{34,5678},{56,9012}},
        "space-05g-original-star",true));
    w.enable_native_galactic_physics(db);
    w.enable_native_3d_probes();
    const auto baseline_ore=w.world().total_ore();
    const auto baseline_npcs=w.world().npc_records().size();
    const auto baseline_entities=w.world().entities().size();
    // Match original identities, not just totals, across independent SQLite recovery.
    std::set<std::uint64_t> baseline_npc_ids;
    for(const auto& item:w.world().npc_records())
        baseline_npc_ids.insert(item.first);
    std::set<EntityId> baseline_entity_ids;
    for(const auto& item:w.world().entities())
        baseline_entity_ids.insert(item.first);
    CHECK(baseline_npc_ids.size()==baseline_npcs &&
          baseline_entity_ids.size()==baseline_entities);
    std::vector<spatial::WorkerLease> leases;
    for(int i=0;i<4;++i){
        const auto cell=spatial::ship_cell(w.world(),ships[i]);
        CHECK(cell && std::none_of(leases.begin(),leases.end(),
            [&](const spatial::WorkerLease& lease){
                return lease.cell==*cell;
            }));
        const std::string id="endurance-"+std::to_string(i);
        const auto region_scope=spatial::scope(*cell);
        regional_events::CheckpointStore checkpoint(db,region_scope);
        regional_events::Scheduler scheduler;
        scheduler.add_colony("physical-"+id,cold_colony::State{});
        CHECK(checkpoint.save(scheduler,0)==1);
        regional_events::RegionalModeAuthority authority(checkpoint);
        const auto regional=authority.enroll();
        leases.push_back(spatial::transfer_cell_worker(db,*cell,id,0,
            regional.generation,regional.checkpoint_revision,
            "endurance-lease-"+id,true));
    }
    auto supervisor=std::make_unique<spatial::NativeMultiCellSupervisor>(db,w);
    const auto attach=[&](spatial::NativeMultiCellSupervisor& host){
        host.use_native_process_workers(native_worker);
        for(const auto& lease:leases)host.register_worker(lease);
        CHECK(host.worker_count()==4);
    };
    attach(*supervisor);
    std::map<std::string,std::size_t> selected_cells;
    std::size_t deliberate_rollbacks=0;
    std::size_t restarted_hosts=0;
    std::size_t independently_recovered=0;
    std::vector<double> tick_ms;
    tick_ms.reserve(static_cast<std::size_t>(endurance_ticks));
    for(int i=0;i<endurance_ticks;++i){
        if(i>0 && i%32==0){
            ++restarted_hosts;
            supervisor.reset(); // actual host object/worker selector restart
            supervisor=std::make_unique<spatial::NativeMultiCellSupervisor>(db,w);
            attach(*supervisor);
        }
        const auto initial_generation=w.generation();
        const auto initial_tick=w.world().tick();
        const auto initial_state=w.world().canonical_state();
        const auto start=std::chrono::steady_clock::now();
        const auto dispatch="endurance-"+std::to_string(i);
        spatial::NativeMultiCellSupervisor::Committed completed;
        if(i%37==17){
            ++deliberate_rollbacks;
            denied([&]{
                (void)supervisor->run_next_tick(dispatch,
                    FailPoint::AfterSnapshotWrite);
            });
            CHECK(w.generation()==initial_generation &&
                  w.world().tick()==initial_tick &&
                  w.world().canonical_state()==initial_state);
            const auto replay=supervisor->resume_pending_tick();
            CHECK(replay && replay->dispatch_id==dispatch);
            completed=*replay;
            CHECK(!supervisor->resume_pending_tick());
        }else{
            completed=supervisor->run_next_tick(dispatch);
        }
        tick_ms.push_back(std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count());
        ++selected_cells[completed.scope];
        // Every completion must come from one of the four authorized leases.
        CHECK(std::any_of(leases.begin(),leases.end(),
            [&](const spatial::WorkerLease& lease){
                return completed.scope==spatial::scope(lease.cell) &&
                       completed.worker==lease.worker;
            }));
        CHECK(w.generation()==initial_generation+1 &&
              w.world().tick()==initial_tick+1 &&
              completed.world_generation==w.generation() &&
              w.world().total_ore()==baseline_ore &&
              w.world().npc_records().size()==baseline_npcs &&
              w.world().entities().size()==baseline_entities);
        for(const auto ship:ships){
            CHECK(w.world().find(ship));
        }
        // Checkpointed independent native SQLite recovery while the
        // original supervisor continues to serve other cells.
        if((i+1)%64==0){
            const auto copy=(root/("endurance-"+std::to_string(i)+".sqlite")).string();
            PersistentWorld::recover_copy(db,copy);
            ++independently_recovered;
            PersistentWorld recovered(copy,Simulation(1));
            CHECK(recovered.generation()==w.generation() &&
                  recovered.world().canonical_state()==w.world().canonical_state());
            // Retain the exact named NPC/object identities across restart.
            std::set<std::uint64_t> recovered_npc_ids;
            for(const auto& item:recovered.world().npc_records())
                recovered_npc_ids.insert(item.first);
            std::set<EntityId> recovered_entity_ids;
            for(const auto& item:recovered.world().entities())
                recovered_entity_ids.insert(item.first);
            CHECK(recovered_npc_ids==baseline_npc_ids &&
                  recovered_entity_ids==baseline_entity_ids);
        }
    }
    CHECK(selected_cells.size()==4);
    for(const auto& item:selected_cells)CHECK(item.second>0);
    CHECK(restarted_hosts==static_cast<std::size_t>((endurance_ticks-1)/32));
    CHECK(independently_recovered==static_cast<std::size_t>(endurance_ticks/64));
    CHECK(deliberate_rollbacks==
          static_cast<std::size_t>((endurance_ticks-1-17)/37+1));
    std::sort(tick_ms.begin(),tick_ms.end());
    std::cout<<"SPACE-05G original four-cell sustained native tick count="
             <<tick_ms.size()<<" p50_ms="<<tick_ms[tick_ms.size()/2]
             <<" p95_ms="<<tick_ms[tick_ms.size()*95/100]
             <<" max_ms="<<tick_ms.back()
             <<" no duplicated ships/NPCs/ore, host_restarts="<<restarted_hosts
             <<" deliberate_rollbacks="<<deliberate_rollbacks
             <<" independent_sqlite_recoveries="<<independently_recovered
             <<" cell_selections=";
    for(const auto& item:selected_cells)
        std::cout<<item.first<<":"<<item.second<<";";
    std::cout<<"\n";
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
        const auto first_probe=spatial::run_cell_process(argv[1],task);
        if(!first_probe.passed)throw std::runtime_error(
            "initial native cell worker: "+first_probe.reason);
        CHECK(!spatial::run_cell_process(argv[2],task,
            std::chrono::milliseconds(50)).passed);
        galaxy_and_probes(root);
        anchored_nonships_and_swap();
        cross_system_physical_contact();
        cross_system_ship_destruction_custody();
        cross_system_outpost_genesis();
        multi_cell_supervision(root,argv[1],argv[2]);
        int endurance_ticks=256;
        if(const char* selected=std::getenv("SPACE_ENDURANCE_TICKS")){
            const std::string value(selected);
            CHECK(value=="256" || value=="1024");
            endurance_ticks=(value=="1024")?1024:256;
        }
        sustained_multi_cell_endurance(root,argv[1],endurance_ticks);
        std::cout<<"PASS exact original galactic 3D, probe persistence and "
                    "durable multi-cell single-writer supervision\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL SPACE-04C/04D/05B: "<<e.what()<<'\n';return 1;
    }
}
