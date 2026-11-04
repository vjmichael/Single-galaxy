#include "galaxy/Simulation.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C6B: ")+#x+" line "+std::to_string(__LINE__));}while(false)
int main(){
    try{
        Simulation world(13507);
        const auto ship=world.add_colony_ship(7,{0,0},60,8);
        world.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
        world.provision_starter_exploration_scanner(ship);
        // A cross-system sample proves rarity is not every asteroid or every
        // planet. All candidate records derive only from saved astronomy.
        for(std::uint32_t system=2;system<=1200;++system)
            world.add_system(system);
        world.ensure_physical_celestial_systems();
        world.ensure_ship_frame_registry();
        CHECK(world.survey_oasis_prospects(8,ship).empty());
        const auto origin=world.canonical_state();
        unsigned candidate_count=0,with_planets=0;
        for(const auto& [system,bodies]:world.physical_celestial_systems()){
            unsigned in_system=0;
            for(const auto& body:bodies){
                const auto a=world.celestial_oasis_prospect(body.id);
                CHECK(a.has_value());
                CHECK(a->body==body.id);
                CHECK(a->water<=1&&a->energy<=1&&a->nutrients<=1&&a->shelter<=1);
                if(body.kind==CelestialKind::Planet)++with_planets;
                if(a->candidate){
                    CHECK(a->water&&a->energy&&a->nutrients&&a->shelter);
                    CHECK(body.kind==CelestialKind::Planet||body.kind==CelestialKind::Moon);
                    ++candidate_count;++in_system;
                }
                const auto again=world.celestial_oasis_prospect(body.id);
                CHECK(again.has_value()&&again->candidate==a->candidate);
            }
            CHECK(in_system<=1);
            (void)system;
        }
        CHECK(with_planets>0);
        CHECK(candidate_count>0);
        CHECK(candidate_count<80); // nominal 2% system draw, not common
        CHECK(!world.celestial_oasis_prospect(0));
        CHECK(!world.celestial_oasis_prospect((std::uint64_t(999999)<<32)|1));
        const auto own=world.survey_oasis_prospects(7,ship);
        CHECK(own.size()<=1);
        for(const auto& prospect:own){
            CHECK((prospect.body>>32)==1);
            CHECK(prospect.candidate&&prospect.water&&prospect.energy&&
                  prospect.nutrients&&prospect.shelter);
        }
        // The oasis is opportunity data ONLY. It does not give supplies,
        // NPCs, farms, habitats, atmosphere, materials or fuel to the ship.
        CHECK(world.canonical_state()==origin);
        const auto restored=Simulation::from_snapshot(origin);
        CHECK(restored.canonical_state()==origin);
        unsigned count_after=0;
        for(const auto& [system,bodies]:restored.physical_celestial_systems())
            for(const auto& body:bodies)
                if(restored.celestial_oasis_prospect(body.id)->candidate)
                    ++count_after;
        CHECK(count_after==candidate_count);
        CHECK(restored.life_support(ship)->food_kg==1000);
        CHECK(restored.life_support(ship)->water_kg==1000);
        CHECK(restored.life_support(ship)->oxygen_kg==1000);
        CHECK(restored.built_outposts().empty());
        std::cout<<"PASS PLAY13C6B rare deterministic unbuilt oasis prospects\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY13C6B "<<e.what()<<'\n';return 1;
    }
}
