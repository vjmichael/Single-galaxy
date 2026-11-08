// FP3D-02F: no-browser, no-Python native desktop command ingress.
// Stdio is an OS-local child pipe; host owns the fixed single-player authority.
// Every accepted MOVE commits world STEP, receipt, pose in one original transaction.
#include "galaxy/NativeInteriorMovement.hpp"
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
void send_pose(const char* action,std::uint64_t session,std::uint64_t request,const Pose& p){
    std::cout<<action<<"|1|"<<session<<'|'<<request<<'|'<<p.sequence<<'|'
             <<p.x_mm<<'|'<<p.y_mm<<'|'<<p.z_mm<<'|'<<p.world_tick<<'|'
             <<p.world_generation<<'|'<<p.ship<<'|'<<(p.seated?1:0)<<'|'<<p.seat_sequence<<'\n'<<std::flush;
    if(!std::cout)throw std::runtime_error("Godot process read pipe closed");
}
void deny(std::uint64_t id,const char* reason){
    std::cout<<"DENY|1|"<<id<<'|'<<reason<<'\n'<<std::flush;
}
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: galaxy_desktop_player_host SQLITE_PATH\n";return 2;}
    try{
        const fs::path save(argv[1]);
        if(save.empty()||save.string().size()>1024||
           (!save.parent_path().empty()&&!fs::exists(save.parent_path())))
            throw std::invalid_argument("invalid native world save path");
        Simulation seed(0xC021);
        const auto station=seed.add_station({0,0});
        const auto ship=seed.spawn_ship_docked(kLocalOwner,station,24,60);
        if(!seed.register_player_person(kLocalOwner,ship))
            throw std::runtime_error("player-person bootstrap refused");
        PersistentWorld world(save.string(),std::move(seed));
        InteriorMovement person(save.string(),world);
        const std::uint64_t session=new_epoch();
        const std::uint64_t receipt_start=new_epoch();
        std::uint64_t latest_request=0,attempts=0;
        const auto frame=person.current(kLocalOwner);
        send_pose("POSE",session,0,frame);
        auto last_move=std::chrono::steady_clock::now()-std::chrono::milliseconds(100);
        std::string line;
        while(std::getline(std::cin,line)){
            if(++attempts>10000){deny(0,"session-request-budget");return 3;}
            if(line=="QUIT")return 0;
            if(line.size()>kMaxLine){deny(0,"oversize");continue;}
            const auto fields=split(line);
            const bool walking=fields.size()==6&&fields[0]=="MOVE";
            const bool seating=fields.size()==5&&fields[0]=="SEAT";
            if(!walking&&!seating){deny(0,"malformed");continue;}
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
            if(walking&&expected!=previous.sequence){deny(req,"stale-pose");continue;}
            if(seating&&expected!=previous.seat_sequence){deny(req,"stale-seat");continue;}
            if(walking&&!person.move(kLocalOwner,expected,receipt,dx,dz)){
                deny(req,"physical-or-custody");continue;
            }
            if(seating&&!person.set_seated(kLocalOwner,expected,receipt,desired!=0)){
                deny(req,"seat-or-custody");continue;
            }
            last_move=now;
            send_pose("ACK",session,req,person.current(kLocalOwner));
        }
        return 0; // stdio EOF = OS client disconnected; never invent a command
    }catch(const std::exception& e){
        std::cerr<<"FP3D native player host aborted: "<<e.what()<<'\n';return 1;
    }
}
