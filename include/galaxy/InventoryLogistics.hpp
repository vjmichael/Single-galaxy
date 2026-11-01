#pragma once
// ECON-02B2c1: deterministic, bounded logistics domain (not yet a world entity).
// Every node holds real catalogue quantities in a finite-volume StackStore.
// A link is directed, filtered and rate-limited. No payload weight or physics.
#include "galaxy/Inventory.hpp"
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>

namespace galaxy::inventory {
using StorageId=std::uint64_t;
using RouteId=std::uint64_t;
struct TransportRoute {
    RouteId id=0;
    StorageId source=0, destination=0;
    std::string item_code; // explicit catalogue item ID; no fuzzy matching
    std::int64_t max_units_per_tick=0;
    bool enabled=true;
    std::uint64_t units_moved=0;
};
class LogisticsNetwork {
public:
    explicit LogisticsNetwork(const Catalogue& catalogue=Catalogue::standard());
    // Construction and stocking authority belong to host-owned world adapter.
    // A node or route is not an implicitly built facility.
    void create_node(StorageId id,std::int64_t capacity_liters);
    void seed_from_authoritative_inventory(StorageId id,const std::string& item,
                                           std::int64_t amount);
    void add_route(RouteId id,StorageId source,StorageId destination,
                   const std::string& item_code,std::int64_t max_units_per_tick);
    void set_route_enabled(RouteId id,bool enabled);
    const StackStore& node(StorageId id) const;
    const TransportRoute& route(RouteId id) const;
    std::size_t node_count() const noexcept {return nodes_.size();}
    std::size_t route_count() const noexcept {return routes_.size();}
    std::uint64_t tick() const noexcept {return tick_;}
    // Uses stable route-ID order for shared-source priority. Returns moved units.
    // Idle networks fast-forward without material changes; a one-route network
    // can move many ticks in one exact, bounded arithmetic operation.
    std::uint64_t advance(std::uint64_t ticks);
    std::map<std::string,std::int64_t> total_stacks() const;
private:
    bool has_path(StorageId from,StorageId to) const;
    const Catalogue* catalogue_;
    std::map<StorageId,StackStore> nodes_;
    std::map<RouteId,TransportRoute> routes_;
    std::uint64_t tick_=0;
};
} // namespace galaxy::inventory
