#include "galaxy/RegionalEventScheduler.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace galaxy::regional_events {
namespace {
constexpr auto u64max=std::numeric_limits<std::uint64_t>::max();
constexpr auto i64max=std::numeric_limits<std::int64_t>::max();
void checked_add(std::uint64_t& x,std::uint64_t y){
    if(x>u64max-y)throw std::overflow_error("regional counter overflow");
    x+=y;
}
void validate(Cargo c){
    if(c.food_kg<0 || c.water_kg<0 || c.oxygen_kg<0 ||
       !(c.food_kg || c.water_kg || c.oxygen_kg))
        throw std::invalid_argument("cargo must be nonempty and nonnegative");
}
void take(Cargo c,ColonyLifeSupport& depot){
    if(depot.food_kg<c.food_kg || depot.water_kg<c.water_kg ||
       depot.oxygen_kg<c.oxygen_kg)
        throw std::invalid_argument("cargo has no physical stock to reserve");
    depot.food_kg-=c.food_kg;
    depot.water_kg-=c.water_kg;
    depot.oxygen_kg-=c.oxygen_kg;
}
void put(Cargo c,ColonyLifeSupport& target){
    if(target.food_kg<0 || target.water_kg<0 || target.oxygen_kg<0 ||
       target.food_kg>i64max-c.food_kg ||
       target.water_kg>i64max-c.water_kg ||
       target.oxygen_kg>i64max-c.oxygen_kg)
        throw std::overflow_error("destination resource capacity overflow");
    target.food_kg+=c.food_kg;
    target.water_kg+=c.water_kg;
    target.oxygen_kg+=c.oxygen_kg;
}
} // namespace
Scheduler::Scheduler(std::uint64_t cooldown,macro_needs::Ration ration)
 :cooldown_(cooldown),ration_(ration){
    if(!cooldown)throw std::invalid_argument("zero regional cooldown");
}
void Scheduler::add_colony(const std::string& id,const cold_colony::State& state){
    if(id.empty() || state.processed_tick!=tick_ ||
       !colonies_.emplace(id,state).second)
        throw std::invalid_argument("invalid or duplicate regional colony");
    shortage_open_[id]=false;
}
const cold_colony::State& Scheduler::colony(const std::string& id) const{
    return colonies_.at(id);
}
const Shipment& Scheduler::shipment(std::uint64_t id) const{
    return shipments_.at(id);
}
void Scheduler::queue(Scheduled event){
    if(event.tick<=tick_)throw std::invalid_argument("event must be in future");
    event.sequence=next_sequence_++;
    events_.push_back(std::move(event));
}
void Scheduler::notify(NoticeKind kind,const std::string& colony,
                       std::uint64_t at,std::uint64_t ref,std::uint64_t shortages){
    notices_.push_back({at,next_sequence_++,kind,colony,ref,shortages});
}
std::uint64_t Scheduler::schedule_shipment(const std::string& from,
                                           const std::string& to,Cargo cargo,
                                           std::uint64_t depart,
                                           std::uint64_t travel){
    validate(cargo);
    if(from==to || !travel || depart<=tick_ || depart>u64max-travel ||
       next_id_==u64max || next_sequence_>u64max-3)
        throw std::invalid_argument("invalid shipment route or time");
    auto origin=colonies_.at(from);
    colonies_.at(to);
    take(cargo,origin.depot); // real reservation before accepting order
    const auto id=next_id_++;
    colonies_.at(from)=origin;
    shipments_.emplace(id,Shipment{id,from,to,depart,depart+travel,cargo,
                                    ShipmentStage::Reserved});
    queue({depart,0,id,EventKind::ShipmentDeparture,from});
    queue({depart+travel,0,id,EventKind::ShipmentArrival,to});
    notify(NoticeKind::CargoReserved,from,tick_,id);
    return id;
}
std::uint64_t Scheduler::schedule_habitat_delivery(const std::string& name,
                                                   Cargo cargo,std::uint64_t at){
    validate(cargo);
    if(at<=tick_ || next_id_==u64max || next_sequence_>u64max-2)
        throw std::invalid_argument("invalid habitat delivery deadline");
    auto copy=colonies_.at(name);
    take(cargo,copy.depot);
    const auto id=next_id_++;
    colonies_.at(name)=copy;
    queue({at,0,id,EventKind::HabitatDelivery,name,cargo});
    notify(NoticeKind::CargoReserved,name,tick_,id);
    return id;
}
void Scheduler::schedule_population(const std::string& name,std::uint64_t population,
                                    std::uint64_t at){
    colonies_.at(name);
    if(at<=tick_ || next_sequence_==u64max)
        throw std::invalid_argument("invalid population deadline");
    queue({at,0,0,EventKind::PopulationChange,name,{},population});
}
void Scheduler::progress(const std::string& name,std::uint64_t until,Stats& stats){
    auto& c=colonies_.at(name);
    if(c.processed_tick>=until)return;
    const auto next_due=macro_needs::next_due(c.processed_tick,cooldown_);
    const auto first_unserved=macro_needs::runway(
        c.habitat,c.population,c.processed_tick,cooldown_,ration_).first_unserved_tick;
    const auto r=cold_colony::advance(c,until,{},cooldown_,ration_);
    checked_add(stats.cohort_evaluations,1);
    checked_add(stats.consumption_periods,r.periods);
    checked_add(stats.shortage_periods,r.shortage_periods);
    if(r.shortage_periods){
        if(!shortage_open_.at(name))
            notify(NoticeKind::SupplyShortage,name,first_unserved,0,1);
        shortage_open_.at(name)=true;
    }else if(r.periods && shortage_open_.at(name)){
        notify(NoticeKind::SupplyRecovered,name,next_due);
        shortage_open_.at(name)=false;
    }
}
void Scheduler::apply(const Scheduled& e){
    switch(e.kind){
    case EventKind::ShipmentDeparture:{
        auto& ship=shipments_.at(e.reference);
        if(ship.stage!=ShipmentStage::Reserved)
            throw std::logic_error("shipment not reserved");
        ship.stage=ShipmentStage::InTransit;
        notify(NoticeKind::ShipmentDeparted,e.colony,e.tick,e.reference);
        break;
    }
    case EventKind::ShipmentArrival:{
        auto& ship=shipments_.at(e.reference);
        if(ship.stage!=ShipmentStage::InTransit)
            throw std::logic_error("shipment not in transit");
        put(ship.cargo,colonies_.at(e.colony).depot);
        ship.stage=ShipmentStage::Delivered;
        notify(NoticeKind::ShipmentArrived,e.colony,e.tick,e.reference);
        break;
    }
    case EventKind::HabitatDelivery:
        put(e.cargo,colonies_.at(e.colony).habitat);
        notify(NoticeKind::HabitatSupplied,e.colony,e.tick,e.reference);
        break;
    case EventKind::PopulationChange:
        colonies_.at(e.colony).population=e.population;
        notify(NoticeKind::PopulationChanged,e.colony,e.tick);
        break;
    }
}
Stats Scheduler::advance_to(std::uint64_t target){
    if(target<tick_)throw std::invalid_argument("regional time rollback");
    Scheduler draft=*this; // transactional in-memory checkpoint boundary
    Stats stats;
    std::stable_sort(draft.events_.begin(),draft.events_.end(),
      [](const Scheduled& a,const Scheduled& b){
          return a.tick<b.tick || (a.tick==b.tick && a.sequence<b.sequence);
      });
    std::size_t consumed=0;
    while(consumed<draft.events_.size() && draft.events_[consumed].tick<=target){
        const auto at=draft.events_[consumed].tick;
        std::size_t end=consumed;
        std::set<std::string> touched;
        while(end<draft.events_.size() && draft.events_[end].tick==at){
            touched.insert(draft.events_[end].colony);
            ++end;
        }
        for(const auto& id:touched)draft.progress(id,at-1,stats);
        while(consumed<end){
            draft.apply(draft.events_[consumed++]);
            checked_add(stats.executed_events,1);
        }
        // All same-tick events take effect BEFORE that tick's ration deadline.
        for(const auto& id:touched)draft.progress(id,at,stats);
    }
    draft.events_.erase(draft.events_.begin(),draft.events_.begin()+static_cast<std::ptrdiff_t>(consumed));
    for(const auto& kv:draft.colonies_)draft.progress(kv.first,target,stats);
    // Distant cohorts may be evaluated after a shipment event. Queue notices
    // by the logical event timestamp, not the order the batch ran in memory.
    std::stable_sort(draft.notices_.begin(),draft.notices_.end(),
        [](const Notice& a,const Notice& b){
            if(a.tick!=b.tick)return a.tick<b.tick;
            if(a.colony!=b.colony)return a.colony<b.colony;
            if(a.kind!=b.kind)return a.kind<b.kind;
            return a.sequence<b.sequence;
        });
    draft.tick_=target;
    *this=std::move(draft);
    return stats;
}
Cargo Scheduler::custody() const {
    Cargo total;
    auto add=[](std::int64_t& dst,std::int64_t value){
        if(value<0 || dst>i64max-value)throw std::overflow_error("regional cargo custody overflow");
        dst+=value;
    };
    const auto accumulate=[&](Cargo c){
        add(total.food_kg,c.food_kg);
        add(total.water_kg,c.water_kg);
        add(total.oxygen_kg,c.oxygen_kg);
    };
    for(const auto& kv:shipments_)
        if(kv.second.stage!=ShipmentStage::Delivered)accumulate(kv.second.cargo);
    for(const auto& e:events_)
        if(e.kind==EventKind::HabitatDelivery)accumulate(e.cargo);
    return total;
}
std::vector<Notice> Scheduler::take_notices(std::size_t limit){
    const auto n=std::min(limit,notices_.size());
    std::vector<Notice> out(notices_.begin(),notices_.begin()+static_cast<std::ptrdiff_t>(n));
    notices_.erase(notices_.begin(),notices_.begin()+static_cast<std::ptrdiff_t>(n));
    return out;
}
} // namespace galaxy::regional_events
