#include "galaxy/ColdColonyNeeds.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace galaxy::cold_colony {
namespace {
constexpr auto u64max=std::numeric_limits<std::uint64_t>::max();
constexpr auto i64max=std::numeric_limits<std::int64_t>::max();
void accumulate(std::uint64_t& count,std::uint64_t delta){
    if(count>u64max-delta)throw std::overflow_error("cold-colony cycle counter overflow");
    count+=delta;
}
std::uint64_t full_periods(std::int64_t stock,std::uint64_t population,std::uint64_t ration){
    if(stock<0)throw std::invalid_argument("negative colony stock");
    if(!population || !ration)return u64max;
    const auto available=static_cast<std::uint64_t>(stock);
    if(population>available/ration)return 0;
    return available/(population*ration);
}
void process_window(State& state,std::uint64_t until,
                    std::uint64_t cooldown,macro_needs::Ration ration,
                    AdvanceResult& result){
    const auto due=macro_needs::periods_due(state.processed_tick,until,cooldown);
    if(due){
        const auto possible=std::min({
            full_periods(state.habitat.food_kg,state.population,ration.food_kg),
            full_periods(state.habitat.water_kg,state.population,ration.water_kg),
            full_periods(state.habitat.oxygen_kg,state.population,ration.oxygen_kg)});
        const auto served=std::min(due,possible);
        const auto shortages=due-served;
        macro_needs::consume(state.habitat,state.population,due,ration);
        accumulate(state.habitat.cycles,due);
        accumulate(state.habitat.shortages,shortages);
        if(shortages)state.habitat.consecutive_supplied_cycles=0;
        else accumulate(state.habitat.consecutive_supplied_cycles,due);
        accumulate(result.periods,due);
        accumulate(result.shortage_periods,shortages);
    }
    state.processed_tick=until;
}
void move(std::int64_t& from,std::int64_t& to,std::int64_t amount){
    if(amount<0 || from<0 || to<0)throw std::invalid_argument("invalid resource delivery");
    if(from<amount)throw std::invalid_argument("unfunded resource delivery");
    if(to>i64max-amount)throw std::overflow_error("resource destination overflow");
    from-=amount;to+=amount;
}
void apply_event(State& state,const Event& event){
    if(event.kind==Event::Kind::DeliveredFromDepot){
        move(state.depot.food_kg,state.habitat.food_kg,event.food_kg);
        move(state.depot.water_kg,state.habitat.water_kg,event.water_kg);
        move(state.depot.oxygen_kg,state.habitat.oxygen_kg,event.oxygen_kg);
    }else if(event.kind==Event::Kind::PopulationChanged){
        if(event.food_kg || event.water_kg || event.oxygen_kg)
            throw std::invalid_argument("population change cannot transfer material");
        state.population=event.new_population;
    }else{
        throw std::invalid_argument("unknown cold colony event");
    }
}
}
AdvanceResult advance(State& state,std::uint64_t target_tick,
                      const std::vector<Event>& events,std::uint64_t cooldown,
                      macro_needs::Ration ration){
    if(!cooldown || target_tick<state.processed_tick)
        throw std::invalid_argument("invalid cold-colony time interval");
    auto previous=state.processed_tick;
    for(const auto& event:events){
        if(event.tick<previous || event.tick<=state.processed_tick || event.tick>target_tick)
            throw std::invalid_argument("cold-colony event outside advancement window or out of order");
        previous=event.tick;
    }
    State next=state; // transactional: no partial mutation on an invalid event
    AdvanceResult result;
    std::size_t i=0;
    while(i<events.size()){
        const auto at=events[i].tick;
        // Every incoming event at 'at' occurs before the consumption deadline
        // at that tick. Process earlier periods as a single mathematical batch.
        process_window(next,at-1,cooldown,ration,result);
        do{
            apply_event(next,events[i]);
            accumulate(result.events,1);
            ++i;
        }while(i<events.size() && events[i].tick==at);
        process_window(next,at,cooldown,ration,result);
    }
    process_window(next,target_tick,cooldown,ration,result);
    state=std::move(next);
    return result;
}
} // namespace galaxy::cold_colony
