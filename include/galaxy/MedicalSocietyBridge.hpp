#pragma once
// DEMOG-05q6a: read-only verification across REAL PersistentWorld, ART02 family
// registry and ECON03 regional checkpoint. This is a pre-handoff *gate*, not
// a second person authority and NOT an atomic cross-store writer.
#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace galaxy {
struct SocietyMedicalProjection {
    EntityId settlement=0;
    std::uint64_t world_generation=0,world_tick=0;
    std::uint64_t family_revision=0,regional_revision=0;
    std::uint64_t family_population=0,dependent_births=0;
    std::uint64_t living_named=0,dead_named=0,unavailable=0;
    std::uint64_t medically_unavailable=0,traveling=0,affected_dependents=0;
    std::array<std::uint64_t,3> ready_workers{{0,0,0}};
    std::array<std::uint64_t,3> assigned_workers{{0,0,0}};
    // v86 surface workforce counts do not masquerade as ship/habitat jobs.
    std::uint64_t ready_planetary_workers=0,assigned_planetary_workers=0;
    // Recomputed from authoritative snapshots, never stored as people.
    bool reconciled=false;
};
// Requires that family registry and regional checkpoint are already populated.
// Every claim is checked against real world person/casualty/role records. Reject
// missing family death links and dependent child double-counts. Caller should
// re-evaluate at every world generation / region revision change.
SocietyMedicalProjection reconcile_medical_society(
    const PersistentWorld& world,
    const NpcAppearanceStore& family,
    const std::string& family_sqlite_path,
    const std::string& family_scope,
    EntityId home_ship,
    const regional_events::CheckpointStore& region,
    const std::string& regional_colony,
    std::uint64_t expected_region_revision,
    std::uint64_t expected_world_generation);
} // namespace galaxy
