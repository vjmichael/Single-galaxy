#include "galaxy/NativeInteriorMovement.hpp"
#include "galaxy/NativeDockingTarget.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::fp3d;
namespace fs=std::filesystem;
static void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
static constexpr std::uint64_t kFlightTag=std::uint64_t{1}<<60;
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("expected SQLite save path");
  const std::string save=argv[1];
  for(const auto& suffix:{"","-wal","-shm"})fs::remove(save+suffix);
  EntityId own_ship=0;
  {
   Simulation initial(0xC021);
   const auto station=initial.add_station({0,0});
   own_ship=initial.spawn_ship_docked(7,station,24,60);
   need(initial.register_player_person(7,own_ship),"register original pilot-person");
   PersistentWorld world(save,std::move(initial));
   InteriorMovement cockpit(save,world);
   {
    Simulation far(0xC023);
    (void)far.add_station({0,0});
    const auto vessel=far.add_ship(7,{20000,0},24);
    const auto* candidate=far.find(vessel);
    need(candidate && !docking::nearest_original_station(far,*candidate),
         "original host refuses station too far away");
   }
   need(!cockpit.pilot(7,0,kFlightTag|1,PilotControl::Undock),
        "unseated input denied");
   need(!world.world().has_pending_cockpit_flight(),"no rejected pending command");
   need(cockpit.set_seated(7,0,900,true),"seat original owner");
   need(!cockpit.pilot(7,0,kFlightTag|2,PilotControl::Undock),
        "stale seat sequence denied");
   need(!world.submit(Command{kFlightTag|3,world.world().tick()+1,0,7,own_ship,0,
       Action::Thrust,1,0,0}),"public API cannot bypass cockpit admission");
   const auto undock=cockpit.pilot(7,1,kFlightTag|4,PilotControl::Undock);
   need(undock && *undock==Outcome::Applied,"original undock executes");
   need(!cockpit.pilot(7,1,kFlightTag|119,PilotControl::Ascend),
        "legacy 2D SQLite must never silently upgrade for vertical input");
   need(!world.world().has_pending_cockpit_flight(),
        "unsupported vertical control leaves original journal untouched");
   auto f=cockpit.flight_state(7);
   need(f.ship==own_ship && f.dock==DockState::InSpace &&
        !world.world().has_pending_cockpit_flight(),"original docking transition completes");
   need(cockpit.pilot(7,1,kFlightTag|5,PilotControl::YawLeft)==Outcome::Applied,
        "native original yaw left");
   f=cockpit.flight_state(7);
   need(f.heading==15,"visual-left yaw persists as native positive heading");
   need(cockpit.pilot(7,1,kFlightTag|6,PilotControl::YawRight)==Outcome::Applied,
        "native original yaw right");
   need(cockpit.flight_state(7).heading==0,"original yaw restored");
   const auto prior=cockpit.flight_state(7);
   need(cockpit.pilot(7,1,kFlightTag|7,PilotControl::Forward)==Outcome::Applied,
        "original forward thrust");
   f=cockpit.flight_state(7);
   need(f.vx>prior.vx && f.x>prior.x,"native ship velocity and position change");
   need(cockpit.pilot(7,1,kFlightTag|8,PilotControl::StrafePort)==Outcome::Applied,
        "original local port strafe");
   f=cockpit.flight_state(7);
   need(f.vy>0,"port strafe is visual-left native positive Y velocity");
   const auto speed_x=f.vx,speed_y=f.vy;
   need(cockpit.pilot(7,1,kFlightTag|9,PilotControl::Counterthrust)==Outcome::Applied,
        "original opposite acceleration");
   f=cockpit.flight_state(7);
   need(f.vx<speed_x&&f.vy<speed_y,"counterthrust damps original velocity");
   need(!cockpit.pilot(7,1,kFlightTag|9,PilotControl::Forward),
        "same canonical command ID never replays");
   need(f.vx==0 && f.vy==0 && f.vz==0,
        "ship must be stationary for original return docking");
   need(world.world().find(own_ship)->dock_station==0,
        "undocked original station identity is cleared");
   need(cockpit.pilot(7,1,kFlightTag|10,PilotControl::Dock)==Outcome::Applied,
        "native seat-authorized return dock near authentic station");
   f=cockpit.flight_state(7);
   need(f.dock==DockState::Docked && f.x==0 && f.y==0 &&
        !world.world().has_pending_cockpit_flight(),
        "native return dock finishes, co-locates at station and persists");
   need(!cockpit.pilot(7,1,kFlightTag|11,PilotControl::Dock),
        "cannot dock an already berthed ship");
   need(cockpit.pilot(7,1,kFlightTag|12,PilotControl::Undock)==Outcome::Applied,
        "same owned ship can depart again after docking");
   need(cockpit.flight_state(7).dock==DockState::InSpace,
        "return-to-space transition completed without creating replacement vessel");
   need(cockpit.set_seated(7,1,901,false),"stand in same hull");
   need(!cockpit.pilot(7,2,kFlightTag|10,PilotControl::Forward),
        "standing player cannot pilot");
   need(!world.world().has_pending_cockpit_flight(),"all flight commands executed");
  }
  {
   Simulation fallback(0xC021);
   PersistentWorld world(save,std::move(fallback));
   InteriorMovement cockpit(save,world);
   need(!cockpit.current(7).seated&&cockpit.current(7).seat_sequence==2,
        "standing persisted in SQLite");
   auto f=cockpit.flight_state(7);
   need(f.ship==own_ship&&f.dock==DockState::InSpace&&f.x>0,
        "actual original ship flight state recovered from SQLite");
   need(cockpit.set_seated(7,2,902,true),"sit after restart");
   bool injected=false;
   try{
    (void)cockpit.pilot(7,3,kFlightTag|13,PilotControl::Dock,
                       PilotFailPoint::AfterAcceptance);
   }catch(const std::runtime_error& e){
    injected=std::string(e.what()).find("injected cockpit flight")!=std::string::npos;
   }
   need(injected&&world.world().has_pending_cockpit_flight(),
        "crash leaves visible pending original flight command");
   need(!cockpit.pilot(7,3,kFlightTag|14,PilotControl::Forward),
        "pending command fails closed");
  }
  {
   Simulation fallback(0xC021);
   PersistentWorld world(save,std::move(fallback));
   need(world.world().has_pending_cockpit_flight(),
        "interrupted command survives SQLite restart and must block native host");
  }
  std::cout<<"FP3D03B source test executable PASS: original yaw, thrust, counterthrust,"
              " dock/undock, seat authority, SQLite restart, pending-dock fail-closed\n";
 }catch(const std::exception& e){
  std::cerr<<"FP3D03B FAIL: "<<e.what()<<'\n';return 1;
 }
 return 0;
}
