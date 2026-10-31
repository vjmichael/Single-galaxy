#pragma once
// ECON-02A: bounded, in-memory design prototype. NOT wired to PersistentWorld.
// A verified gameplay integration must bind inputs/outputs to authoritative
// inventory and journaled physical effects before offering this to players.
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::economy {

enum class Item : std::uint8_t {
    AluminumFeedstock, AluminumIngot, AluminumSheet, AluminumWire,
    WeldedBracket, ValveBody, HullPanel, PumpAssembly, Count
};
enum class Skill : std::uint8_t { Refining, Metalwork, Welding, Mechanical, Count };
enum class RecipeId : std::uint8_t {
    SmeltIngot, RollSheet, DrawWire, WeldBracket, FitValve,
    AssembleHullPanel, AssemblePump, Count
};
enum class BoardStage : std::uint8_t { Volunteer, WorkBoard, LaborDivision, LaborDepartment };
enum class OrderStatus : std::uint8_t { Proposed, Open, Active, Completed, Cancelled };

struct Requirement { Skill skill; unsigned minimum; };
struct Ingredient { Item item; std::int64_t quantity; };
struct Recipe {
    RecipeId id;
    const char* name;
    std::vector<Ingredient> inputs;
    Ingredient output;
    std::vector<Requirement> capabilities;
    unsigned powered_ticks = 1;
};
const Recipe& recipe(RecipeId id);
std::int64_t item_mass_kg(Item item);

struct Worker {
    std::uint64_t id = 0;
    bool living = true;
    bool on_site = true;
    std::array<unsigned,static_cast<std::size_t>(Skill::Count)> proficiency{};
    std::uint64_t current_order = 0;
};
struct WorkOrder {
    std::uint64_t id = 0;
    RecipeId recipe_id = RecipeId::SmeltIngot;
    OrderStatus status = OrderStatus::Open;
    std::uint64_t worker_id = 0;
    unsigned remaining_powered_ticks = 0;
    bool human_approved = true;
};
struct WorkReceipt {
    std::uint64_t sequence = 0;
    std::uint64_t order_id = 0;
    std::uint64_t worker_id = 0;
    RecipeId recipe_id = RecipeId::SmeltIngot;
    Ingredient output{};
    std::uint64_t tick = 0;
};

class Workshop {
public:
    explicit Workshop(unsigned workstations = 1);
    void add_worker(Worker worker);
    // Host/test-fixture injection. This is NOT a gameplay mining or hauling API.
    void seed_stock(Item item, std::int64_t quantity);
    std::int64_t stock(Item item) const;
    std::int64_t total_stock_mass_kg() const;
    void set_worker_presence(std::uint64_t id, bool on_site);
    void set_powered(bool powered) noexcept { powered_ = powered; }
    void set_board_stage(BoardStage value) noexcept { board_stage_ = value; }
    BoardStage board_stage() const noexcept { return board_stage_; }

    std::uint64_t post(RecipeId id, bool human_approved = true);
    // A need signal may propose work, but may never silently authorize it.
    std::uint64_t propose_shortage(RecipeId id);
    void approve(std::uint64_t id);
    void assign(std::uint64_t id, std::uint64_t worker_id);
    void cancel(std::uint64_t id);
    void advance(std::uint64_t ticks = 1);

    const WorkOrder& order(std::uint64_t id) const;
    const Worker& worker(std::uint64_t id) const;
    const std::vector<WorkReceipt>& receipts() const noexcept { return receipts_; }
    std::uint64_t tick() const noexcept { return tick_; }
    unsigned busy_stations() const noexcept { return busy_stations_; }
private:
    std::array<std::int64_t, static_cast<std::size_t>(Item::Count)> stock_{};
    std::map<std::uint64_t, Worker> workers_;
    std::map<std::uint64_t, WorkOrder> orders_;
    std::vector<WorkReceipt> receipts_;
    std::uint64_t next_order_ = 1, tick_ = 0;
    unsigned station_capacity_ = 1, busy_stations_ = 0;
    bool powered_ = true;
    BoardStage board_stage_ = BoardStage::Volunteer;
};
} // namespace galaxy::economy
