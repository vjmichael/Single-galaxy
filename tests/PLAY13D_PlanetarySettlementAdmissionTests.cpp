#include "galaxy/PlanetarySettlementAdmission.hpp"
#include <cassert>
using namespace galaxy::planetary;
constexpr SettlementAdmissionFacts eligible(){
    return {true,true,true,true,true,false,false,false,true,true,true,true,true,true};
}
constexpr auto accepted=eligible();
static_assert(preflight_settlement_admission(accepted)==SettlementAdmissionDenial::None);
constexpr auto missing_consent=[]{
    auto f=eligible();f.voluntary_consent_attested=false;return f;
}();
static_assert(preflight_settlement_admission(missing_consent)==SettlementAdmissionDenial::ConsentMissing);
constexpr auto absent_surface=[]{
    auto f=eligible();f.surface_custody_matches=false;return f;
}();
static_assert(preflight_settlement_admission(absent_surface)==SettlementAdmissionDenial::NotPhysicallyPresent);
int main(){
    const auto base=eligible();
    auto deny=[&](auto change,SettlementAdmissionDenial expected){
        auto f=base;change(f);assert(preflight_settlement_admission(f)==expected);
    };
    deny([](auto& f){f.durable_unique_request=false;},SettlementAdmissionDenial::InvalidRequest);
    deny([](auto& f){f.valid_npc=false;},SettlementAdmissionDenial::InvalidIdentity);
    deny([](auto& f){f.owner_and_site_match=false;},SettlementAdmissionDenial::WrongOwnerOrSite);
    deny([](auto& f){f.operational_spaceport=false;},SettlementAdmissionDenial::NoOperationalSpaceport);
    deny([](auto& f){f.shuttle_disembarked_visitor=false;},SettlementAdmissionDenial::NotPhysicallyPresent);
    deny([](auto& f){f.already_permanently_assigned=true;},SettlementAdmissionDenial::AlreadySettled);
    deny([](auto& f){f.active_ferry_or_shuttle_transit=true;},SettlementAdmissionDenial::ActiveTransport);
    deny([](auto& f){f.competing_permanent_assignment=true;},SettlementAdmissionDenial::OtherAssignment);
    deny([](auto& f){f.guardian_or_dependent_custody_resolved=false;},SettlementAdmissionDenial::FamilyCustodyUnresolved);
    deny([](auto& f){f.voluntary_consent_attested=false;},SettlementAdmissionDenial::ConsentMissing);
    deny([](auto& f){f.finite_housing_reserved=false;},SettlementAdmissionDenial::InsufficientHousing);
    deny([](auto& f){f.food_water_oxygen_reserved=false;},SettlementAdmissionDenial::InsufficientLifeSupport);
    deny([](auto& f){f.permanent_work_slot_reserved=false;},SettlementAdmissionDenial::NoWorkSlot);
}
