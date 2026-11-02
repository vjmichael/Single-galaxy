#pragma once
// ECON-03c3 — prototype opt-in regional event/notification scheduler.
// Not wired into authoritative Simulation or durable SQLite. Use one timeline,
// bounded notification outbox, and algebraic cold-cohort advancement.
#include "galaxy/ColdColonyNeeds.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::regional_events {
class CheckpointStore;
struct Cargo {
    std::int64_t food_kg=0, water_kg=0, oxygen_kg=0;
};
enum class NoticeKind {
    CargoReserved, ShipmentDeparted, ShipmentArrived, HabitatSupplied,
    PopulationChanged, SupplyShortage, SupplyRecovered
};
struct Notice {
    std::uint64_t tick=0;
    std::uint64_t sequence=0;
    NoticeKind kind=NoticeKind::CargoReserved;
    std::string colony;
    std::uint64_t reference=0;
    std::uint64_t shortage_periods=0;
};
struct Stats {
    std::uint64_t executed_events=0;
    std::uint64_t cohort_evaluations=0;
    std::uint64_t consumption_periods=0;
    std::uint64_t shortage_periods=0;
};
enum class ShipmentStage { Reserved, InTransit, Delivered };
struct Shipment {
    std::uint64_t id=0;
    std::string origin, destination;
    std::uint64_t departure_tick=0, arrival_tick=0;
    Cargo cargo{};
    ShipmentStage stage=ShipmentStage::Reserved;
};
class Scheduler {
    friend class CheckpointStore;
public:
    explicit Scheduler(std::uint64_t cooldown_ticks=macro_needs::default_cooldown_ticks,
                       macro_needs::Ration ration={});
    std::uint64_t tick() const noexcept {return tick_;}
    void add_colony(const std::string& id, const cold_colony::State& state);
    const cold_colony::State& colony(const std::string& id) const;
    const Shipment& shipment(std::uint64_t id) const;
    const std::map<std::uint64_t,Shipment>& shipments() const noexcept {return shipments_;}
    // ECON-03c9: host-owned physical stock operations. These mutate the
    // candidate scheduler only; commit_command persists them atomically with
    // industry/cargo receipts. No remote caller may access these directly.
    void debit_depot(const std::string& colony, Cargo cargo);
    void credit_depot(const std::string& colony, Cargo cargo);
    void cold_life_work(const std::string& colony,
                        std::int64_t farm_food_kg,
                        Cargo reclaimed_waste);
    std::uint64_t schedule_shipment(const std::string& origin,
                                    const std::string& destination, Cargo cargo,
                                    std::uint64_t departure_tick,
                                    std::uint64_t travel_ticks);
    std::uint64_t schedule_habitat_delivery(const std::string& colony, Cargo cargo,
                                            std::uint64_t arrival_tick);
    void schedule_population(const std::string& colony, std::uint64_t new_population,
                             std::uint64_t effective_tick);
    // Transactional on error: event queue, colonies, escrow and notices unchanged.
    Stats advance_to(std::uint64_t target_tick);
    // Durable host must persist this outbox before acknowledging these notices.
    std::vector<Notice> take_notices(std::size_t limit);
    const std::vector<Notice>& pending_notices() const noexcept {return notices_;}
    std::size_t pending_events() const noexcept {return events_.size();}
    std::size_t colony_count() const noexcept {return colonies_.size();}
    // Materials reserved for shipments/local deliveries, not currently in any depot.
    Cargo custody() const;
private:
    enum class EventKind { ShipmentDeparture, ShipmentArrival,
                           HabitatDelivery, PopulationChange };
    struct Scheduled {
        std::uint64_t tick=0, sequence=0, reference=0;
        EventKind kind=EventKind::ShipmentDeparture;
        std::string colony;
        Cargo cargo{};
        std::uint64_t population=0;
    };
    std::uint64_t tick_=0,cooldown_=0,next_id_=1,next_sequence_=1;
    macro_needs::Ration ration_{};
    std::map<std::string,cold_colony::State> colonies_;
    std::map<std::uint64_t,Shipment> shipments_;
    std::map<std::string,bool> shortage_open_;
    std::vector<Scheduled> events_;
    std::vector<Notice> notices_;
    void queue(Scheduled event);
    void notify(NoticeKind kind,const std::string& colony,
                std::uint64_t tick,std::uint64_t reference=0,
                std::uint64_t shortages=0);
    void progress(const std::string& id,std::uint64_t until,Stats& stats);
    void apply(const Scheduled& event);
};
} // namespace galaxy::regional_events
