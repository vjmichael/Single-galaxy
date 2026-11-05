#pragma once
// Mode-independent admission envelope for ONE authoritative migration writer.
// This is a fail-closed preflight, not a second residence ledger or a writer.
// All flags MUST be derived by the host from the existing family, immigration,
// transport, housing and world journal; the caller cannot attest its own move.
#include "galaxy/NpcTransport.hpp"
#include <cstdint>

namespace galaxy::migration {
enum class Denial : std::uint8_t {
    None=0, InvalidRequest, InvalidTransport, WrongDestination,
    MissingPhysicalCustody, UnresolvedHousehold, ConsentRequired,
    NoHousing, NoLifeSupport, StaleRevision, CompetingResidence
};
struct Admission {
    std::uint64_t command_id=0,npc_id=0;
    transport::Endpoint origin{},destination{};
    std::uint64_t expected_generation=0,current_generation=0;
    bool physical_custody_at_destination=false;
    bool household_reconciled=false;
    bool consent_required=true;
    bool verified_consent=false;
    bool housing_reserved=false;
    bool life_support_reserved=false;
    bool no_competing_residence=false;
};

// A vehicle mode does not choose migration eligibility. A visitor can live
// permanently without a job; employment must use a separate job-capacity gate.
inline Denial preflight(const Admission& a,const transport::NpcTransport& t) {
    if(!a.command_id || !a.npc_id || a.origin.id==0 || a.destination.id==0 ||
       a.origin==a.destination)return Denial::InvalidRequest;
    if(!transport::completed_transport(t,a.npc_id))
        return Denial::InvalidTransport;
    if(t.source()!=a.origin || t.destination()!=a.destination)
        return Denial::WrongDestination;
    if(!a.expected_generation || a.expected_generation!=a.current_generation)
        return Denial::StaleRevision;
    if(!a.physical_custody_at_destination)
        return Denial::MissingPhysicalCustody;
    if(!a.household_reconciled)
        return Denial::UnresolvedHousehold;
    if(a.consent_required && !a.verified_consent)
        return Denial::ConsentRequired;
    if(!a.housing_reserved)
        return Denial::NoHousing;
    if(!a.life_support_reserved)
        return Denial::NoLifeSupport;
    if(!a.no_competing_residence)
        return Denial::CompetingResidence;
    return Denial::None;
}
} // namespace galaxy::migration
