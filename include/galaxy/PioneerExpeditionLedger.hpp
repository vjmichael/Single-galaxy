#pragma once
// DEMOG-04a. Durable, host-gated pioneer expedition escrow and lifecycle.
// Owns an isolated prototype custody ledger; NOT YET bridged to PersistentWorld.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace galaxy::pioneers {
enum class Phase : std::uint8_t { Reserved=0, InTransit=1, Arrived=2, Cancelled=3, Lost=4 };
enum class Operation : std::uint8_t { Reserve=1, Depart=2, Arrive=3, Cancel=4, Destroy=5 };
struct Stock { std::uint64_t food=0, water=0, oxygen=0, material=0; };
struct Group { // explicit age/occupation/skill/expectation category; no fictional fake NPC IDs
    std::uint8_t adult=0, occupation=0, skill=0, expectation=0;
    std::uint64_t count=0;
};
struct Ship { std::uint64_t id=0, location=0, capacity=0; int state=0; };
struct SiteRecord {
    std::uint64_t id=0; bool surveyed=false, reachable=false;
    std::uint64_t landing_capacity=0, food_support=0, water_support=0;
    std::uint32_t danger_permille=1000;
};
struct Mission {
    std::string id, actor;
    std::uint64_t origin=0, destination=0, ship_id=0, travel_ticks=0;
    Stock cargo{};
    std::vector<Group> manifest;
};
struct Expedition {
    std::string id;
    std::uint64_t origin=0, destination=0, ship_id=0, population=0, adults=0;
    std::uint64_t depart_tick=0, arrive_tick=0;
    Stock cargo{}, consumed{};
    Phase phase=Phase::Reserved;
};
struct Census {std::uint64_t colonists=0, reserved=0, in_transit=0, lost=0, colonies=0;};
struct StockTotals {Stock in_colonies{}, in_escrow{}, consumed{}, lost{};};
using Permission=std::function<bool(const std::string&,Operation,std::uint64_t,std::uint64_t)>;
enum class Fault {None, AfterDebitsBeforeCommit};
class ExpeditionLedger {
public:
    ExpeditionLedger(const std::string& path, Permission permission);
    ~ExpeditionLedger();
    ExpeditionLedger(const ExpeditionLedger&)=delete;
    ExpeditionLedger& operator=(const ExpeditionLedger&)=delete;
    // Privileged setup for a world already physically constructed elsewhere.
    // These never appear as NPC player actions; only for integration fixtures now.
    void bootstrap_colony(std::uint64_t id,Stock stock,const std::vector<Group>& residents);
    void bootstrap_site(const SiteRecord& site);
    void bootstrap_ship(std::uint64_t id,std::uint64_t colony,std::uint64_t capacity);
    std::uint64_t revision() const;
    Expedition reserve(const Mission& mission,std::uint64_t expected_revision, Fault fault=Fault::None);
    Expedition transition(const std::string& expedition_id, Operation operation,
        const std::string& command_id, const std::string& actor,
        std::uint64_t logical_tick,std::uint64_t expected_revision,Fault fault=Fault::None);
    Expedition expedition(const std::string& id) const;
    Stock stock(std::uint64_t colony) const;
    std::uint64_t population(std::uint64_t colony) const;
    Ship ship(std::uint64_t id) const;
    Census census() const;
    StockTotals stock_totals() const;
    void verify_integrity() const;
private:
    struct Impl;Impl* p_;
};
} // namespace galaxy::pioneers
