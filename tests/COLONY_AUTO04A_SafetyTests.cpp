#include "galaxy/ColonySafety.hpp"
#include "galaxy/ColdColonyNeeds.hpp"
#include "galaxy/Simulation.hpp"
#include <sqlite3.h>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::colony_safety;
#define CHECK(v) do {if(!(v))throw std::runtime_error(std::string("COLONY_AUTO04A failed: ")+ #v+" line "+std::to_string(__LINE__));}while(0)
static bool sqlite_rejects(sqlite3* db,const char* sql){
    char* errmsg=nullptr;const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&errmsg);
    if(errmsg)sqlite3_free(errmsg);
    return rc!=SQLITE_OK;
}
int main(){
try{
    Simulation world(10404);
    const auto ship=world.add_colony_ship(7,{0,0},100,4);
    world.provision_colony_genesis(ship,100,100,100,200,500);
    auto initial=assess(world,ship,ship);
    CHECK(initial.owner==7 && initial.population==4);
    CHECK(initial.risk==Risk::Safe && !initial.emergency_override);
    CHECK(initial.remaining.food_periods==25);
    auto status=preflight(world,ship,ship,7,Order::SetLocalProduction);
    CHECK(status.allowed && !status.deliberate_hazard_attempt);
    CHECK(!preflight(world,ship,ship,8,Order::SetLocalProduction).allowed);
    // The safety preflight cannot itself mutate stocks or bypass actual consumption.
    CHECK(world.life_support(ship)->food_kg==100);
    world.steps(21*20);
    CHECK(world.life_support(ship)->food_kg==16);
    auto warn=assess(world,ship,ship);
    CHECK(warn.risk==Risk::ForecastShortage);
    CHECK(warn.should_prepare_departure && !warn.emergency_override);
    auto blocked=preflight(world,ship,ship,7,Order::SealOccupiedExit);
    CHECK(!blocked.allowed && blocked.deliberate_hazard_attempt);
    CHECK(!preflight(world,ship,ship,7,Order::DisableDistress).allowed);
    CHECK(!preflight(world,ship,ship,7,Order::DumpOccupiedAtmosphere).allowed);
    CHECK(!preflight(world,ship,ship,7,Order::PreventEmergencyRepair).allowed);
    world.steps(3*20);
    CHECK(world.life_support(ship)->food_kg==4);
    CHECK(assess(world,ship,ship).risk==Risk::Critical);
    const auto restored=Simulation::from_snapshot(world.canonical_state());
    CHECK(assess(restored,ship,ship).risk==Risk::Critical);
    CHECK(restored.life_support(ship)->food_kg==4);
    CHECK(assess(world,ship,ship).emergency_override);
    world.steps(20);
    CHECK(world.life_support(ship)->food_kg==0);
    CHECK(assess(world,ship,ship).risk==Risk::Emergency);
    CHECK(assess(world,ship,ship).should_prepare_departure);
    CHECK(world.colony_ship(ship)->crew==4); // no phantom migration or deaths
    // Social tolerance must not postpone life-preserving emergency assessment.
    // No 'angry' scalar is even an input to the physical-risk calculation.

    // Real macro-mode consumption remains physical and independent of local
    // production policy. A scheduled import is NOT accepted as actual stock.
    cold_colony::State cold{};
    cold.population=3;
    cold.habitat.food_kg=6;
    cold.habitat.water_kg=6;
    cold.habitat.oxygen_kg=6;
    cold.depot.food_kg=9;
    cold.depot.water_kg=9;
    cold.depot.oxygen_kg=9;
    const auto result=cold_colony::advance(cold,60);
    CHECK(result.periods==3 && result.shortage_periods==1);
    CHECK(cold.habitat.food_kg==0 && cold.depot.food_kg==9);
    CHECK(cold.population==3 && cold.habitat.organic_waste_kg==6);
    const auto delivered=cold_colony::advance(cold,80,{{80,cold_colony::Event::Kind::DeliveredFromDepot,3,3,3}});
    CHECK(delivered.periods==1 && delivered.shortage_periods==0);
    CHECK(cold.depot.food_kg==6 && cold.habitat.food_kg==0);

    // Append attempted hazard to caller-owned SQLite writer; no second DB.
    const auto name="sg_colony_auto04a_evidence_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite";
    const std::string path=(std::filesystem::temp_directory_path()/name).string();
    std::remove(path.c_str());
    sqlite3* db=nullptr;
    CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    create_evidence_table(db);
    CHECK(sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK);
    record_decision(db,1001,blocked);
    CHECK(evidence_count(db)==1);
    CHECK(sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr)==SQLITE_OK);
    CHECK(sqlite_rejects(db,"DELETE FROM sg_colony_safety_evidence WHERE event_id=1001"));
    CHECK(sqlite_rejects(db,"UPDATE sg_colony_safety_evidence SET attempted_hazard=0 WHERE event_id=1001"));
    bool duplicate=false;
    try{record_decision(db,1001,blocked);}catch(const std::runtime_error&){duplicate=true;}
    CHECK(duplicate);
    CHECK(sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK);
    record_decision(db,1002,status);
    CHECK(evidence_count(db)==2);
    CHECK(sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK);
    CHECK(evidence_count(db)==1);
    CHECK(sqlite3_close(db)==SQLITE_OK);
    CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    CHECK(evidence_count(db)==1);
    CHECK(sqlite3_close(db)==SQLITE_OK);
    std::remove(path.c_str());
    std::cout<<"COLONY_AUTO04A safety policy assessment and audit: PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
