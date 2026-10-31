#include "galaxy/EconomyPrototype.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

namespace galaxy::economy {
namespace {
using E = Item; using S = Skill; using R = RecipeId;
const std::array<Recipe, static_cast<std::size_t>(R::Count)> catalog{{
    {R::SmeltIngot,"Smelt aluminum ingot",{{E::AluminumFeedstock,2}},{E::AluminumIngot,2},{{S::Refining,0}},2},
    {R::RollSheet,"Roll aluminum sheet",{{E::AluminumIngot,1}},{E::AluminumSheet,1},{{S::Metalwork,0}},2},
    {R::DrawWire,"Draw aluminum wire",{{E::AluminumIngot,1}},{E::AluminumWire,1},{{S::Metalwork,0}},2},
    {R::WeldBracket,"Weld bracket",{{E::AluminumSheet,1}},{E::WeldedBracket,1},{{S::Welding,0}},2},
    {R::FitValve,"Fit valve body",{{E::AluminumSheet,1}},{E::ValveBody,1},{{S::Mechanical,0}},2},
    {R::AssembleHullPanel,"Assemble reinforced hull panel",{{E::AluminumSheet,2},{E::WeldedBracket,1}},
        {E::HullPanel,1},{{S::Metalwork,2},{S::Welding,2}},3},
    {R::AssemblePump,"Assemble utility pump",{{E::AluminumWire,1},{E::ValveBody,1}},
        {E::PumpAssembly,1},{{S::Metalwork,2},{S::Mechanical,2}},3}
}};
std::size_t idx(Item x) {
    const auto i=static_cast<std::size_t>(x);
    if(i>=static_cast<std::size_t>(Item::Count))throw std::invalid_argument("bad item");
    return i;
}
void plus(std::int64_t &v,std::int64_t n){
    if(n<0||v>std::numeric_limits<std::int64_t>::max()-n)throw std::overflow_error("inventory overflow");
    v+=n;
}
}
std::int64_t item_mass_kg(Item item){
    constexpr std::array<std::int64_t,static_cast<std::size_t>(Item::Count)> masses{{1,1,1,1,1,1,3,2}};
    return masses.at(idx(item));
}
const Recipe& recipe(RecipeId id){
    const auto i=static_cast<std::size_t>(id);
    if(i>=catalog.size())throw std::invalid_argument("unknown recipe");
    return catalog.at(i);
}
Workshop::Workshop(unsigned workstations):station_capacity_(workstations){
    if(workstations==0||workstations>32)throw std::invalid_argument("invalid number of workstations");
}
void Workshop::add_worker(Worker w){
    if(w.id==0||w.current_order||!workers_.emplace(w.id,w).second)
        throw std::invalid_argument("worker identity not unique or not idle");
}
void Workshop::seed_stock(Item item,std::int64_t qty){
    if(qty<=0)throw std::invalid_argument("nonpositive quantity");
    plus(stock_[idx(item)],qty);
}
std::int64_t Workshop::stock(Item item)const {return stock_[idx(item)];}
std::int64_t Workshop::total_stock_mass_kg() const {
    std::int64_t mass=0;
    for(std::size_t i=0;i<stock_.size();++i){
        const auto units=stock_[i], unit_mass=item_mass_kg(static_cast<Item>(i));
        if(units>std::numeric_limits<std::int64_t>::max()/unit_mass ||
           mass>std::numeric_limits<std::int64_t>::max()-units*unit_mass)
            throw std::overflow_error("mass exceeds integer bound");
        mass+=units*unit_mass;
    }
    return mass;
}
void Workshop::set_worker_presence(std::uint64_t id,bool on_site){ workers_.at(id).on_site=on_site; }
std::uint64_t Workshop::post(RecipeId id,bool approved){
    (void)recipe(id);
    if(next_order_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("order ids");
    const auto oid=next_order_++;
    orders_.emplace(oid,WorkOrder{oid,id,approved?OrderStatus::Open:OrderStatus::Proposed,0,0,approved});
    return oid;
}
std::uint64_t Workshop::propose_shortage(RecipeId id){return post(id,false);}
void Workshop::approve(std::uint64_t id){
    auto &o=orders_.at(id);
    if(o.status!=OrderStatus::Proposed)throw std::logic_error("order cannot be approved");
    o.status=OrderStatus::Open;
    o.human_approved=true;
}
void Workshop::assign(std::uint64_t id,std::uint64_t worker_id){
    auto &o=orders_.at(id);
    const auto &r=recipe(o.recipe_id);
    auto &w=workers_.at(worker_id);
    if(o.status!=OrderStatus::Open||!o.human_approved)throw std::logic_error("order not authorized");
    if(!w.living||!w.on_site||w.current_order)throw std::logic_error("worker unavailable");
    if(busy_stations_>=station_capacity_)throw std::logic_error("workshop occupied");
    for(const auto &need:r.capabilities)
        if(w.proficiency.at(static_cast<std::size_t>(need.skill))<need.minimum)
            throw std::logic_error("missing combined proficiencies");
    for(const auto &in:r.inputs)
        if(stock(in.item)<in.quantity)throw std::logic_error("required materials unavailable");
    if(stock(r.output.item)>std::numeric_limits<std::int64_t>::max()-r.output.quantity)
        throw std::overflow_error("finished inventory would overflow");
    // All preconditions checked before reserving any input.
    for(const auto &in:r.inputs)stock_[idx(in.item)]-=in.quantity;
    o.status=OrderStatus::Active;
    o.worker_id=worker_id;
    o.remaining_powered_ticks=r.powered_ticks;
    w.current_order=id;
    ++busy_stations_;
}
void Workshop::cancel(std::uint64_t id){
    auto &o=orders_.at(id);
    if(o.status==OrderStatus::Completed||o.status==OrderStatus::Cancelled)
        throw std::logic_error("finished order cannot be cancelled");
    if(o.status==OrderStatus::Active){
        const auto &r=recipe(o.recipe_id);
        for(const auto &in:r.inputs)plus(stock_[idx(in.item)],in.quantity);
        workers_.at(o.worker_id).current_order=0;
        --busy_stations_;
    }
    o.status=OrderStatus::Cancelled;
}
void Workshop::advance(std::uint64_t ticks){
    if(ticks>1000000||tick_>std::numeric_limits<std::uint64_t>::max()-ticks)
        throw std::invalid_argument("invalid tick interval");
    for(std::uint64_t j=0;j<ticks;++j){
        ++tick_;
        if(!powered_)continue;
        for(auto &[id,o]:orders_){
            if(o.status!=OrderStatus::Active)continue;
            auto &w=workers_.at(o.worker_id);
            // Crew absence never produces a good while the worker is elsewhere.
            if(!w.living||!w.on_site)continue;
            if(o.remaining_powered_ticks>0)--o.remaining_powered_ticks;
            if(o.remaining_powered_ticks>0)continue;
            const auto &r=recipe(o.recipe_id);
            plus(stock_[idx(r.output.item)],r.output.quantity);
            o.status=OrderStatus::Completed;
            w.current_order=0;
            --busy_stations_;
            for(const auto &need:r.capabilities){
                auto &level=w.proficiency.at(static_cast<std::size_t>(need.skill));
                if(level<std::numeric_limits<unsigned>::max())++level;
            }
            receipts_.push_back(WorkReceipt{static_cast<std::uint64_t>(receipts_.size())+1,id,w.id,
                o.recipe_id,r.output,tick_});
        }
    }
}
const WorkOrder& Workshop::order(std::uint64_t id)const{return orders_.at(id);}
const Worker& Workshop::worker(std::uint64_t id)const{return workers_.at(id);}
} // namespace galaxy::economy
