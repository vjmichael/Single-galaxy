#include "galaxy/NativeWriterLease.hpp"
#include "galaxy/NativeWindowsUtf8Argv.hpp"
#include "galaxy/ServerBreadcrumb.hpp"
// FP3D-02F: no-browser, no-Python native desktop command ingress.
// Stdio is an OS-local child pipe; host owns the fixed single-player authority.
// Every accepted MOVE commits world STEP, receipt, pose in one original transaction.
#include "galaxy/NativeInteriorMovement.hpp"
#include "galaxy/SpaceMiningContact.hpp"
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
using namespace galaxy::fp3d;
namespace fs=std::filesystem;
namespace {
constexpr PlayerId kLocalOwner=7;
constexpr std::size_t kMaxLine=160;
std::uint64_t new_epoch(){
    std::random_device rd;
    auto n=((std::uint64_t(rd())<<32)|std::uint64_t(rd())) & 0x3fffffffffffffffULL;
    return n?n:1;
}
std::vector<std::string_view> split(const std::string& line){
    std::vector<std::string_view> fields;std::size_t start=0;
    do{
        const auto next=line.find('|',start);
        fields.emplace_back(line.data()+start,(next==std::string::npos?line.size():next)-start);
        if(next==std::string::npos)break;
        start=next+1;
        if(fields.size()>8)break;
    }while(true);
    return fields;
}
template<class T>bool number(std::string_view s,T& out){
    if(s.empty()||s.size()>20||s.front()=='+'||s.find_first_of(" \t\r\n")!=std::string_view::npos)return false;
    const char* begin=s.data();const char* end=begin+s.size();
    auto [p,ec]=std::from_chars(begin,end,out);
    return ec==std::errc{}&&p==end;
}
void send_pose(const char* action,std::uint64_t session,std::uint64_t request,
               const Pose& p,const PersistentWorld& world){
    std::cout<<action<<"|1|"<<session<<'|'<<request<<'|'<<p.sequence<<'|'
             <<p.x_mm<<'|'<<p.y_mm<<'|'<<p.z_mm<<'|'<<world.world().tick()<<'|'
             <<world.generation()<<'|'<<p.ship<<'|'<<(p.seated?1:0)<<'|'<<p.seat_sequence<<'\n'<<std::flush;
    if(!std::cout)throw std::runtime_error("Godot process read pipe closed");
}

void send_ship(std::uint64_t session,std::uint64_t req,std::uint64_t command_id,
               Outcome outcome,const FlightState& f){
    // Grounded in the ORIGINAL ship Entity, NEVER client-predicted motion.
    std::cout<<"SHIP|1|"<<session<<'|'<<req<<'|'<<command_id<<'|'
             <<static_cast<int>(outcome)<<'|'<<f.ship<<'|'<<f.tick<<'|'<<f.generation
             <<'|'<<f.x<<'|'<<f.y<<'|'<<f.z
             <<'|'<<f.vx<<'|'<<f.vy<<'|'<<f.vz
             <<'|'<<f.heading<<'|'<<static_cast<int>(f.dock)<<'\n'<<std::flush;
    if(!std::cout)throw std::runtime_error("Godot flight observation pipe closed");
}
void send_rock_contact(std::uint64_t session,std::uint64_t req,
                       const PersistentWorld& world,EntityId ship_id) {
    // Read-only bounded original physical geometry. No Godot mining authority.
    const auto* ship=world.world().find(ship_id);
    if(!ship || ship->owner_id!=kLocalOwner || ship->type!=EntityType::Ship)
        throw std::runtime_error("original owned ship missing for rock telemetry");
    const auto rock=space_mining_view::nearest_original_asteroid(
        world.world(),kLocalOwner,ship_id);
    EntityId contact_rock=0;
    std::uint64_t contact_tick=0;
    const bool swept=world.world().native_swept_asteroid_collision_enabled();
    // Only a genuine current original STEP contact; no replay on reconnect.
    if(swept && req) {
        const auto& history=world.world().activity_events();
        for(auto it=history.rbegin();it!=history.rend();++it) {
            if(it->tick<world.world().tick())break;
            if(it->tick==world.world().tick() &&
               it->kind==ActivityKind::ShipAsteroidContact &&
               it->actor==ship_id && it->result==Outcome::Applied) {
                contact_rock=it->target;
                contact_tick=it->tick;
                break;
            }
        }
    }
    if(rock) {
        std::cout<<"ROCK|1|"<<session<<'|'<<req<<'|'<<ship_id<<'|'
                 <<world.world().tick()<<'|'<<world.generation()<<'|'
                 <<rock->rock<<'|'<<rock->ore_tonnes<<'|'
                 <<rock->x_mm<<'|'<<rock->y_mm<<'|'<<rock->z_mm<<'|'
                 <<rock->radius_mm<<'|'<<unsigned(rock->taxonomy)<<'|'
                 <<rock->cargo_tonnes<<'|'<<rock->cargo_capacity_tonnes<<'|'
                 <<(rock->mining_tool_online?1:0);
        if(swept)std::cout<<'|'<<contact_rock<<'|'<<contact_tick;
        std::cout<<'\n'<<std::flush;
    } else {
        std::cout<<"ROCKNONE|1|"<<session<<'|'<<req<<'|'<<ship_id<<'|'
                 <<world.world().tick()<<'|'<<world.generation()<<'|'
                 <<ship->cargo<<'|'<<ship->cargo_capacity<<'|'
                 <<(space_mining_view::tool_online(world.world(),*ship)?1:0);
        if(swept)std::cout<<'|'<<contact_rock<<'|'<<contact_tick;
        std::cout<<'\n'<<std::flush;
    }
    if(!std::cout)throw std::runtime_error("Godot original-rock observation pipe closed");
}
bool flight_action(std::string_view name,PilotControl& out){
    if(name=="UND")out=PilotControl::Undock;
    else if(name=="DCK")out=PilotControl::Dock;
    else if(name=="LEFT")out=PilotControl::YawLeft;
    else if(name=="RIGHT")out=PilotControl::YawRight;
    else if(name=="FWD")out=PilotControl::Forward;
    else if(name=="REV")out=PilotControl::Reverse;
    else if(name=="PORT")out=PilotControl::StrafePort;
    else if(name=="STAR")out=PilotControl::StrafeStarboard;
    else if(name=="ASC")out=PilotControl::Ascend;
    else if(name=="DES")out=PilotControl::Descend;
    else if(name=="BRAKE")out=PilotControl::Counterthrust;
    else return false;
    return true;
}

void deny(std::uint64_t id,const char* reason){
    std::cout<<"DENY|1|"<<id<<'|'<<reason<<'\n'<<std::flush;
}
}
static int run_host(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: galaxy_desktop_player_host SQLITE_PATH\n";return 2;}
    try{
        const std::string save_utf8(argv[1]);
   // sqlite3_open_v2 takes UTF-8; NativeWriterLease uses an accurately
   // decoded Windows filesystem path. Never round-trip via path.string(),
   // which may convert it back into a non-UTF8 active code page.
   const fs::path save=fs::u8path(save_utf8);
        if(save.empty()||save_utf8.size()>1024||
           (!save.parent_path().empty()&&!fs::exists(save.parent_path())))
            throw std::invalid_argument("invalid native world save path");
        NativeWriterLease custody(save);
        Simulation seed(0xC021);
        const auto station=seed.add_station({0,0});
        const auto ship=seed.spawn_ship_docked(kLocalOwner,station,24,60);
        if(!seed.register_player_person(kLocalOwner,ship))
            throw std::runtime_error("player-person bootstrap refused");
        PersistentWorld world(save_utf8,std::move(seed));
        InteriorMovement person(save_utf8,world);
        if(world.world().has_pending_cockpit_flight())
            throw std::runtime_error("Interrupted cockpit flight command pending in original journal; refusing silent replay");
        const std::uint64_t session=new_epoch();
        const std::uint64_t receipt_start=new_epoch();
        // Reserved signed-positive command ID namespace, unique per child session.
        const std::uint64_t flight_base=(std::uint64_t{1}<<60) |
            ((new_epoch()&((std::uint64_t{1}<<60)-1)) & ~((std::uint64_t{1}<<20)-1));
        std::uint64_t latest_request=0,attempts=0;
        const auto frame=person.current(kLocalOwner);
        send_pose("POSE",session,0,frame,world);
        send_ship(session,0,0,Outcome::Applied,person.flight_state(kLocalOwner));
        send_rock_contact(session,0,world,frame.ship);
        auto last_move=std::chrono::steady_clock::now()-std::chrono::milliseconds(100);
        std::string line;
        while(std::getline(std::cin,line)){
            if(++attempts>10000){deny(0,"session-request-budget");return 3;}
            if(line=="QUIT")return 0;
            if(line.size()>kMaxLine){deny(0,"oversize");continue;}
            const auto fields=split(line);
            const bool walking=fields.size()==6&&fields[0]=="MOVE";
            const bool seating=fields.size()==5&&fields[0]=="SEAT";
            const bool flying=fields.size()==5&&fields[0]=="FLIGHT";
            if(!walking&&!seating&&!flying){deny(0,"malformed");continue;}
            std::uint64_t supplied_session=0,req=0,expected=0,desired=0;
            std::int64_t dx=0,dz=0;
            if(!number(fields[1],supplied_session)||!number(fields[2],req)||
               !number(fields[3],expected)||
               (walking&&(!number(fields[4],dx)||!number(fields[5],dz)))||
               (seating&&(!number(fields[4],desired)||desired>1))){
                deny(0,"bad-integer");continue;
            }
            if(supplied_session!=session){deny(req,"session");continue;}
            if(req<=latest_request||req>1000000000ULL){deny(req,"replayed-request");continue;}
            latest_request=req; // denied requests cannot be replayed
            if(walking&&dx==0&&dz==0){deny(req,"zero-step");continue;}
            if(walking&&(dx>100||dx< -100||dz>100||dz< -100||dx*dx+dz*dz>10000)){
                deny(req,"speed");continue;
            }
            const auto now=std::chrono::steady_clock::now();
            if(now-last_move<std::chrono::milliseconds(45)){
                deny(req,"rate-limit");continue;
            }
            const auto receipt=receipt_start+req;
            if(receipt>std::uint64_t(std::numeric_limits<std::int64_t>::max())){
                deny(req,"receipt-range");continue;
            }
            const auto previous=person.current(kLocalOwner);
            if(flying){
                PilotControl action=PilotControl::Forward;
                if(req>10000||!flight_action(fields[4],action)){
                    deny(req,"invalid-flight-action");continue;
                }
                if(!previous.seated || expected!=previous.seat_sequence){
                    deny(req,"not-seated-or-stale-seat");continue;
                }
                const auto command_id=flight_base|req;
                const auto outcome=person.pilot(kLocalOwner,expected,command_id,action);
                if(!outcome){deny(req,"native-flight-refused");continue;}
                last_move=now;
                galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
                send_ship(session,req,command_id,*outcome,person.flight_state(kLocalOwner));
                send_rock_contact(session,req,world,previous.ship);
                continue;
            }
            if(walking&&expected!=previous.sequence){deny(req,"stale-pose");continue;}
            if(seating&&expected!=previous.seat_sequence){deny(req,"stale-seat");continue;}
            if(walking&&!person.move(kLocalOwner,expected,receipt,dx,dz)){
                deny(req,"physical-or-custody");continue;
            }
            if(seating&&!person.set_seated(kLocalOwner,expected,receipt,desired!=0)){
                deny(req,"seat-or-custody");continue;
            }
            last_move=now;
            galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
            send_pose("ACK",session,req,person.current(kLocalOwner),world);
        }
        return 0; // stdio EOF = OS client disconnected; never invent a command
    }catch(const std::exception& e){
        std::cerr<<"FP3D native player host aborted: "<<e.what()<<'\n';return 1;
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
