#include "galaxy/ColdColonyNeeds.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::cold_colony;
using ColdEvent=galaxy::cold_colony::Event;
#define CHECK(expr) do { if(!(expr)) throw std::runtime_error(std::string("ECON03c2 CHECK failed: ")+ #expr+" at "+std::to_string(__LINE__)); }while(0)
static std::int64_t mass(const ColonyLifeSupport& l){
    return l.food_kg+l.water_kg+l.oxygen_kg+l.organic_waste_kg+l.wastewater_kg+l.co2_kg;
}
static void equal(const State& a,const State& b){
    CHECK(a.processed_tick==b.processed_tick);
    CHECK(a.population==b.population);
    CHECK(a.habitat.food_kg==b.habitat.food_kg);
    CHECK(a.habitat.water_kg==b.habitat.water_kg);
    CHECK(a.habitat.oxygen_kg==b.habitat.oxygen_kg);
    CHECK(a.habitat.organic_waste_kg==b.habitat.organic_waste_kg);
    CHECK(a.habitat.wastewater_kg==b.habitat.wastewater_kg);
    CHECK(a.habitat.co2_kg==b.habitat.co2_kg);
    CHECK(a.habitat.cycles==b.habitat.cycles);
    CHECK(a.habitat.shortages==b.habitat.shortages);
    CHECK(a.habitat.consecutive_supplied_cycles==b.habitat.consecutive_supplied_cycles);
    CHECK(a.depot.food_kg==b.depot.food_kg);
    CHECK(a.depot.water_kg==b.depot.water_kg);
    CHECK(a.depot.oxygen_kg==b.depot.oxygen_kg);
}
int main(){
 try {
    State base;
    base.population=2;
    base.depot.food_kg=20; base.depot.water_kg=20; base.depot.oxygen_kg=20;
    const auto original_mass=mass(base.habitat)+mass(base.depot);
    const std::vector<ColdEvent> events{
        {20, ColdEvent::Kind::DeliveredFromDepot, 4, 0, 0},
        {20, ColdEvent::Kind::DeliveredFromDepot, 0, 4, 4},
        {40, ColdEvent::Kind::PopulationChanged, 0, 0, 0, 3},
        {60, ColdEvent::Kind::DeliveredFromDepot, 9, 9, 9}
    };
    State one=base;
    const auto summary=advance(one,100,events);
    CHECK(summary.events==4 && summary.periods==5 && summary.shortage_periods==1);
    CHECK(one.habitat.cycles==5 && one.habitat.shortages==1);
    CHECK(one.habitat.consecutive_supplied_cycles==3);
    CHECK(one.habitat.food_kg==0 && one.habitat.water_kg==0 && one.habitat.oxygen_kg==0);
    CHECK(one.habitat.organic_waste_kg==13 && one.habitat.wastewater_kg==13 && one.habitat.co2_kg==13);
    CHECK(mass(one.habitat)+mass(one.depot)==original_mass);
    // Independent small-scale oracle: process every tick and compare final
    // inventory, shortage count, and consecutive-supply statistics exactly.
    State tickwise=base;
    std::size_t event_index=0;
    for(std::uint64_t t=1;t<=100;++t){
        while(event_index<events.size() && events[event_index].tick==t){
            const auto& e=events[event_index++];
            if(e.kind==ColdEvent::Kind::PopulationChanged){
                tickwise.population=e.new_population;
            }else{
                tickwise.depot.food_kg-=e.food_kg;
                tickwise.depot.water_kg-=e.water_kg;
                tickwise.depot.oxygen_kg-=e.oxygen_kg;
                tickwise.habitat.food_kg+=e.food_kg;
                tickwise.habitat.water_kg+=e.water_kg;
                tickwise.habitat.oxygen_kg+=e.oxygen_kg;
            }
        }
        if(t%20==0){
            const auto supplied=macro_needs::consume(tickwise.habitat,tickwise.population).fully_supplied();
            ++tickwise.habitat.cycles;
            if(supplied)++tickwise.habitat.consecutive_supplied_cycles;
            else {tickwise.habitat.consecutive_supplied_cycles=0;++tickwise.habitat.shortages;}
        }
        tickwise.processed_tick=t;
    }
    equal(one,tickwise);

    // Checkpoint/restart equivalence: an arbitrary split cannot alter results.
    State split=base;
    CHECK(advance(split,40,{events.begin(),events.begin()+3}).periods==2);
    const State checkpoint=split;
    CHECK(advance(split,100,{events.begin()+3,events.end()}).periods==3);
    equal(one,split);
    State resumed=checkpoint;
    advance(resumed,100,{events.begin()+3,events.end()});
    equal(one,resumed);
    CHECK(advance(one,100).periods==0); // idempotent at same tick

    // Large numbers: 1.3T people, a trillion ticks, zero individual timers.
    State huge;
    huge.population=1300000000000ULL;
    huge.habitat.food_kg=5200000000000LL;
    huge.habitat.water_kg=5200000000000LL;
    huge.habitat.oxygen_kg=5200000000000LL;
    const auto massive_mass=mass(huge.habitat);
    const auto vast=advance(huge,1000000000000ULL);
    CHECK(vast.periods==50000000000ULL);
    CHECK(vast.shortage_periods==49999999996ULL);
    CHECK(huge.habitat.cycles==50000000000ULL);
    CHECK(huge.habitat.food_kg==0 && huge.habitat.organic_waste_kg==5200000000000LL);
    CHECK(mass(huge.habitat)==massive_mass);

    // Invalid partial/mixed deliveries must roll back the WHOLE advancement.
    const State pristine=checkpoint;
    bool rejected=false;
    try{(void)advance(split,120,{{120,ColdEvent::Kind::DeliveredFromDepot,1,999,1}});}
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);equal(split,one); // current split already at tick 100
    State before_failure=pristine;
    rejected=false;
    try{(void)advance(before_failure,120,{{60,ColdEvent::Kind::DeliveredFromDepot,1,999,1}});}
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);equal(before_failure,pristine);
    rejected=false;
    try{(void)advance(before_failure,120,{{80,ColdEvent::Kind::PopulationChanged,1,0,0,7}});}
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);equal(before_failure,pristine);
    rejected=false;
    try{(void)advance(before_failure,120,{{80,ColdEvent::Kind::PopulationChanged},{70,ColdEvent::Kind::PopulationChanged}});}
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);equal(before_failure,pristine);

    // Deadline coinciding with population change uses new population.
    State changed; changed.population=1;
    changed.habitat.food_kg=2;changed.habitat.water_kg=2;changed.habitat.oxygen_kg=2;
    auto change=advance(changed,20,{{20,ColdEvent::Kind::PopulationChanged,0,0,0,3}});
    CHECK(change.periods==1 && change.shortage_periods==1);
    CHECK(changed.habitat.food_kg==0 && changed.habitat.shortages==1);

    std::cout << "PASS ECON03c2 cold-colony time batching; same-tick deadlines; "
                 "depot conservation; 1.3T cohort; 1T ticks; restart-equivalence; rollback\n";
    return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
