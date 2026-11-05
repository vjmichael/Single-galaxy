#include "galaxy/TransportIdentity.hpp"
#include "galaxy/NpcTransport.hpp"
#include <cassert>
using namespace galaxy::transport;
int main() {
    assert(canonical_manifest({11,12,13}));
    assert(!canonical_manifest({}));
    assert(!canonical_manifest({0,11}));
    assert(!canonical_manifest({11,11}));
    assert(!canonical_manifest({12,11}));
    const ArrivalEvidence ferry{Mode::Ferry,Phase::Arrived,900,2,3,{11,12}};
    const ArrivalEvidence shuttle{Mode::PlanetaryShuttle,Phase::Arrived,901,2,3,{11,12}};
    assert(completed_arrival(ferry,11,2,3));
    assert(completed_arrival(shuttle,12,2,3));
    assert(!completed_arrival(ferry,13,2,3));
    assert(!completed_arrival(ferry,11,3,2));
    assert(!completed_arrival({Mode::Ferry,Phase::InTransit,900,2,3,{11}},11,2,3));
    assert(!completed_arrival({Mode::Ferry,Phase::Arrived,0,2,3,{11}},11,2,3));
    assert(!completed_arrival({Mode::Ferry,Phase::Arrived,900,2,3,{11,11}},11,2,3));
    const Endpoint ship{LocationKind::ColonyShip,2};
    const Endpoint planet{LocationKind::PlanetarySite,3};
    const Endpoint colony{LocationKind::SpaceColony,3};
    const Endpoint site{LocationKind::OtherNpcLocation,4};
    assert(completed_arrival(shuttle,11,ship,planet));
    assert(completed_arrival(ferry,11,ship,colony));
    assert(!completed_arrival(shuttle,13,ship,planet));
    assert(!completed_arrival(shuttle,11,planet,ship));
    assert(!completed_arrival({Mode::PlanetaryShuttle,Phase::Booked,901,2,3,{11}},11,ship,planet));
    assert(!completed_arrival({Mode::PlanetaryShuttle,Phase::Arrived,0,2,3,{11}},11,ship,planet));
    assert(!completed_arrival({Mode::PlanetaryShuttle,Phase::Arrived,901,2,3,{11,11}},11,ship,planet));
    // A ground transport can use identical rules across arbitrary named locations.
    const ArrivalEvidence ground{Mode::SurfaceVehicle,Phase::Arrived,902,3,4,{11,12}};
    assert(completed_arrival(ground,11,planet,site));
    assert(!completed_arrival(ground,11,site,planet));
    assert(!completed_arrival(ground,11,Endpoint{LocationKind::PlanetarySite,0},site));
    const NpcTransport ferry_vehicle{900,Mode::Ferry,ship,colony,3,{11,12},TransportState::Arrived};
    const NpcTransport shuttle_vehicle{901,Mode::PlanetaryShuttle,ship,planet,3,{11,12},TransportState::Arrived};
    const NpcTransport ground_vehicle{902,Mode::SurfaceVehicle,planet,site,3,{11,12},TransportState::Arrived};
    assert(completed_transport(ferry_vehicle,11));
    assert(completed_transport(shuttle_vehicle,12));
    assert(completed_transport(ground_vehicle,11));
    assert(!completed_transport(ground_vehicle,13));
    assert(!completed_transport(NpcTransport{902,Mode::SurfaceVehicle,planet,site,1,{11,12},TransportState::Arrived},11));
    assert(!completed_transport(NpcTransport{902,Mode::SurfaceVehicle,planet,site,3,{11,11},TransportState::Arrived},11));
    assert(!completed_transport(NpcTransport{902,Mode::SurfaceVehicle,planet,site,3,{11},TransportState::InTransit},11));
    assert(!completed_transport(NpcTransport{0,Mode::Ferry,ship,colony,3,{11},TransportState::Arrived},11));
}
