#pragma once
// ECON-03c10: durable materialization of an ACTIVE economic region.
// Active entities are bounded facility/carrier records. Population remains a
// population cohort, not one million instanced NPCs. Physical inventory is
// NEVER copied: the real scheduler/industry/freight ledgers remain authoritative.
#include "galaxy/RegionalColdIndustry.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::warm_region {
struct Entity {
    std::string id,colony,kind;
    std::int64_t x=0,y=0,z=0;
    std::uint64_t population=0; // Only habitat cohort entities carry population
};
struct Snapshot {
    std::uint64_t generation=0,tick=0;
    std::vector<Entity> entities;
};
class Runtime {
public:
    Runtime(regional_events::CheckpointStore& store,
            regional_events::RegionalModeAuthority& authority,
            cold_industry::Economy& economy,
            const std::string& sqlite_path);
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    regional_events::RegionalAuthorityState activate(const std::string& handoff_id);
    regional_events::RegionalAuthorityState deactivate(const std::string& handoff_id);
    Snapshot reconstruct() const; // Reads durable, host-authorized active identity manifest
    std::uint64_t run_active_shift(const std::string& actor,std::uint64_t tick);
private:
    regional_events::CheckpointStore& store_;
    regional_events::RegionalModeAuthority& authority_;
    cold_industry::Economy& economy_;
    sqlite3* db_=nullptr;
    std::string scope_;
};
} // namespace galaxy::warm_region
