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
// First executable scope: exactly two physically present parent/guardian
// adults and ALL their legally and physically accounted children, on one
// same-owner receiving ship. More complex households fail closed.
struct ExecutionPlan {
    DeparturePlan prepared;
    std::uint64_t command_id=0;
    std::uint64_t expected_region_revision=0,expected_owner_generation=0;
    std::uint64_t expected_origin_family_revision=0;
    std::uint64_t expected_destination_family_revision=0;
    // Legacy single-household move events stay source-compatible.
    // For the v77 two-household gate these fields MUST remain empty/zero.
    std::uint64_t co_parent_event_id=0;
    std::vector<std::uint64_t> child_move_events;
    struct ChildMove {
        std::uint64_t child_id=0, event_id=0;
    };
    struct Household {
        // Canonical guardian < co-parent; both are actual resident parents.
        std::uint64_t guardian_id=0, co_parent_id=0;
        std::uint64_t guardian_event_id=0, co_parent_event_id=0;
        std::vector<ChildMove> children; // sorted, exact ART02 descendants
    };
    // v77: exactly two disjoint parent pairs in one family/region scope,
    // with at least one living dependent per pair. No split destinations.
    // The first guardian's event ID is the world departure command ID.
    std::vector<Household> households;
    std::string region_scope,origin_colony,destination_colony;
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
