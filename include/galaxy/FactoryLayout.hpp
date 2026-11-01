#pragma once
// ECON-02B2d1: spatial factory layout authoring and validation domain.
// Grid coordinates and sizes are gameplay abstractions, not structural physics.
// This is an unbuilt blueprint; creating it does NOT create free world equipment.
#include "galaxy/InventoryLogistics.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::industry {
using DeviceId = std::uint64_t;
using ConveyorId = std::uint64_t;
struct GridCell {
    std::int64_t x=0,y=0;
    bool operator==(const GridCell& rhs) const noexcept {return x==rhs.x && y==rhs.y;}
    bool operator<(const GridCell& rhs) const noexcept {return x<rhs.x || (x==rhs.x && y<rhs.y);}
};
enum class DeviceKind { Storage, Machine, Splitter, Filter, Merger };
struct Device {
    DeviceId id=0;
    DeviceKind kind=DeviceKind::Storage;
    GridCell origin{};
    std::int64_t width=1,height=1;
    std::uint8_t tier=0;
    std::int64_t capacity_liters=0;
    std::vector<std::string> inputs,outputs;
    std::string filter_item;
};
struct Conveyor {
    ConveyorId id=0;
    DeviceId from=0,to=0;
    std::string item_code;
    std::int64_t units_per_tick=0;
    // Consecutive grid cells, from just outside source to just outside destination.
    // The endpoints MUST be adjacent to their machines/storage/splitter footprints.
    std::vector<GridCell> path;
};
class FactoryLayout {
public:
    explicit FactoryLayout(const inventory::Catalogue& catalogue=inventory::Catalogue::standard());
    // All dimensions are positive and footprints must not overlap. There is NO
    // site/factory area cap. The coordinate range is for safe arithmetic only.
    void place_storage(DeviceId id,GridCell origin,std::int64_t width,std::int64_t height,
                       std::int64_t capacity_liters);
    void place_machine(DeviceId id,GridCell origin,std::uint8_t tier,
                       std::vector<std::string> inputs,std::vector<std::string> outputs);
    void place_junction(DeviceId id,DeviceKind kind,GridCell origin,
                        std::string filter_item={});
    void add_conveyor(ConveyorId id,DeviceId from,DeviceId to,std::string item_code,
                      std::int64_t units_per_tick,std::vector<GridCell> path);
    const std::map<DeviceId,Device>& devices() const noexcept {return devices_;}
    const std::map<ConveyorId,Conveyor>& conveyors() const noexcept {return conveyors_;}
    // A separate simulator needs authoritative, material-backed machine production;
    // this compiles only the route graph into the already-tested logistics kernel.
    inventory::LogisticsNetwork compile_transport() const;
    std::string canonical_blueprint() const;
    static FactoryLayout from_blueprint(const std::string& text,
             const inventory::Catalogue& catalogue=inventory::Catalogue::standard());
private:
    void place(Device device);
    bool reachable(DeviceId start,DeviceId target) const;
    const inventory::Catalogue* catalogue_;
    std::map<DeviceId,Device> devices_;
    std::map<ConveyorId,Conveyor> conveyors_;
};
} // namespace galaxy::industry
