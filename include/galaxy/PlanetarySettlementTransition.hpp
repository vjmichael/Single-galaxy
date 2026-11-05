#pragma once
// PLAY-13D.4C.9B: invariant guard for an authoritative settlement candidate.
// This is a POSTCONDITION verifier, never a competing population ledger.
// The writer must supply values from canonical Simulation before/after clones.
// It does not commit SQLite, establish consent, or fabricate migration receipts.
#include <cstdint>
#include <limits>

namespace galaxy::planetary {

struct SettlementTransitionEvidence {
    std::uint64_t npc_before=0, npc_after=0;
    std::uint64_t source_ship=0, surface_location=0;
    std::uint64_t home_ship_before=0, home_ship_after=0;
    std::uint64_t physical_location_before=0, physical_location_after=0;
    std::uint64_t assignment_before=0, assignment_after=0;
    std::uint32_t role_before=0, role_after=0; // NpcRole: Ship=0, PlanetarySurface=3
    std::int64_t source_crew_before=0, source_crew_after=0;
    std::int64_t permanent_workers_before=0, permanent_workers_after=0;
    std::uint64_t shuttle_visitors_before=0, shuttle_visitors_after=0;
    std::uint64_t shuttle_duty_before=0, shuttle_duty_after=0;
    std::uint64_t population_before=0, population_after=0;
    std::int64_t material_total_before=0, material_total_after=0;
    bool was_disembarked=false, still_disembarked=false;
    bool had_temporary_duty=false, still_has_temporary_duty=false;
    bool active_transport_before=false, active_transport_after=false;
    bool has_registered_dependent=false;
    bool consent_verified_by_authoritative_migration_store=false;
    bool resource_reservation_verified=false;
    bool household_residence_verified=false;
};

// Early bounded gate: single unencumbered consenting adult promoted into a
// counted permanent work slot on the same site. A separate resident-not-worker
// pathway must be implemented rather than forced into these accounting rules.
// Family moves deliberately fail closed until physical co-manifest support.
constexpr bool valid_settlement_transition(
    const SettlementTransitionEvidence& e) noexcept {
    if(!e.npc_before || e.npc_before!=e.npc_after ||
       !e.source_ship || !e.surface_location ||
       e.home_ship_before!=e.source_ship ||
       e.home_ship_after!=e.source_ship ||
       e.physical_location_before!=e.surface_location ||
       e.physical_location_after!=e.surface_location ||
       e.assignment_before!=e.source_ship ||
       e.assignment_after!=e.surface_location ||
       e.role_before!=0 || e.role_after!=3 ||
       !e.was_disembarked || e.still_disembarked ||
       e.active_transport_before || e.active_transport_after ||
       e.has_registered_dependent ||
       !e.consent_verified_by_authoritative_migration_store ||
       !e.resource_reservation_verified ||
       !e.household_residence_verified ||
       e.population_before!=e.population_after ||
       e.material_total_before!=e.material_total_after ||
       e.source_crew_before<=0 ||
       e.source_crew_after!=e.source_crew_before-1 ||
       e.permanent_workers_before<0 ||
       e.permanent_workers_before==std::numeric_limits<std::int64_t>::max() ||
       e.permanent_workers_after!=e.permanent_workers_before+1 ||
       !e.shuttle_visitors_before ||
       e.shuttle_visitors_after!=e.shuttle_visitors_before-1 ||
       e.still_has_temporary_duty ||
       (e.had_temporary_duty?
          (!e.shuttle_duty_before ||
           e.shuttle_duty_after!=e.shuttle_duty_before-1):
          e.shuttle_duty_after!=e.shuttle_duty_before))
        return false;
    return true;
}

} // namespace galaxy::planetary
