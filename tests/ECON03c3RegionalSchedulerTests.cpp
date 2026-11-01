#include "galaxy/RegionalEventScheduler.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
using namespace galaxy;
using namespace galaxy::regional_events;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("ECON03c3 CHECK ")+ #x+" at "+std::to_string(__LINE__));}while(false)
static std::int64_t mass(const ColonyLifeSupport& l){
    return l.food_kg+l.water_kg+l.oxygen_kg+l.organic_waste_kg+l.wastewater_kg+l.co2_kg;
}
static std::int64_t total(const Scheduler& s){
    const auto& a=s.colony("Alpha");
    const auto& b=s.colony("Beta");
    return mass(a.depot)+mass(a.habitat)+mass(b.depot)+mass(b.habitat);
}
static std::set<std::tuple<std::uint64_t,int,std::string>> notices(const Scheduler& s){
    std::set<std::tuple<std::uint64_t,int,std::string>> out;
    for(const auto& x:s.pending_notices())out.emplace(x.tick,static_cast<int>(x.kind),x.colony);
    return out;
}
static Scheduler world(){
    Scheduler s;
    cold_colony::State a,b;
    a.population=2;
    a.depot.food_kg=100;a.depot.water_kg=100;a.depot.oxygen_kg=100;
    b.population=1;
    s.add_colony("Alpha",a);s.add_colony("Beta",b);
    return s;
}
int main(){
    try {
        auto one=world();
        const auto initial=total(one);
        const auto shipment=one.schedule_shipment("Alpha","Beta",{30,30,30},10,30);
        const auto local=one.schedule_habitat_delivery("Alpha",{10,10,10},20);
        one.schedule_population("Beta",2,60);
        CHECK(one.pending_events()==4);
        CHECK(one.colony("Alpha").depot.food_kg==60);
        auto split=one;
        auto stats=one.advance_to(100);
        CHECK(stats.executed_events==4);
        CHECK(stats.consumption_periods==10); // five deadlines in each of two cohorts
        CHECK(one.shipment(shipment).stage==ShipmentStage::Delivered);
        CHECK(one.colony("Beta").depot.food_kg==30);
        CHECK(one.colony("Alpha").habitat.organic_waste_kg==10);
        CHECK(one.colony("Alpha").habitat.food_kg==0);
        CHECK(one.pending_events()==0);
        // Shipments stay in custody while in flight; no cross-colony teleport.
        CHECK(split.advance_to(19).executed_events==1);
        CHECK(split.shipment(shipment).stage==ShipmentStage::InTransit);
        CHECK(split.colony("Beta").depot.food_kg==0);
        CHECK(split.advance_to(20).executed_events==1);
        CHECK(split.advance_to(39).executed_events==0);
        CHECK(split.colony("Beta").depot.food_kg==0);
        CHECK(split.advance_to(40).executed_events==1);
        CHECK(split.colony("Beta").depot.food_kg==30);
        CHECK(split.advance_to(100).executed_events==1);
        CHECK(split.colony("Beta").population==2);
        CHECK(total(one)==total(split));
        CHECK(mass(one.colony("Alpha").habitat)==mass(split.colony("Alpha").habitat));
        CHECK(one.colony("Beta").habitat.shortages==split.colony("Beta").habitat.shortages);
        CHECK(notices(one)==notices(split));
        for(std::size_t i=1;i<one.pending_notices().size();++i)
            CHECK(one.pending_notices()[i-1].tick<=one.pending_notices()[i].tick);
        // Even a distant colony cannot duplicate freight or issue free resources.
        CHECK(total(one)+0==initial);
        CHECK(total(split)+0==initial);
        bool threw=false;
        try{one.schedule_shipment("Beta","Alpha",{31,0,0},101,2);}catch(const std::invalid_argument&){threw=true;}
        CHECK(threw);
        CHECK(one.pending_events()==0);
        CHECK(total(one)==initial);
        // Invalid arrival overflow rolls back the *whole* advance and notification log.
        auto overflow=world();
        overflow.schedule_shipment("Alpha","Beta",{1,1,1},2,3);
        cold_colony::State huge;
        huge.population=0;
        huge.depot.food_kg=std::numeric_limits<std::int64_t>::max();
        // An isolated destination with a full depot triggers transactional failure.
        overflow.add_colony("Full",huge);
        overflow.schedule_shipment("Alpha","Full",{1,0,0},2,3);
        const auto before=overflow.pending_notices().size();
        threw=false;
        try{overflow.advance_to(5);}catch(const std::overflow_error&){threw=true;}
        CHECK(threw && overflow.tick()==0);
        CHECK(overflow.pending_notices().size()==before);
        CHECK(overflow.shipment(1).stage==ShipmentStage::Reserved);
        CHECK(overflow.colony("Full").depot.food_kg==std::numeric_limits<std::int64_t>::max());
        // A trillion residents are still one cohort; no per-person loop.
        Scheduler billion;
        cold_colony::State large;
        large.population=1'300'000'000'000ULL;
        billion.add_colony("Large",large);
        const auto x=billion.advance_to(1'000'000'000'000ULL);
        CHECK(x.cohort_evaluations==1);
        CHECK(x.shortage_periods==50'000'000'000ULL);
        CHECK(billion.pending_notices().size()==1);
        CHECK(billion.pending_notices()[0].tick==20);
        CHECK(billion.take_notices(1).size()==1);
        CHECK(billion.pending_notices().empty());
        std::cout<<"PASS ECON03c3 regional delivery/escrow; split invariance; chronological notifications; transactional failures; trillion cohort\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
