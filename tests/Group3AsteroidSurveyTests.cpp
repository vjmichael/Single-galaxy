// GROUP3-B: existing PLAY-11 fitted original-world 50m survey acceptance.
// Drawing range and unpowered passive rock contacts never grant discoveries.
#include "galaxy/SpaceMiningSurvey.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("Group3-B line ")+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
 try {
    Simulation w(0x330B);
    const auto ship=w.add_colony_ship(7,{0,0},24,3);
    const auto rock=w.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
    const auto near=w.add_geological_asteroid({42000,0},AsteroidClass::Silicate,8,1100);
    const auto distant=w.add_geological_asteroid({1450000,0},AsteroidClass::Carbonaceous,7,1100);
    w.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    w.enable_station_ore_provenance();
    w.enable_ship_mining_equipment();
    w.provision_mining_test_refined_stock(ship); // explicit finite 2t test seed
    CHECK(w.register_player_person(7,ship));
    const auto original_mass=w.total_material_kg();
    CHECK(mining_survey::nearby_original_asteroids(w,7,ship).empty());
    // Manufacture ONE original material-backed module and fit the documented
    // PLAY-11 scanner hardpoint. No UI authority, extra stock or free sensor.
    std::uint64_t id=1;
    auto action=[&](Action verb,std::int64_t amount) {
        Command c{};c.id=id++;c.sequence=c.id;c.execute_tick=w.tick()+1;
        c.issuer=7;c.actor=ship;c.action=verb;c.amount=amount;
        CHECK(w.submit(c));w.step();
        CHECK(!w.events().empty() && w.events().back().command_id==c.id);
        return w.events().back().result;
    };
    CHECK(action(Action::FabricateModule,1)==Outcome::Applied);
    CHECK(w.colony_ship(ship)->modules==1);
    CHECK(action(Action::ConfigureExplorationScanner,1)==Outcome::Applied);
    CHECK(w.colony_ship(ship)->modules==0);
    CHECK(ship_sensors::powered_survey_scanner(w,*w.find(ship)));
    CHECK(w.total_material_kg()==original_mass);
    const auto state=w.canonical_state();
    const auto rows=mining_survey::nearby_original_asteroids(w,7,ship);
    CHECK(rows.size()==2);
    CHECK(rows[0].rock==rock && rows[1].rock==near);
    CHECK(rows[0].x_mm==25000 && rows[1].x_mm==42000);
    CHECK(rows[0].ore_tonnes==w.find(rock)->ore && rows[1].ore_tonnes==w.find(near)->ore);
    CHECK(rows[0].taxonomy==static_cast<std::uint8_t>(AsteroidClass::Metallic));
    CHECK(rows[0].geological_detail && rows[1].geological_detail);
    CHECK(mining_survey::nearby_original_asteroids(w,8,ship).empty());
    CHECK(mining_survey::nearby_original_asteroids(w,7,distant).empty());
    CHECK(mining_survey::nearby_original_asteroids(w,7,ship,0).empty());
    CHECK(mining_survey::nearby_original_asteroids(w,7,ship,17).empty());
    CHECK(w.canonical_state()==state); // survey never mutates the world
    CHECK(action(Action::SetHardpointPower,0)==Outcome::Applied);
    CHECK(!ship_sensors::powered_survey_scanner(w,*w.find(ship)));
    CHECK(mining_survey::nearby_original_asteroids(w,7,ship).empty());
    CHECK(w.total_material_kg()==original_mass);
    const galactic::Axis edge{123,galactic::kSectorEdgeMm-5};
    const galactic::Axis next{124,5};
    const auto across=space_mining_view::nearby_axis_delta(edge,next,mining_survey::kSurveyRangeMm);
    CHECK(across && *across==10);
    CHECK(!space_mining_view::nearby_axis_delta(
        edge,galactic::Axis{130,5},mining_survey::kSurveyRangeMm));
    CHECK(!space_mining_view::nearby_axis_delta(
        galactic::Axis{std::numeric_limits<std::int64_t>::max(),5},
        galactic::Axis{std::numeric_limits<std::int64_t>::min(),5},
        mining_survey::kSurveyRangeMm));
    std::cout<<"GROUP3-B PASS: only genuine powered 50m PLAY-11 scanner discloses existing asteroids/geology; no remote draw-distance discovery, immutable owner/mass/tick\n";
    return 0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
