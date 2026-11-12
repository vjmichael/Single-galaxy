#include "galaxy/Stage6GodotPacket.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"
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
