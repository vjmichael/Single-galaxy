// B7.0a developer terminal harness, NOT the player-facing UI or production server.
// Proves actual user-input commands against the same authoritative PersistentWorld
// used by the old live host: new-world colony start, mining, manufacturing, first build.
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
namespace {
constexpr PlayerId kLocalPilot=7;
constexpr std::int64_t kSensorRange=25000;
void help(){
    std::cout<<"Commands: status, scan, mine <asteroid_id> <1..1000>, refine <1..1000>, "
             <<"fabricate <1..1000>, build, thrust <-1..1> <-1..1>, "
             <<"dock <station_id>, undock, step <1..50>, help, quit\n";
}
bool near(Vector2 a, Vector2 b){
    const auto dx=a.x-b.x,dy=a.y-b.y;
    return dx>=-kSensorRange && dx<=kSensorRange && dy>=-kSensorRange && dy<=kSensorRange &&
           dx*dx+dy*dy<=kSensorRange*kSensorRange;
}
void status(const Simulation& s,EntityId ship_id){
    const auto* e=s.find(ship_id);const auto* c=s.colony_ship(ship_id);
    if(!e || !c)throw std::runtime_error("save lacks starter colony ship");
    std::cout<<"STATUS tick="<<s.tick()<<" ship="<<ship_id<<" position=("<<e->position.x<<","<<e->position.y
             <<") velocity=("<<e->velocity.x<<","<<e->velocity.y<<")\n"
             <<"  crew="<<c->crew<<" raw_ore="<<e->cargo<<" refined="<<c->refined
             <<" modules="<<c->modules<<" loaded_mass="<<(e->cargo+c->refined+2*c->modules)
             <<"/"<<e->cargo_capacity<<" outposts="<<s.built_outposts().size()
             <<" total_ore_equivalent="<<s.total_ore()<<"\n";
}
void scan(const Simulation& s,EntityId ship_id){
    const auto* ship=s.find(ship_id);
    if(!ship)throw std::runtime_error("missing ship");
    std::cout<<"SCAN radius="<<kSensorRange<<" mm (local development range)\n";
    for(const auto& [id,e]:s.entities()){
        if(id==ship_id || e.system_id!=ship->system_id || !near(ship->position,e.position))continue;
        if(e.type==EntityType::Asteroid)
            std::cout<<"  asteroid="<<id<<" ore="<<e.ore<<" position=("<<e.position.x<<","<<e.position.y<<")\n";
        else if(e.type==EntityType::Station)
            std::cout<<"  outpost="<<id<<" owner="<<e.owner_id<<" position=("<<e.position.x<<","<<e.position.y<<")\n";
    }
}
std::int64_t parse_int(std::istringstream& is){
    std::string v; if(!(is>>v) || v.empty() || v.size()>18 ||
       (v[0]=='-' && v.size()==1)) throw std::invalid_argument("missing/invalid integer");
    for(std::size_t i=v[0]=='-'?1:0;i<v.size();++i)
        if(v[i]<'0'||v[i]>'9')throw std::invalid_argument("non-numeric argument");
    return std::stoll(v);
}
void end_args(std::istringstream& is){
    std::string extra;if(is>>extra)throw std::invalid_argument("extra argument");
}
}
int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"usage: galaxy_colony_cli COLONY_SAVE.sqlite\n";return 2;}
    try{
        Simulation initial(0xB700A);
        const EntityId ship=initial.add_colony_ship(kLocalPilot,{0,0},60,24);
        initial.add_asteroid({2000,1000},40);
        initial.add_asteroid({15000,0},30);
        initial.add_asteroid({-60000,-2000},100); // outside starting scan radius
        // No station, market or stargate created at all: exactly one colony ship.
        const bool existing=fs::exists(argv[1]);
        PersistentWorld world(argv[1],std::move(initial));
        const auto* c=world.world().colony_ship(ship);
        if(!c || !world.world().find(ship) || world.world().find(ship)->owner_id!=kLocalPilot)
            throw std::runtime_error("this save is not a B7.0a local colony");
        std::cout<<(existing?"RESUMED":"NEW_COLONY")<<" one colony ship, "<<c->crew
                 <<" colonists, no prebuilt infrastructure\n";
        help();status(world.world(),ship);
        std::string input;
        while(std::getline(std::cin,input)){
            std::istringstream is(input);std::string word;
            if(!(is>>word))continue;
            try{
                if(word=="quit" || word=="exit"){end_args(is);break;}
                if(word=="help"){end_args(is);help();continue;}
                if(word=="status"){end_args(is);status(world.world(),ship);continue;}
                if(word=="scan"){end_args(is);scan(world.world(),ship);continue;}
                if(word=="step"){
                    auto n=parse_int(is);end_args(is);
                    if(n<1||n>50)throw std::invalid_argument("step count must be 1..50");
                    world.steps(static_cast<std::uint64_t>(n));status(world.world(),ship);continue;
                }
                Command cmd{};
                cmd.id=10000+world.generation();
                while(world.world().has_command_id(cmd.id))++cmd.id;
                cmd.sequence=cmd.id;cmd.execute_tick=world.world().tick()+1;
                cmd.issuer=kLocalPilot;cmd.actor=ship;
                if(word=="mine"){
                    const auto rock=parse_int(is),n=parse_int(is);end_args(is);
                    if(rock<=0||n<1||n>1000)throw std::invalid_argument("invalid asteroid or amount");
                    cmd.target=static_cast<EntityId>(rock);cmd.amount=n;cmd.action=Action::Mine;
                }else if(word=="refine"){
                    cmd.amount=parse_int(is);end_args(is);cmd.action=Action::RefineOre;
                }else if(word=="fabricate"){
                    cmd.amount=parse_int(is);end_args(is);cmd.action=Action::FabricateModule;
                }else if(word=="build"){
                    end_args(is);cmd.amount=1;cmd.action=Action::ConstructOutpost;
                }else if(word=="thrust"){
                    const auto x=parse_int(is),y=parse_int(is);end_args(is);
                    if(x<-1||x>1||y<-1||y>1)throw std::invalid_argument("invalid thrust axis");
                    cmd.action=Action::Thrust;cmd.thrust_x=static_cast<int>(x);cmd.thrust_y=static_cast<int>(y);
                }else if(word=="dock"){
                    const auto id=parse_int(is);end_args(is);
                    if(id<=0)throw std::invalid_argument("invalid station");
                    cmd.action=Action::RequestDock;cmd.target=static_cast<EntityId>(id);
                }else if(word=="undock"){
                    end_args(is);cmd.action=Action::RequestUndock;
                }else{std::cout<<"ERROR unknown command; enter help\n";continue;}
                if(!world.submit(cmd)){std::cout<<"ERROR command rejected at submission\n";continue;}
                world.step();
                const auto& event=world.world().events().back();
                std::cout<<"RESULT "<<to_string(event.result)<<" transfer="<<event.transferred<<"\n";
                status(world.world(),ship);
            }catch(const std::exception& ex){std::cout<<"ERROR "<<ex.what()<<'\n';}
        }
        std::cout<<"SAVE_CLOSED generation="<<world.generation()<<"\n";
        return 0;
    }catch(const std::exception&ex){std::cerr<<"FATAL "<<ex.what()<<'\n';return 1;}
}
