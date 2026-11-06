#pragma once
// WORK-01B.3: explicit, bounded policy for already-authorized original work.
// Read-only: no emergency executor, NPC displacement, stock escrow, or
// inference of new owner/supplier permission is created here.
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace galaxy::work {

enum class PolicyHold : std::uint8_t {
    None=0,LifeSupportEmergency=1,PhysicalFundingUnavailable=2
};
enum class PolicyClass : std::uint8_t {
    NewDiscretionaryFunding=0,
    CommittedConstructionOrHandover=1,
    FinalStaffing=2
};
struct WorkPolicyContext {
    // Production derives this only from the original ColonySafety risk
    // assessment (Critical/Emergency) on an actually occupied compartment.
    // This callback must not mutate the world or dispatch a rescue.
    std::function<bool(EntityId)> critical_life_support{};
    // Original Simulation-owned dry run of buyer payment and supplier yard
    // readiness, before selecting a scarce physical slip.
    std::function<bool(PlayerId,std::uint64_t)> can_fund_contract{};
    // Fairness rotation on ties occurs once per committed simulation tick.
    // No wall clock, random seed, new persistent ledger or inferred skill.
    std::uint64_t fairness_quantum_ticks=1;
};
inline PolicyClass policy_class(ShipyardCustomerStage stage,
                               bool requires_staffing) noexcept {
    switch(stage){
    case ShipyardCustomerStage::Requested:
    case ShipyardCustomerStage::Accepted:
        return PolicyClass::NewDiscretionaryFunding;
    case ShipyardCustomerStage::Funded:
        return PolicyClass::CommittedConstructionOrHandover;
    case ShipyardCustomerStage::Delivered:
        return requires_staffing ? PolicyClass::FinalStaffing :
            PolicyClass::CommittedConstructionOrHandover;
    }
    return PolicyClass::NewDiscretionaryFunding;
}
inline PolicyHold inspect_policy(const WorkPolicyContext& context,
                                  const WorkDirective& directive,
                                  const ShipyardCustomerOrder& contract) {
    // Completion of already-paid manufacture and physical hull handover
    // continues. They cannot be undone by a policy hold.
    const bool voluntary_material_debit=
        contract.stage==ShipyardCustomerStage::Accepted;
    const bool voluntary_crew_transfer=
        contract.stage==ShipyardCustomerStage::Delivered &&
        directive.commissioning_crew[0]!=0;
    if((voluntary_material_debit || voluntary_crew_transfer) &&
       context.critical_life_support &&
       context.critical_life_support(directive.colony_ship))
        return PolicyHold::LifeSupportEmergency;
    if(voluntary_material_debit && context.can_fund_contract &&
       !context.can_fund_contract(directive.owner,directive.source_order))
        return PolicyHold::PhysicalFundingUnavailable;
    return PolicyHold::None;
}
// Tied priority bands rotate through canonical IDs at deterministic ticks.
// When age caps at 999, even requests posted at different earlier ticks
// eventually share a band. Any work blocked by native constraints does not
// reserve slots and thus cannot starve an executable peer.
// This does not guarantee fairness between unrelated, non-equivalent stages.
template<class Row>
void fair_rotate_ties(std::vector<Row>& rows,std::uint64_t tick,
                      std::uint64_t quantum=1){
    if(quantum==0)quantum=1;
    std::size_t first=0;
    while(first<rows.size()){
        std::size_t end=first+1;
        while(end<rows.size() && rows[end].priority==rows[first].priority)
            ++end;
        const auto n=end-first;
        if(n>1){
            // Stable ID order is guaranteed by the caller. Rotation is
            // bounded by the actual tie cohort, never arbitrary IDs.
            const auto shift=static_cast<std::size_t>((tick/quantum)%n);
            std::rotate(rows.begin()+first,rows.begin()+first+shift,
                        rows.begin()+end);
        }
        first=end;
    }
}
} // namespace galaxy::work
