#pragma once
// v50: real, bounded single-factory cold/detailed state handoff. The ordinary
// full-game industrial runtime has not yet adopted this adapter.
#include "galaxy/QuantifiedProduction.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalPopulationAudit.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::industry {
enum class HandoffFailure { None, AfterFactoryWrite, AfterEventWrite };
struct DurableFactory {
    EntityId home_ship=0;
    std::vector<std::uint64_t> workers;
    Recipe recipe;
    Machine machine;
    State state;
    std::int64_t initial_mass=0;
};
// All operations require a single physical SQLite database and stable region
// authority. Named factory stock is a host-authorized escrow, not free goods.
class RegionalFactoryHandoff {
public:
    RegionalFactoryHandoff(PersistentWorld& world,regional_events::CheckpointStore& region,
        regional_events::RegionalModeAuthority& authority,std::string sqlite_path,
        std::string colony,std::vector<std::string> family_scopes);
    ~RegionalFactoryHandoff();
    RegionalFactoryHandoff(const RegionalFactoryHandoff&)=delete;
    RegionalFactoryHandoff& operator=(const RegionalFactoryHandoff&)=delete;
    void bootstrap(const DurableFactory& factory,const std::string& stock_receipt);
    // Returns the actual active material/WIP snapshot, not the inactive cold copy.
    DurableFactory active() const;
    std::uint64_t advance_cold(std::uint64_t ticks,const std::string& command_id,
        HandoffFailure fault=HandoffFailure::None);
    regional_events::RegionalAuthorityState promote(const std::string& id,
        HandoffFailure fault=HandoffFailure::None);
    std::uint64_t advance_detailed(std::uint64_t ticks,const std::string& command_id,
        HandoffFailure fault=HandoffFailure::None);
    regional_events::RegionalAuthorityState demote(const std::string& id,
        HandoffFailure fault=HandoffFailure::None);
    // Cross-checks saved owner, cold and detailed states, immutable event chain,
    // deterministic quantified production and the active workforce proof.
    void verify() const;
private:
    PersistentWorld& world_;
    regional_events::CheckpointStore& region_;
    regional_events::RegionalModeAuthority& authority_;
    sqlite3* db_=nullptr;
    std::string colony_;
    std::vector<std::string> families_;
    void preflight(const regional_events::RegionalAuthorityState& owner,
        bool require_stable=true) const;
};
} // namespace galaxy::industry
