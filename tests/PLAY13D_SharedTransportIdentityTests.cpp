#include "galaxy/TransportIdentity.hpp"
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
    assert(completed_shuttle_visit(900,true,{11,12},11));
    assert(!completed_shuttle_visit(0,true,{11},11));
    assert(!completed_shuttle_visit(900,false,{11},11));
    assert(!completed_shuttle_visit(900,true,{11},12));
    assert(!completed_shuttle_visit(900,true,{11,11},11));
}
