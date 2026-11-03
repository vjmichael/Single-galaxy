#pragma once
// v49a: a fail-closed read-only pre-handoff tripwire. This is NOT a warm
// industrial runtime and does not mutate the simulation or regional owner.
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy {
struct RegionalPopulationProof {
    std::uint64_t world_generation=0,world_tick=0;
    std::uint64_t checkpoint_revision=0,owner_generation=0;
    std::uint64_t living_named=0,dependents=0,registered=0;
    std::uint64_t named_away_or_transit=0,named_present_for_duty=0;
};
// One shared settlement may have multiple ART02 family registries, but their
// resident identity sets MUST be disjoint and exhaust every living named NPC.
// The sum of exact current family census entries must equal the corresponding
// real ECON03 regional colony checkpoint. Registered home != physical location.
// Strict presence is a conservative workforce pre-handoff gate; passing it
// does not certify industrial effects or qualify as full warm promotion.
RegionalPopulationProof verify_regional_population(const PersistentWorld& world,
    const std::string& sqlite_path,EntityId home_ship,
    const std::vector<std::string>& family_scopes,const std::string& region_scope,
    const std::string& regional_colony,std::uint64_t expected_region_revision,
    std::uint64_t expected_owner_generation,bool require_named_present=false);
}
