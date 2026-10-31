#include "galaxy/Persistence.hpp"
#include "galaxy/Region.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <random>
using namespace galaxy;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string(#x)+" failed at line "+std::to_string(__LINE__));} while(0)
namespace fs=std::filesystem;
static fs::path fresh_db(const std::string& suffix) {
  static std::random_device rd;
  auto dir=fs::temp_directory_path()/("sg_b65_"+suffix+"_"+std::to_string(rd()));
  fs::create_directories(dir);
  return dir/"world.sqlite";
}
static Simulation fixture(RegionState& region,EntityId& player,EntityId& gate12,EntityId& gate21) {
  Simulation world(0xB65);
  world.add_system(2);
  auto home=world.add_station({0,0});
  player=world.add_ship(7,{1500,0},20); // directly in gate activation range
  auto ore=world.add_asteroid({2500,700},40);
  auto frontier=world.add_station_in_system(2,{17000,0});
  gate12=world.add_gate(1,{1500,0},2,{5000,0});
  gate21=world.add_gate(2,{17000,0},1,{-12000,0});
  auto ore2=world.add_asteroid_in_system(2,{18000,800},25);
  region=build_lumen_reach(world,home,frontier,ore,ore2,gate12,gate21);
  return world;
}
int main() {
  try {
    RegionState r;EntityId ship=0,g12=0,g21=0;
    auto initial=fixture(r,ship,g12,g21);
    const auto ore_origin=initial.total_ore();
    auto db=fresh_db("outage");
    const auto short_route=std::vector<std::uint32_t>{1,2,6,7};
    const auto safe_route=std::vector<std::uint32_t>{1,3,4,5,7};
    {
      PersistentWorld w(db.string(),std::move(initial));
      auto source=r.gate(2,6),reverse=r.gate(6,2);
      CHECK(source && reverse);
      CHECK(r.operational_routes(w.world()).route(1,7,RoutePolicy::Shortest).systems==short_route);
      Command spoof{};spoof.id=900;spoof.execute_tick=w.world().tick()+1;
      spoof.sequence=900;spoof.issuer=7;spoof.actor=reverse;spoof.target=source;
      spoof.action=Action::SetGatePairOnline;spoof.amount=0;
      CHECK(!w.submit(spoof)); // Public/player command API is not an admin authority.
      CHECK(!w.submit_testing_gate_change(source,g12,false,901));
      CHECK(w.submit_testing_gate_change(source,reverse,false,1000));
      CHECK(w.world().find(source)->gate_online); // only changes on committed tick
      w.step();
      CHECK(!w.world().find(source)->gate_online && !w.world().find(reverse)->gate_online);
      CHECK(r.operational_routes(w.world()).route(1,7,RoutePolicy::PreferSecure).systems==safe_route);
      auto detour=r.operational_routes(w.world()).route(1,7,RoutePolicy::Shortest).systems;
      CHECK(detour.size()>=4 && detour.front()==1 && detour.back()==7);
      for(std::size_t i=1;i<detour.size();++i) CHECK(!(detour[i-1]==2 && detour[i]==6));
      CHECK(w.world().total_ore()==ore_origin);
      CHECK(w.world().find(ship)->system_id==1);
      auto s=w.world().canonical_state();
      CHECK(s.find("event|")!=std::string::npos);
      CHECK(w.world().find(ship)->cargo==0);
      try { (void)w.submit_testing_gate_change(source,reverse,true,1001,FailPoint::AfterReceiptWrite);
            throw std::runtime_error("injected failure did not throw"); }
      catch (const std::runtime_error& ex) {CHECK(std::string(ex.what()).find("injected failure")!=std::string::npos);}
      CHECK(!w.world().find(source)->gate_online);
      CHECK(!w.world().has_command_id(1001));
    }
    {
      // Rebuild a distinct recovery copy using the verified operation journal,
      // including the privileged gate outage command (never mutate source).
      auto recovery=fresh_db("replay");
      PersistentWorld::recover_copy(db.string(),recovery.string());
      PersistentWorld audited(recovery.string(),Simulation(1));
      CHECK(!audited.world().find(r.gate(2,6))->gate_online);
      CHECK(audited.world().total_ore()==ore_origin);
      fs::remove_all(recovery.parent_path());
    }
    {
      PersistentWorld restored(db.string(),Simulation(1));
      auto source=r.gate(2,6),reverse=r.gate(6,2);
      CHECK(!restored.world().find(source)->gate_online);
      CHECK(r.operational_routes(restored.world()).route(1,7,RoutePolicy::PreferSecure).systems==safe_route);
      CHECK(restored.submit_testing_gate_change(source,reverse,true,1002));
      restored.step();
      CHECK(restored.world().find(source)->gate_online);
      CHECK(restored.world().find(reverse)->gate_online);
      CHECK(r.operational_routes(restored.world()).route(1,7,RoutePolicy::Shortest).systems==short_route);
      CHECK(restored.world().find(ship)->system_id==1);
    }
    {
      PersistentWorld restored(db.string(),Simulation(1));
      CHECK(r.operational_routes(restored.world()).route(1,7,RoutePolicy::Shortest).systems==short_route);
    }
    // An already accepted transfer is allowed to finish even if the link fails.
    RegionState r2;EntityId pilot=0,ab=0,ba=0;
    auto initial2=fixture(r2,pilot,ab,ba);
    auto db2=fresh_db("inflight");
    {
      PersistentWorld w(db2.string(),std::move(initial2));
      Command jump{};jump.id=2100;jump.sequence=2100;
      jump.execute_tick=w.world().tick()+1;jump.issuer=7;jump.actor=pilot;
      jump.target=ab;jump.action=Action::RequestJump;
      CHECK(w.submit(jump));w.step();
      CHECK(w.world().find(pilot)->jump_state==ShipJumpState::Jumping);
      CHECK(w.submit_testing_gate_change(ab,ba,false,2101));w.step();
      CHECK(!w.world().find(ab)->gate_online);
      w.step();
      CHECK(w.world().find(pilot)->system_id==2);
      CHECK(w.world().find(pilot)->jump_state==ShipJumpState::ArrivalCloak);
    }
    fs::remove_all(db.parent_path());fs::remove_all(db2.parent_path());
    std::cout<<"PASS B6.5 durable bidirectional gate outage/repair, route replanning, issuer isolation, rollback, restart and in-flight jump\n";
  } catch(const std::exception& e) {std::cerr<<"FAIL B6.5: "<<e.what()<<'\n';return 1;}
}
