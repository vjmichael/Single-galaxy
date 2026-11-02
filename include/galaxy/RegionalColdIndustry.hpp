#pragma once
// ECON-03c9: real, finite-stock, host-controlled cold economic operations.
// Every action is committed with its regional checkpoint and command receipt.
// The active PersistentWorld adapter and detailed NPC/ship entities are not
// implemented here: this is the authoritative opt-in *cold* backend.
#include "galaxy/RegionalCheckpointStore.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace galaxy::cold_industry {
using regional_events::Cargo;
using regional_events::CheckpointStore;
using regional_events::CommandReceipt;
struct Goods {
    std::int64_t food=0,water=0,oxygen=0,ore=0,metal=0,parts=0;
    std::int64_t mass() const;
};
struct SiteSeed {
    std::string colony;
    std::int64_t x_m=0,y_m=0,z_m=0;
    std::int64_t deposit_ore_kg=100'000'000;
    std::int64_t battery_kwh=2'000'000;
    std::int64_t battery_max_kwh=3'000'000;
    std::int64_t solar_kwh_per_hour=400'000;
    std::int64_t workforce=80'000;
};
struct Work {
    std::string command_id,actor,colony;
    std::uint64_t tick=0;
    std::int64_t mine_kg=0,harvest_food_kg=0;
    Cargo reclaim{}; // organic waste -> nutrients; wastewater -> water; CO2 -> oxygen
    std::int64_t smelt_ore_kg=0,fabricate_metal_kg=0;
    Cargo habitat_supply{}; // reserved physical depot material; delivered next tick
};
// Durable per-colony self-running production policy (quantities per 15-min shift).
// The host may advance many shifts at once; only event/shift boundaries run.
struct AutoPlan {
    std::int64_t mine_kg=0,harvest_food_kg=0;
    std::int64_t recycle_organic_kg=0,reclaim_water_kg=0,reclaim_oxygen_kg=0;
    std::int64_t smelt_ore_kg=0,fabricate_metal_kg=0;
    Cargo habitat_supply{};
};
struct CarrierSeed {
    std::string id,home;
    std::int64_t capacity_kg=900'000;
    std::int64_t speed_m_per_hour=10'000'000;
    std::int64_t fuel_units=1'000'000;
    std::int64_t crew=4;
};
struct Freight {
    std::string id,origin,destination,carrier;
    Goods cargo{};
    std::uint64_t departed=0,arrives=0,returns=0;
    int stage=0; // 0 = physical custody in transit; 1 = credited at destination
};
struct SiteState {
    std::int64_t deposit=0,ore=0,metal=0,parts=0,tailings=0,scrap=0;
    std::int64_t battery=0,battery_max=0,solar_per_hour=0,workforce=0;
    std::uint64_t last_work_tick=0;
};
using HostAuthorization=std::function<bool(const std::string& actor,
                                           const std::string& colony)>;
class Economy {
public:
    Economy(CheckpointStore& store,const std::string& database_path,
            HostAuthorization authorize);
    ~Economy();
    Economy(const Economy&)=delete;
    Economy& operator=(const Economy&)=delete;
    void seed(const SiteSeed& site);     // initial, host-only bootstrap
    void seed_carrier(const CarrierSeed& carrier);
    void set_initial_plan(const std::string& colony,const AutoPlan& plan);
    // Host-driven clock advancement, but no per-colony manual work dispatch.
    // Uses committed schedules/finite inventory; each work batch is receipted.
    std::uint64_t advance_autonomous(const std::string& actor,std::uint64_t through_tick,
                                     std::uint64_t owner_generation);
    SiteState site(const std::string& colony) const;
    std::vector<Freight> freight() const;
    CommandReceipt work(const Work& order,std::uint64_t revision,
                        std::uint64_t owner_generation);
    CommandReceipt dispatch(const std::string& command_id,const std::string& actor,
                            const std::string& freight_id,const std::string& carrier,
                            const std::string& origin,const std::string& destination,
                            Goods cargo,std::uint64_t depart_tick,
                            std::uint64_t revision,std::uint64_t owner_generation);
    CommandReceipt deliver(const std::string& command_id,const std::string& actor,
                           const std::string& freight_id,
                           std::uint64_t revision,std::uint64_t owner_generation);
    const std::string& scope() const noexcept{return scope_;}
private:
    CheckpointStore& store_;
    sqlite3* db_=nullptr;
    std::string scope_;
    HostAuthorization authorize_;
};
} // namespace galaxy::cold_industry
