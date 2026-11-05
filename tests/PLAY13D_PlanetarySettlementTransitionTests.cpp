#include "galaxy/PlanetarySettlementTransition.hpp"
#include <cassert>
using namespace galaxy::planetary;

constexpr SettlementTransitionEvidence valid() {
    SettlementTransitionEvidence e{};
    e.npc_before=17;e.npc_after=17;
    e.source_ship=4;e.surface_location=500;
    e.home_ship_before=4;e.home_ship_after=4;
    e.physical_location_before=500;e.physical_location_after=500;
    e.assignment_before=4;e.assignment_after=500;
    e.role_before=0;e.role_after=3;
    e.source_crew_before=6;e.source_crew_after=5;
    e.permanent_workers_before=2;e.permanent_workers_after=3;
    e.shuttle_visitors_before=1;e.shuttle_visitors_after=0;
    e.shuttle_duty_before=1;e.shuttle_duty_after=0;
    e.population_before=8;e.population_after=8;
    e.material_total_before=1000;e.material_total_after=1000;
    e.was_disembarked=true;e.had_temporary_duty=true;
    e.consent_verified_by_authoritative_migration_store=true;
    e.resource_reservation_verified=true;
    e.household_residence_verified=true;
    return e;
}
static_assert(valid_settlement_transition(valid()));
int main(){
    auto base=valid();
    auto reject=[&](auto f){auto e=base;f(e);assert(!valid_settlement_transition(e));};
    reject([](auto& e){e.npc_after=18;});
    reject([](auto& e){e.source_crew_after=6;});
    reject([](auto& e){e.permanent_workers_after=2;});
    reject([](auto& e){e.population_after=9;});
    reject([](auto& e){e.material_total_after=999;});
    reject([](auto& e){e.home_ship_after=100;});
    reject([](auto& e){e.physical_location_after=4;});
    reject([](auto& e){e.assignment_after=4;});
    reject([](auto& e){e.role_after=0;});
    reject([](auto& e){e.shuttle_visitors_after=1;});
    reject([](auto& e){e.still_disembarked=true;});
    reject([](auto& e){e.still_has_temporary_duty=true;});
    reject([](auto& e){e.shuttle_duty_after=1;});
    reject([](auto& e){e.active_transport_after=true;});
    reject([](auto& e){e.has_registered_dependent=true;});
    reject([](auto& e){e.consent_verified_by_authoritative_migration_store=false;});
    reject([](auto& e){e.resource_reservation_verified=false;});
    reject([](auto& e){e.household_residence_verified=false;});
    auto without_duty=base;
    without_duty.had_temporary_duty=false;
    without_duty.shuttle_duty_before=0;
    without_duty.shuttle_duty_after=0;
    assert(valid_settlement_transition(without_duty));
}
