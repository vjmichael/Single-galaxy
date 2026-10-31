#include "galaxy/GateTransit.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace galaxy {
namespace {
bool in_bounds(Vector2 v){return std::llabs(v.x)<=Simulation::kWorldBoundMm &&
                                  std::llabs(v.y)<=Simulation::kWorldBoundMm;}
}
void GateTransit::add_endpoint(GateEndpoint gate) {
    if(!gate.id||!gate.origin||!gate.destination||gate.origin==gate.destination||
       !in_bounds(gate.position)||!in_bounds(gate.arrival_center))
        throw std::invalid_argument("invalid gate endpoint");
    (void)map_.system(gate.origin);(void)map_.system(gate.destination);
    if(!gates_.emplace(gate.id,GateRecord{gate,true}).second)
        throw std::invalid_argument("duplicate gate endpoint");
}
void GateTransit::add_vessel(TransitVessel ship) {
    if(!ship.id||!ship.owner||!ship.system||!in_bounds(ship.position)||
       !in_bounds(ship.velocity)||ship.phase!=TransitPhase::InSpace||
       ship.pending_gate||ship.phase_ticks)
        throw std::invalid_argument("invalid transit vessel");
    (void)map_.system(ship.system);
    if(!ships_.emplace(ship.id,ship).second)throw std::invalid_argument("duplicate transit vessel");
}
void GateTransit::set_gate_online(std::uint64_t gate,bool online) {
    auto it=gates_.find(gate);if(it==gates_.end())throw std::invalid_argument("unknown gate");
    it->second.online=online;
}
JumpOutcome GateTransit::request_jump(PlayerId issuer,EntityId ship,std::uint64_t gate_id) {
    auto it=ships_.find(ship);
    if(it==ships_.end())return JumpOutcome::UnknownShip;
    auto& vessel=it->second;
    if(!issuer||vessel.owner!=issuer)return JumpOutcome::Unauthorized;
    auto gt=gates_.find(gate_id);
    if(gt==gates_.end())return JumpOutcome::UnknownGate;
    const auto& gate=gt->second.gate;
    if(gate.origin!=vessel.system)return JumpOutcome::WrongSystem;
    if(vessel.phase!=TransitPhase::InSpace)return JumpOutcome::TransitionBusy;
    if(vessel.weapon_lock)return JumpOutcome::WeaponLock;
    const auto dist=std::hypot(double(vessel.position.x)-gate.position.x,
                               double(vessel.position.y)-gate.position.y);
    if(dist>kGateRangeMm)return JumpOutcome::OutOfRange;
    // Link may be disabled independently from the local endpoint.
    const auto shortest=map_.route(gate.origin,gate.destination,RoutePolicy::Shortest).systems;
    if(!gt->second.online||shortest.size()!=2||shortest[0]!=gate.origin||shortest[1]!=gate.destination)
        return JumpOutcome::GateOffline;
    vessel.phase=TransitPhase::Jumping;
    vessel.pending_gate=gate_id;
    vessel.phase_ticks=kTransitTicks;
    return JumpOutcome::Granted;
}
void GateTransit::advance(std::uint64_t tick) {
    if(tick!=tick_+1)throw std::invalid_argument("nonsequential transit tick");
    tick_=tick;
    for(auto& [id,ship]:ships_) {
        (void)id;
        if(ship.phase==TransitPhase::InSpace)continue;
        if(!ship.phase_ticks)throw std::runtime_error("invalid transition timer");
        --ship.phase_ticks;
        if(ship.phase_ticks)continue;
        if(ship.phase==TransitPhase::GateCloaked){ship.phase=TransitPhase::InSpace;continue;}
        const auto pending=gates_.find(ship.pending_gate);
        if(pending==gates_.end())throw std::runtime_error("lost committed jump endpoint");
        const auto& gate=pending->second.gate;
        // Complete previously granted transit even if infrastructure closes mid-jump.
        const std::int64_t sign=ship.id%2?-1:1;
        const Vector2 arrival={gate.arrival_center.x+sign*12000,gate.arrival_center.y};
        if(!in_bounds(arrival))throw std::runtime_error("bad arrival coordinates");
        ship.system=gate.destination;
        ship.position=arrival;
        ship.velocity={};
        ship.pending_gate=0;
        ship.phase=TransitPhase::GateCloaked;
        ship.phase_ticks=kCloakTicks;
    }
}
bool GateTransit::request_movement(PlayerId issuer,EntityId id,Vector2 new_velocity) {
    const auto it=ships_.find(id);
    if(it==ships_.end()||!issuer||it->second.owner!=issuer||!in_bounds(new_velocity))return false;
    auto& ship=it->second;
    if(ship.phase==TransitPhase::Jumping)return false;
    if(ship.phase==TransitPhase::GateCloaked && (new_velocity.x||new_velocity.y)) {
        ship.phase=TransitPhase::InSpace;ship.phase_ticks=0;
    }
    ship.velocity=new_velocity;return true;
}
const TransitVessel& GateTransit::vessel(EntityId id) const {return ships_.at(id);}
bool GateTransit::protected_from_attack(EntityId id) const {
    const auto phase=vessel(id).phase;
    return phase==TransitPhase::Jumping||phase==TransitPhase::GateCloaked;
}
} // namespace galaxy
