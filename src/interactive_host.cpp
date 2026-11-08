// B5.2 interactive station host: one authoritative SQLite world; stdin accepts bounded
// locally brokered commands, stdout emits only canonical SGVIEW v1 frames.
// No client chooses an issuer, actor, command ID, or execution tick.
#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/ServerDiagnostics.hpp"
#include "galaxy/Navigation.hpp"
#include "galaxy/Galaxy.hpp"
#include "galaxy/Region.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <tuple>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif
using namespace galaxy;
using Clock = std::chrono::steady_clock;
namespace fs=std::filesystem;

struct Input { enum class Kind { Thrust, Mine, Stop, Undock, Dock, Unload, Load, Sell, TravelA, TravelB, TravelGate, TravelHop, Jump, JumpHop, Directional, ProbeDeploy, ProbeScan, ProbeRecover, Archive, History, Publish, PlanRoute, QaAuto, QaStop, QaGate } kind; std::uint32_t from=0; bool online=true; std::uint32_t destination=0; RoutePolicy route_policy=RoutePolicy::Shortest; int x=0,y=0; std::int64_t amount=0; int angle=0,cone=360,range=25000; };

static bool parse(const std::string& s, Input& o) {
    // QA commands are parsed but NEVER accepted unless the native host has
    // an explicitly enabled, local testing mode. No player identity is supplied.
    if(s=="QA_STOP") {o.kind=Input::Kind::QaStop;return true;}
    if(s.size()==12 && s.rfind("QA_ROUTE|",0)==0 && s[10]=='|' &&
       s[9]>='1' && s[9]<='7' && (s[11]=='S'||s[11]=='Q')) {
        o.kind=Input::Kind::QaAuto;o.destination=unsigned(s[9]-'0');
        o.route_policy=s[11]=='Q'?RoutePolicy::PreferSecure:RoutePolicy::Shortest;
        return true;
    }
    if(s.size()==13 && s.rfind("QA_GATE|",0)==0 && s[9]=='|' && s[11]=='|' &&
       s[8]>='1' && s[8]<='7' && s[10]>='1' && s[10]<='7' &&
       (s[12]=='0'||s[12]=='1')) {
        o.kind=Input::Kind::QaGate;o.from=unsigned(s[8]-'0');
        o.destination=unsigned(s[10]-'0');o.online=s[12]=='1';return true;
    }
    if(s=="PROBE_DEPLOY") {o.kind=Input::Kind::ProbeDeploy;return true;}
    if(s=="PROBE_SCAN") {o.kind=Input::Kind::ProbeScan;return true;}
    if(s=="PROBE_RECOVER") {o.kind=Input::Kind::ProbeRecover;return true;}
    if(s=="SURVEY_ARCHIVE") {o.kind=Input::Kind::Archive;return true;}
    if(s=="SURVEY_HISTORY") {o.kind=Input::Kind::History;return true;}
    if(s.rfind("ROUTE|",0)==0) {
        // Fixed bounded 7-system region; never trust a client-supplied gate id.
        if(s.size()==9 && s.substr(0,6)=="ROUTE|" &&
           s[6]>='1' && s[6]<='7' && s[7]=='|' && (s[8]=='S'||s[8]=='Q')) {
            o.kind=Input::Kind::PlanRoute;o.destination=std::uint32_t(s[6]-'0');
            o.route_policy=s[8]=='Q'?RoutePolicy::PreferSecure:RoutePolicy::Shortest;
            return true;
        }
        return false;
    }
    if(s=="SURVEY_PUBLISH") {o.kind=Input::Kind::Publish;return true;}
    if(s.rfind("DSCAN|",0)==0) {
        const auto n=s.substr(6);const auto split=n.find('|');
        if(split==std::string::npos)return false;
        const auto a=n.substr(0,split),c=n.substr(split+1);
        if(a.empty()||a.size()>3||a.find_first_not_of("0123456789")!=std::string::npos||
           c.empty()||c.size()>3||c.find_first_not_of("0123456789")!=std::string::npos)return false;
        const int angle=std::stoi(a),cone=std::stoi(c);
        if(angle>359||(cone!=30&&cone!=90&&cone!=180&&cone!=360))return false;
        o.kind=Input::Kind::Directional;o.angle=angle;o.cone=cone;return true;
    }
    if(s=="STOP") {o.kind=Input::Kind::Stop; return true;}
    if(s=="UNDOCK") {o.kind=Input::Kind::Undock; return true;}
    if(s=="DOCK") {o.kind=Input::Kind::Dock; return true;}
    if(s=="LOAD") {o.kind=Input::Kind::Load; return true;}
    if(s=="SELL") {o.kind=Input::Kind::Sell; return true;}
    if(s=="TRAVEL_HOP") {o.kind=Input::Kind::TravelHop;return true;}
    if(s=="JUMP_HOP") {o.kind=Input::Kind::JumpHop;return true;}
    if(s=="TRAVEL_GATE") {o.kind=Input::Kind::TravelGate;return true;}
    if(s=="JUMP") {o.kind=Input::Kind::Jump;return true;}
    if(s=="TRAVEL_A") {o.kind=Input::Kind::TravelA; return true;}
    if(s=="TRAVEL_B") {o.kind=Input::Kind::TravelB; return true;}
    if(s.rfind("UNLOAD|",0)==0) {
        const auto n=s.substr(7);
        if(n.empty()||n.size()>10||n.find_first_not_of("0123456789")!=std::string::npos)return false;
        const auto amount=std::stoll(n);
        if(amount<1 || amount>1000000000LL)return false;
        o.kind=Input::Kind::Unload; o.amount=amount;return true;
    }
    if(s.rfind("MINE|",0)==0) {
        const std::string n=s.substr(5);
        if(n.empty()||n.size()>2||n.find_first_not_of("0123456789")!=std::string::npos) return false;
        const int value=std::stoi(n);
        if(value<1||value>10) return false;
        o.kind=Input::Kind::Mine;o.amount=value;return true;
    }
    if(s.rfind("THRUST|",0)==0) {
        const auto p=s.find('|',7);
        if(p==std::string::npos) return false;
        const std::string x=s.substr(7,p-7),y=s.substr(p+1);
        const auto valid=[](const std::string& v){return v=="-1"||v=="0"||v=="1";};
        if(!valid(x)||!valid(y)) return false;
        o.kind=Input::Kind::Thrust;o.x=std::stoi(x);o.y=std::stoi(y);return true;
    }
    return false;
}
// The host never blocks on stdin: this also permits normal shutdown while
// a browser keeps its local command pipe open (no detached input thread).
static void poll_commands(std::string& partial, std::deque<Input>& queue) {
    char bytes[1024];
    for(unsigned round=0;round<4;++round) {
        int got=0;
#ifdef _WIN32
        HANDLE h=GetStdHandle(STD_INPUT_HANDLE);
        if(h==INVALID_HANDLE_VALUE || GetFileType(h)!=FILE_TYPE_PIPE) break;
        DWORD available=0,used=0;
        if(!PeekNamedPipe(h,nullptr,0,nullptr,&available,nullptr)||!available)break;
        if(!ReadFile(h,bytes,(DWORD)std::min<std::size_t>(sizeof(bytes),available),&used,nullptr))break;
        got=(int)used;
#else
        struct pollfd fd{STDIN_FILENO,POLLIN,0};
        if(::poll(&fd,1,0)!=1 || !(fd.revents&POLLIN))break;
        got=(int)::read(STDIN_FILENO,bytes,sizeof(bytes));
#endif
        if(got<=0)break;
        for(int i=0;i<got;++i) {
            if(bytes[i]=='\n') {
                Input c;
                if(partial.size()<=64 && parse(partial,c) && queue.size()<64)
                    queue.push_back(c);
                else std::cerr<<"B4 INPUT REJECT syntax/capacity\n";
                partial.clear();
            } else if(partial.size()<1024)partial.push_back(bytes[i]);
        }
    }
}
static std::uint64_t fresh_stream_id() {
    std::random_device rd;
    auto x=(std::uint64_t(rd())<<32)^std::uint64_t(rd());
    x ^= std::uint64_t(Clock::now().time_since_epoch().count());
    return x?x:1;
}
static void send_frame(const ViewFrame& frame) {
    std::cout<<encode_frame(frame)<<std::flush;
    if(!std::cout) throw std::runtime_error("stream is no longer writable");
}
int main(int argc,char** argv) {
    if(argc<3||argc>4) {
        std::cerr<<"Usage: galaxy_interactive_host FRESH_SAVE_PATH TICK_MS [MAX_TICKS]\n";
        return 2;
    }
    server_dx::Progress diagnostic_progress{};
    try {
#ifdef _WIN32
        if(_setmode(_fileno(stdout),_O_BINARY)==-1) throw std::runtime_error("stdout binary failed");
#endif
        const std::string ms_s=argv[2];
        if(ms_s.empty()||ms_s.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("invalid period");
        const auto ms=std::stoul(ms_s);
        if(ms<20||ms>5000) throw std::invalid_argument("period must be 20..5000 ms");
        std::uint64_t limit=600;
        if(argc==4) {
            const std::string ticks_s=argv[3];
            if(ticks_s.empty()||ticks_s.find_first_not_of("0123456789")!=std::string::npos)
                throw std::invalid_argument("invalid max ticks");
            limit=std::stoull(ticks_s);
            if(limit<5||limit>1200) throw std::invalid_argument("max ticks must be 5..1200");
        }
        const bool qa_mode=[] {
#ifdef SG_QA_AUTOMATION
            const auto* env=std::getenv("SINGLE_GALAXY_QA_MODE");
            return env && std::string(env)=="1";
#else
            return false; // production host has no QA automation authority
#endif
        }();
        if(qa_mode) std::cerr<<"QA AUDIT: local test automation explicitly enabled; not evidence of cheating\n";
        const fs::path save=argv[1];
        if(fs::exists(save)) throw std::runtime_error("refusing to overwrite existing database");
        Simulation initial(0xB44);
        initial.add_system(2);
        const EntityId station=initial.add_station({0,0});
        const EntityId ship=initial.spawn_ship_docked(7,station,20,100);
        const EntityId rock=initial.add_asteroid({2500,700},40);
        const EntityId station_b=initial.add_station({12000,7000});
        const EntityId gate_a=initial.add_gate(1,{1500,0},2,{5000,0});
        const EntityId station_c=initial.add_station_in_system(2,{17000,0});
        const EntityId gate_b=initial.add_gate(2,{17000,0},1,{-12000,0});
        const EntityId rock_b=initial.add_asteroid_in_system(2,{18000,800},25);
        initial.configure_market(station,2,1000);
        initial.configure_market(station_b,5,1000);
        initial.configure_market(station_c,7,1000);
        // This catalog registers real stations, asteroids, sensor sites and 18
        // directed gate entities spanning 7 small original systems.
        RegionState region=build_lumen_reach(initial,station,station_c,rock,rock_b,gate_a,gate_b);
        // Starter NPC-controlled / established gates are staffed and stocked at
        // world creation. These are explicit demo fixtures, not free recurring
        // resources or autonomous NPC workers.
        for (const auto& [edge,id]:region.directed_gates) {
            if(edge.first<edge.second)
                initial.configure_gate_operations(id,region.gate(edge.second,edge.first),1000,200,2);
        }
        PersistentWorld world(save.string(),std::move(initial));
        diagnostic_progress={world.world().tick(),world.generation(),"startup"};
        // Hidden catalog sites stay server-side; ordinary SGVIEW only sends local real entities.
        const GalaxyMap& survey_map=region.map;
        SurveySensors probes(survey_map);
        probes.register_probe(7001,7);probes.register_probe(7002,7);
        SurveyArchive survey_archive(save.string());
        ServerViewBridge bridge(world.world(),fresh_stream_id(),7);
        send_frame(bridge.snapshot());
        std::string incomplete_input;
        std::deque<Input> pending;
        std::uint64_t command_id=1000;
        EntityId assist_station=0; // developer demonstration navigation assistant
        std::vector<std::uint32_t> active_route; // per-local-player advisory, never simulated authority
        RoutePolicy active_policy=RoutePolicy::Shortest;
        std::uint32_t active_destination=0;
        bool qa_autoroute=false;
        std::vector<std::tuple<std::uint32_t,std::uint32_t,bool>> qa_gate_changes;
        const auto next_leg=[&](std::uint32_t sys) -> std::uint32_t {
            const auto it=std::find(active_route.begin(),active_route.end(),sys);
            return it!=active_route.end() && (it+1)!=active_route.end()?*(it+1):0;
        };
        auto sensor_message=[&](std::uint64_t tick,const std::string& data) {
            // Bounded machine-readable server telemetry on a separate local relay channel.
            // stdout remains canonical SGVIEW v1; no client-supplied site IDs or scan truths.
            std::cerr<<"SURVEY|1|"<<tick<<"|"<<data<<'\n'<<std::flush;
        };
        const auto announce_gate_status=[&](std::uint32_t sys) {
            // Only current-system gate condition is observable. Do not reveal
            // remote infrastructure state without an authorized survey.
            for(const auto& [edge,id]:region.directed_gates) {
                if(edge.first!=sys)continue;
                const auto* gate=world.world().find(id);
                const auto* op=world.world().gate_operations(id,region.gate(edge.second,edge.first));
                if(!gate || !op)throw std::runtime_error("missing gate operations record");
                sensor_message(world.world().tick(),"GATE_STATUS|"+
                    std::to_string(edge.first)+"|"+std::to_string(edge.second)+"|"+
                    std::to_string(id)+"|"+std::to_string(op->condition)+"|"+
                    std::to_string(op->technicians)+"|"+std::to_string(op->spare_parts)+"|"+
                    std::to_string(gate->gate_online?1:0));
            }
        };
        // Every system entry begins a new sensor view; historical observations
        // remain in the private archive but are not sent as live coordinates.
        const auto announce_system=[&](std::uint32_t sys) {
            sensor_message(world.world().tick(),"SYSTEM|"+std::to_string(sys));
            const auto next=next_leg(sys);
            if(!active_route.empty())sensor_message(world.world().tick(),
                "LEG|"+std::to_string(next)+"|"+std::to_string(next?region.gate(sys,next):0));
            // Restore the player's latest historical observations on re-entry.
            // Observation ticks are retained, so the UI never treats them as live scans.
            for(const auto& previous:survey_archive.personal(7,sys)) {
                sensor_message(world.world().tick(),"ARCHIVE|"+std::to_string(previous.site)+"|"+
                    std::to_string(int(previous.kind))+"|"+
                    std::to_string(previous.estimated_position.x)+"|"+
                    std::to_string(previous.estimated_position.y)+"|"+
                    std::to_string(previous.confidence)+"|"+
                    std::to_string(previous.observed_tick)+"|"+
                    std::to_string(previous.published?1:0));
            }
            announce_gate_status(sys);
            for(const auto& known:survey_map.visible_on_entry(sys)) {
                sensor_message(world.world().tick(),"KNOWN|"+std::to_string(known.id)+"|"+
                    std::to_string(int(known.kind))+"|"+std::to_string(known.position.x)+"|"+
                    std::to_string(known.position.y));
            }
        };
        announce_system(world.world().find(ship)->system_id);
        if(qa_mode)sensor_message(world.world().tick(),"QA_MODE|1");
        const auto duration=std::chrono::milliseconds(ms);
        const auto start=Clock::now();
        for(std::uint64_t t=1;t<=limit;++t) {
            std::this_thread::sleep_until(start+duration*t);
            poll_commands(incomplete_input,pending);
            std::deque<Input> batch;
            batch.swap(pending);
            bool direct_thrust=false;
            const auto starting_system=world.world().find(ship)->system_id;
            std::vector<Input> sensor_jobs;
            for(const Input& in:batch) {
                if(in.kind==Input::Kind::QaStop || in.kind==Input::Kind::QaAuto ||
                   in.kind==Input::Kind::QaGate) {
                    if(!qa_mode) {sensor_message(world.world().tick(),"ERROR|QA_DISABLED");continue;}
                    if(in.kind==Input::Kind::QaStop) {
                        qa_autoroute=false;assist_station=0;
                        sensor_message(world.world().tick(),"QA_STOPPED|1");continue;
                    }
                    if(in.kind==Input::Kind::QaAuto) {
                        active_policy=in.route_policy;active_destination=in.destination;
                        auto plan=region.operational_routes(world.world()).route(
                            world.world().find(ship)->system_id,active_destination,active_policy);
                        active_route=plan.systems;qa_autoroute=!active_route.empty();
                        sensor_message(world.world().tick(),qa_autoroute?"QA_STARTED|"+std::to_string(active_destination):
                            "ERROR|QA_NO_ROUTE");continue;
                    }
                    const auto forward=region.gate(in.from,in.destination);
                    const auto reverse=region.gate(in.destination,in.from);
                    if(!forward || !reverse ||
                        !world.submit_testing_gate_change(forward,reverse,in.online,++command_id)) {
                        sensor_message(world.world().tick(),"ERROR|INVALID_GATE");continue;
                    }
                    qa_gate_changes.emplace_back(in.from,in.destination,in.online);
                    std::cerr<<"QA AUDIT gate "<<in.from<<"-"<<in.destination<<" request online="<<in.online<<"\n";
                    continue;
                }
                if(in.kind==Input::Kind::Directional || in.kind==Input::Kind::ProbeDeploy ||
                   in.kind==Input::Kind::ProbeScan || in.kind==Input::Kind::ProbeRecover ||
                   in.kind==Input::Kind::Archive || in.kind==Input::Kind::History ||
                   in.kind==Input::Kind::Publish || in.kind==Input::Kind::PlanRoute) {
                    sensor_jobs.push_back(in);continue;
                }
                if(in.kind==Input::Kind::TravelA || in.kind==Input::Kind::TravelB ||
                   in.kind==Input::Kind::TravelGate || in.kind==Input::Kind::TravelHop) {
                    const auto* current=world.world().find(ship);
                    if(current && current->dock_state==DockState::InSpace &&
                       current->jump_state==ShipJumpState::Ready) {
                        if(in.kind==Input::Kind::TravelHop) {
                            const auto next=next_leg(current->system_id);
                            assist_station=next?region.gate(current->system_id,next):0;
                        } else if(in.kind==Input::Kind::TravelGate) {
                            assist_station=current->system_id==1?gate_a:
                                current->system_id==2?gate_b:0;
                        } else if(in.kind==Input::Kind::TravelA) {
                            assist_station=current->system_id==1?station:region.stations.at(current->system_id);
                        } else {
                            assist_station=current->system_id==1?station_b:region.stations.at(current->system_id);
                        }
                        if(in.kind==Input::Kind::TravelHop && !assist_station)
                            sensor_message(world.world().tick(),"ERROR|NO_ROUTE_LEG");
                    } else std::cerr<<"B6.2c travel assist requires ready ship in space\n";
                    continue;
                }
                if(in.kind==Input::Kind::Thrust || in.kind==Input::Kind::Dock ||
                   in.kind==Input::Kind::Mine || in.kind==Input::Kind::Jump ||
                   in.kind==Input::Kind::JumpHop) assist_station=0;
                if(in.kind==Input::Kind::Thrust) direct_thrust=true;
                Command c{};
                c.id=++command_id;c.sequence=c.id;
                c.execute_tick=world.world().tick()+1;c.issuer=7;c.actor=ship;c.target=rock;
                if(in.kind==Input::Kind::Thrust){c.action=Action::Thrust;c.target=0;c.thrust_x=in.x;c.thrust_y=in.y;}
                else if(in.kind==Input::Kind::Mine){c.action=Action::StartMining;c.amount=in.amount;}
                else if(in.kind==Input::Kind::Undock){
                    c.action=Action::RequestUndock;
                    const auto* self=world.world().find(ship);
                    c.target=self?self->dock_station:station;
                }
                else if(in.kind==Input::Kind::Jump || in.kind==Input::Kind::JumpHop) {
                    c.action=Action::RequestJump;
                    const auto* self=world.world().find(ship);
                    const auto sys=self?self->system_id:0;
                    const auto hop=in.kind==Input::Kind::JumpHop?next_leg(sys):
                        sys==1?2U:sys==2?1U:0U;
                    c.target=region.gate(sys,hop);
                    if(!c.target) {sensor_message(world.world().tick(),"ERROR|NO_ROUTE_LEG");continue;}
                }
                else if(in.kind==Input::Kind::Dock){
                    c.action=Action::RequestDock;
                    const auto* self=world.world().find(ship);
                    // Server chooses nearest station. The client supplies no target ID.
                    const auto* a=world.world().find(station);
                    const auto* b=world.world().find(station_b);
                    const auto* remote=world.world().find(station_c);
                    if(self && self->system_id!=1)c.target=region.stations.at(self->system_id);
                    else c.target=(self && a && b &&
                        std::llabs(self->position.x-b->position.x)<
                        std::llabs(self->position.x-a->position.x))?station_b:station;
                    (void)remote;
                }
                else if(in.kind==Input::Kind::Unload || in.kind==Input::Kind::Load || in.kind==Input::Kind::Sell){
                    c.action=in.kind==Input::Kind::Unload ? Action::UnloadOre :
                        in.kind==Input::Kind::Load ? Action::LoadOre : Action::SellOre;
                    const auto* self=world.world().find(ship);
                    c.target=self?self->dock_station:station;
                    c.amount=in.kind==Input::Kind::Unload?in.amount:1000000000LL;
                }
                else {c.action=Action::StopMining;c.target=0;}
                if(!world.submit(c)) throw std::runtime_error("host-generated command rejected");
            }
            // QA automation still uses the same server-authorized undock,
            // physical steering and gate-jump actions as an ordinary player.
            if(qa_autoroute && !direct_thrust) {
                const auto* self=world.world().find(ship);
                if(self && self->system_id==active_destination) {
                    qa_autoroute=false;assist_station=0;
                    sensor_message(world.world().tick(),"QA_DONE|"+std::to_string(active_destination));
                } else if(self && self->dock_state==DockState::Docked) {
                    Command c{};c.id=++command_id;c.sequence=c.id;
                    c.execute_tick=world.world().tick()+1;c.issuer=7;c.actor=ship;
                    c.target=self->dock_station;c.action=Action::RequestUndock;
                    if(!world.submit(c))throw std::runtime_error("QA undock rejected");
                } else if(self && self->dock_state==DockState::InSpace &&
                          self->jump_state==ShipJumpState::Ready) {
                    const auto leg=next_leg(self->system_id);
                    const auto id=leg?region.gate(self->system_id,leg):0;
                    const auto* gate=world.world().find(id);
                    if(gate && gate->gate_online) {
                        const auto dx=self->position.x-gate->position.x;
                        const auto dy=self->position.y-gate->position.y;
                        if(dx>=-Simulation::kJumpActivationMm && dx<=Simulation::kJumpActivationMm &&
                           dy>=-Simulation::kJumpActivationMm && dy<=Simulation::kJumpActivationMm &&
                           dx*dx+dy*dy<=Simulation::kJumpActivationMm*Simulation::kJumpActivationMm) {
                            assist_station=0;
                            Command c{};c.id=++command_id;c.sequence=c.id;
                            c.execute_tick=world.world().tick()+1;c.issuer=7;c.actor=ship;
                            c.target=id;c.action=Action::RequestJump;
                            if(!world.submit(c))throw std::runtime_error("QA jump rejected");
                        } else assist_station=id;
                    }
                }
            }
            if(assist_station && !direct_thrust) {
                const auto* self=world.world().find(ship);
                const auto* target=world.world().find(assist_station);
                if(!self || !target || self->dock_state!=DockState::InSpace ||
                   self->system_id!=target->system_id || self->jump_state!=ShipJumpState::Ready) assist_station=0;
                else {
                    // B5.4: two-axis approach, real authorized acceleration and braking.
                    const auto steer=assist_to_station(*self,target->position);
                    if(steer.arrived) assist_station=0;
                    else {
                        Command autopilot{};
                        autopilot.id=++command_id;autopilot.sequence=autopilot.id;
                        autopilot.execute_tick=world.world().tick()+1;
                        autopilot.issuer=7;autopilot.actor=ship;
                        autopilot.action=Action::Thrust;
                        autopilot.thrust_x=steer.thrust_x;
                        autopilot.thrust_y=steer.thrust_y;
                        if(!world.submit(autopilot)) throw std::runtime_error("assist thrust rejected");
                    }
                }
            }
            diagnostic_progress.phase="world_step";
            world.step();  // SQLite commit before publishing effects to subscribers.
            diagnostic_progress={world.world().tick(),world.generation(),"post_commit"};
            for(const auto& [from,to,online]:qa_gate_changes) {
                const auto* a=world.world().find(region.gate(from,to));
                const auto* b=world.world().find(region.gate(to,from));
                if(!a || !b || a->gate_online!=online || b->gate_online!=online)
                    throw std::runtime_error("QA gate transaction did not commit");
                sensor_message(world.world().tick(),"QA_GATE|"+std::to_string(from)+"|"+
                    std::to_string(to)+"|"+std::to_string(online?1:0));
            }
            qa_gate_changes.clear();
            // Validate each planned hop against committed physical gate state.
            // When the graph changes, reroute from the ship's current system.
            if(active_destination && !active_route.empty()) {
                const auto available=region.operational_routes(world.world());
                bool broken=false;
                for(std::size_t i=1;i<active_route.size();++i) {
                    const auto a=active_route[i-1],b=active_route[i];
                    const auto* gate=world.world().find(region.gate(a,b));
                    const auto* opposite=world.world().find(region.gate(b,a));
                    if(!gate || !opposite || !gate->gate_online || !opposite->gate_online)
                        {broken=true;break;}
                }
                if(broken) {
                    const auto current=world.world().find(ship)->system_id;
                    const auto plan=available.route(current,active_destination,active_policy);
                    active_route=plan.systems;assist_station=0;
                    if(active_route.empty()) {
                        qa_autoroute=false;
                        sensor_message(world.world().tick(),"ROUTE_NONE|"+std::to_string(active_destination)+"|"+
                            std::string(active_policy==RoutePolicy::PreferSecure?"Q":"S"));
                        sensor_message(world.world().tick(),"LEG|0|0");
                    } else {
                        std::string path;
                        for(auto sys:active_route) {if(!path.empty())path+='-';path+=std::to_string(sys);}
                        sensor_message(world.world().tick(),"REROUTE|"+std::to_string(active_destination)+"|"+
                            std::string(active_policy==RoutePolicy::PreferSecure?"Q":"S")+"|"+
                            std::to_string(plan.cost)+"|"+path);
                        const auto leg=next_leg(current);
                        sensor_message(world.world().tick(),"LEG|"+std::to_string(leg)+"|"+
                            std::to_string(leg?region.gate(current,leg):0));
                    }
                }
            }
            send_frame(bridge.after_committed_step(world.world()));
            if(world.world().tick()%10==0)
                announce_gate_status(world.world().find(ship)->system_id);
            if(world.world().find(ship)->system_id!=starting_system) {
                assist_station=0;
                announce_system(world.world().find(ship)->system_id);
        if(qa_mode)sensor_message(world.world().tick(),"QA_MODE|1");
            }
            // Survey requests are processed from committed ship position and a server-owned map.
            // These records are not movement commands and cannot grant inventory or authority.
            for(const Input& job:sensor_jobs) {
                const auto tick=world.world().tick();
                const auto* self=world.world().find(ship);
                if(!self) continue;
                const auto sys=self->system_id;
                // Planning and reading personal history can happen safely while docked.
                if(job.kind==Input::Kind::PlanRoute) {
                    const auto routing=region.operational_routes(world.world());
                    const auto plan=routing.route(sys,job.destination,job.route_policy);
                    active_route=plan.systems; // advisory; no automatic movement or jump
                    active_destination=job.destination;active_policy=job.route_policy;

                    const auto policy=job.route_policy==RoutePolicy::Shortest?"S":"Q";
                    if(plan.systems.empty()) {
                        sensor_message(tick,"ROUTE_NONE|"+std::to_string(job.destination)+"|"+policy);
                        sensor_message(tick,"LEG|0|0");
                    } else {
                        std::string path;
                        for(auto id:plan.systems) {
                            if(!path.empty())path+='-';
                            path+=std::to_string(id);
                        }
                        sensor_message(tick,"ROUTE|"+std::to_string(job.destination)+"|"+policy+"|"+
                            std::to_string(plan.cost)+"|"+path);
                        const auto leg=next_leg(sys);
                        sensor_message(tick,"LEG|"+std::to_string(leg)+"|"+
                            std::to_string(leg?region.gate(sys,leg):0));
                    }
                    continue;
                }
                if(job.kind==Input::Kind::History) {
                    const auto records=survey_archive.history(7,sys,100);
                    for(const auto& f:records) sensor_message(tick,"HISTORY|"+
                        std::to_string(f.site)+"|"+std::to_string(int(f.kind))+"|"+
                        std::to_string(f.estimated_position.x)+"|"+
                        std::to_string(f.estimated_position.y)+"|"+
                        std::to_string(f.confidence)+"|"+std::to_string(f.observed_tick)+"|"+
                        std::to_string(f.published?1:0));
                    sensor_message(tick,"HISTORY_DONE|"+std::to_string(records.size()));
                    continue;
                }
                if(self->dock_state!=DockState::InSpace || self->jump_state==ShipJumpState::Jumping) {
                    sensor_message(tick,"ERROR|NOT_IN_SPACE");continue;
                }
                if(job.kind==Input::Kind::Directional) {
                    GalaxyMap dynamic_map;
                    dynamic_map.add_system(survey_map.system(sys));
                    for(const auto& [id,e]:world.world().entities()) {
                        if(id==ship || e.system_id!=sys)continue;
                        const auto kind=e.type==EntityType::Ship?SiteKind::Ship:
                            e.type==EntityType::Asteroid?SiteKind::AsteroidDeposit:
                            e.type==EntityType::Stargate?SiteKind::Structure:SiteKind::Station;
                        dynamic_map.add_site(sys,{id,kind,e.position,"Detected contact",false,true,0});
                    }
                    const SurveySensors sensors(dynamic_map);
                    const auto contacts=sensors.directional(sys,self->position,job.angle,job.cone,job.range);
                    for(const auto& c:contacts) sensor_message(tick,"D|"+std::to_string(c.site)+"|"+
                        std::to_string(int(c.kind))+"|"+std::to_string(c.bearing_deg)+"|"+
                        std::to_string(c.range_band_mm));
                    sensor_message(tick,"D_DONE|"+std::to_string(contacts.size()));
                } else if(job.kind==Input::Kind::ProbeDeploy) {
                    bool deployed=false;
                    for(const auto id:{7001ULL,7002ULL}) {
                        if(probes.deploy(id,7,sys,self->position)) {
                            sensor_message(tick,"DEPLOY|"+std::to_string(id)+"|"+
                                std::to_string(self->position.x)+"|"+std::to_string(self->position.y));
                            deployed=true;break;
                        }
                    }
                    if(!deployed)sensor_message(tick,"ERROR|NO_FREE_PROBES");
                } else if(job.kind==Input::Kind::ProbeRecover) {
                    int recovered=0;
                    for(const auto id:{7001ULL,7002ULL})if(probes.recover(id,7,sys))++recovered;
                    sensor_message(tick,"RECOVER|"+std::to_string(recovered));
                } else if(job.kind==Input::Kind::ProbeScan) {
                    const auto found=probes.probe_scan(7,sys,tick,25000,90);
                    for(const auto& f:found) {
                        survey_archive.record(f); // private by default, durable SQLite write.
                        sensor_message(tick,"P|"+std::to_string(f.site)+"|"+std::to_string(int(f.kind))+"|"+
                            std::to_string(f.estimated_position.x)+"|"+std::to_string(f.estimated_position.y)+"|"+
                            std::to_string(f.confidence));
                    }
                    sensor_message(tick,"P_DONE|"+std::to_string(found.size()));
                } else if(job.kind==Input::Kind::Publish) {
                    int released=0;
                    for(auto entry:survey_archive.personal(7,sys)) {
                        if(entry.published)continue;
                        entry.published=true;
                        if(survey_archive.record(entry))++released;
                    }
                    sensor_message(tick,"PUBLISH|"+std::to_string(released));
                } else if(job.kind==Input::Kind::Archive) {
                    const auto entries=survey_archive.personal(7,sys);
                    for(const auto& f:entries) sensor_message(tick,"ARCHIVE|"+
                        std::to_string(f.site)+"|"+std::to_string(int(f.kind))+"|"+
                        std::to_string(f.estimated_position.x)+"|"+std::to_string(f.estimated_position.y)+"|"+
                        std::to_string(f.confidence)+"|"+std::to_string(f.observed_tick)+"|"+
                        std::to_string(f.published?1:0));
                    sensor_message(tick,"ARCHIVE_DONE|"+std::to_string(entries.size()));
                }
            }
        }
        std::cerr<<"B5.3 INTERACTIVE PASS ticks="<<limit<<" total_ore="<<world.world().total_ore()<<'\n';
        return 0;
    } catch(const std::exception& ex) {
        server_dx::record_exception(argv[1],"interactive_host",ex.what(),diagnostic_progress);
        std::cerr<<"B5.3 INTERACTIVE FAIL "<<ex.what()<<'\n';return 1;
    }catch(...){
        server_dx::record_exception(argv[1],"interactive_host","unknown native exception",diagnostic_progress);
        std::cerr<<"B5.3 INTERACTIVE FAIL unknown exception\n";return 1;
    }
}
