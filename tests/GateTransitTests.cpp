#include "galaxy/GateTransit.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("failed ")+#x+" at "+std::to_string(__LINE__));}while(0)
int main(){try{
    GalaxyMap map;map.add_system({1,"Core",90,Sovereignty::NPC});
    map.add_system({2,"Frontier",0,Sovereignty::Unclaimed});map.link(1,2);
    GateTransit service(map);
    service.add_endpoint({100,1,2,{10000,0},{5000,0}});
    service.add_endpoint({200,2,1,{5000,0},{10000,0}});
    service.add_vessel({9,7,1,{0,0},{},TransitPhase::InSpace,0,0,false});
    CHECK(service.request_jump(7,9,100)==JumpOutcome::OutOfRange);
    service.add_vessel({10,8,1,{10000,0},{},TransitPhase::InSpace,0,0,false});
    CHECK(service.request_jump(7,10,100)==JumpOutcome::Unauthorized);
    CHECK(service.request_jump(8,10,200)==JumpOutcome::WrongSystem);
    CHECK(service.request_jump(8,10,404)==JumpOutcome::UnknownGate);
    map.set_link_operational(1,2,false);
    CHECK(service.request_jump(8,10,100)==JumpOutcome::GateOffline);
    map.set_link_operational(1,2,true);
    service.set_gate_online(100,false);
    CHECK(service.request_jump(8,10,100)==JumpOutcome::GateOffline);
    service.set_gate_online(100,true);
    CHECK(service.request_jump(8,10,100)==JumpOutcome::Granted);
    CHECK(service.request_jump(8,10,100)==JumpOutcome::TransitionBusy);
    CHECK(service.vessel(10).system==1);
    CHECK(service.protected_from_attack(10));
    CHECK(!service.request_movement(8,10,{1,0}));
    service.advance(1);
    CHECK(service.vessel(10).system==1);
    // A gate closing after acceptance does not strand the ship mid-transfer.
    service.set_gate_online(100,false);
    service.advance(2);
    CHECK(service.vessel(10).system==2 && service.vessel(10).phase==TransitPhase::GateCloaked);
    CHECK(service.vessel(10).position.x==17000);
    CHECK(service.vessel(10).velocity.x==0);
    CHECK(service.protected_from_attack(10));
    CHECK(service.request_jump(8,10,200)==JumpOutcome::TransitionBusy); // arrival cloak still active
    CHECK(service.request_movement(8,10,{0,0}));
    CHECK(service.protected_from_attack(10));
    CHECK(!service.request_movement(7,10,{1,0}));
    CHECK(service.request_movement(8,10,{1,0}));
    CHECK(service.vessel(10).phase==TransitPhase::InSpace);
    CHECK(!service.protected_from_attack(10));
    CHECK(service.request_jump(8,10,200)==JumpOutcome::OutOfRange);
    service.advance(3);
    bool threw=false;
    try{service.advance(3);}catch(const std::invalid_argument&){threw=true;}
    CHECK(threw);
    // Second vessel: arrival protection expires automatically after exactly five ticks.
    service.set_gate_online(100,true);
    service.add_vessel({12,8,1,{10000,0},{},TransitPhase::InSpace,0,0,false});
    CHECK(service.request_jump(8,12,100)==JumpOutcome::Granted);
    service.advance(4);service.advance(5);
    CHECK(service.protected_from_attack(12));
    for(int t=6;t<=9;++t){service.advance(t);CHECK(service.protected_from_attack(12));}
    service.advance(10);CHECK(!service.protected_from_attack(12));
    // Weapon lock cannot be imposed by a client through request_jump; modeled as host-owned state.
    service.add_vessel({14,8,1,{10000,0},{},TransitPhase::InSpace,0,0,true});
    CHECK(service.request_jump(8,14,100)==JumpOutcome::WeaponLock);
    std::cout<<"PASS B6.2a proximity, authorization, gate closure, transit, arrival cloak, movement, timer and weapon lock\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
