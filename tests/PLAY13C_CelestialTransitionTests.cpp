#ifdef NDEBUG
#undef NDEBUG
#endif
#include "galaxy/CelestialFrameTransition.hpp"
#include <cassert>
using namespace galaxy;
int main() {
    Simulation world(0xB70B);
    const auto own=world.add_colony_ship(21,{0,0},60,4);
    world.provision_starter_exploration_scanner(own);
    world.ensure_physical_celestial_systems();
    const auto sid=world.find(own)->system_id;
    const auto bodies=world.physical_celestial_systems().at(sid);
    assert(!bodies.empty());
    const auto body=bodies.front().id;
    const auto first=world.canonical_state();
    auto denied=plan_celestial_frame_arrival(world,21,own,body,0);
    assert(denied.rejection==FrameTransitionRejection::MissingTransit);
    denied=plan_celestial_frame_arrival(world,22,own,body,888);
    assert(denied.rejection==FrameTransitionRejection::Unauthorized);
    denied=plan_celestial_frame_arrival(world,21,own,(std::uint64_t(sid+1)<<32)|1,888);
    assert(denied.rejection==FrameTransitionRejection::WrongSystem);
    denied=plan_celestial_frame_arrival(world,21,own,(std::uint64_t(sid)<<32)|999999,888);
    assert(denied.rejection==FrameTransitionRejection::InvalidBody);
    auto result=plan_celestial_frame_arrival(world,21,own,body,888);
    assert(result.rejection==FrameTransitionRejection::None && result.plan);
    const auto& p=*result.plan;
    assert(p.ship_id==own && p.owner==21 && p.destination_body==body);
    assert(p.completed_transit_id==888 && p.arrival_position.x==0 &&
           p.arrival_position.y==0 && p.arrival_velocity.x==0);
    auto arrival=local_to_cosmic(p.destination_frame,{0,0,0});
    assert(arrival && arrival->x_mm==bodies.front().position.x_mm+
           std::int64_t(bodies.front().physical_radius_mm)+100000000LL);
    // Planning is read-only; neither crew, inventory, ship physics nor the
    // canonical persistent world snapshot can change during preflight.
    assert(world.canonical_state()==first);
    assert(world.colony_ship(own)->crew==4);
}
