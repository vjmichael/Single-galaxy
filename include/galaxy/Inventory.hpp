#pragma once
// ECON-02B2a: reusable *game-volume* item catalogue and exact stack transfers.
// No mass, density or payload-weight rules; 1 m3 = 1,000 internal litres.
// StackStore is a value object. Simulation remains the authoritative state holder.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::inventory {
struct ItemDefinition {
    std::string code;
    std::int64_t liters_per_unit=0;
};

class Catalogue {
public:
    explicit Catalogue(std::vector<ItemDefinition> definitions);
    static const Catalogue& standard();
    bool contains(const std::string& code) const noexcept;
    std::int64_t liters_per_unit(const std::string& code) const;
private:
    std::map<std::string,std::int64_t> volumes_;
};

enum class TransferStatus {
    Applied, InvalidItem, InvalidAmount, SourceShortage, DestinationFull,
    IncompatibleCatalogue
};

class StackStore {
public:
    StackStore(const Catalogue& catalogue, std::int64_t capacity_liters);
    // For reconstructing pre-existing, authoritative world state. Validates
    // catalogue membership, nonnegative quantity and available volume.
    void load(const std::string& code, std::int64_t amount);
    TransferStatus transfer_to(StackStore& destination,
                               const std::string& code, std::int64_t amount);
    std::int64_t amount(const std::string& code) const noexcept;
    std::int64_t used_liters() const noexcept { return used_liters_; }
    std::int64_t capacity_liters() const noexcept { return capacity_liters_; }
    std::int64_t free_liters() const noexcept { return capacity_liters_-used_liters_; }
    const std::map<std::string,std::int64_t>& stacks() const noexcept { return stacks_; }
private:
    const Catalogue* catalogue_;
    std::int64_t capacity_liters_;
    std::int64_t used_liters_=0;
    std::map<std::string,std::int64_t> stacks_;
};
} // namespace galaxy::inventory
