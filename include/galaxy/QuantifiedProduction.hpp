#pragma once
// ECON-02B0: deterministic, mass-accounted factory-line prototype.
// NOT an authoritative PersistentWorld command, depot or NPC job API.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::industry {
struct Material { std::string code; std::int64_t grams = 0; };

struct Store {
    std::int64_t capacity_grams = 0;
    std::map<std::string, std::int64_t> grams;
    std::int64_t amount(const std::string& item) const;
    std::int64_t used() const;
    std::int64_t free_space() const;
    void deposit(const std::string& item, std::int64_t amount);
    void withdraw(const std::string& item, std::int64_t amount);
};

struct Recipe {
    std::string name;
    std::string operation; // capability, not a machine/item whitelist
    std::vector<Material> inputs;
    Material product;
    Material scrap;
};

// A standard machine cycle must handle at least this many copies of the recipe.
// One valid machine batch scales ALL ingredients and ALL outputs together.
struct Machine {
    unsigned tier = 1;
    std::string operation;
    bool powered = true;
    bool staffed = true;
    bool maintained = true;
};

struct State {
    Store feed, finished, scrap;
    bool has_work_in_progress = false;
    std::uint32_t ticks_remaining = 0;
    std::vector<Material> reserved_inputs;
    std::uint64_t elapsed_ticks = 0;
    std::uint64_t completed_batches = 0;
};

enum class Blockage {
    None, NoPower, NoStaff, NeedsMaintenance, InsufficientInput,
    FinishedStorageFull, ScrapStorageFull
};

struct AdvanceResult {
    std::uint64_t completed_batches = 0;
    Blockage blockage = Blockage::None;
};

class QuantifiedLine {
public:
    QuantifiedLine(Recipe recipe, Machine machine, State initial);
    // Time may be advanced in one chunk or incrementally, with identical inventory
    // effects when there are no intervening external deliveries/withdrawals.
    AdvanceResult advance(std::uint64_t ticks);
    const State& state() const noexcept { return state_; }
    const Recipe& recipe() const noexcept { return recipe_; }
    const Machine& machine() const noexcept { return machine_; }
    void set_power(bool enabled) noexcept { machine_.powered = enabled; }
    void set_staff(bool available) noexcept { machine_.staffed = available; }
    void set_maintenance(bool serviced) noexcept { machine_.maintained = serviced; }
    Blockage blockage() const;
    std::int64_t total_mass_grams() const;
    std::uint32_t cycle_ticks() const noexcept;
    std::int64_t recipes_per_batch() const noexcept;
private:
    Recipe recipe_;
    Machine machine_;
    State state_;
    std::vector<Material> batch_inputs_;
    Material batch_product_, batch_scrap_;
    bool enough_inputs() const;
    bool finished_space() const;
    bool scrap_space() const;
    void reserve_batch();
    void finish_batch();
};
} // namespace galaxy::industry
