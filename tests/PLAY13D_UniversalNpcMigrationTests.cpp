#include "galaxy/UniversalNpcMigration.hpp"
#include <cassert>
using namespace galaxy;
using namespace galaxy::migration;
using namespace galaxy::transport;
int main(){
    const Endpoint ship{LocationKind::ColonyShip,10};
    const Endpoint planet{LocationKind::PlanetarySite,900};
    const Endpoint colony{LocationKind::SpaceColony,40};
    const NpcTransport shuttle{55,Mode::PlanetaryShuttle,ship,planet,3,{100,101},
                               TransportState::Arrived};
    const NpcTransport ferry{56,Mode::Ferry,ship,colony,3,{100,101},
                             TransportState::Arrived};
    Admission base{};
    base.command_id=200;base.npc_id=100;base.origin=ship;base.destination=planet;
    base.expected_generation=7;base.current_generation=7;
    base.physical_custody_at_destination=true;
    base.household_reconciled=true;base.verified_consent=true;
    base.housing_reserved=true;base.life_support_reserved=true;
    base.no_competing_residence=true;
    assert(preflight(base,shuttle)==Denial::None);
    auto space=base;space.destination=colony;
    assert(preflight(space,ferry)==Denial::None);
    auto bad=base;bad.verified_consent=false;
    assert(preflight(bad,shuttle)==Denial::ConsentRequired);
    bad=base;bad.household_reconciled=false;
    assert(preflight(bad,shuttle)==Denial::UnresolvedHousehold);
    bad=base;bad.housing_reserved=false;
    assert(preflight(bad,shuttle)==Denial::NoHousing);
    bad=base;bad.life_support_reserved=false;
    assert(preflight(bad,shuttle)==Denial::NoLifeSupport);
    bad=base;bad.physical_custody_at_destination=false;
    assert(preflight(bad,shuttle)==Denial::MissingPhysicalCustody);
    bad=base;bad.no_competing_residence=false;
    assert(preflight(bad,shuttle)==Denial::CompetingResidence);
    bad=base;bad.expected_generation=6;
    assert(preflight(bad,shuttle)==Denial::StaleRevision);
    bad=base;bad.destination=colony;
    assert(preflight(bad,shuttle)==Denial::WrongDestination);
    bad=base;bad.npc_id=102;
    assert(preflight(bad,shuttle)==Denial::InvalidTransport);
    bad=base;bad.command_id=0;
    assert(preflight(bad,shuttle)==Denial::InvalidRequest);
    bad=base;bad.consent_required=false;bad.verified_consent=false;
    assert(preflight(bad,shuttle)==Denial::None); // historical host-only moves
    const NpcTransport not_arrived{55,Mode::PlanetaryShuttle,ship,planet,3,
                                   {100},TransportState::InTransit};
    assert(preflight(base,not_arrived)==Denial::InvalidTransport);
}
