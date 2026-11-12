#include "galaxy/Stage6GodotPacket.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
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
int main(){
try {
    const auto selected=choose();
    const PlanetarySiteKey key{selected.body,selected.lat,selected.lon,1};
    const auto dir=fs::temp_directory_path()/
        ("sg_stage6_landing_"+std::to_string(std::chrono::steady_clock::now().
                                          time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}}
        cleanup{dir};
    const auto db=(dir/"original.sqlite").string();
    Simulation seed(613,selected.system);
    const auto ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.provision_starter_exploration_scanner(ship);
    const auto rock=seed.add_asteroid({2000,0},60);
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    const auto ore=seed.total_ore();
    PersistentWorld world(db,std::move(seed));
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
    CHECK(!stage6::observe_native_landed_shuttle(world,7,ship,key));
    CHECK(!stage6::observe_native_landed_shuttle(world,8,ship,key));
    CHECK(!stage6::observe_native_landed_shuttle(
        world,7,ship,PlanetarySiteKey{selected.body,selected.lat,
                                     std::uint16_t(selected.lon+1),1}));
    const auto before=world.world().state_hash();
    CHECK(!world.launch_planetary_shuttle(8,ship,key,81234,false));
    CHECK(world.world().state_hash()==before);
    CHECK(world.launch_planetary_shuttle(7,ship,key,81234,false));
    CHECK(!stage6::observe_native_landed_shuttle(world,7,ship,key));
    world.steps(2);
    CHECK(!stage6::observe_native_landed_shuttle(world,7,ship,key));
    world.step();
    CHECK(world.world().planetary_shuttles().at(key).flight_phase==2);
    const auto landed=stage6::observe_native_landed_shuttle(world,7,ship,key);
    CHECK(landed && landed->original_shuttle_landed &&
          landed->pilot_npc==crew[2] &&
          landed->original_descent_receipt==81234 &&
          landed->original_surface_location==
              world.world().planetary_sites().at(key).surface_location &&
          landed->surveyed.original_body_id==selected.body &&
          landed->still_current(world));
    // S6-F: the actual landed NPC shuttle may be OBSERVED in a bounded
    // Godot tile packet; owner/body/region are never a new player travel grant.
    const galaxy::stage6::RenderTileAddress landing_tile{
        static_cast<std::uint8_t>(std::min<unsigned>(selected.lat/7,stage6::kLatitudeBands-1)),
        static_cast<std::uint8_t>(selected.lon/6),1};
    const auto packet=stage6::original_godot_mesh_packet(
        world,7,ship,selected.body,{landing_tile},key);
    CHECK(packet &&
          packet->find(R"("landing_verified":true)")!=std::string::npos &&
          packet->find(R"("original_descent_receipt":"81234")")!=std::string::npos &&
          packet->find("\"shuttle_pilot_npc\":")!=std::string::npos &&
          packet->find("\"surface_location\":")!=std::string::npos);
    CHECK(!stage6::original_godot_mesh_packet(
        world,8,ship,selected.body,{landing_tile},key));
    CHECK(!stage6::original_godot_mesh_packet(
        world,7,ship,selected.body,{landing_tile},
        PlanetarySiteKey{selected.body,selected.lat,selected.lon,2}));
    CHECK(world.world().total_ore()==ore);
    const auto copy=(dir/"recovered.sqlite").string();
    PersistentWorld::recover_copy(db,copy);
    {
        PersistentWorld independently_restored(copy,Simulation(613));
        const auto copy_landing=stage6::observe_native_landed_shuttle(
            independently_restored,7,ship,key);
        CHECK(copy_landing && copy_landing->original_descent_receipt==81234 &&
              copy_landing->original_surface_location==
                  landed->original_surface_location &&
              independently_restored.world().total_ore()==ore);
        CHECK(!stage6::observe_native_landed_shuttle(
            independently_restored,8,ship,key));
    }
    CHECK(world.launch_planetary_shuttle(7,ship,key,81235,true));
    CHECK(!landed->still_current(world));
    CHECK(!stage6::observe_native_landed_shuttle(world,7,ship,key));
    CHECK(!stage6::original_godot_mesh_packet(
        world,7,ship,selected.body,{landing_tile},key));
    world.steps(3);
    CHECK(world.world().planetary_shuttles().at(key).flight_phase==0);
    CHECK(!stage6::observe_native_landed_shuttle(world,7,ship,key));
    CHECK(world.world().total_ore()==ore);
    std::cout<<"STAGE6-C original owner-authorized descent, actual pilot/surface "
                "custody and one-use receipt, return, SQLite recovery PASS\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"STAGE6-C FAIL: "<<e.what()<<'\n';return 1;
}
}
