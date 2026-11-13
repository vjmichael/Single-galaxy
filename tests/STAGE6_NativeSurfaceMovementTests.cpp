#include "galaxy/Stage6GodotPacket.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"
#include "galaxy/PlanetaryBodyGravity.hpp"
#include "galaxy/PlanetaryVerticalPhysics.hpp"
#include "galaxy/PlanetaryAtmosphere.hpp"
#include "galaxy/Stage6WalkingPatch.hpp"
#include "galaxy/Stage6ConsolidatedAdmission.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    "STAGE6-C native line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice {std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
static Choice choose(){
    for(std::uint32_t system=1;system<=2000;++system){
        Simulation candidate(613,system);
        const auto ship=candidate.add_colony_ship(7,{0,0},60,8);
        candidate.provision_starter_exploration_scanner(ship);
        candidate.ensure_physical_celestial_systems();
        for(const auto& body:candidate.physical_celestial_systems().at(system)){
            if(body.kind!=CelestialKind::Planet)continue;
            for(const auto [lat,lon]:std::vector<std::pair<int,int>>{
                {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27}}){
                const auto s=candidate.scan_planetary_geography(
                    7,ship,body.id,std::uint16_t(lat),std::uint16_t(lon));
                if(s && !s->landscape.region.surface_water)
                    return {system,body.id,std::uint16_t(lat),std::uint16_t(lon)};
            }
        }
    }
    throw std::runtime_error("No original buildable planetary surface found");
}
static void act(PersistentWorld& w,EntityId ship,std::uint64_t id,
                Action kind,EntityId target=0,std::int64_t quantity=1){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.action=kind;c.target=target;c.amount=quantity;
    c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
int main() {
try {
    const auto selected=choose();
    const PlanetarySiteKey key{selected.body,selected.lat,selected.lon,1};
    const auto dir=fs::temp_directory_path()/
        ("sg_stage6f5_surface_"+std::to_string(std::chrono::steady_clock::now().
                                          time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}}
        cleanup{dir};
    const auto db=(dir/"original-player.sqlite").string();
    Simulation seed(613,selected.system);
    const auto ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.provision_starter_exploration_scanner(ship);
    const auto rock=seed.add_asteroid({2000,0},60);
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    seed.enable_native_galactic_physics({
        {selected.system,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    // Every v127+ snapshot must carry the complete immutable planet
    // catalogue before any transport/ground state can promote the schema.
    seed.ensure_planetary_genesis_manifest();
    const auto origin=seed.total_ore();
    PersistentWorld world(db,std::move(seed));
    CHECK(world.register_player_person(7,ship));
    CHECK(!world.register_player_person(8,ship));
    act(world,ship,1001,Action::Mine,rock,30);
    act(world,ship,1002,Action::RefineOre,0,30);
    act(world,ship,1003,Action::FabricateModule,0,8);
    CHECK(world.begin_celestial_transit(7,ship,selected.body,90001));
    world.steps(4);
    CHECK(world.complete_celestial_transit(90001));
    std::vector<std::uint64_t> crew;
    for(const auto& [id,npc]:world.world().npc_records())
        if(npc.home_ship==ship && npc.location==ship &&
           npc.role==NpcRole::Ship)crew.push_back(id);
    CHECK(crew.size()>=4);
    CHECK(world.build_planetary_site(7,ship,selected.body,selected.lat,
                                    selected.lon,crew[0],crew[1]));
    world.steps(4);
    CHECK(world.support_planetary_crew(7,ship,selected.body,selected.lat,
                                      selected.lon,false,10));
    CHECK(world.support_planetary_crew(7,ship,selected.body,selected.lat,
                                      selected.lon,true,10));
    world.steps(240);
    CHECK(world.world().planetary_spaceport_operational(
        selected.body,selected.lat,selected.lon));
    CHECK(world.build_planetary_shuttle(7,ship,key));
    CHECK(world.assign_planetary_shuttle_pilot(7,ship,key,crew[2]));
    CHECK(world.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    // Admit original v128-v135 source manifests before native on-foot actions.
    (void)stage6::consolidated::admit(world);
    // No original player was transported just because an NPC pilot exists.
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,82001));
    CHECK(!world.planetary_surface_move(8,ship,key,crew[0],250,0,82001));
    CHECK(!world.planetary_surface_move(7,ship,key,12345678,250,0,82001));
    CHECK(!world.planetary_surface_move(7,ship,key,0,301,0,82001));
    CHECK(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Board,81101));
    CHECK(world.launch_planetary_shuttle(7,ship,key,81234,false));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,82001));
    world.steps(3);
    CHECK(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Disembark,81102));
    CHECK(!world.planetary_surface_move(7,ship,key,0,0,0,82001));
    CHECK(!world.planetary_surface_move(7,ship,key,0,301,0,82001));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,300,82001));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,81102));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,81234));
    const auto before_g=world.generation(), before_h=world.world().state_hash();
    bool fault=false;
    try{
        (void)world.planetary_surface_move(
            7,ship,key,0,300,0,82001,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){fault=true;}
    CHECK(fault && world.generation()==before_g);
    CHECK(world.world().state_hash()==before_h);
    CHECK(world.planetary_surface_move(7,ship,key,0,300,0,82001));
    const auto source=world.world().scan_planetary_geography(
        7,ship,selected.body,selected.lat,selected.lon);
    CHECK(source);
    const auto& onfoot=world.world().player_people().at(7);
    const auto expected_surface_y_mm=onfoot.surface_pose.y_mm;
    CHECK(onfoot.state==PlayerPersonState::AtPlanetarySurface);
    CHECK(onfoot.surface_pose.x_mm==300 && onfoot.surface_pose.z_mm==0);
    CHECK(onfoot.surface_pose.y_mm==
          std::int64_t(source->landscape.region.elevation)*1000);
    CHECK(onfoot.surface_pose.last_move_receipt==82001);
    CHECK(onfoot.surface_pose.surface_location==
          world.world().planetary_sites().at(key).surface_location);
    const auto native_origin=planetary::continuous_surface::cell_center(
        {selected.lat,selected.lon});
    CHECK(native_origin);
    const auto expected_native=planetary::continuous_surface::native_step(
        *native_origin,300,0,
        world.world().celestial_navigation_anchor(
            7,ship,selected.body)->physical_radius_mm);
    CHECK(expected_native);
    CHECK(onfoot.surface_pose.latitude_pdeg==expected_native->latitude_pdeg);
    CHECK(onfoot.surface_pose.longitude_pdeg==expected_native->longitude_pdeg);
    CHECK(world.world().continuous_native_planet_surface());
    CHECK(world.world().original_surface_atmosphere_enabled());
    const stage6::RenderTileAddress mesh_tile{
        static_cast<std::uint8_t>(std::min<unsigned>(selected.lat/7,4)),
        static_cast<std::uint8_t>(selected.lon/6),1};
    const auto observed_packet=stage6::original_godot_mesh_packet(
        world,7,ship,selected.body,{mesh_tile},key);
    CHECK(observed_packet &&
          observed_packet->find(R"("native_surface_pose_verified":true)")!=std::string::npos &&
          observed_packet->find(R"("native_surface_x_mm":"300")")!=std::string::npos &&
          observed_packet->find(R"("native_surface_move_receipt":"82001")")!=std::string::npos);
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,82002));
    CHECK(!world.planetary_surface_move(7,ship,key,crew[2],300,0,82002));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,82001));
    CHECK(!world.planetary_surface_move(8,ship,key,0,300,0,82002));
    // The same actor cannot move twice within a committed original tick.
    // A second actor can be moved only under separate native authorization.
    world.step();
    CHECK(world.planetary_surface_move(7,ship,key,0,300,0,82002));
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,82003));

    const auto in_motion_copy=(dir/"moving.sqlite").string();
    PersistentWorld::recover_copy(db,in_motion_copy);
    {
        PersistentWorld restored(in_motion_copy,Simulation(613));
        const auto& pos=restored.world().player_people().at(7).surface_pose;
        CHECK(pos.x_mm==600 && pos.last_move_receipt==82002);
        CHECK(pos.surface_location==
              world.world().planetary_sites().at(key).surface_location);
        CHECK(pos.longitude_pdeg>native_origin->longitude_pdeg);
        CHECK(pos.latitude_pdeg==native_origin->latitude_pdeg);
        CHECK(restored.world().state_hash()==world.world().state_hash());
        CHECK(!restored.planetary_surface_move(7,ship,key,0,300,0,82002));
    }
    // Original source geometry/hazard guard, not a Godot hit-test result.
    const auto ground=planetary::surface_motion::original_ground_contact(
        *source,nullptr,0,0);
    CHECK(ground && ground->ground_y_mm==expected_surface_y_mm);
    auto dangerous=planetary::PhysicalEnvironment{};
    dangerous.hazard=planetary::PhysicalHazard::Flood;
    CHECK(!planetary::surface_motion::original_ground_contact(
        *source,&dangerous,0,0)->safe_to_walk);
    dangerous.hazard=planetary::PhysicalHazard::Cold;
    CHECK(!planetary::surface_motion::original_ground_contact(
        *source,&dangerous,0,0)->safe_to_walk);
    CHECK(!planetary::surface_motion::original_ground_contact(
        *source,nullptr,32001,0));
    CHECK(!planetary::surface_motion::native_surface_structure_clear(
        10000,0,true,false));
    CHECK(!planetary::surface_motion::native_surface_structure_clear(
        -10000,0,false,true));

    // Move out beyond hatch; remote REBOARD is refused. Return physically.
    for(int i=0;i<5;++i) {
        world.step();
        CHECK(world.planetary_surface_move(7,ship,key,0,300,0,82010+i));
    }
    CHECK(world.world().player_people().at(7).surface_pose.x_mm==2100);
    CHECK(!world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Reboard,81103));
    CHECK(!world.launch_planetary_shuttle(7,ship,key,81235,true));
    for(int i=0;i<7;++i){
        world.step();
        CHECK(world.planetary_surface_move(7,ship,key,0,-300,0,82020+i));
    }
    CHECK(world.world().player_people().at(7).surface_pose.x_mm==0);

    // S6-F5-C: a new canonical body-mass source is adopted ONLY through an
    // original approved SQLite physics tick. No Godot gravity or instant
    // healing/oxygen supply can be supplied by this test caller.
    CHECK(!world.planetary_surface_vertical_action(
        8,ship,key,false,87001));
    const auto pre_vertical_gen=world.generation();
    const auto pre_vertical_hash=world.world().state_hash();
    bool vertical_fault=false;
    try{
        (void)world.planetary_surface_vertical_action(
            7,ship,key,false,87001,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){vertical_fault=true;}
    CHECK(vertical_fault && world.generation()==pre_vertical_gen);
    CHECK(world.world().state_hash()==pre_vertical_hash);
    CHECK(world.planetary_surface_vertical_action(7,ship,key,false,87001));
    CHECK(!world.planetary_surface_vertical_action(7,ship,key,false,87001));
    CHECK(world.world().canonical_state().find("galaxy-kernel-v134|")==0);
    const auto planet=world.world().celestial_navigation_anchor(7,ship,selected.body);
    CHECK(planet && planet->physical_mass_1e18_kg>0);
    const auto g=planetary::gravity::surface_gravity_mm_s2(
        planet->physical_radius_mm,planet->physical_mass_1e18_kg);
    CHECK(g && *g>0 && *g<=50000);
    CHECK(world.world().player_people().at(7).surface_survival.initialized);
    CHECK(world.world().player_people().at(7).surface_survival.grounded);
    CHECK(world.planetary_surface_vertical_action(7,ship,key,true,87002));
    CHECK(!world.world().player_people().at(7).surface_survival.grounded);
    CHECK(world.world().player_people().at(7).surface_survival.radial_speed_mm_s==
          planetary::vertical::kJumpSpeedMmS);
    bool landed_again=false;
    for(int i=0;i<80;++i){
        world.step();
        if(world.world().player_people().at(7).surface_survival.grounded){
            landed_again=true;break;
        }
    }
    CHECK(landed_again);
    CHECK(world.world().player_people().at(7).surface_survival.radial_speed_mm_s==0);
    const auto oxygen_before=world.world().planetary_sites().at(key).oxygen_kg;
    if(oxygen_before>0){
        CHECK(world.planetary_surface_oxygen_action(7,ship,key,false,87003));
        CHECK(world.world().planetary_sites().at(key).oxygen_kg==oxygen_before-1);
        CHECK(world.world().player_people().at(7).surface_survival.oxygen_kg==1);
        CHECK(!world.planetary_surface_oxygen_action(7,ship,key,false,87003));
        CHECK(!world.planetary_player_shuttle_action(
            7,ship,key,PlanetaryPlayerAction::Reboard,81103));
        CHECK(world.planetary_surface_oxygen_action(7,ship,key,true,87004));
        CHECK(world.world().planetary_sites().at(key).oxygen_kg==oxygen_before);
    }else{
        CHECK(!world.planetary_surface_oxygen_action(7,ship,key,false,87003));
    }
    const auto survival_copy=(dir/"v134-survival.sqlite").string();
    PersistentWorld::recover_copy(db,survival_copy);
    {
        PersistentWorld restored(survival_copy,Simulation(613));
        CHECK(restored.world().state_hash()==world.world().state_hash());
        CHECK(restored.world().player_people().at(7).surface_survival.initialized);
        CHECK(restored.world().physical_celestial_systems().at(selected.system)
              .size()==world.world().physical_celestial_systems()
              .at(selected.system).size());
    }
    auto tampered_mass=world.world().canonical_state();
    const auto first_body= tampered_mass.find("celestial|");
    CHECK(first_body!=std::string::npos);
    const auto end_body=tampered_mass.find('\n',first_body);
    CHECK(end_body!=std::string::npos);
    const auto last_separator=tampered_mass.rfind('|',end_body);
    CHECK(last_separator!=std::string::npos && last_separator>first_body);
    tampered_mass.replace(last_separator+1,end_body-last_separator-1,"1234567");
    bool mass_tamper_refused=false;
    try{(void)Simulation::from_snapshot(tampered_mass);}
    catch(const std::exception&){mass_tamper_refused=true;}
    CHECK(mass_tamper_refused);
    // Same exact original world, verified landed PERSON and source survey.
    // Installed render mesh must be source-backed, bounded and non-authority.
    const auto walkpatch=stage6::original_walking_patch(world,7,ship,key);
    CHECK(walkpatch);
    CHECK(walkpatch->find("SG-STAGE6-WALKPATCH-01")!=std::string::npos);
    CHECK(walkpatch->find(R"("width":17)")!=std::string::npos);
    CHECK(!stage6::original_walking_patch(world,8,ship,key));
    const auto tiles=stage6::original_walking_tiles(world,7,ship,key,44001);
    CHECK(tiles && tiles->find("SG-STAGE6-WALKTILES-01")!=std::string::npos);
    CHECK(tiles->find(R"("session":"44001")")!=std::string::npos);
    CHECK(tiles->find(R"("center_x_mm":"0")")!=std::string::npos);
    CHECK(tiles->size()<1000000);
    CHECK(!stage6::original_walking_tiles(world,8,ship,key,44001));
    CHECK(!stage6::original_walking_tiles(world,7,ship,key,0));
    CHECK(!stage6::original_walking_patch(world,7,ship,key,128000,0));

    // Never assume an existing v134 player has atmospheric oxygen simply
    // from a biosphere or life-support tank. The original v135 migration
    // must be source-verified, journaled and recoverable after failpoints.
    const auto v135_file=(dir/"v135-original-atmosphere.sqlite").string();
    PersistentWorld::recover_copy(db,v135_file);
    {
        PersistentWorld native(v135_file,Simulation(613));
        const auto before_migration=native.world().state_hash();
        const auto before_generation=native.generation();
        bool injected=false;
        try{native.ensure_planetary_atmosphere_manifest(
             FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){injected=true;}
        CHECK(injected);
        CHECK(native.world().state_hash()==before_migration);
        CHECK(native.generation()==before_generation);
        native.ensure_planetary_atmosphere_manifest();
        const auto migration_generation=native.generation();
        native.ensure_planetary_atmosphere_manifest();
        CHECK(native.generation()==migration_generation); // idempotent
        CHECK(native.world().original_surface_atmosphere_enabled());
        const auto verified=native.world().celestial_navigation_anchor(
            7,ship,selected.body);
        CHECK(verified);
        const galaxy::planetary::air::State source_air{
            verified->atmosphere_pressure_pa,
            verified->atmosphere_oxygen_permille,
            verified->atmosphere_toxic};
        CHECK(galaxy::planetary::air::valid(source_air));
        const auto denied_before=native.world().state_hash();
        CHECK(!native.planetary_surface_suit_action(7,ship,key,
              Simulation::OriginalSuitAction::Equip,88001));
        CHECK(!native.planetary_surface_treatment(7,ship,key,88002));
        CHECK(native.world().state_hash()==denied_before);
        // Actual original world STEP advances survival without any client
        // PMOVE/PSTEP/JUMP. No separate simulation health record is created.
        const auto previous_tick=native.world().player_people().at(7)
                                    .surface_survival.last_vertical_tick;
        native.step();
        const auto& survival=native.world().player_people().at(7)
                               .surface_survival;
        CHECK(survival.last_vertical_tick>previous_tick);
        const auto hash_after_tick=native.world().state_hash();
        CHECK(Simulation::from_snapshot(native.world().canonical_state())
              .state_hash()==hash_after_tick);
        auto tampered_air=native.world().canonical_state();
        const auto first_celestial=tampered_air.find("celestial|");
        CHECK(first_celestial!=std::string::npos);
        const auto newline=tampered_air.find('\n',first_celestial);
        const auto separator=tampered_air.rfind('|',newline);
        CHECK(newline!=std::string::npos && separator>first_celestial);
        const auto prior_value=tampered_air.substr(
            separator+1,newline-separator-1);
        tampered_air.replace(separator+1,newline-separator-1,
                             prior_value=="0"?"1":"0");
        bool tamper_refused=false;
        try{(void)Simulation::from_snapshot(tampered_air);}
        catch(const std::exception&){tamper_refused=true;}
        CHECK(tamper_refused);
        const auto durable_copy=(dir/"v135-durable-copy.sqlite").string();
        PersistentWorld::recover_copy(v135_file,durable_copy);
        PersistentWorld restored(durable_copy,Simulation(613));
        CHECK(restored.world().state_hash()==native.world().state_hash());
        // Simulated unattended ticks are original journal commits, not
        // client input. A recovered copy must contain the exact last tick,
        // survival delta and source state without offline catch-up.
        const auto start_tick=native.world().tick();
        const auto start_vertical=native.world().player_people().at(7)
                                  .surface_survival.last_vertical_tick;
        for(int i=0;i<12;++i)native.step();
        CHECK(native.world().tick()==start_tick+12);
        CHECK(native.world().player_people().at(7)
              .surface_survival.last_vertical_tick>start_vertical);
        const auto recovered_file=(dir/"native-unattended-recovery.sqlite").string();
        PersistentWorld::recover_copy(v135_file,recovered_file);
        PersistentWorld resumed(recovered_file,Simulation(613));
        CHECK(resumed.world().tick()==native.world().tick());
        CHECK(resumed.generation()==native.generation());
        CHECK(resumed.world().state_hash()==native.world().state_hash());
        const auto resumed_tick=resumed.world().tick();
        resumed.step();
        CHECK(resumed.world().tick()==resumed_tick+1);
        CHECK(resumed.world().player_people().at(7)
              .surface_survival.last_vertical_tick==
              resumed.world().tick());
    }

    CHECK(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Reboard,81103));
    CHECK(world.world().player_people().at(7).surface_pose.last_move_receipt==0);

    // The physically landed original NPC pilot also obeys a hatch radius.
    for(int i=0;i<8;++i){
        world.step();
        CHECK(world.planetary_surface_move(
            7,ship,key,crew[2],250,0,83000+i));
    }
    CHECK(world.world().npc_records().at(crew[2]).surface_pose.x_mm==2000);
    const auto npc_copy=(dir/"pilot-walking.sqlite").string();
    PersistentWorld::recover_copy(db,npc_copy);
    {
        PersistentWorld restored(npc_copy,Simulation(613));
        CHECK(restored.world().npc_records().at(crew[2]).surface_pose.x_mm==2000);
        CHECK(!restored.launch_planetary_shuttle(7,ship,key,81235,true));
    }
    CHECK(!world.launch_planetary_shuttle(7,ship,key,81235,true));
    for(int i=0;i<8;++i){
        world.step();
        CHECK(world.planetary_surface_move(
            7,ship,key,crew[2],-250,0,83010+i));
    }
    CHECK(world.world().npc_records().at(crew[2]).surface_pose.x_mm==0);
    CHECK(world.launch_planetary_shuttle(7,ship,key,81235,true));
    CHECK(world.world().npc_records().at(crew[2]).surface_pose.last_move_receipt==0);
    world.steps(3);
    CHECK(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Exit,81104));
    CHECK(world.world().player_people().at(7).state==
          PlayerPersonState::AboardOwnShip);
    CHECK(!world.planetary_surface_move(7,ship,key,0,300,0,84000));
    CHECK(!world.planetary_surface_move(7,ship,key,crew[2],250,0,84001));
    CHECK(world.world().total_ore()==origin);
    CHECK(Simulation::from_snapshot(world.world().canonical_state()).state_hash()
          ==world.world().state_hash());
    std::cout<<"S6-F5 source-level native player/NPC movement, surface contact, "
             <<"hazard, hatch, replay, failpoint and restart PASS\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"S6-F5 FAIL: "<<e.what()<<'\n';return 1;
}
}
