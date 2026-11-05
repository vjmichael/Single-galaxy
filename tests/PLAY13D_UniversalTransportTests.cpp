#include "galaxy/UniversalTransport.hpp"
#include <cassert>
using namespace galaxy::transport;
int main(){
    const Endpoint orbit{LocationKind::ColonyShip,10};
    const Endpoint ground{LocationKind::PlanetarySite,99};
    const Endpoint unrelated{LocationKind::SpaceColony,99};
    Manifest shuttle{};
    shuttle.vehicle=222;shuttle.owner=7;
    shuttle.mode=Mode::PlanetaryShuttle;
    shuttle.berth=orbit;shuttle.other_end=ground;
    shuttle.pilot=41;shuttle.seat_capacity=3;shuttle.cargo_capacity_kg=1000;
    assert(shuttle.valid());
    shuttle.contents={
        {GoodKind::Npc,42,1,0},
        {GoodKind::Npc,43,1,0},
        {GoodKind::Resource,1,300,300},
        {GoodKind::Resource,3,500,500}
    };
    assert(shuttle.valid());
    assert(contains_good(shuttle,GoodKind::Npc,43,1));
    assert(contains_good(shuttle,GoodKind::Resource,3,500));
    assert(!contains_good(shuttle,GoodKind::Resource,3,501));
    const Instruction outbound{
        1001,7,222,orbit,ground,{{GoodKind::Resource,3,500,500}}};
    const Instruction return_trip{
        1002,7,222,ground,orbit,{{GoodKind::Resource,3,500,500}}};
    assert(outbound.valid() && serves(shuttle,outbound));
    assert(serves(shuttle,return_trip));
    auto wrong=outbound;wrong.owner=8;
    assert(wrong.valid() && !serves(shuttle,wrong));
    wrong=outbound;wrong.vehicle=223;
    assert(!serves(shuttle,wrong));
    wrong=outbound;wrong.delivery=unrelated;
    assert(!serves(shuttle,wrong));
    wrong=outbound;wrong.pickup=wrong.delivery;
    assert(!wrong.valid());
    wrong=outbound;wrong.command_id=0;
    assert(!wrong.valid());
    wrong=outbound;wrong.goods.push_back({GoodKind::Resource,3,2,2});
    assert(!wrong.valid());
    wrong=outbound;wrong.goods[0]={GoodKind::Resource,1,100,99};
    assert(!wrong.valid());
    wrong=outbound;wrong.goods[0]={GoodKind::Equipment,0,1,10};
    assert(!wrong.valid());
    wrong=outbound;wrong.goods[0]={GoodKind::Equipment,100,2,10};
    assert(wrong.valid()); // schema only: vehicle adapters still fail closed
    assert(valid_good({GoodKind::Npc,51,1,0}));
    assert(!valid_good({GoodKind::Npc,51,2,0}));
    assert(!valid_good({GoodKind::Npc,51,1,80}));
    assert(!valid_good({GoodKind::Resource,99,1,1}));
    assert(!valid_good({GoodKind::Resource,1,1,0}));
    auto invalid=shuttle;
    invalid.contents.push_back({GoodKind::Resource,3,5,5});
    assert(!invalid.valid());
    invalid=shuttle;
    invalid.contents[3].amount=701;invalid.contents[3].mass_kg=701;
    assert(!invalid.valid()); // 300 + 701 exceeds shared cargo hold
    invalid=shuttle;
    invalid.contents.insert(invalid.contents.begin()+2,{GoodKind::Npc,44,1,0});
    assert(invalid.valid());
    invalid.contents.insert(invalid.contents.begin()+3,{GoodKind::Npc,45,1,0});
    assert(!invalid.valid()); // real seats, not cargo mass
    invalid=shuttle;invalid.pilot=42;
    assert(!invalid.valid()); // pilot is never also a passenger
    invalid=shuttle;invalid.in_transit=true;
    assert(!invalid.valid()); // in-flight receipt required
    invalid.flight_receipt=777;
    assert(invalid.valid());
    invalid=shuttle;invalid.berth=unrelated;invalid.other_end=unrelated;
    assert(!invalid.valid());
    return 0;
}
