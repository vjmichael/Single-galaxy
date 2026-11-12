// S6-F5-E subprocess acceptance fixture: construct a genuinely landed
// ORIGINAL PlayerPerson, then reopen/inspect (never rewrite) its SQLite world.
// Test helper only. Not exposed by installed gameplay or a planet genesis path.
#include "galaxy/Persistence.hpp"
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(\
 "S6-F5-E native fixture line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice{std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
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
    throw std::runtime_error("No actual buildable surveyed planet");
}
static void act(PersistentWorld& w,EntityId ship,std::uint64_t id,
                Action kind,EntityId target=0,std::int64_t quantity=1){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.action=kind;c.target=target;c.amount=quantity;
    c.execute_tick=w.world().tick()+1;
    REQUIRE(w.submit(c));
    w.step();
    REQUIRE(w.world().events().back().command_id==id &&
            w.world().events().back().result==Outcome::Applied);
}
static void create_landed_world(const std::string& file,bool land=true){
    REQUIRE(!fs::exists(fs::u8path(file)));
    const auto selected=choose();
    const PlanetarySiteKey key{selected.body,selected.lat,selected.lon,1};
    Simulation seed(613,selected.system);
    const auto ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.provision_starter_exploration_scanner(ship);
    const auto rock=seed.add_asteroid({2000,0},60);
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    PersistentWorld world(file,std::move(seed));
    REQUIRE(world.register_player_person(7,ship));
    act(world,ship,1001,Action::Mine,rock,30);
    act(world,ship,1002,Action::RefineOre,0,30);
    act(world,ship,1003,Action::FabricateModule,0,8);
    REQUIRE(world.begin_celestial_transit(7,ship,selected.body,90001));
    world.steps(4);
    REQUIRE(world.complete_celestial_transit(90001));
    std::vector<std::uint64_t> crew;
    for(const auto& [id,npc]:world.world().npc_records())
        if(npc.home_ship==ship && npc.location==ship &&
           npc.role==NpcRole::Ship)crew.push_back(id);
    REQUIRE(crew.size()>=4);
    REQUIRE(world.build_planetary_site(7,ship,selected.body,selected.lat,
                                     selected.lon,crew[0],crew[1]));
    world.steps(4);
    REQUIRE(world.support_planetary_crew(7,ship,selected.body,selected.lat,
                                       selected.lon,false,10));
    REQUIRE(world.support_planetary_crew(7,ship,selected.body,selected.lat,
                                       selected.lon,true,10));
    world.steps(240);
    REQUIRE(world.world().planetary_spaceport_operational(
        selected.body,selected.lat,selected.lon));
    REQUIRE(world.build_planetary_shuttle(7,ship,key));
    REQUIRE(world.assign_planetary_shuttle_pilot(7,ship,key,crew[2]));
    REQUIRE(world.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    if(!land){
        REQUIRE(world.world().player_people().at(7).state==
                PlayerPersonState::AboardOwnShip);
        return; // original real constructed site and orbit shuttle, NO landing
    }
    REQUIRE(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Board,81101));
    REQUIRE(world.launch_planetary_shuttle(7,ship,key,81234,false));
    world.steps(3);
    REQUIRE(world.planetary_player_shuttle_action(
        7,ship,key,PlanetaryPlayerAction::Disembark,81102));
    const auto& player=world.world().player_people().at(7);
    REQUIRE(player.state==PlayerPersonState::AtPlanetarySurface);
    REQUIRE(player.planet_body==selected.body);
    REQUIRE(player.planet_descent_receipt!=0);
}
static void inspect(const std::string& file){
    REQUIRE(fs::exists(fs::u8path(file)));
    PersistentWorld world(file,Simulation(0xC021));
    const auto& original=world.world();
    const auto& p=original.player_people().at(7);
    REQUIRE(p.state==PlayerPersonState::AtPlanetarySurface);
    REQUIRE(p.planet_descent_receipt!=0);
    std::cout<<"CHECK|"<<original.tick()<<'|'<<world.generation()<<'|'
             <<p.surface_survival.last_vertical_tick<<'|'
             <<p.surface_pose.last_move_receipt<<'|'
             <<p.surface_pose.x_mm<<'|'<<p.surface_pose.y_mm<<'|'
             <<p.surface_pose.z_mm<<'|'<<original.state_hash()<<"\n";
}
static void inspect_transfer(const std::string& file) {
    REQUIRE(fs::exists(fs::u8path(file)));
    PersistentWorld world(file,Simulation(0xC021));
    const auto& p=world.world().player_people().at(7);
    REQUIRE(p.home_ship>0);
    std::uint64_t phase=0,site_id=0;
    for(const auto& [key,shuttle]:world.world().planetary_shuttles()){
        if(shuttle.source_ship!=p.home_ship||shuttle.owner!=7)continue;
        phase=shuttle.flight_phase;
        const auto it=world.world().planetary_sites().find(key);
        REQUIRE(it!=world.world().planetary_sites().end());
        site_id=it->second.surface_location;
        break;
    }
    REQUIRE(site_id>0);
    std::cout<<"TRANSFER|"<<unsigned(p.state)<<'|'<<phase<<'|'
        <<world.world().tick()<<'|'<<world.generation()<<'|'
        <<p.home_ship<<'|'<<p.planet_body<<'|'
        <<p.planet_descent_receipt<<'|'<<p.planet_ascent_receipt<<'|'
        <<world.world().total_ore()<<'|'<<world.world().state_hash()<<"\n";
}
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::invalid_argument("usage: fixture --seed|--seed-orbit|--inspect|--inspect-transfer PATH");
        const std::string mode=argv[1],file=argv[2];
        if(file.empty()||file.size()>1024)
            throw std::invalid_argument("invalid fixture SQLite path");
        if(mode=="--seed"){
            create_landed_world(file);
            inspect(file);
        }else if(mode=="--seed-orbit"){
            create_landed_world(file,false);
            inspect_transfer(file);
        }else if(mode=="--inspect")inspect(file);
        else if(mode=="--inspect-transfer")inspect_transfer(file);
        else throw std::invalid_argument("unknown fixture mode");
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"S6-F5-E fixture: "<<e.what()<<'\n';
        return 1;
    }
}
