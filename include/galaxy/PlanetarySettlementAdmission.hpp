#pragma once
// PLAY-13D.4C.9A: pure preflight for a permanent surface-residency transfer.
// This is NOT a population writer or a substitute for the authoritative
// migration/SQLite transaction. Inputs must be obtained from canonical state.
#include <cstdint>

namespace galaxy::planetary {

enum class SettlementAdmissionDenial : std::uint8_t {
    None=0, InvalidIdentity, WrongOwnerOrSite, NoOperationalSpaceport,
    NotPhysicallyPresent, AlreadySettled, ActiveTransport, OtherAssignment,
    FamilyCustodyUnresolved, ConsentMissing, InsufficientHousing,
    InsufficientLifeSupport, NoWorkSlot, InvalidRequest
};

struct SettlementAdmissionFacts {
    bool valid_npc=false;
    bool owner_and_site_match=false;
    bool operational_spaceport=false;
    bool surface_custody_matches=false;
    bool shuttle_disembarked_visitor=false;
    bool already_permanently_assigned=false;
    bool active_ferry_or_shuttle_transit=false;
    bool competing_permanent_assignment=false;
    bool guardian_or_dependent_custody_resolved=false;
    bool voluntary_consent_attested=false;
    bool finite_housing_reserved=false;
    bool food_water_oxygen_reserved=false;
    bool permanent_work_slot_reserved=false;
    bool durable_unique_request=false;
};

// Evaluation order is fixed for deterministic denial receipts. A returned
// success permits only the *attempt* to commit a single atomic transfer.
// A second check must occur inside the authoritative writer's transaction.
constexpr SettlementAdmissionDenial preflight_settlement_admission(
    const SettlementAdmissionFacts& f) noexcept {
    if(!f.durable_unique_request) return SettlementAdmissionDenial::InvalidRequest;
    if(!f.valid_npc) return SettlementAdmissionDenial::InvalidIdentity;
    if(!f.owner_and_site_match) return SettlementAdmissionDenial::WrongOwnerOrSite;
    if(!f.operational_spaceport) return SettlementAdmissionDenial::NoOperationalSpaceport;
    if(!f.surface_custody_matches || !f.shuttle_disembarked_visitor)
        return SettlementAdmissionDenial::NotPhysicallyPresent;
    if(f.already_permanently_assigned) return SettlementAdmissionDenial::AlreadySettled;
    if(f.active_ferry_or_shuttle_transit) return SettlementAdmissionDenial::ActiveTransport;
    if(f.competing_permanent_assignment) return SettlementAdmissionDenial::OtherAssignment;
    if(!f.guardian_or_dependent_custody_resolved)
        return SettlementAdmissionDenial::FamilyCustodyUnresolved;
    if(!f.voluntary_consent_attested) return SettlementAdmissionDenial::ConsentMissing;
    if(!f.finite_housing_reserved) return SettlementAdmissionDenial::InsufficientHousing;
    if(!f.food_water_oxygen_reserved) return SettlementAdmissionDenial::InsufficientLifeSupport;
    if(!f.permanent_work_slot_reserved) return SettlementAdmissionDenial::NoWorkSlot;
    return SettlementAdmissionDenial::None;
}

} // namespace galaxy::planetary
