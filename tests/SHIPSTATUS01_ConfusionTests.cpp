#include "galaxy/ShipStatusFitting.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
using namespace galaxy::ship_status;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
static bool same(const std::vector<ModuleState>& a,const std::vector<ModuleState>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(!(a[i].key==b[i].key) || a[i].requested_on!=b[i].requested_on ||
           a[i].effective_on!=b[i].effective_on || a[i].confused!=b[i].confused)
            return false;
    return true;
}
static std::vector<ModuleControl> roster() {
    return {
        {{0,0,1},true,true,false},
        {{0,1,2},false,true,false},
        {{1,0,3},true,true,false},
        {{1,1,4},false,true,false},
        {{2,0,5},true,true,false},
        {{2,1,6},false,true,false},
        {{3,0,7},true,false,true} // never compromised: isolated controller
    };
}
static Effect confusion(std::uint64_t id,std::uint64_t target=20,
                        unsigned intensity=80,unsigned resistance=10) {
    Effect e{};
    e.id=id;e.source_ship=10;e.target_ship=target;
    e.started_tick=4;e.expires_tick=16;e.pulse_ticks=2;
    e.intensity=static_cast<std::uint8_t>(intensity);
    e.resistance=static_cast<std::uint8_t>(resistance);
    e.seed=0xdeadbeef01234567ULL;
    return e;
}
int main() {
 try {
  auto original=roster();
  Engine engine;
  CHECK(!engine.inflict_confusion(confusion(0),original,4));
  CHECK(!engine.inflict_confusion(confusion(1,20,60,60),original,4));
  CHECK(!engine.inflict_confusion(confusion(2,20,60,80),original,4));
  CHECK(!engine.inflict_confusion(confusion(3),original,5)); // stale start
  CHECK(engine.inflict_confusion(confusion(4),original,4));
  CHECK(!engine.inflict_confusion(confusion(4),original,4)); // idempotency
  CHECK(!engine.inflict_confusion(confusion(5),original,4)); // no stacking
  const auto before=engine.project(20,3,original);
  for(std::size_t i=0;i<before.size();++i)
    CHECK(before[i].effective_on==original[i].requested_on);
  const auto first=engine.project(20,4,original);
  CHECK(first.size()==original.size());
  CHECK(same(first,engine.project(20,4,original)));
  CHECK(first[6].effective_on==original[6].requested_on &&
        !first[6].confused);
  std::size_t changed=0;
  for(const auto& s:first) if(s.confused)++changed;
  CHECK(changed>=1 && changed<=5);
  CHECK(same(first,engine.project(20,5,original))); // no extra pulse
  const auto second=engine.project(20,6,original);
  bool advanced=false;
  for(std::size_t i=0;i<first.size();++i)
    if(first[i].effective_on!=second[i].effective_on)advanced=true;
  CHECK(advanced);
  CHECK(same(engine.project(20,13,original),engine.project(20,13,original)));
  CHECK(engine.encode()==Engine::decode(engine.encode()).encode());
  const auto restored=Engine::decode(engine.encode());
  for(std::uint64_t tick=3;tick<=17;++tick)
    CHECK(same(engine.project(20,tick,original),
               restored.project(20,tick,original)));
  // Player intent remains authoritative and returns at expiry.
  original[1].requested_on=true;
  const auto expired=engine.project(20,16,original);
  for(std::size_t i=0;i<original.size();++i)
    CHECK(!expired[i].confused && expired[i].effective_on==original[i].requested_on);
  // Existing hardware changed or hardened mid-fight cannot be held hostage
  // by an old slot key. This is NOT a new inventory or fitting source.
  auto hardened=roster();hardened[0].isolated=true;
  hardened[1].key.hardware=9;
  const auto mitigated=engine.project(20,4,hardened);
  CHECK(!mitigated[0].confused && !mitigated[1].confused);
  CHECK(mitigated[0].effective_on==hardened[0].requested_on);
  CHECK(mitigated[1].effective_on==hardened[1].requested_on);
  // A second ship remains unaffected (no omniscient area attack).
  for(const auto& s:engine.project(21,4,roster()))
    CHECK(!s.confused);
  bool caught=false;
  try{(void)Engine::decode("ship-status-v1\nC 4 10 20 4 16 0 2 80 10 1 0 0 1\n");}
  catch(const std::invalid_argument&){caught=true;}
  CHECK(caught);
  caught=false;
  try{(void)Engine::decode("ship-status-v1\nC 4 10 20 4 16 88 2 80 10 2 0 0 1 0 0 1\n");}
  catch(const std::invalid_argument&){caught=true;}
  CHECK(caught);
  caught=false;
  try{(void)Engine::decode(engine.encode()+"F 42 0\n");}
  catch(const std::invalid_argument&){caught=true;}
  CHECK(caught);
  // Bind the projection to original PLAY-11 real fitted hardpoints.
  Simulation simulation(10101);
  const auto ship=simulation.add_colony_ship(7,{0,0},100,6);
  simulation.provision_starter_exploration_scanner(ship);
  simulation.provision_starter_probe_kit(ship);
  const auto* entity=simulation.find(ship);
  CHECK(entity && entity->fitting.hardpoints[0]==1 &&
        entity->fitting.hardpoints[1]==2);
  const std::array<bool,2> intent{{true,false}};
  const std::array<bool,2> exposed{{true,true}};
  const std::array<bool,2> isolated{{false,false}};
  const auto actual=fitted_controls(*entity,intent,exposed,isolated);
  CHECK(actual.size()==2 && actual[0].key.hardware==1 &&
        actual[1].key.hardware==2);
  Engine actual_engine;
  auto attack=confusion(100,ship);attack.source_ship=ship+1;
  CHECK(actual_engine.inflict_confusion(attack,actual,4));
  CHECK(actual_engine.project(ship,4,actual).size()==2);
  CHECK(effective_hardpoint_on(actual_engine,*entity,16,0,
      intent,exposed,isolated));
  CHECK(!effective_hardpoint_on(actual_engine,*entity,16,1,
      intent,exposed,isolated));
  CHECK(simulation.find(ship)->hull==100);
  CHECK(simulation.find(ship)->weapon_charges==12);
  std::cout<<"PASS SHIP-STATUS-01 confusion: deterministic toggles, resistance, isolation, intent restoration, replay and real fitted modules\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL SHIP-STATUS-01: "<<e.what()<<'\n';
  return 1;
 }
}
