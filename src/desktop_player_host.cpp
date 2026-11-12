#include "galaxy/NativeWriterLease.hpp"
#include "galaxy/NativeWindowsUtf8Argv.hpp"
#include "galaxy/ServerBreadcrumb.hpp"
// FP3D-02F: no-browser, no-Python native desktop command ingress.
// Stdio is an OS-local child pipe; host owns the fixed single-player authority.
// Every accepted MOVE commits world STEP, receipt, pose in one original transaction.
#include "galaxy/NativeInteriorMovement.hpp"
#include "galaxy/SpaceMiningContact.hpp"
#include "galaxy/SpaceMiningSurvey.hpp"
#include "galaxy/ColonyOnboarding.hpp"
#include "galaxy/PlanetaryContinuousSurface.hpp"
#include "galaxy/PlanetarySurfaceMovement.hpp"
#include "galaxy/PlanetaryBodyGravity.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <atomic>
#include <mutex>
#include <thread>
#include "galaxy/PlanetaryAtmosphere.hpp"
#include "galaxy/Stage6WalkingPatch.hpp"
#include <fstream>
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
// Read-only navigation: the nearest genuine station sharing this original
// ship's coordinate frame. Unlike docking admission, this observation is
// available beyond the 2.5m dock envelope. No station/ship control is granted.
void send_original_station_nav(std::uint64_t session,std::uint64_t req,
                               const PersistentWorld& world,EntityId ship_id) {
    const auto& original=world.world();
    const auto* ship=original.find(ship_id);
    if(!ship || ship->type!=EntityType::Ship || ship->owner_id!=kLocalOwner)
        throw std::runtime_error("native navigation missing original owned ship");
    constexpr std::int64_t cap=1000000000LL; // bounded 1000 km, never a synthetic waypoint
    constexpr std::size_t max_scan=8192;
    std::int64_t best_squared=cap*cap+1;
    EntityId nearest=0;
    std::int64_t best_dx=0,best_dy=0,best_dz=0;
    std::size_t scanned=0;
    for(const auto station_id:original.entity_ids_in_system(ship->system_id)) {
        if(++scanned>max_scan)break;
        const auto* station=original.find(station_id);
        if(!station || station->type!=EntityType::Station ||
           !original.same_local_frame(ship_id,station_id) ||
           ship->physical_galactic_authoritative!=station->physical_galactic_authoritative)
            continue;
        std::optional<std::int64_t> dx,dy,dz;
        if(ship->physical_galactic_authoritative){
            dx=space_mining_view::nearby_axis_delta(ship->physical_galactic.x,
                station->physical_galactic.x,cap);
            dy=space_mining_view::nearby_axis_delta(ship->physical_galactic.y,
                station->physical_galactic.y,cap);
            dz=space_mining_view::nearby_axis_delta(ship->physical_galactic.z,
                station->physical_galactic.z,cap);
        } else {
            const auto x=station->position.x-ship->position.x;
            const auto y=station->position.y-ship->position.y;
            const auto z=station->position_z_mm-ship->position_z_mm;
            if(x>=-cap&&x<=cap&&y>=-cap&&y<=cap&&z>=-cap&&z<=cap){
                dx=x;dy=y;dz=z;
            }
        }
        if(!dx||!dy||!dz)continue;
        const auto squared=(*dx)*(*dx)+(*dy)*(*dy)+(*dz)*(*dz);
        if(squared>cap*cap || squared>=best_squared)continue;
        nearest=station_id;
        best_squared=squared;
        best_dx=*dx;best_dy=*dy;best_dz=*dz;
    }
    if(nearest){
        std::cout<<"NAV|1|"<<session<<'|'<<req<<'|'<<ship_id<<'|'
                 <<original.tick()<<'|'<<world.generation()<<'|'<<nearest<<'|'
                 <<best_dx<<'|'<<best_dy<<'|'<<best_dz<<'\n'<<std::flush;
    } else {
        std::cout<<"NAVNONE|1|"<<session<<'|'<<req<<'|'<<ship_id<<'|'
                 <<original.tick()<<'|'<<world.generation()<<'\n'<<std::flush;
    }
    if(!std::cout)throw std::runtime_error("original station navigation pipe closed");
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

//// S6-F5-C ingress reconciliation: this is a distinct mode of the EXISTING
//// desktop executable and the SAME PersistentWorld SQLite writer, not another
//// planet, NPC, Godot physics authority, or new game-save genesis.
//// It intentionally refuses nonexistent or not-yet-landed originals.

// Source-derived, bounded 3x3 installed Godot tile stream. Published ONLY by the same
// authenticated, original-world owning process, never from a Godot vertex.
// Atomic replace keeps renderer from observing partial JSON.
static void publish_original_walking_patch(const std::string& save,
        const PersistentWorld& world,PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t session){
    const auto packet=stage6::original_walking_tiles(
        world,owner,ship,key,session);
    const fs::path target=fs::u8path(save+".stage6.walkpatch.json");
    if(!packet){
        std::error_code ec;fs::remove(target,ec);
        return; // unavailable original survey means no synthetic floor
    }
    const fs::path staged=fs::u8path(save+".stage6.walkpatch."+
        std::to_string(session)+".tmp");
    if(fs::is_symlink(staged))
        throw std::runtime_error("unsafe original terrain staging path");
    {
        std::ofstream file(staged,std::ios::binary|std::ios::trunc);
        if(!file)throw std::runtime_error("native walking patch staging refused");
        file<<*packet;file.flush();
        if(!file)throw std::runtime_error("native walking patch write failed");
    }
#ifdef _WIN32
    if(!::MoveFileExW(staged.c_str(),target.c_str(),
          MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("native walking patch atomic publish failed");
#else
    fs::rename(staged,target);
#endif
}

static int run_planet_host(const std::string& save_utf8){
    namespace grid=planetary::continuous_surface;
    namespace surface=planetary::surface_motion;
    try{
        const fs::path save=fs::u8path(save_utf8);
        if(save_utf8.empty()||save_utf8.size()>1024||
           !fs::exists(save)||!fs::is_regular_file(save))
            throw std::invalid_argument("planetary mode requires an existing original SQLite world");
        NativeWriterLease custody(save);
        // Constructor ignores this seed for an existing verified save.
        // No room, colony, generated body, inventory or player is created.
        PersistentWorld world(save_utf8,Simulation(0xC021));
        const auto landed=[&]()->const PlayerPerson*{
            const auto it=world.world().player_people().find(kLocalOwner);
            if(it==world.world().player_people().end())return nullptr;
            const auto& p=it->second;
            if(p.state!=PlayerPersonState::AtPlanetarySurface ||
               !p.home_ship || p.physical_ship || p.physical_station ||
               !p.planet_body || p.planet_revision!=1 ||
               p.planet_latitude>=36 || p.planet_longitude>=72 ||
               !p.planet_descent_receipt)return nullptr;
            const PlanetarySiteKey key{p.planet_body,p.planet_latitude,
                                       p.planet_longitude,p.planet_revision};
            const auto it_site=world.world().planetary_sites().find(key);
            const auto it_shuttle=world.world().planetary_shuttles().find(key);
            const auto* ship=world.world().find(p.home_ship);
            if(it_site==world.world().planetary_sites().end() ||
               it_shuttle==world.world().planetary_shuttles().end() ||
               !ship || ship->type!=EntityType::Ship ||
               ship->owner_id!=kLocalOwner ||
               it_site->second.owner!=kLocalOwner ||
               it_site->second.source_ship!=p.home_ship ||
               !it_site->second.surface_location ||
               it_site->second.spaceport_phase!=3 ||
               it_shuttle->second.owner!=kLocalOwner ||
               it_shuttle->second.source_ship!=p.home_ship ||
               it_shuttle->second.flight_phase!=2 ||
               it_shuttle->second.descent_receipt!=p.planet_descent_receipt)
                return nullptr;
            return &p;
        };
        if(!landed())throw std::runtime_error(
            "original player not verified ashore; no native planet session");
        // This is a deterministic source-only manifest migration under
        // the already acquired original OS writer lease and SQLite journal.
        // Not an automatic landing, equipment grant or oxygen supply.
        world.ensure_planetary_atmosphere_manifest();
        const auto session=new_epoch();
        const auto receipt_base=(new_epoch()&((std::uint64_t{1}<<60)-1))
              & ~((std::uint64_t{1}<<20)-1);
        std::uint64_t last_request=0,attempts=0;
        auto last_step=std::chrono::steady_clock::now()-std::chrono::milliseconds(100);
        const auto emit=[&](std::uint64_t request,std::uint64_t command_receipt){
            const auto* p=landed();
            if(!p)throw std::runtime_error("planetary player custody lost");
            const auto& pose=p->surface_pose;
            auto latitude=pose.latitude_pdeg,longitude=pose.longitude_pdeg;
            if(!pose.last_move_receipt){
                const auto center=grid::cell_center(
                    {p->planet_latitude,p->planet_longitude});
                if(!center)throw std::runtime_error("invalid native planetary home cell");
                latitude=center->latitude_pdeg;
                longitude=center->longitude_pdeg;
            }
            const auto& survival=p->surface_survival;
            std::int64_t body_g=0;
            const auto body=world.world().celestial_navigation_anchor(
                kLocalOwner,p->home_ship,p->planet_body);
            if(body){
                const auto value=planetary::gravity::surface_gravity_mm_s2(
                    body->physical_radius_mm,body->physical_mass_1e18_kg);
                if(value)body_g=*value;
            }
            const auto projected_y=(!pose.last_move_receipt && survival.initialized)?
                survival.radial_height_mm:pose.y_mm;
            const auto body_air=body?planetary::air::State{
                body->atmosphere_pressure_pa,
                body->atmosphere_oxygen_permille,
                body->atmosphere_toxic}:planetary::air::State{};
            const bool atmosphere_known=
                world.world().original_surface_atmosphere_enabled() &&
                body!=nullptr;
            const bool breathable=atmosphere_known &&
                planetary::air::breathable(body_air);
            std::cout<<"PFRAME|1|"<<session<<'|'<<request<<'|'<<p->home_ship
                     <<'|'<<p->planet_body<<'|'<<p->planet_latitude
                     <<'|'<<p->planet_longitude<<'|'<<pose.x_mm
                     <<'|'<<projected_y<<'|'<<pose.z_mm
                     <<'|'<<latitude<<'|'<<longitude
                     <<'|'<<pose.last_move_tick<<'|'<<pose.last_move_receipt
                     <<'|'<<world.world().tick()<<'|'<<world.generation()
                     <<'|'<<world.world().planetary_sites().at(
                        PlanetarySiteKey{p->planet_body,p->planet_latitude,
                           p->planet_longitude,p->planet_revision}).surface_location
                     <<'|'<<body_g<<'|'<<survival.radial_speed_mm_s
                     <<'|'<<unsigned(survival.grounded)
                     <<'|'<<survival.oxygen_kg<<'|'<<survival.exhaled_co2_kg
                     <<'|'<<survival.thermal_exposure_ms
                     <<'|'<<survival.unverified_air_exposure_ms
                     <<'|'<<survival.injury_points
                     <<'|'<<survival.last_vertical_tick
                     <<'|'<<command_receipt
                     <<'|'<<(atmosphere_known?body_air.pressure_pa:0)
                     <<'|'<<(atmosphere_known?body_air.oxygen_permille:0)
                     <<'|'<<unsigned(atmosphere_known && body_air.toxic)
                     <<'|'<<unsigned(atmosphere_known)
                     <<'|'<<unsigned(breathable)
                     <<'|'<<survival.suit_embodied_kg
                     <<'|'<<survival.suit_integrity
                     <<'|'<<survival.suit_charge
                     <<'|'<<unsigned(survival.suit_equipped)
                     <<'|'<<survival.hypoxia_exposure_ms
                     <<'|'<<survival.medical_treatments
                     <<'|'<<survival.last_medical_tick
                     <<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Godot planet pipe closed");
        };
        emit(0,0);
        const auto source_person=landed();
        if(!source_person)throw std::runtime_error("original landed player lost");
        const PlanetarySiteKey walking_site{
            source_person->planet_body,source_person->planet_latitude,
            source_person->planet_longitude,source_person->planet_revision};
        std::int64_t published_x=source_person->surface_pose.x_mm;
        std::int64_t published_z=source_person->surface_pose.z_mm;
        publish_original_walking_patch(save_utf8,world,kLocalOwner,
            source_person->home_ship,walking_site,session);
        // Native host has one mutex-protected original PersistentWorld.
        // Autonomous world ticks continue independently of blocked stdin
        // and of Godot FPS/input; no detached second SQLite writer exists.
        std::mutex original_writer;
        std::atomic<bool> running{true};
        std::thread ticker([&](){
            auto deadline=std::chrono::steady_clock::now()+
                std::chrono::milliseconds(100);
            while(running.load()){
                std::this_thread::sleep_until(deadline);
                if(!running.load())break;
                try{
                    std::lock_guard<std::mutex> guarded(original_writer);
                    if(!landed())throw std::runtime_error(
                        "original owner left landed custody");
                    world.step(); // one canonical journaled tick
                    emit(0,0); // read-only unsolicited host observation
                }catch(const std::exception& e){
                    std::cerr<<"Original surface ticker stopped: "
                             <<e.what()<<'\n';
                    running.store(false);
                    break;
                }
                deadline+=std::chrono::milliseconds(100);
                const auto now=std::chrono::steady_clock::now();
                if(deadline<now)deadline=now+std::chrono::milliseconds(100);
            }
        });
        struct JoinTicker {
            std::atomic<bool>& running;
            std::thread& worker;
            ~JoinTicker(){running.store(false);if(worker.joinable())worker.join();}
        } joiner{running,ticker};
        std::string line;
        while(running.load() && std::getline(std::cin,line)){
            if(line=="QUIT")break;
            std::lock_guard<std::mutex> guarded(original_writer);
            if(++attempts>100000){deny(0,"planet-session-budget");return 3;}
            if(line.size()>kMaxLine){deny(0,"planet-command-oversize");continue;}
            const auto f=split(line);
            std::uint64_t supplied=0,req=0;
            std::int64_t dx=0,dz=0;
            const bool walking=f.size()==5 && f[0]=="PMOVE";
            const bool vertical=f.size()==4 && f[0]=="PSTEP" &&
                (f[3]=="JUMP" || f[3]=="WAIT");
            const bool oxygen=f.size()==4 && f[0]=="POXY" &&
                (f[3]=="CHARGE" || f[3]=="RETURN");
            const bool suit=f.size()==4 && f[0]=="PSUIT" &&
                (f[3]=="EQUIP" || f[3]=="UNEQUIP" || f[3]=="CHARGE");
            const bool care=f.size()==4 && f[0]=="PMED" &&
                f[3]=="TREAT";
            if((!walking&&!vertical&&!oxygen&&!suit&&!care) ||
               !number(f[1],supplied) || !number(f[2],req) ||
               (walking&&(!number(f[3],dx)||!number(f[4],dz)))){
                deny(0,"planet-command-malformed");continue;
            }
            if(supplied!=session){deny(req,"planet-session-mismatch");continue;}
            if(!req || req<=last_request || req>=1000000){
                deny(req,"planet-request-replayed");continue;
            }
            last_request=req;
            if(walking && !surface::within_step(dx,dz,true)){
                deny(req,"planet-step-limit");continue;
            }
            // Native timing is separate from Godot frame rate. A fast or
            // malicious client cannot accelerate original ecology/world STEP
            // by flooding physically valid 300 mm movement requests.
            const auto now=std::chrono::steady_clock::now();
            if(now-last_step<std::chrono::milliseconds(50)){
                deny(req,"planet-rate-limit");continue;
            }
            last_step=now;
            const auto* before=landed();
            if(!before){deny(req,"planet-custody-lost");return 3;}
            const PlanetarySiteKey key{before->planet_body,before->planet_latitude,
                before->planet_longitude,before->planet_revision};
            const auto ship=before->home_ship;
            // An original world tick is required between ground steps.
            // No host-side local interpolation or physics may advance the person.
            const auto proof=receipt_base+req;
            bool accepted=false;
            if(walking){
                // The supervised autonomous ticker, not user key repetition,
                // advances native time. A same-tick second move is denied
                // by the existing original surface rule.
                accepted=world.planetary_surface_move(
                    kLocalOwner,ship,key,0,dx,dz,proof);
            }else if(vertical){
                // One original STEP, vertical update and authorization receipt
                // in the same transaction, no Godot-supplied impulse.
                accepted=world.planetary_surface_vertical_action(
                    kLocalOwner,ship,key,f[3]=="JUMP",proof);
            }else if(oxygen){
                accepted=world.planetary_surface_oxygen_action(
                    kLocalOwner,ship,key,f[3]=="RETURN",proof);
            }else if(suit){
                const auto action=f[3]=="EQUIP"?
                    Simulation::OriginalSuitAction::Equip:
                    f[3]=="UNEQUIP"?
                    Simulation::OriginalSuitAction::Unequip:
                    Simulation::OriginalSuitAction::Charge;
                accepted=world.planetary_surface_suit_action(
                    kLocalOwner,ship,key,action,proof);
            }else if(care){
                accepted=world.planetary_surface_treatment(
                    kLocalOwner,ship,key,proof);
            }
            if(!accepted){
                deny(req,"planet-native-action-refused");continue;
            }
            if(walking){
                const auto* current=landed();
                if(current){
                    const auto x=current->surface_pose.x_mm;
                    const auto z=current->surface_pose.z_mm;
                    if(std::llabs(x-published_x)>=16000 ||
                       std::llabs(z-published_z)>=16000){
                        publish_original_walking_patch(save_utf8,world,
                            kLocalOwner,ship,key,session);
                        published_x=x;published_z=z;
                    }
                }
            }
            emit(req,proof); // only AFTER the verified SQLite atomic commit
        }
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Original planetary player host refused: "<<e.what()<<'\n';
        return 3;
    }
}
namespace galaxy::installed_planet_transfer { int run(const std::string&); }
static int run_host(int argc,char** argv){
    if(argc==3 && std::string(argv[2])=="--planet")return run_planet_host(argv[1]);
    if(argc==3 && std::string(argv[2])=="--planet-transfer")
        return galaxy::installed_planet_transfer::run(argv[1]);
    if(argc!=2){std::cerr<<"Usage: galaxy_desktop_player_host SQLITE_PATH [--planet|--planet-transfer]\n";return 2;}
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
        // A genuinely NEW standalone cockpit fixture has native vertical
        // propulsion. Existing SQLite worlds load their own original format;
        // they are never silently upgraded or reset by the desktop host.
        seed.enable_native_3d_physics();
        // Preserve source-lot authority; future mining tool manufacturing
        // is explicitly deferred and never installed at game genesis.
        seed.enable_station_ore_provenance();
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
        std::cout<<colony_onboarding::source_frame(world,kLocalOwner,frame.ship,session,0)<<std::flush;
        send_rock_contact(session,0,world,frame.ship);
        send_original_station_nav(session,0,world,frame.ship);
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
            const bool mining=fields.size()==5&&fields[0]=="MINE";
            const bool fitting=fields.size()==5&&(fields[0]=="FIT"||fields[0]=="UNFIT");
            const bool fabricating=fields.size()==5&&fields[0]=="FAB";
            const bool powering=fields.size()==5&&fields[0]=="POWER";
            const bool surveying=fields.size()==5&&fields[0]=="SURVEY";
            // DOCK-09: one explicitly selected original ship construction
            // or material operation. No arbitrary recipes or world position.
            const bool colony_build=fields.size()==5&&fields[0]=="ONBUILD";
            if(!walking&&!seating&&!flying&&!mining&&!fitting&&!fabricating&&!powering&&!surveying&&!colony_build){
                deny(0,"malformed");continue;
            }
            std::uint64_t supplied_session=0,req=0,expected=0,desired=0;
            std::int64_t dx=0,dz=0;
            if(!number(fields[1],supplied_session)||!number(fields[2],req)||
               !number(fields[3],expected)||
               (walking&&(!number(fields[4],dx)||!number(fields[5],dz)))||
               ((seating||fitting||fabricating||powering||mining||surveying||colony_build)&&
                (!number(fields[4],desired)||
                 (seating&&desired>1)||
                 (fitting&&desired!=1)||
                 (fabricating&&desired!=1)||
                 (powering&&desired>1)||
                 (surveying&&desired!=1)||
                 (colony_build&&(desired<1||desired>4))))){
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
                std::cout<<colony_onboarding::source_frame(
                    world,kLocalOwner,previous.ship,session,req)<<std::flush;
                send_rock_contact(session,req,world,previous.ship);
                send_original_station_nav(session,req,world,previous.ship);
                continue;
            }
            if(surveying){
                if(!previous.seated || expected!=previous.seat_sequence ||
                   !world.world().find(previous.ship) ||
                   req>10000){
                    deny(req,"survey-requires-verified-original-pilot");continue;
                }
                // PLAY-11: the 50m geological survey requires an actual
                // installed, effective native survey scanner. Never infer
                // sensor capability from the 8.5km Godot draw distance.
                const auto* survey_ship=world.world().find(previous.ship);
                if(!survey_ship ||
                   !ship_sensors::powered_survey_scanner(world.world(),*survey_ship)){
                    deny(req,"no-powered-original-survey-scanner");continue;
                }
                const auto rows=mining_survey::nearby_original_asteroids(
                    world.world(),kLocalOwner,previous.ship);
                // One bounded observation across a stable source generation;
                // not a mining authorization, world step or hidden ore reward.
                const auto tick=world.world().tick();
                const auto generation=world.generation();
                const bool truncated=!rows.empty() && rows.front().scan_truncated;
                std::cout<<"SHEAD|1|"<<session<<'|'<<req<<'|'<<previous.ship<<'|'
                         <<tick<<'|'<<generation<<'|'<<rows.size()<<'|'
                         <<(truncated?1:0)<<'\n';
                for(const auto& row:rows){
                    std::cout<<"SROW|1|"<<session<<'|'<<req<<'|'<<previous.ship<<'|'
                             <<tick<<'|'<<generation<<'|'<<row.rock<<'|'
                             <<row.x_mm<<'|'<<row.y_mm<<'|'<<row.z_mm<<'|'
                             <<row.radius_mm<<'|'<<row.ore_tonnes<<'|'
                             <<unsigned(row.taxonomy)<<'|'
                             <<(row.geological_detail?1:0)<<'\n';
                }
                std::cout<<"SEND|1|"<<session<<'|'<<req<<'|'<<previous.ship<<'|'
                         <<tick<<'|'<<generation<<'\n'<<std::flush;
                if(!std::cout)throw std::runtime_error("native survey pipe closed");
                last_move=now;
                continue;
            }
            if(mining||fitting||fabricating||powering||colony_build){
                if(!previous.seated || expected!=previous.seat_sequence ||
                   !world.world().player_people().count(kLocalOwner)){
                    deny(req,"not-seated-or-stale-seat");continue;
                }
                if(req>10000){deny(req,"equipment-request-budget");continue;}
                const auto* ship_entity=world.world().find(previous.ship);
                if(!ship_entity || ship_entity->type!=EntityType::Ship ||
                   ship_entity->owner_id!=kLocalOwner){
                    deny(req,"missing-owned-ship");continue;
                }
                Command c{};
                c.id=flight_base|req;
                c.sequence=c.id;
                c.execute_tick=world.world().tick()+1;
                c.issuer=kLocalOwner;c.actor=previous.ship;
                if(colony_build){
                    // Original-world proposal checks source mass and physical
                    // hull; the kernel remains the sole producer of receipts.
                    const auto original=colony_onboarding::propose(
                        world.world(),kLocalOwner,previous.ship,c.id,
                        static_cast<colony_onboarding::Operation>(desired));
                    if(!original){
                        deny(req,"physical-colony-construction-prerequisite");continue;
                    }
                    c=*original;
                }else if(mining){
                    // Client-provided rock is only a selector. The ORIGINAL
                    // physical ship/rock proximity sensor independently
                    // identifies it before the simulation checks hull range.
                    const auto contact=space_mining_view::nearest_original_asteroid(
                        world.world(),kLocalOwner,previous.ship);
                    if(!contact || contact->rock!=desired) {
                        deny(req,"no-matching-original-rock-contact");continue;
                    }
                    // The authoritative original Simulation::mine_status
                    // performs ALL actual eligibility, position, inventory
                    // and (in opt-in v126 only) fitted-tool power checks.
                    // A rendering-side advisory is never a second miner.
                    c.action=Action::Mine;c.target=desired;c.amount=1;
                }else if(fitting){
                    c.action=Action::ConfigureMiningTool;
                    c.amount=fields[0]=="FIT"?1:-1;
                }else if(fabricating){
                    // The original action consumes 2 finite refined tonnes;
                    // a generic cockpit cannot fabricate tools in flight.
                    if(ship_entity->dock_state!=DockState::Docked){
                        deny(req,"fabrication-requires-docked-ship");continue;
                    }
                    c.action=Action::FabricateModule;c.amount=1;
                }else{
                    // Power request cannot choose an arbitrary rack: the
                    // original installed tool determines its actual slot.
                    std::uint8_t slot=2;
                    for(std::uint8_t i=0;i<2;++i)
                        if(ship_entity->fitting.hardpoints[i]==
                           space_mining_view::kMiningToolHardpoint)slot=i;
                    if(slot>=2){deny(req,"no-installed-original-drill");continue;}
                    c.action=Action::SetHardpointPower;
                    c.amount=static_cast<std::int64_t>(slot)*2+
                             (desired==1?1:0);
                }
                if(!world.submit(c)){deny(req,"original-equipment-command-not-authorized");continue;}
                const auto earlier=world.world().events().size();
                world.step();
                const auto& events=world.world().events();
                if(events.size()<=earlier || events.back().command_id!=c.id)
                    throw std::runtime_error("original equipment STEP lacks exact immutable command receipt");
                const auto outcome=events.back().result;
                galaxy::server_dx::committed_breadcrumb(world.world().tick(),world.generation());
                last_move=now;
                send_ship(session,req,c.id,outcome,person.flight_state(kLocalOwner));
                std::cout<<colony_onboarding::source_frame(
                    world,kLocalOwner,previous.ship,session,req)<<std::flush;
                send_rock_contact(session,req,world,previous.ship);
                send_original_station_nav(session,req,world,previous.ship);
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
