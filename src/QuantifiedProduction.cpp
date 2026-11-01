#include "galaxy/QuantifiedProduction.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace galaxy::industry {
namespace {
using I = std::int64_t;
using U = std::uint64_t;
I add(I a, I b) {
    if(a<0 || b<0 || a>std::numeric_limits<I>::max()-b)
        throw std::overflow_error("mass arithmetic overflow");
    return a+b;
}
I mul(I a, I b) {
    if(a<0 || b<0 || (b!=0 && a>std::numeric_limits<I>::max()/b))
        throw std::overflow_error("mass arithmetic overflow");
    return a*b;
}
U add_u(U a, U b) {
    if(a>std::numeric_limits<U>::max()-b)throw std::overflow_error("tick/count overflow");
    return a+b;
}
void validate_store(const Store& s) {
    if(s.capacity_grams<0) throw std::invalid_argument("negative store capacity");
    (void)s.free_space();
}
} // namespace
I Store::amount(const std::string& item)const {
    const auto it=grams.find(item);
    return it==grams.end()?0:it->second;
}
I Store::used()const {
    I total=0;
    for(const auto& entry:grams) {
        if(entry.first.empty() || entry.second<0)throw std::invalid_argument("invalid stored material");
        total=add(total,entry.second);
    }
    return total;
}
I Store::free_space()const {
    const I filled=used();
    if(capacity_grams<0 || filled>capacity_grams)throw std::invalid_argument("store over capacity");
    return capacity_grams-filled;
}
void Store::deposit(const std::string& item, I amount_g) {
    if(item.empty() || amount_g<=0 || amount_g>free_space())
        throw std::invalid_argument("invalid or over-capacity deposit");
    const auto after=add(amount(item),amount_g);
    grams[item]=after;
}
void Store::withdraw(const std::string& item, I amount_g) {
    if(item.empty() || amount_g<=0 || amount(item)<amount_g)
        throw std::invalid_argument("not enough stored material");
    grams[item]-=amount_g;
    if(grams[item]==0)grams.erase(item);
}
I QuantifiedLine::recipes_per_batch()const noexcept {
    constexpr I counts[5]{1,4,16,64,256};
    return counts[machine_.tier-1];
}
std::uint32_t QuantifiedLine::cycle_ticks()const noexcept {return machine_.tier==1?5u:6u;}
QuantifiedLine::QuantifiedLine(Recipe recipe, Machine machine, State initial)
    :recipe_(std::move(recipe)),machine_(std::move(machine)),state_(std::move(initial)) {
    if(machine_.tier<1 || machine_.tier>5 || machine_.operation.empty() ||
       recipe_.operation!=machine_.operation || recipe_.inputs.empty() ||
       recipe_.product.code.empty() || recipe_.scrap.code.empty() ||
       recipe_.product.code==recipe_.scrap.code)
        throw std::invalid_argument("invalid machine/recipe operation");
    validate_store(state_.feed); validate_store(state_.finished); validate_store(state_.scrap);
    I total=0;
    for(const auto& in:recipe_.inputs){
        if(in.code.empty() || in.grams<=0)throw std::invalid_argument("invalid ingredient");
        total=add(total,in.grams);
        if(std::count_if(recipe_.inputs.begin(),recipe_.inputs.end(),
            [&](const Material& other){return other.code==in.code;})!=1)
            throw std::invalid_argument("duplicate ingredient code");
        batch_inputs_.push_back({in.code,mul(in.grams,recipes_per_batch())});
    }
    if(recipe_.product.grams<=0 || recipe_.scrap.grams<0 ||
       add(recipe_.product.grams,recipe_.scrap.grams)!=total)
        throw std::invalid_argument("recipe must conserve mass");
    // For material processing, loss is exactly 5%; assemblies may have a
    // separately authored balanced recipe. Never generate waste on shortage.
    if(recipe_.operation!="assemble" &&
       mul(recipe_.scrap.grams,20)!=total)
        throw std::invalid_argument("material processing requires 5% recoverable scrap");
    batch_product_={recipe_.product.code,mul(recipe_.product.grams,recipes_per_batch())};
    batch_scrap_={recipe_.scrap.code,mul(recipe_.scrap.grams,recipes_per_batch())};
    if(state_.has_work_in_progress){
        if(state_.ticks_remaining>cycle_ticks() ||
           state_.reserved_inputs.size()!=batch_inputs_.size())
            throw std::invalid_argument("invalid resumed batch");
        for(std::size_t i=0;i<batch_inputs_.size();++i){
            if(state_.reserved_inputs[i].code!=batch_inputs_[i].code ||
               state_.reserved_inputs[i].grams!=batch_inputs_[i].grams)
                throw std::invalid_argument("inconsistent work-in-progress reservation");
        }
    }else if(state_.ticks_remaining || !state_.reserved_inputs.empty()){
        throw std::invalid_argument("unbacked work-in-progress");
    }
}
bool QuantifiedLine::enough_inputs()const {
    for(const auto& in:batch_inputs_)if(state_.feed.amount(in.code)<in.grams)return false;
    return true;
}
bool QuantifiedLine::finished_space()const {return state_.finished.free_space()>=batch_product_.grams;}
bool QuantifiedLine::scrap_space()const {return state_.scrap.free_space()>=batch_scrap_.grams;}
Blockage QuantifiedLine::blockage()const {
    if(!machine_.powered)return Blockage::NoPower;
    if(!machine_.staffed)return Blockage::NoStaff;
    if(!machine_.maintained)return Blockage::NeedsMaintenance;
    if(!finished_space())return Blockage::FinishedStorageFull;
    if(!scrap_space())return Blockage::ScrapStorageFull;
    if(!state_.has_work_in_progress && !enough_inputs())return Blockage::InsufficientInput;
    return Blockage::None;
}
void QuantifiedLine::reserve_batch(){
    if(state_.has_work_in_progress || blockage()!=Blockage::None)
        throw std::logic_error("cannot start batch");
    for(const auto& in:batch_inputs_)state_.feed.withdraw(in.code,in.grams);
    state_.reserved_inputs=batch_inputs_;
    state_.has_work_in_progress=true;
    state_.ticks_remaining=cycle_ticks();
}
void QuantifiedLine::finish_batch(){
    if(!state_.has_work_in_progress || state_.ticks_remaining!=0 || !finished_space() || !scrap_space())
        throw std::logic_error("cannot finish batch");
    state_.finished.deposit(batch_product_.code,batch_product_.grams);
    if(batch_scrap_.grams)state_.scrap.deposit(batch_scrap_.code,batch_scrap_.grams);
    state_.reserved_inputs.clear();
    state_.has_work_in_progress=false;
    state_.completed_batches=add_u(state_.completed_batches,1);
}
AdvanceResult QuantifiedLine::advance(U ticks) {
    if(ticks>std::numeric_limits<U>::max()-state_.elapsed_ticks)
        throw std::overflow_error("elapsed ticks overflow");
    // Elapsed world time always advances, even during a power failure/blockage.
    state_.elapsed_ticks+=ticks;
    AdvanceResult result{};
    while(ticks>0){
        if(!machine_.powered || !machine_.staffed || !machine_.maintained)break;
        if(state_.has_work_in_progress){
            const U can_run=std::min(ticks,static_cast<U>(state_.ticks_remaining));
            state_.ticks_remaining-=static_cast<std::uint32_t>(can_run);
            ticks-=can_run;
            if(state_.ticks_remaining || !finished_space() || !scrap_space())break;
            finish_batch();
            result.completed_batches=add_u(result.completed_batches,1);
            continue;
        }
        if(blockage()!=Blockage::None)break;
        const U duration=cycle_ticks();
        // Vectorized remote execution: no per-tick and no per-item iteration.
        // Batches are limited independently by every ingredient and destination.
        U n=ticks/duration;
        for(const auto& in:batch_inputs_)
            n=std::min(n,static_cast<U>(state_.feed.amount(in.code)/in.grams));
        n=std::min(n,static_cast<U>(state_.finished.free_space()/batch_product_.grams));
        if(batch_scrap_.grams)
            n=std::min(n,static_cast<U>(state_.scrap.free_space()/batch_scrap_.grams));
        n=std::min(n,std::numeric_limits<U>::max()-state_.completed_batches);
        if(n>0){
            for(const auto& in:batch_inputs_)
                state_.feed.withdraw(in.code,mul(in.grams,static_cast<I>(n)));
            state_.finished.deposit(batch_product_.code,mul(batch_product_.grams,static_cast<I>(n)));
            if(batch_scrap_.grams)
                state_.scrap.deposit(batch_scrap_.code,mul(batch_scrap_.grams,static_cast<I>(n)));
            state_.completed_batches+=n;
            result.completed_batches=add_u(result.completed_batches,n);
            ticks-=n*duration;
            continue;
        }
        // A partial machine cycle reserves a complete batch, then persists
        // that WIP. No subminimum reservation, no output, and no scrap.
        if(ticks<duration){
            reserve_batch();
            state_.ticks_remaining-=static_cast<std::uint32_t>(ticks);
            ticks=0;
        }
        break;
    }
    result.blockage=blockage();
    return result;
}
I QuantifiedLine::total_mass_grams()const {
    I mass=add(add(state_.feed.used(),state_.finished.used()),state_.scrap.used());
    for(const auto& in:state_.reserved_inputs)mass=add(mass,in.grams);
    return mass;
}
} // namespace galaxy::industry
