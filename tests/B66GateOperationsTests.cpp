#include "galaxy/Persistence.hpp"
#include "galaxy/Region.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <random>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(expr) do {if(!(expr))throw std::runtime_error(std::string("CHECK failed: ")+ #expr + " at line " + std::to_string(__LINE__));}while(false)
static fs::path temp(const char* tag){static std::random_device rd;auto d=fs::temp_directory_path()/(std::string("sg_b66_")+tag+"_"+std::to_string(rd()));fs::create_directories(d);return d/"world.sqlite";}
struct Fixture{Simulation sim;RegionState region;EntityId a,b,ship;Fixture():sim(0xB66){
  sim.add_system(2);auto st=sim.add_station({0,0});ship=sim.add_ship(7,{1500,0},40);
  auto ore=sim.add_asteroid({2500,700},80);auto st2=sim.add_station_in_system(2,{17000,0});
  a=sim.add_gate(1,{1500,0},2,{5000,0});b=sim.add_gate(2,{17000,0},1,{-12000,0});
  auto ore2=sim.add_asteroid_in_system(2,{18000,800},80);
  region=build_lumen_reach(sim,st,st2,ore,ore2,a,b);
}};
int main(){
 try {
  Fixture f;
  const auto ga=f.region.gate(2,6),gb=f.region.gate(6,2);
  CHECK(ga && gb);f.sim.configure_gate_operations(ga,gb,45,0,0);
  CHECK(f.sim.gate_operations(ga,gb)->condition==45);
  CHECK(f.sim.canonical_state().find("galaxy-kernel-v6|")==0);
  auto serialized=f.sim.canonical_state();
  CHECK(Simulation::from_snapshot(serialized).canonical_state()==serialized);
  // Snapshots must reject forged staffing, impossible condition, or inconsistent pair.
  auto tampered=serialized;
  auto pos=tampered.find("gateops|"); CHECK(pos!=std::string::npos);
  auto end=tampered.find('\n',pos);CHECK(end!=std::string::npos);
  const auto good=tampered.substr(pos,end-pos);
  auto piece=good.find('|',8);CHECK(piece!=std::string::npos);
  // Force condition=1001 in otherwise canonical record.
  auto p3=good.find('|',piece+1);CHECK(p3!=std::string::npos);
  auto p4=good.find('|',p3+1);CHECK(p4!=std::string::npos);
  auto bad=good.substr(0,p3+1)+"1001"+good.substr(p4);
  tampered.replace(pos,end-pos,bad);
  bool rejected=false;try{(void)Simulation::from_snapshot(tampered);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
  auto db=temp("ops");const auto beforeOre=f.sim.total_ore();const auto beforeCredits=f.sim.total_credits();
  {
    PersistentWorld w(db.string(),std::move(f.sim));
    const auto baseRoute=std::vector<std::uint32_t>{1,2,6,7};
    CHECK(f.region.operational_routes(w.world()).route(1,7,RoutePolicy::Shortest).systems==baseRoute);
    // Explicit public player requests are rejected even if they spoof the reserved server issuer.
    Command forged{};forged.id=1000;forged.sequence=1000;forged.execute_tick=1;
    forged.issuer=7;forged.actor=gb;forged.target=ga;forged.action=Action::SetGateStaff;forged.amount=16;
    CHECK(!w.submit(forged));forged.issuer=Simulation::kServerAuthority;CHECK(!w.submit(forged));
    forged.action=Action::DeliverGateSpares;CHECK(!w.submit(forged));
    CHECK(!w.submit_testing_gate_operations(ga,gb,Action::Thrust,20,900));
    CHECK(!w.submit_testing_gate_operations(ga,f.a,Action::SetGateStaff,2,901));
    // Two cycles without staff exhaust condition and stop both directional endpoints.
    w.steps(20);CHECK(!w.world().find(ga)->gate_online && !w.world().find(gb)->gate_online);
    CHECK(w.world().gate_operations(ga,gb)->condition==0);
    CHECK(f.region.operational_routes(w.world()).route(1,7,RoutePolicy::Shortest).systems!=baseRoute);
    // Can't restart a broken gate by changing the availability bit.
    CHECK(w.submit_testing_gate_change(ga,gb,true,910));w.step();
    CHECK(!w.world().find(ga)->gate_online);
    CHECK(w.world().events().back().result==Outcome::GateOffline);
    CHECK(w.submit_testing_gate_operations(ga,gb,Action::SetGateStaff,2,911));w.step();
    CHECK(w.world().gate_operations(ga,gb)->technicians==2);
    CHECK(w.submit_testing_gate_operations(ga,gb,Action::DeliverGateSpares,3,912));w.step();
    CHECK(w.world().gate_operations(ga,gb)->spare_parts==3);
    const auto old=w.world().canonical_state();
    try{(void)w.submit_testing_gate_operations(ga,gb,Action::DeliverGateSpares,4,913,FailPoint::AfterReceiptWrite);
       throw std::runtime_error("fault injection did not throw");}
    catch(const std::runtime_error& ex){CHECK(std::string(ex.what()).find("injected failure")!=std::string::npos);}
    CHECK(w.world().canonical_state()==old);CHECK(!w.world().has_command_id(913));
    w.steps(10);
    auto op=w.world().gate_operations(ga,gb);
    CHECK(op && op->condition==40 && op->spare_parts==2 && op->consumed_parts==1);
    CHECK(!w.world().find(ga)->gate_online); // repairs don't automatically reactivate
    CHECK(w.submit_testing_gate_change(ga,gb,true,914));w.step();
    CHECK(w.world().find(ga)->gate_online && w.world().find(gb)->gate_online);
    CHECK(f.region.operational_routes(w.world()).route(1,7,RoutePolicy::Shortest).systems==baseRoute);
    CHECK(w.world().total_ore()==beforeOre && w.world().total_credits()==beforeCredits);
    // Authorized ops and their results survive restart and independent journal recovery.
    auto recovery=temp("copy");
    PersistentWorld::recover_copy(db.string(),recovery.string());
    PersistentWorld restored(recovery.string(),Simulation(1));
    CHECK(restored.world().canonical_state()==w.world().canonical_state());
    CHECK(restored.world().gate_operations(ga,gb) && restored.world().gate_operations(ga,gb)->consumed_parts==1);
    fs::remove_all(recovery.parent_path());
  }
  {
    PersistentWorld w(db.string(),Simulation(1));
    CHECK(w.world().find(ga)->gate_online && w.world().find(gb)->gate_online);
    CHECK(w.world().gate_operations(ga,gb)->technicians==2);
    CHECK(w.world().total_ore()==beforeOre);
    w.steps(10);
    CHECK(w.world().gate_operations(ga,gb)->consumed_parts==2);
  }
  fs::remove_all(db.parent_path());
  // A jump executed *after* the v6 gate-operations snapshot must never downgrade
  // the checkpoint to v5 or silently discard the maintenance records.
  Fixture jump_fixture;
  jump_fixture.sim.configure_gate_operations(jump_fixture.a,jump_fixture.b,1000,10,1);
  auto jump_db=temp("managed_jump");
  {
    PersistentWorld j(jump_db.string(),std::move(jump_fixture.sim));
    Command go{};go.id=3101;go.sequence=3101;go.execute_tick=1;
    go.issuer=7;go.actor=jump_fixture.ship;go.target=jump_fixture.a;
    go.action=Action::RequestJump;
    CHECK(j.submit(go));j.step();j.steps(2);
    CHECK(j.world().find(jump_fixture.ship)->system_id==2);
    CHECK(j.world().canonical_state().find("galaxy-kernel-v6|")==0);
    CHECK(j.world().gate_operations(jump_fixture.a,jump_fixture.b));
  }
  {
    PersistentWorld restored(jump_db.string(),Simulation(1));
    CHECK(restored.world().find(jump_fixture.ship)->system_id==2);
    CHECK(restored.world().gate_operations(jump_fixture.a,jump_fixture.b)->spare_parts==10);
  }
  fs::remove_all(jump_db.parent_path());
  std::cout << "PASS B6.6 durable gate condition, staff, parts, wear, repair, routed outage, authorization, rollback, restart and recovery copy\n";
 }catch(const std::exception& e){std::cerr<<"FAIL B6.6: "<<e.what()<<'\n';return 1;}
}
