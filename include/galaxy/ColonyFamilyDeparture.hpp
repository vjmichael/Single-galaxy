#pragma once
// COLONY-AUTO-04D.4D1: ART02 family proof against canonical world identities.
// This is a preflight, not a second child identity or a rescue execution.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <string>
#include <vector>
struct sqlite3;
namespace galaxy::colony_family {
struct Dependent {
    std::uint64_t child_id=0, guardian_id=0;
};
struct DeparturePlan {
    EntityId source=0, receiver=0;
    // These are for durable host preparation, not the read-only audit.
    std::uint64_t intent_id=0, expected_world_generation=0;
    std::string family_scope;
    std::uint64_t family_seed=0;
    std::vector<std::uint64_t> adult_ids;  // entire world population, sorted
    std::vector<Dependent> children;      // entire ART02 child population, sorted
};
struct Audit {
    std::uint64_t world_tick=0;
    std::uint64_t named_adults=0, dependent_children=0;
    bool complete_adult_roster=false;
    bool complete_child_roster=false;
    bool parent_or_guardian_custody=false;
    bool birth_provenance=false;
    bool census_reconciled=false;
    bool receiving_supplies=false;
    bool ready_for_transaction=false;
    // No one can claim complete evacuation without world + ART02 + ECON03
    // commit and child needs in the simulation.
    bool evacuated=false;
    std::string refusal;
};
Audit assess(const Simulation& world,sqlite3* authoritative_db,
             const DeparturePlan& request);
}
