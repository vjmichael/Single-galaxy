#include "galaxy/NativeWriterLease.hpp"
#include "galaxy/ServerBreadcrumb.hpp"
// PLAY-STATION-02 development-native station spawn and boarding bridge.
// Real person, original SQLite world and exact berth custody; fixture-only hull.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationLaunch.hpp"
#include "galaxy/StationRefineryProtocol.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
namespace {
constexpr PlayerId kOwner=7;
std::uint64_t epoch(){std::random_device r;const auto n=((std::uint64_t(r())<<32)|r())&0x3fffffffffffffffULL;return n?n:1;}
std::vector<std::string_view> split(const std::string& s){std::vector<std::string_view> v;std::size_t at=0;for(;;){const auto end=s.find('|',at);v.emplace_back(s.data()+at,(end==s.npos?s.size():end)-at);if(end==s.npos)break;at=end+1;if(v.size()>7)break;}return v;}
template<class T>bool parse(std::string_view s,T& v){if(s.empty()||s.size()>20||s[0]=='+'||s.find_first_of(" \n\r\t")!=s.npos)return false;const auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),v);return ec==std::errc{}&&p==s.data()+s.size();}
void deny(std::uint64_t req,const char* reason){std::cout<<"DENY|2|"<<req<<'|'<<reason<<'\n'<<std::flush;}
void send(std::uint64_t session,std::uint64_t request,const PersistentWorld& w){
 const auto& people=w.world().player_people();const auto p=people.find(kOwner);
 if(p==people.end())throw std::runtime_error("native station player missing");
 const auto& person=p->second;
 const EntityId station=person.state==PlayerPersonState::AtStation?person.physical_station:
       (w.world().find(person.physical_ship)?w.world().find(person.physical_ship)->dock_station:0);
 const auto available=station?station_launch::available_ship(w.world(),kOwner,station):std::nullopt;
 std::cout<<"PERSON|2|"<<session<<'|'<<request<<'|'<<w.world().tick()<<'|'<<w.generation()
          <<'|'<<unsigned(person.state)<<'|'<<station<<'|'<<person.physical_ship
          <<'|'<<person.station_x_mm<<'|'<<person.station_z_mm
          <<'|'<<available.value_or(0)<<'|'<<person.last_receipt<<'\n'<<std::flush;
 if(!std::cout)throw std::runtime_error("client station pipe disconnected");
}
}
int main(int argc,char** argv){
 if(argc!=3||std::string_view(argv[2])!="--explicit-test-station"){
  std::cerr<<"Usage: galaxy_desktop_station_host SQLITE_PATH --explicit-test-station\n";return 2;
 }
 try{
  const fs::path save(argv[1]);
  if(save.empty()||save.string().size()>1024||
    (!save.parent_path().empty()&&!fs::exists(save.parent_path())))
    throw std::runtime_error("invalid native station save path");
  NativeWriterLease custody(save);
  const bool fresh=!fs::exists(save);
  // This optional fixture does NOT alter ordinary Single-Galaxy genesis.
  Simulation seed(0x5202);
  const auto test_station=seed.add_station({0,0});
  (void)seed.spawn_ship_docked(kOwner,test_station,12,24);
  (void)seed.add_colony_ship(8,{15000,0},8,2);
  // Explicit test-station genesis ONLY: a single persistent finite asteroid.
  // Existing saves ignore this seed. A visual Godot sample never adds ore.
  (void)seed.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
  seed.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
  // Opt in ONLY the genesis of this explicit test station. Existing
  // SQLite worlds must load their own original rules and resources.
  seed.enable_native_swept_asteroid_collision();
  PersistentWorld world(save.string(),std::move(seed));
  if(fresh){
   if(!world.register_player_at_station(kOwner,test_station))
    throw std::runtime_error("native test station registration denied");
  }
  if(!world.world().player_people().count(kOwner))
    throw std::runtime_error("existing world lacks registered person; no silent bootstrap");
  const auto session=epoch();
  const auto receipt_prefix=(epoch()&((std::uint64_t{1}<<45)-1))<<14;
  std::uint64_t latest_request=0,budget=0;
  auto last=std::chrono::steady_clock::now()-std::chrono::milliseconds(100);
  send(session,0,world);
  std::string line;
  while(std::getline(std::cin,line)){
   if(++budget>10000){deny(0,"budget");return 3;}
   if(line=="QUIT")return 0;
   if(line.size()>140){deny(0,"oversize");continue;}
   const auto f=split(line);
   const bool walking=f.size()==6&&f[0]=="WALK";
   const bool boarding=f.size()==5&&f[0]=="BOARD";
   const bool exiting=f.size()==5&&f[0]=="EXIT";
   const bool view=f.size()==5&&f[0]=="REFVIEW";
   const bool refine=f.size()==5&&f[0]=="REFINE";
   const bool unload=f.size()==5&&f[0]=="UNLOAD";
   if(!walking&&!boarding&&!exiting&&!view&&!refine&&!unload){deny(0,"malformed");continue;}
   std::uint64_t requested_session=0,req=0,expected=0,ship=0;
   std::int64_t dx=0,dz=0;
   if(!parse(f[1],requested_session)||!parse(f[2],req)||!parse(f[3],expected)||
      (walking&&(!parse(f[4],dx)||!parse(f[5],dz)))||
      (!walking&&!parse(f[4],ship))){deny(0,"integer");continue;}
   if(requested_session!=session){deny(req,"session");continue;}
   if(req<=latest_request||req>10000){deny(req,"replay");continue;}
   latest_request=req;
   if(expected!=world.generation()){deny(req,"stale-world");continue;}
   const auto now=std::chrono::steady_clock::now();
   if(now-last<std::chrono::milliseconds(45)){deny(req,"rate");continue;}
   const auto& person=world.world().player_people().at(kOwner);
   if(view || refine || unload){
    if(!station_refinery_protocol::accessible(person) ||
       (view && ship!=person.physical_station)){
       deny(req,"refinery-terminal-distance-or-custody");continue;
    }
    int result=0;
    std::int64_t transferred=0;
    if(refine){
       const auto* plant=world.world().station_refinery(person.physical_station);
       // A target lot ID is never a mass, a facility ID or client-selected actor.
       if(!plant || plant->owner!=kOwner || ship==0 ||
          ship>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
          !world.world().station_ore_lots().count(ship)){
          deny(req,"no-owned-refinery-or-lot");continue;
       }
       Command c{};
       c.id=receipt_prefix|req;
       c.sequence=c.id;
       c.execute_tick=world.world().tick()+1;
       c.action=Action::RefineStationOre;
       c.actor=plant->source_ship;
       c.target=person.physical_station;
       c.issuer=kOwner;
       c.amount=static_cast<std::int64_t>(ship);
       if(!world.submit(c)){
          deny(req,"original-world-command-not-authorized");continue;
       }
       // Original SQLite STEP commits both the event and physical inventory.
       // Exception means the process fails closed; do not emit an ACK.
       world.step();
       galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
       const auto& event=world.world().events().back();
       if(event.command_id!=c.id)
          throw std::runtime_error("refinery world journal did not return exact command");
       result=event.result==Outcome::Applied?1:2;
       transferred=event.transferred;
    }
    if(unload){
       // The original PERSON receipt identifies the owned physical station.
       // A client-supplied ship ID is only a selector: revalidate the actual
       // docked vessel, station and finite cargo on every command.
       const auto eligible=station_launch::available_ship(
           world.world(),kOwner,person.physical_station);
       const auto* vessel=world.world().find(ship);
       if(!eligible || *eligible!=ship || !vessel ||
          vessel->owner_id!=kOwner || vessel->dock_state!=DockState::Docked ||
          vessel->dock_station!=person.physical_station || vessel->cargo<1){
          deny(req,"no-owned-docked-ship-with-ore");continue;
       }
       Command c{};
       c.id=receipt_prefix|req;
       c.sequence=c.id;
       c.execute_tick=world.world().tick()+1;
       c.action=Action::UnloadOre;
       c.actor=ship;
       c.target=person.physical_station;
       c.issuer=kOwner;
       c.amount=1; // one physical tonne; never accept client-supplied mass
       if(!world.submit(c)){
          deny(req,"original-unload-not-authorized");continue;
       }
       // No optimistic Godot cargo update; the SQLite STEP and original
       // lot-provenance ledger are authoritative after a durable commit.
       world.step();
       galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
       const auto& event=world.world().events().back();
       if(event.command_id!=c.id)
          throw std::runtime_error("unload world journal did not return exact command");
       result=event.result==Outcome::Applied?3:4;
       transferred=event.result==Outcome::Applied?event.transferred:0;
       if(transferred<0 || transferred>1)
          throw std::runtime_error("invalid original single-tonne unload receipt");
    }
    last=now;
    std::cout<<station_refinery_protocol::make_snapshot(world,kOwner,session,req,result,transferred)<<std::flush;
    if(!std::cout)throw std::runtime_error("refinery terminal stdout disconnected");
    continue;
   }
   EntityId station=person.physical_station;
   if(exiting){
    const auto* vessel=world.world().find(person.physical_ship);
    if(!vessel||vessel->dock_station!=ship){deny(req,"wrong-berth");continue;}
    station=ship;ship=person.physical_ship;
   }
   const auto kind=walking?StationPlayerAction::Walk:boarding?
       StationPlayerAction::Board:StationPlayerAction::Disembark;
   if(!world.station_player_action(kOwner,station,boarding?ship:(exiting?ship:0),
                                 kind,dx,dz,receipt_prefix|req)){
    deny(req,"station-custody-or-distance");continue;
   }
   last=now;
   galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
   send(session,req,world);
  }
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"PLAY-STATION-02 native host failed: "<<e.what()<<'\n';return 1;
 }
}
