#include "galaxy/InventoryLogistics.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <utility>

namespace galaxy::inventory {
LogisticsNetwork::LogisticsNetwork(const Catalogue& catalogue):catalogue_(&catalogue) {}
void LogisticsNetwork::create_node(StorageId id,std::int64_t capacity_liters) {
    if(!id || capacity_liters<=0 || capacity_liters>1000000000000LL || nodes_.count(id))
        throw std::invalid_argument("invalid or duplicate logistics storage");
    nodes_.emplace(std::piecewise_construct,std::forward_as_tuple(id),
                   std::forward_as_tuple(*catalogue_,capacity_liters));
}
void LogisticsNetwork::seed_from_authoritative_inventory(StorageId id,
                                                          const std::string& item,
                                                          std::int64_t amount) {
    if(amount<=0)throw std::invalid_argument("invalid logistics seed quantity");
    nodes_.at(id).load(item,amount);
}
const StackStore& LogisticsNetwork::node(StorageId id) const {return nodes_.at(id);}
const TransportRoute& LogisticsNetwork::route(RouteId id) const {return routes_.at(id);}
bool LogisticsNetwork::has_path(StorageId from,StorageId to) const {
    if(from==to)return true;
    std::vector<StorageId> todo{from};std::set<StorageId> seen;
    while(!todo.empty()) {
        auto id=todo.back();todo.pop_back();
        if(!seen.insert(id).second)continue;
        for(const auto& [route_id,r]:routes_) {
            (void)route_id;
            if(r.source!=id)continue;
            if(r.destination==to)return true;
            todo.push_back(r.destination);
        }
    }
    return false;
}
void LogisticsNetwork::add_route(RouteId id,StorageId source,StorageId destination,
                                 const std::string& item,std::int64_t max_per_tick) {
    if(!id || !nodes_.count(source) || !nodes_.count(destination) ||
       !catalogue_->contains(item) || max_per_tick<=0 || max_per_tick>1000000000LL ||
       routes_.count(id) || has_path(destination,source))
        throw std::invalid_argument("invalid or cyclic logistics route");
    routes_.emplace(id,TransportRoute{id,source,destination,item,max_per_tick,true,0});
}
void LogisticsNetwork::set_route_enabled(RouteId id,bool enabled) {
    routes_.at(id).enabled=enabled;
}
std::map<std::string,std::int64_t> LogisticsNetwork::total_stacks() const {
    std::map<std::string,std::int64_t> totals;
    for(const auto& [id,store]:nodes_){(void)id;
        for(const auto& [code,amount]:store.stacks()){
            auto& dest=totals[code];
            if(amount>std::numeric_limits<std::int64_t>::max()-dest)
                throw std::overflow_error("logistics total overflow");
            dest+=amount;
        }
    }
    return totals;
}
std::uint64_t LogisticsNetwork::advance(std::uint64_t ticks) {
    if(ticks>std::numeric_limits<std::uint64_t>::max()-tick_)
        throw std::overflow_error("logistics tick overflow");
    if(!ticks)return 0;
    if(routes_.size()==1){
        auto& r=routes_.begin()->second;
        if(!r.enabled){tick_+=ticks;return 0;}
        auto& from=nodes_.at(r.source);
        auto& to=nodes_.at(r.destination);
        const auto vol=catalogue_->liters_per_unit(r.item_code);
        const auto possible=std::min(from.amount(r.item_code),to.free_liters()/vol);
        const auto limited=static_cast<std::uint64_t>(possible);
        const auto rate=static_cast<std::uint64_t>(r.max_units_per_tick);
        const auto amount=std::min(limited,ticks>limited/rate ? limited : ticks*rate);
        if(amount){
            if(r.units_moved>std::numeric_limits<std::uint64_t>::max()-amount)
                throw std::overflow_error("logistics route counter overflow");
            if(from.transfer_to(to,r.item_code,static_cast<std::int64_t>(amount))!=TransferStatus::Applied)
                throw std::logic_error("logistics transfer validation contradiction");
            r.units_moved+=amount;
        }
        tick_+=ticks;return amount;
    }
    std::uint64_t total=0;
    for(std::uint64_t step=0;step<ticks;++step){
        bool changed=false;
        for(auto& [id,r]:routes_){(void)id;
            if(!r.enabled)continue;
            auto& from=nodes_.at(r.source);
            auto& to=nodes_.at(r.destination);
            const auto vol=catalogue_->liters_per_unit(r.item_code);
            const auto quantity=std::min({r.max_units_per_tick,from.amount(r.item_code),to.free_liters()/vol});
            if(quantity<=0)continue;
            if(r.units_moved>std::numeric_limits<std::uint64_t>::max()-static_cast<std::uint64_t>(quantity) ||
               total>std::numeric_limits<std::uint64_t>::max()-static_cast<std::uint64_t>(quantity))
                throw std::overflow_error("logistics total transferred overflow");
            if(from.transfer_to(to,r.item_code,quantity)!=TransferStatus::Applied)
                throw std::logic_error("logistics transfer validation contradiction");
            r.units_moved+=static_cast<std::uint64_t>(quantity);
            total+=static_cast<std::uint64_t>(quantity);changed=true;
        }
        ++tick_;
        if(!changed){
            tick_+=ticks-step-1;
            break;
        }
    }
    return total;
}
} // namespace galaxy::inventory
