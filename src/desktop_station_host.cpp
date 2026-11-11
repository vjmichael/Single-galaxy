#include "galaxy/NativeWriterLease.hpp"
#include "galaxy/NativeWindowsUtf8Argv.hpp"
#include "galaxy/ServerBreadcrumb.hpp"
// PLAY-STATION-02 development-native station spawn and boarding bridge.
// Real person, original SQLite world and exact berth custody; fixture-only hull.
#include "galaxy/Persistence.hpp"
#include "galaxy/DockLogistics.hpp"
#include "galaxy/Dock04Traffic.hpp"
#include "galaxy/StationLaunch.hpp"
#include "galaxy/StationRefineryProtocol.hpp"
#include <algorithm>
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
static int run_host(int argc,char** argv){
 if(argc!=3||std::string_view(argv[2])!="--explicit-test-station"){
  std::cerr<<"Usage: galaxy_desktop_station_host SQLITE_PATH --explicit-test-station\n";return 2;
 }
 try{
  const std::string save_utf8(argv[1]);
   // sqlite3_open_v2 takes UTF-8; NativeWriterLease uses an accurately
   // decoded Windows filesystem path. Never round-trip via path.string(),
   // which may convert it back into a non-UTF8 active code page.
   const fs::path save=fs::u8path(save_utf8);
  if(save.empty()||save_utf8.size()>1024||
    (!save.parent_path().empty()&&!fs::exists(save.parent_path())))
    throw std::runtime_error("invalid native station save path");
  NativeWriterLease custody(save);
  const bool fresh=!fs::exists(save);
  // This optional fixture does NOT alter ordinary Single-Galaxy genesis.
  Simulation seed(0x5202);
  const auto test_station=seed.add_station({0,0});
  // One original colony ship with actual finite individually tracked crew.
  // The prior generic docked hull could never fit/mine with the original
  // colony materials model. This is a NEW-WORLD genesis change only.
  // The ship is not gifted a drill or physical refined stock.
  const auto starting_ship=seed.add_colony_ship(kOwner,{0,0},12,8);
  (void)seed.add_colony_ship(8,{15000,0},8,2);
  // Native v125 SQLite restore requires a matching durable NPC roster for
  // EVERY original colony ship. Initialize after both crews exist.
  seed.enable_individual_workforce();
  // Explicit test-station genesis ONLY: a single persistent finite asteroid.
  // Existing saves ignore this seed. A visual Godot sample never adds ore.
  (void)seed.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
  seed.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
  // Opt in ONLY the genesis of this explicit test station. Existing
  // SQLite worlds must load their own original rules and resources.
  seed.enable_native_swept_asteroid_collision();
  // Default production collection/refinery uses original source custody
  // (v123+), not the deferred v126 two-tonne mining-module prototype.
  seed.enable_station_ore_provenance();
  PersistentWorld world(save_utf8,std::move(seed));
  if(fresh){
   // One genuine original ship docking command and transition in the
   // persisted simulation, not a Godot-side dock state or second hull.
   Command berth{};
   berth.id=900001;berth.sequence=berth.id;
   berth.execute_tick=world.world().tick()+1;
   berth.issuer=kOwner;berth.actor=starting_ship;
   berth.target=test_station;berth.action=Action::RequestDock;
   if(!world.submit(berth))throw std::runtime_error("original colony berth command refused");
   world.step();
   const auto& dock_event=world.world().events().back();
   if(dock_event.command_id!=berth.id || dock_event.result!=Outcome::Applied)
     throw std::runtime_error("original starting colony berth not committed");
   world.steps(Simulation::kDockTransitionTicks);
   const auto* docked=world.world().find(starting_ship);
   if(!docked || docked->dock_state!=DockState::Docked ||
      docked->dock_station!=test_station ||
      !world.world().colony_ship(starting_ship) ||
      world.world().colony_ship(starting_ship)->crew!=8 ||
      docked->fitting.mining_tool())
     throw std::runtime_error("physical starter ship/custody or drill-free contract failed");
   if(!world.register_player_at_station(kOwner,test_station))
    throw std::runtime_error("native test station registration denied");
  }
  if(!world.world().player_people().count(kOwner))
    throw std::runtime_error("existing world lacks registered person; no silent bootstrap");
  // The dock ledger references real world IDs and never stores material stock.
  // Reopen the SAME SQLite save under this already-acquired native writer lease.
  dock::Ledger dock_ledger(save_utf8);
  dock04::Traffic dock_traffic(save_utf8);
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
   // COL* is colony-local virtual access only: inventory is the SAME
   // original station owner hangar and refinery. No item relocation.
   const bool colony_console=f.size()==5 &&
       (f[0]=="COLVIEW"||f[0]=="COLREFINE"||f[0]=="COLUNLOAD");
   const bool colony_view=colony_console&&f[0]=="COLVIEW";
   const bool colony_refine=colony_console&&f[0]=="COLREFINE";
   const bool colony_unload=colony_console&&f[0]=="COLUNLOAD";
   const bool population=f.size()==5&&f[0]=="POPVIEW";
   const bool dock_view=f.size()==5&&f[0]=="DOCKVIEW";
   const bool dock_assign=f.size()==5&&f[0]=="DOCKASSIGN";
   const bool dock_release=f.size()==5&&f[0]=="DOCKRELEASE";
   const bool dock_passenger=f.size()==5&&f[0]=="DOCKPASS";
   const bool dock_freight=f.size()==6&&f[0]=="DOCKFREIGHT";
   const bool dock_work=f.size()==5&&f[0]=="DOCKWORK";
   const bool dock_action=dock_view||dock_assign||dock_release||
                          dock_passenger||dock_freight||dock_work;
   if(!walking&&!boarding&&!exiting&&!view&&!refine&&!unload&&!colony_console&&!population&&!dock_action){deny(0,"malformed");continue;}
   std::uint64_t requested_session=0,req=0,expected=0,ship=0,quantity=0;
   std::int64_t dx=0,dz=0;
   if(!parse(f[1],requested_session)||!parse(f[2],req)||!parse(f[3],expected)||
      (walking&&(!parse(f[4],dx)||!parse(f[5],dz)))||
      (!walking&&!parse(f[4],ship))||
      (dock_freight&&!parse(f[5],quantity))){deny(0,"integer");continue;}
   if(requested_session!=session){deny(req,"session");continue;}
   if(req<=latest_request||req>10000){deny(req,"replay");continue;}
   latest_request=req;
   if(expected!=world.generation()){deny(req,"stale-world");continue;}
   const auto now=std::chrono::steady_clock::now();
   if(now-last<std::chrono::milliseconds(45)){deny(req,"rate");continue;}
   const auto& person=world.world().player_people().at(kOwner);
   if(population){
    if(person.state!=PlayerPersonState::AtStation ||
       person.owner!=kOwner || !person.physical_station ||
       ship!=person.physical_station){
       deny(req,"population-is-local-to-current-colony");continue;
    }
    // One bounded read-only census sampling pass, NEVER per-frame
    // simulation of millions of named 3D NPCs. Truncation is explicit.
    // Only station-located named workers with a real station site
    // assignment and ship/logistics role qualify as dock workers.
    std::uint64_t present=0,scanned=0;
    bool truncated=false;
    for(const auto& [id,npc]:world.world().npc_records()){
       (void)id;
       if(++scanned>4096){truncated=true;break;}
       if(npc.location!=ship)continue;
       ++present;
    }
    const auto assigned=dock_ledger.active_staff(world.world(),ship,kOwner);
    std::cout<<"COLPOP|1|"<<session<<'|'<<req<<'|'<<world.world().tick()
             <<'|'<<world.generation()<<'|'<<ship<<'|'<<present<<'|'
             <<assigned<<'|'<<(truncated?1:0)<<'\n'<<std::flush;
    if(!std::cout)throw std::runtime_error("colony population pipe closed");
    last=now;
    continue;
   }
   if(dock_action){
    // Only the original authenticated physical PERSON opens local dock work.
    if(person.owner!=kOwner || person.state!=PlayerPersonState::AtStation ||
       !person.physical_station){
      deny(req,"dock-service-requires-local-owner");continue;
    }
    const EntityId local_station=person.physical_station;
    if((dock_view||dock_work) && ship!=local_station){
      deny(req,"dock-service-is-local");continue;
    }
    std::uint64_t result=0;
    if(dock_assign){
      bool appointed=false;
      // The original ship NPC crosses the same real docked berth. No
      // generic crewman is counted as local labor before its SQLite STEP.
      const auto recruit=[&](std::uint64_t candidate)->bool{
        const auto it=world.world().npc_records().find(candidate);
        if(it==world.world().npc_records().end() ||
           it->second.role!=NpcRole::Ship || it->second.transport_id)
          return false;
        const auto* home=world.world().find(it->second.home_ship);
        if(!home || home->owner_id!=kOwner ||
           home->dock_state!=DockState::Docked ||
           home->dock_station!=local_station)return false;
        if(it->second.location==local_station)
          return dock_ledger.assign(world.world(),local_station,kOwner,candidate);
        if(it->second.location!=it->second.home_ship ||
           dock_ledger.active_staff(world.world(),local_station,kOwner)>=
               dock::Ledger::kMaxStaff)return false;
        // Verify exact named NPC custody and genuine station access before
        // declaring employment. The transfer's generation and immutable
        // receipt are committed within the original PersistentWorld STEP.
        if(!world.station_dock_worker_transfer(kOwner,it->second.home_ship,
                local_station,candidate,true,receipt_prefix|req))return false;
        galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
        if(!dock_ledger.assign(world.world(),local_station,kOwner,candidate))
          throw std::runtime_error("physically transferred dock worker could not receive shift");
        return true;
      };
      if(ship!=0)appointed=recruit(ship);
      else{
        // Choose an original identity BEFORE a world STEP swaps Simulation.
        // Never mutate the map while iterating a reference into it.
        std::uint64_t selected=0,scanned=0;
        for(const auto& [npc_id,npc]:world.world().npc_records()){
          if(++scanned>4096)break;
          if(npc.role!=NpcRole::Ship || npc.transport_id)continue;
          const auto* hull=world.world().find(npc.home_ship);
          if(!hull || hull->owner_id!=kOwner ||
             hull->dock_state!=DockState::Docked ||
             hull->dock_station!=local_station)continue;
          if(npc.location==local_station){
            if(dock_ledger.active_staff(world.world(),local_station,kOwner)<
               dock::Ledger::kMaxStaff &&
               world.world().npc_available_for_duty(npc_id)){
              selected=npc_id;break;
            }
          }else if(npc.location==npc.home_ship){
            auto preview=world.world();
            if(preview.station_dock_worker_transfer(
                   kOwner,npc.home_ship,local_station,npc_id,true)){
              selected=npc_id;break;
            }
          }
        }
        if(selected)appointed=recruit(selected);
      }
      if(!appointed){deny(req,"no-eligible-physical-ship-crew-for-dock");continue;}
      result=1;
    }else if(dock_release){
      // End shore duty by returning the SAME named person to the SAME
      // physically docked original ship; keep shift if return is refused.
      std::uint64_t candidate=ship?ship:
          dock_ledger.first_assigned(local_station,kOwner);
      if(!candidate){
        // A crash after original shore movement but before shift insert
        // must not strand crew. Recovery searches real canonical custody,
        // not a fabricated staff list or an arbitrary station teleport.
        std::uint64_t scanned=0;
        for(const auto& [id,npc]:world.world().npc_records()){
          if(++scanned>4096)break;
          const auto* home=world.world().find(npc.home_ship);
          if(npc.role==NpcRole::Ship && npc.location==local_station &&
             home && home->owner_id==kOwner &&
             home->dock_state==DockState::Docked &&
             home->dock_station==local_station){
            candidate=id;break;
          }
        }
      }
      const auto it=world.world().npc_records().find(candidate);
      if(!candidate || it==world.world().npc_records().end() ||
         it->second.location!=local_station ||
         !world.station_dock_worker_transfer(kOwner,it->second.home_ship,
              local_station,candidate,false,receipt_prefix|req)){
        deny(req,"dock-worker-cannot-return-to-original-berth");continue;
      }
      galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
      // A recovery return may legitimately have no appointment because
      // the previous process crashed after the original custody commit.
      (void)dock_ledger.release(local_station,kOwner,candidate);
      result=2;
    }else if(dock_passenger){
      // DOCK-04: select a REAL ferry with seats, provisions and the same
      // original physical station as a DISTINCT, owned source ship.
      // No station-only check-in may be mislabeled "boarded".
      bool queued=false;
      std::uint64_t scanned=0;
      for(const auto& [npc_id,npc]:world.world().npc_records()){
        if(++scanned>4096)break;
        if(npc.location!=local_station &&
           npc.location!=npc.home_ship)continue;
        if(ship){
          if(dock_traffic.enqueue(world.world(),local_station,kOwner,npc_id,ship)){
            queued=true;break;
          }
        }else{
          for(const auto& [hull_id,hull]:world.world().entities()){
            if(hull.type!=EntityType::Ship || hull.owner_id!=kOwner ||
               hull.dock_state!=DockState::Docked ||
               hull.dock_station!=local_station)continue;
            if(dock_traffic.enqueue(world.world(),local_station,kOwner,npc_id,hull_id)){
              queued=true;break;
            }
          }
          if(queued)break;
        }
      }
      if(!queued){
        deny(req,"boarding-needs-two-real-docked-ships-and-available-seats");
        continue;
      }
      result=3;
    }else if(dock_freight){
      if(!dock_ledger.enqueue_freight(world.world(),local_station,kOwner,ship,quantity)){
        deny(req,"freight-not-docked-or-queue-full");continue;
      }
      result=4;
    }else if(dock_work){
      // One original station writer serves both traffic classes. Real dock
      // workers are the shared throughput budget; two gangways reserve up
      // to two passenger transfers/cycle. Weighted freight turns ensure
      // urgent passenger handling cannot indefinitely starve real cargo.
      dock_traffic.reconcile(world,local_station,kOwner);
      const auto roster=dock_ledger.service_roster(world.world(),local_station,kOwner);
      std::uint64_t completed=0,passengers=0,freight=0;
      for(const auto worker:roster){
        (void)worker; // roster is source-attested, not a virtual service actor
        const auto traffic=dock_traffic.snapshot(world.world(),local_station,kOwner);
        const auto cargo=dock_ledger.snapshot(world.world(),local_station,kOwner);
        const bool passenger_ready=traffic.waiting>0 &&
             traffic.passenger_gangways>0 && passengers<2;
        const bool freight_ready=cargo.freight_tonnes>0 && freight<4;
        if(!passenger_ready && !freight_ready)break;
        // Max two consecutive passenger boardings before one freight turn
        // where both queues are occupied; emergency pressure breaks ties
        // but cannot expand the physical gangway count.
        const bool choose_passenger=passenger_ready &&
            (!freight_ready ||
             (traffic.passenger_streak<2 &&
              (traffic.urgent>0 || freight>0 || passengers<2)));
        bool applied=false;
        if(choose_passenger){
          applied=dock_traffic.serve_one(world,local_station,kOwner);
          if(applied)++passengers;
        }
        if(!applied && freight_ready){
          applied=dock_ledger.serve_one(world,local_station,kOwner,worker,true);
          if(applied){
            ++freight;
            dock_traffic.freight_completed(local_station,kOwner);
          }
        }
        if(!applied && !choose_passenger && passenger_ready){
          applied=dock_traffic.serve_one(world,local_station,kOwner);
          if(applied)++passengers;
        }
        if(!applied)break;
        ++completed;
      }
      result=completed?5:6;
      if(completed)
        galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
    }else if(dock_view){
      // A view is the safe place to reconcile already-committed original
      // travel receipts after native host restart; never retry on ambiguity.
      dock_traffic.reconcile(world,local_station,kOwner);
    }
    const auto data=dock_ledger.snapshot(world.world(),local_station,kOwner);
    const auto traffic=dock_traffic.snapshot(world.world(),local_station,kOwner);
    std::cout<<"DOCK|1|"<<session<<'|'<<req<<'|'<<world.world().tick()
             <<'|'<<world.generation()<<'|'<<local_station
             <<'|'<<data.staff<<'|'<<data.passengers<<'|'<<data.freight_tonnes
             <<'|'<<data.freight_jobs<<'|'<<data.held_jobs
             <<'|'<<data.completed_passengers<<'|'<<data.completed_tonnes
             <<'|'<<result<<'|'<<data.qualified_staff
             <<'|'<<data.service_capacity
             <<'|'<<traffic.waiting<<'|'<<traffic.boarded
             <<'|'<<traffic.held<<'|'<<traffic.urgent
             <<'|'<<traffic.passenger_gangways
             <<'|'<<traffic.congestion<<'\n'<<std::flush;
    if(!std::cout)throw std::runtime_error("dock service response pipe closed");
    last=now;continue;
   }
   if(view || refine || unload || colony_console){
    // The local remote terminal saves walking, NOT travel. Identity is
    // taken from original PERSON physical_station. Never trust a remote
    // station ID or client-claimed item/cargo quantity.
    if(!(colony_console ?
         station_refinery_protocol::colony_resident_access(person) :
         station_refinery_protocol::accessible(person)) ||
       ((view||colony_view) && ship!=person.physical_station)){
       deny(req,"refinery-terminal-distance-or-custody");continue;
    }
    int result=0;
    std::int64_t transferred=0;
    if(refine||colony_refine){
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
    if(unload||colony_unload){
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
    std::cout<<station_refinery_protocol::make_snapshot(
        world,kOwner,session,req,result,transferred,colony_console)<<std::flush;
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

#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
    try {
        auto utf8=galaxy::windows_utf8_argv::convert_all(argc,argv);
        auto pointers=galaxy::windows_utf8_argv::views(utf8);
        return run_host(argc,pointers.data());
    }catch(const std::exception& error){
        std::cerr<<"Windows native host UTF-8 startup error: "<<error.what()<<'\n';
        return 2;
    }
}
#else
int main(int argc,char** argv){return run_host(argc,argv);}
#endif
