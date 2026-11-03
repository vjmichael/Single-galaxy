#pragma once
// v51a: bounded, authoritative two-to-eight machine network with a material
// warehouse. All physical material movement is journaled in one SQLite file.
// No graphical conveyors or integration with the external player warehouse yet.
#include "galaxy/RegionalFactoryHandoff.hpp"
#include <map>
namespace galaxy::industry {
struct NetworkMachine {
    std::uint64_t id=0;
    EntityId worksite=0;
    std::vector<std::uint64_t> workers;
    Recipe recipe;
    Machine machine;
    State state;
};
struct NetworkState {
    std::uint64_t tick=0;
    std::int64_t initial_mass=0;
    Store warehouse;
    std::vector<NetworkMachine> machines; // sorted unique IDs
};
enum class NetworkBin { Warehouse=0, Feed=1, Finished=2, Scrap=3 };
struct NetworkTransfer {
    std::uint64_t from_machine=0,to_machine=0; // 0 = warehouse
    NetworkBin from=NetworkBin::Warehouse,to=NetworkBin::Warehouse;
    std::string material;
    std::int64_t grams=0;
};
enum class NetworkFault { None, AfterPayload, AfterEvent, AfterWorldSnapshot };
class RegionalProductionNetwork {
public:
    RegionalProductionNetwork(PersistentWorld&,regional_events::CheckpointStore&,
      regional_events::RegionalModeAuthority&,std::string database,
      std::string colony,std::vector<std::string> family_scopes);
    ~RegionalProductionNetwork();
    RegionalProductionNetwork(const RegionalProductionNetwork&)=delete;
    RegionalProductionNetwork& operator=(const RegionalProductionNetwork&)=delete;
    void bootstrap(const NetworkState&,const std::string& source_receipt);
    // v52: single authoritative world+network commit, debiting an existing
    // player-constructed storage and reserving the same mass in world escrow.
    // The first bounded implementation transfers whole kg of aluminum.
    void bootstrap_from_constructed_storage(const NetworkState&,
        const std::string& event_id, EntityId constructed_storage,
        std::uint64_t world_command_id,NetworkFault=NetworkFault::None);
    // v53: return exact whole-kg finished goods from real network stock to
    // the constructed world storage; world escrow and network balance together.
    void return_finished_to_constructed_storage(const std::string& event_id,
        EntityId constructed_storage,std::uint64_t world_command_id,
        const std::string& finished_code,std::int64_t kilograms,
        NetworkFault=NetworkFault::None);
    NetworkState active() const;
    void move(const std::string& event_id,const NetworkTransfer&,NetworkFault=NetworkFault::None);
    std::uint64_t advance_cold(std::uint64_t ticks,const std::string& event_id,
        NetworkFault=NetworkFault::None);
    std::uint64_t advance_detailed(std::uint64_t ticks,const std::string& event_id,
        NetworkFault=NetworkFault::None);
    regional_events::RegionalAuthorityState promote(const std::string& event_id,
        NetworkFault=NetworkFault::None);
    regional_events::RegionalAuthorityState demote(const std::string& event_id,
        NetworkFault=NetworkFault::None);
    void verify()const;
private:
    PersistentWorld& world_;
    regional_events::CheckpointStore& region_;
    regional_events::RegionalModeAuthority& authority_;
    sqlite3* db_=nullptr;
    std::string colony_;
    std::vector<std::string> families_;
    void preflight(const regional_events::RegionalAuthorityState&)const;
};
} // namespace galaxy::industry
