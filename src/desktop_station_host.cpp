#include "galaxy/NativeWriterLease.hpp"
// PLAY-STATION-02 development-native station spawn and boarding bridge.
// Real person, original SQLite world and exact berth custody; fixture-only hull.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationLaunch.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
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
  seed.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
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
   if(!walking&&!boarding&&!exiting){deny(0,"malformed");continue;}
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
   send(session,req,world);
  }
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"PLAY-STATION-02 native host failed: "<<e.what()<<'\n';return 1;
 }
}
