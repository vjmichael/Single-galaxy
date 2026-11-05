#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace galaxy::planetary {

// PLAY-13D.1: physical material-custody model. All quantities are kg.
// This component does not bypass the authoritative PersistentWorld host.
// Integration with host authorization, celestial arrival, receipts and SQLite
// is a subsequent gate; no standalone API here is a player command.
using SiteId = std::uint64_t;
enum class Material { Water, Nutrients, Food, Wastewater, OrganicWaste, Compost };
struct Stock {
    std::int64_t water=0, nutrients=0, food=0, wastewater=0, organic_waste=0, compost=0;
    std::int64_t total() const;
    std::int64_t& at(Material m);
    const std::int64_t& at(Material m) const;
};
struct Site {
    SiteId body=0;
    std::uint64_t owner=0;
    Stock stock{};
    std::uint64_t harvest_cycles=0;
};
class MaterialCustody {
public:
    // Genesis inventories must be injected ONCE by the real world genesis
    // authority, never by player commands or routine ticks.
    void genesis_site(SiteId body, std::uint64_t owner, const Stock& starting);
    void genesis_ship(std::uint64_t ship, const Stock& starting);
    const Site& site(SiteId body) const;
    const Stock& ship(std::uint64_t ship) const;
    std::int64_t total_mass() const;
    // Transfers require the host to independently attest physical co-location
    // and owner authorization. The domain layer enforces quantities and custody.
    bool transfer_ship_to_site(std::uint64_t ship, SiteId body, Material material, std::int64_t kg);
    bool transfer_site_to_ship(SiteId body, std::uint64_t ship, Material material, std::int64_t kg);
    // Gameplay mass-balanced recipes; intentionally not chemical stoichiometry.
    // Farm: 10 kg water + 5 kg nutrients -> 12 kg food + 3 kg wastewater.
    bool grow(SiteId body, std::int64_t batches);
    // Consumption: 12 kg food -> 8 kg organic waste + 4 kg wastewater.
    bool consume_ship_food(std::uint64_t ship, std::int64_t batches);
    // Compost: 8 kg organic waste -> 6 kg compost + 2 kg wastewater.
    bool compost(SiteId body, std::int64_t batches);
private:
    std::map<SiteId,Site> sites_;
    std::map<std::uint64_t,Stock> ships_;
};
} // namespace galaxy::planetary
