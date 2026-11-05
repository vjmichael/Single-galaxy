#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
#define REQUIRE(x) do {if(!(x))throw std::runtime_error(std::string(#x));}while(false)
struct SiteChoice {std::uint32_t system; std::uint64_t body; std::uint16_t lat,lon;};
static SiteChoice choose() {
    for(std::uint32_t system=1;system<=2000;++system) {
        Simulation probe(613,system);
        auto ship=probe.add_colony_ship(7,{0,0},60,8);
        probe.provision_starter_exploration_scanner(ship);
        probe.ensure_physical_celestial_systems();
        for(const auto& body:probe.physical_celestial_systems().at(system)) {
            if(body.kind!=CelestialKind::Planet && body.kind!=CelestialKind::Moon)continue;
            for(const auto [lat,lon]:std::vector<std::pair<unsigned,unsigned>>{
                    {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27},
                    {26,52},{7,15},{28,8},{12,40},{24,55},{19,50},{15,64}}) {
                auto region=probe.scan_planetary_geography(7,ship,body.id,
                    std::uint16_t(lat),std::uint16_t(lon));
                if(region && !region->landscape.region.surface_water)
                    return {system,body.id,std::uint16_t(lat),std::uint16_t(lon)};
            }
        }
    }
    throw std::runtime_error("no available catalogued planetary region");
}
int main(int argc,char** argv) {
    try {
        if(argc==10 && std::string(argv[1])=="--verify") {
            // Independent native reopening of the SQLite save after BOTH host
            // processes have ended. No browser or Python mass approximation.
            const std::string database=argv[2];
            const auto body=std::stoull(argv[3]);
            const auto lat=std::stoull(argv[4]),lon=std::stoull(argv[5]);
            const auto pilot=std::stoull(argv[6]);
            const auto mass=std::stoll(argv[7]);
            const auto ship=std::stoull(argv[8]);
            const auto passenger=std::stoull(argv[9]);
            REQUIRE(lat<36 && lon<72);
            PersistentWorld saved(database,Simulation(2));
            const PlanetarySiteKey key{body,std::uint16_t(lat),std::uint16_t(lon),1};
            REQUIRE(saved.world().total_ore()==mass);
            const auto& shuttle=saved.world().planetary_shuttles().at(key);
            REQUIRE(shuttle.flight_phase==0 && shuttle.pilot_npc==pilot);
            REQUIRE(shuttle.source_ship==ship && shuttle.water_kg==138 &&
                    shuttle.food_kg==80 && shuttle.descent_receipt==81001 &&
                    shuttle.ascent_receipt==81002);
            REQUIRE(saved.world().npc_custody(pilot).physical_location==ship);
            REQUIRE(shuttle.passenger_ids==std::vector<std::uint64_t>{passenger});
            REQUIRE(saved.world().npc_custody(passenger).physical_location==ship);
            REQUIRE(!saved.world().npc_available_for_duty(passenger));
            REQUIRE(saved.world().planetary_spaceport_operational(
                body,std::uint16_t(lat),std::uint16_t(lon)));
            const auto snapshot=saved.world().canonical_state();
            REQUIRE(snapshot.rfind("galaxy-kernel-v87|",0)==0);
            REQUIRE(shuttle.nutrients_kg==0);
            REQUIRE(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
            std::cout<<"PASS native committed shuttle stock, NPC custody, receipts, mass and snapshot\n";
            return 0;
        }
        if(argc!=2)throw std::runtime_error("usage: shuttle_fixture SAVE.sqlite");
        auto selected=choose();
        Simulation seed(613,selected.system);
        auto ship=seed.add_colony_ship(7,{0,0},60,8);
        seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        seed.enable_individual_workforce();
        seed.provision_starter_exploration_scanner(ship);
        seed.add_asteroid({2000,0},60);
        seed.ensure_physical_celestial_systems();
        seed.ensure_ship_frame_registry();
        const auto initial_mass=seed.total_ore();
        PersistentWorld world(argv[1],std::move(seed));
        auto submit=[&](std::uint64_t id,Action action,std::int64_t amount,EntityId target=0) {
            Command command{};
            command.id=id;command.sequence=id;
            command.execute_tick=world.world().tick()+1;command.issuer=7;
            command.actor=ship;command.action=action;
            command.amount=amount;command.target=target;
            REQUIRE(world.submit(command));
            world.step();
            REQUIRE(world.world().events().back().result==Outcome::Applied);
        };
        for(std::uint64_t batch=0;batch<2;++batch) {
            submit(2001+batch*3,Action::Mine,4,ship+1);
            submit(2002+batch*3,Action::RefineOre,4);
            submit(2003+batch*3,Action::FabricateModule,2);
        }
        REQUIRE(world.begin_celestial_transit(7,ship,selected.body,90001));
        world.steps(4);
        REQUIRE(world.complete_celestial_transit(90001));
        std::vector<std::uint64_t> roster;
        for(const auto& [id,n]:world.world().npc_records())
            if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
                roster.push_back(id);
        REQUIRE(roster.size()>=4);
        REQUIRE(world.build_planetary_site(7,ship,selected.body,
            selected.lat,selected.lon,roster[0],roster[1]));
        world.steps(4);
        REQUIRE(world.support_planetary_crew(7,ship,selected.body,
            selected.lat,selected.lon,false,10));
        REQUIRE(world.support_planetary_crew(7,ship,selected.body,
            selected.lat,selected.lon,true,10));
        world.steps(240);
        REQUIRE(world.world().planetary_spaceport_operational(
            selected.body,selected.lat,selected.lon));
        REQUIRE(world.world().colony_ship(ship)->modules>=2);
        REQUIRE(world.world().total_ore()==initial_mass);
        // The fixture is a real authoritative SQLite world, not a test stub.
        // The player must still build a shuttle, choose its pilot and load it.
        std::cout<<selected.body<<' '<<selected.lat<<' '<<selected.lon
                 <<' '<<ship<<' '<<roster[2]<<' '<<initial_mass<<' '<<roster[3]<<'\n';
    }catch(const std::exception& e) {
        std::cerr<<"PLAY13D4C5 fixture failure: "<<e.what()<<'\n';
        return 1;
    }
}
