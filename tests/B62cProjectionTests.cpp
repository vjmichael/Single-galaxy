#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(c) do { if(!(c)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #c); } while(false)
struct Fixture {EntityId ship, origin_station, origin_gate, dest_station, dest_gate, remote_rock;};
Simulation seed(Fixture& ids) {
  Simulation s(457);
  s.add_system(2);
  ids.origin_station=s.add_station({0,0});
  ids.ship=s.spawn_ship_docked(7,ids.origin_station,30,100);
  s.add_asteroid({2500,700},20);
  ids.origin_gate=s.add_gate(1,{1500,0},2,{5000,0});
  ids.dest_station=s.add_station_in_system(2,{17000,0});
  ids.dest_gate=s.add_gate(2,{17000,0},1,{-12000,0});
  ids.remote_rock=s.add_asteroid_in_system(2,{18100,800},100);
  s.configure_market(ids.origin_station,2,1000);
  s.configure_market(ids.dest_station,5,1000);
  return s;
}
void request(PersistentWorld& w, Fixture ids, std::uint64_t id, Action action, EntityId target, std::int64_t amount=0) {
  Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ids.ship;c.action=action;
  c.target=target;c.amount=amount;c.execute_tick=w.world().tick()+1;
  CHECK(w.submit(c));
}
int main() {
  const auto path=fs::temp_directory_path()/("galaxy_b62c_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
  try {
    Fixture ids{};
    {
      PersistentWorld world(path.string(),seed(ids));
      bool refused=false;
      try{ServerViewBridge unauthenticated(world.world(),72478,999);}catch(const std::invalid_argument&){refused=true;}
      CHECK(refused);
      ServerViewBridge bridge(world.world(),72477,7);
      ClientView client;
      auto frame=decode_frame(encode_frame(bridge.snapshot()));
      CHECK(client.apply(frame)==ApplyResult::Applied);
      CHECK(client.world().system_id==1);
      CHECK(!client.world().entities.count(ids.dest_station));
      CHECK(!client.world().entities.count(ids.remote_rock));
      CHECK(!client.world().markets.count(ids.dest_station));
      CHECK(client.world().entities.count(ids.origin_gate));
      auto tick=[&]() {
        world.step();
        auto next=decode_frame(encode_frame(bridge.after_committed_step(world.world())));
        CHECK(client.apply(next)==ApplyResult::Applied);
        return next;
      };
      request(world,ids,1,Action::RequestUndock,ids.origin_station);tick();tick();tick();
      request(world,ids,2,Action::RequestJump,ids.origin_gate);
      auto accepted=tick();
      CHECK(accepted.kind==FrameKind::Delta);
      CHECK(client.world().jumps.at(ids.ship).state==ShipJumpState::Jumping);
      CHECK(client.world().jumps.at(ids.ship).gate==ids.origin_gate);
      tick();
      auto entry=tick();
      CHECK(entry.kind==FrameKind::Snapshot && entry.system_id==2);
      CHECK(client.world().system_id==2);
      CHECK(client.world().entities.count(ids.ship));
      CHECK(client.world().entities.count(ids.dest_station));
      CHECK(client.world().entities.count(ids.dest_gate));
      CHECK(!client.world().entities.count(ids.origin_station));
      CHECK(!client.world().entities.count(ids.origin_gate));
      CHECK(!client.world().markets.count(ids.origin_station));
      CHECK(client.world().jumps.at(ids.ship).state==ShipJumpState::ArrivalCloak);
      CHECK(world.world().protected_from_attack(ids.ship));
      auto corrupted=entry;
      for(auto& change:corrupted.changes)if(change.kind==ChangeKind::Upsert && change.entity.id==ids.ship)
        change.entity.system_id=1;
      bool denied=false;
      try{ClientView fresh;(void)fresh.apply(corrupted);}catch(const std::exception&){denied=true;}
      CHECK(denied && client.world().system_id==2);
      for(int i=0;i<5;++i)tick();
      CHECK(!world.world().protected_from_attack(ids.ship));
      CHECK(client.world().jumps.empty());
      request(world,ids,3,Action::RequestJump,ids.dest_gate);tick();tick();
      auto home=tick();
      CHECK(home.kind==FrameKind::Snapshot && home.system_id==1);
      CHECK(client.world().entities.count(ids.origin_gate));
      CHECK(!client.world().entities.count(ids.dest_gate));
      CHECK(world.world().entities().count(ids.ship)==1);
    }
    {
      PersistentWorld recovered(path.string(),Simulation(1));
      CHECK(recovered.world().find(ids.ship)->system_id==1);
      CHECK(recovered.world().entities().count(ids.ship)==1);
    }
    fs::remove(path);
    std::cout<<"PASS B6.2c: per-owner view privacy, bounded jump telemetry, system-change snapshots, tamper rejection, roundtrip and restart\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"B6.2c failure: "<<e.what()<<"\n";fs::remove(path);return 1;}
}
