#include "galaxy/MacroNeeds.hpp"
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace galaxy::macro_needs {
namespace {
constexpr auto u64max=std::numeric_limits<std::uint64_t>::max();
constexpr auto i64max=std::numeric_limits<std::int64_t>::max();
std::uint64_t saturating_add(std::uint64_t a,std::uint64_t b){
    return a>u64max-b?u64max:a+b;
}
std::uint64_t saturating_multiply(std::uint64_t a,std::uint64_t b){
    return b && a>u64max/b?u64max:a*b;
}
std::int64_t checked_stock(std::int64_t stock,std::int64_t waste){
    if(stock<0 || waste<0)throw std::invalid_argument("negative population resource compartment");
    return stock;
}
struct Portion {std::int64_t consumed;bool supplied;};
Portion take(std::int64_t available,std::uint64_t population,
             std::uint64_t periods,std::uint64_t kg_each){
    if(!population || !periods || !kg_each)return {0,true};
    // Compare via division FIRST; never overflow population*periods*kg_each.
    const auto a=static_cast<std::uint64_t>(available);
    if(population>a/kg_each/periods)return {available,false};
    return {static_cast<std::int64_t>(population*kg_each*periods),true};
}
std::uint64_t possible(std::int64_t amount,std::uint64_t population,std::uint64_t ration){
    if(!population || !ration)return u64max;
    const auto available=static_cast<std::uint64_t>(amount);
    if(population>available/ration)return 0;
    return available/(population*ration);
}
}
std::uint64_t periods_due(std::uint64_t before,std::uint64_t now,std::uint64_t cooldown){
    if(!cooldown || now<before)throw std::invalid_argument("invalid consumption interval");
    return now/cooldown-before/cooldown;
}
std::uint64_t next_due(std::uint64_t now,std::uint64_t cooldown){
    if(!cooldown)throw std::invalid_argument("zero consumption cooldown");
    const auto delta=cooldown-now%cooldown;
    if(now>u64max-delta)throw std::overflow_error("next consumption tick overflow");
    return now+delta;
}
Result consume(ColonyLifeSupport& stock,std::uint64_t population,
               std::uint64_t due_periods,Ration ration){
    checked_stock(stock.food_kg,stock.organic_waste_kg);
    checked_stock(stock.water_kg,stock.wastewater_kg);
    checked_stock(stock.oxygen_kg,stock.co2_kg);
    const auto f=take(stock.food_kg,population,due_periods,ration.food_kg);
    const auto w=take(stock.water_kg,population,due_periods,ration.water_kg);
    const auto o=take(stock.oxygen_kg,population,due_periods,ration.oxygen_kg);
    // Preflight all writes: a failed conversion must not partly mutate stocks.
    if(stock.organic_waste_kg>i64max-f.consumed ||
       stock.wastewater_kg>i64max-w.consumed ||
       stock.co2_kg>i64max-o.consumed)
        throw std::overflow_error("population waste bin capacity overflow");
    stock.food_kg-=f.consumed;stock.organic_waste_kg+=f.consumed;
    stock.water_kg-=w.consumed;stock.wastewater_kg+=w.consumed;
    stock.oxygen_kg-=o.consumed;stock.co2_kg+=o.consumed;
    return {f.consumed,w.consumed,o.consumed,f.supplied,w.supplied,o.supplied};
}
Runway runway(const ColonyLifeSupport& stock,std::uint64_t population,
              std::uint64_t now,std::uint64_t cooldown,Ration ration){
    checked_stock(stock.food_kg,stock.organic_waste_kg);
    checked_stock(stock.water_kg,stock.wastewater_kg);
    checked_stock(stock.oxygen_kg,stock.co2_kg);
    Runway out;
    out.next_due_tick=next_due(now,cooldown);
    out.food_periods=possible(stock.food_kg,population,ration.food_kg);
    out.water_periods=possible(stock.water_kg,population,ration.water_kg);
    out.oxygen_periods=possible(stock.oxygen_kg,population,ration.oxygen_kg);
    const auto periods=std::min({out.food_periods,out.water_periods,out.oxygen_periods});
    out.first_unserved_tick=saturating_add(out.next_due_tick,
                                            saturating_multiply(periods,cooldown));
    return out;
}
} // namespace galaxy::macro_needs
