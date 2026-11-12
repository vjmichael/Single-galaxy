// S6-F5-F: installed Godot -> original C++ shuttle transfer mode.
// This code is linked into the existing desktop player host. It never creates
// a new world, player, shuttle, site or SQLite writer, and cannot be used on
// an absent save. All flight, transport, inventory and custody mutations call
// the original PersistentWorld transaction methods.
#include "galaxy/NativeWriterLease.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/Simulation.hpp"
#include "galaxy/Stage6RecoveryDecision.hpp"
#include "galaxy/Stage6ConsolidatedAdmission.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace galaxy::installed_planet_transfer {
namespace {
namespace fs = std::filesystem;
constexpr PlayerId owner=7;
constexpr std::size_t max_sites=16;
std::uint64_t epoch() {
    std::random_device rd;
    const auto n=((std::uint64_t(rd())<<32)|std::uint64_t(rd()))&
        0x3fffffffffffffffULL;
    return n?n:1;
}
std::vector<std::string_view> fields(const std::string& line) {
    std::vector<std::string_view> f;
    std::size_t start=0;
    while(true) {
        const auto next=line.find('|',start);
        f.emplace_back(line.data()+start,
            (next==std::string::npos?line.size():next)-start);
        if(next==std::string::npos||f.size()>7)break;
        start=next+1;
    }
    return f;
}
bool number(std::string_view text,std::uint64_t& value) {
    if(text.empty()||text.size()>20||text.front()=='+')return false;
    const auto [last,err]=std::from_chars(text.data(),text.data()+text.size(),value);
    return err==std::errc{}&&last==text.data()+text.size();
}
const PlayerPerson& person(const PersistentWorld& world) {
    const auto it=world.world().player_people().find(owner);
    if(it==world.world().player_people().end()||!it->second.home_ship)
        throw std::runtime_error("original player person absent; no transfer");
    const auto& p=it->second;
    const auto* ship=world.world().find(p.home_ship);
    if(!ship || ship->owner_id!=owner || ship->type!=EntityType::Ship)
        throw std::runtime_error("original parent ship custody absent");
    if(p.state!=PlayerPersonState::AboardOwnShip &&
       p.state!=PlayerPersonState::InPlanetaryShuttle &&
       p.state!=PlayerPersonState::PlanetaryTransit)
        throw std::runtime_error("player neither aboard original ship nor shuttle");
    return p;
}
struct Option {
    PlanetarySiteKey key{};
    EntityId surface=0;
};
std::vector<Option> sites(const PersistentWorld& world,const PlayerPerson& p) {
    std::vector<Option> options;
    for(const auto& [key,shuttle]:world.world().planetary_shuttles()) {
        if(shuttle.owner!=owner || shuttle.source_ship!=p.home_ship ||
           !shuttle.pilot_npc)continue;
        const auto it=world.world().planetary_sites().find(key);
        if(it==world.world().planetary_sites().end())continue;
        const auto& site=it->second;
        if(site.owner!=owner || site.source_ship!=p.home_ship ||
           site.spaceport_phase!=3 || !site.surface_location)continue;
        if(p.state!=PlayerPersonState::AboardOwnShip &&
           (p.planet_body!=std::get<0>(key) || p.planet_latitude!=std::get<1>(key) ||
            p.planet_longitude!=std::get<2>(key) || p.planet_revision!=std::get<3>(key)))
            continue;
        options.push_back({key,site.surface_location});
        if(options.size()>max_sites)
            throw std::runtime_error("more than 16 real owned shuttle sites: narrow source selection");
    }
    return options;
}
void denial(std::uint64_t request,const char* reason) {
    std::cout<<"DENY|1|"<<request<<'|'<<reason<<'\n'<<std::flush;
}
} // namespace

// S6-F6: short-lived native recovery preflight. This acquires the SAME
// non-blocking OS writer lease before reading an existing SQLite world.
// The lock is released on process exit BEFORE Godot launches a writer.
// It never advances time, mutates stock, creates a world or guesses a role.
int probe(const std::string& save_utf8) {
    try {
        const auto save=fs::u8path(save_utf8);
        if(save_utf8.empty() || save_utf8.size()>1024 ||
           !fs::is_regular_file(save))
            throw std::invalid_argument("recovery probe requires existing SQLite world");
        NativeWriterLease custody(save);
        PersistentWorld world(save_utf8,Simulation(0xC021));
        const auto role=stage6::recovery::inspect(world.world(),owner);
        if(!role || role->role==stage6::recovery::Role::Invalid)
            throw std::runtime_error("saved player transport custody inconsistent");
        // The resulting role is a *read-only decision*, NOT a handoff grant.
        // The next writer reopens/revalidates every original source record.
        std::cout<<"RFRAME|1|"<<unsigned(role->role)<<'|'
            <<unsigned(role->person_state)<<'|'<<role->ship<<'|'
            <<role->body<<'|'<<role->latitude<<'|'<<role->longitude<<'|'
            <<role->surface<<'|'<<unsigned(role->shuttle_phase)<<'|'
            <<world.world().tick()<<'|'<<world.generation()<<'\n'
            <<std::flush;
        if(!std::cout)throw std::runtime_error("recovery protocol pipe broken");
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Original-world recovery preflight refused: "<<e.what()<<'\n';
        return 3;
    }
}

int run(const std::string& save_utf8) {
    try {
        const fs::path save=fs::u8path(save_utf8);
        if(save_utf8.empty()||save_utf8.size()>1024 ||
           !fs::is_regular_file(save))
            throw std::invalid_argument("transfer requires existing original SQLite world");
        NativeWriterLease custody(save);
        PersistentWorld world(save_utf8,Simulation(0xC021));
        // The recovery probe intentionally leaves source unchanged.
        // Only the newly admitted protected writer upgrades v127-v135.
        (void)stage6::consolidated::admit(world);
        const auto& initial=person(world);
        const auto options=sites(world,initial);
        if(options.empty())
            throw std::runtime_error("no original built, crewed and owner-matched planetary shuttle site");
        std::size_t selected=0;
        if(initial.state!=PlayerPersonState::AboardOwnShip) {
            // Source-custody site is forced by the saved player, never selected
            // from a UI position or guessed during crash recovery.
            selected=0;
        }
        const auto session=epoch();
        // Independent high receipt namespace; transport changes are original
        // single-use SQLite-validated human actions, not client authority.
        const auto base=(epoch()&((std::uint64_t{1}<<60)-1))&
            ~((std::uint64_t{1}<<21)-1);
        std::uint64_t last_request=0,attempts=0;
        std::mutex writer;
        std::atomic<bool> running{true};
        const auto emit=[&](std::uint64_t request,std::uint64_t receipt) {
            const auto& p=person(world);
            const auto& option=options.at(selected);
            const auto& key=option.key;
            const auto sh=world.world().planetary_shuttles().find(key);
            const auto site=world.world().planetary_sites().find(key);
            if(sh==world.world().planetary_shuttles().end() ||
               site==world.world().planetary_sites().end() ||
               sh->second.owner!=owner || sh->second.source_ship!=p.home_ship ||
               site->second.owner!=owner || site->second.source_ship!=p.home_ship ||
               site->second.spaceport_phase!=3 ||
               site->second.surface_location!=option.surface)
                throw std::runtime_error("selected original shuttle site custody changed");
            if(p.state!=PlayerPersonState::AboardOwnShip &&
                (p.planet_body!=std::get<0>(key) || p.planet_latitude!=std::get<1>(key) ||
                 p.planet_longitude!=std::get<2>(key) ||
                 p.planet_revision!=std::get<3>(key)))
                throw std::runtime_error("player left selected original shuttle");
            std::cout<<"TFRAME|1|"<<session<<'|'<<request<<'|'<<p.home_ship<<'|'
                <<unsigned(p.state)<<'|'<<std::get<0>(key)<<'|'<<std::get<1>(key)<<'|'
                <<std::get<2>(key)<<'|'<<unsigned(sh->second.flight_phase)<<'|'
                <<world.world().tick()<<'|'<<world.generation()<<'|'
                <<option.surface<<'|'<<p.planet_flight_receipt<<'|'
                <<p.planet_descent_receipt<<'|'<<p.planet_ascent_receipt<<'|'
                <<receipt<<'|'<<selected<<'|'<<options.size()<<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Godot shuttle observation pipe closed");
        };
        for(std::size_t i=0;i<options.size();++i) {
            const auto& o=options[i];
            const auto phase=world.world().planetary_shuttles().at(o.key).flight_phase;
            std::cout<<"TSITE|1|"<<session<<'|'<<i<<'|'<<std::get<0>(o.key)<<'|'
                <<std::get<1>(o.key)<<'|'<<std::get<2>(o.key)<<'|'<<o.surface<<'|'
                <<unsigned(phase)<<'\n';
        }
        std::cout<<std::flush;
        emit(0,0);
        // Clock ownership remains in the original SQLite journal, independent
        // of viewport focus or whether Godot is supplying input.
        std::thread ticker([&] {
            auto next=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
            while(running.load()) {
                std::this_thread::sleep_until(next);
                if(!running.load())break;
                try {
                    std::lock_guard<std::mutex> lock(writer);
                    (void)person(world);
                    world.step();
                    emit(0,0);
                } catch(const std::exception& e) {
                    std::cerr<<"Original planet transfer clock stopped: "<<e.what()<<'\n';
                    running.store(false);
                    break;
                }
                next+=std::chrono::milliseconds(100);
                if(next<std::chrono::steady_clock::now())
                    next=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
            }
        });
        struct Join {
            std::atomic<bool>& flag;
            std::thread& thread;
            ~Join(){flag.store(false);if(thread.joinable())thread.join();}
        } join{running,ticker};
        std::string line;
        while(running.load()&&std::getline(std::cin,line)) {
            if(line=="QUIT")break;
            std::lock_guard<std::mutex> lock(writer);
            if(++attempts>100000) {
                denial(0,"session-budget");return 3;
            }
            if(line.size()>160) {
                denial(0,"command-too-long");continue;
            }
            const auto f=fields(line);
            std::uint64_t given_session=0,request=0,site_index=0;
            const bool select=f.size()==4&&f[0]=="TSELECT";
            const bool action=f.size()==4&&f[0]=="TDO"&&
                (f[3]=="BOARD"||f[3]=="DESCEND"||
                 f[3]=="DISEMBARK"||f[3]=="ASCEND"||f[3]=="EXIT");
            if((!select&&!action)||!number(f[1],given_session)||
               !number(f[2],request)||
               (select&&!number(f[3],site_index))) {
                denial(0,"malformed-transfer-request");continue;
            }
            if(given_session!=session) {
                denial(request,"stale-session");continue;
            }
            if(request==0 || request<=last_request || request>=1000000) {
                denial(request,"replayed-transfer-request");continue;
            }
            last_request=request;
            const auto& p=person(world);
            const auto proof=base+request*2;
            bool accepted=false;
            if(select) {
                if(p.state==PlayerPersonState::AboardOwnShip&&
                   site_index<options.size()) {
                    const auto live=sites(world,p);
                    accepted=live.size()==options.size();
                    if(accepted) {
                        for(std::size_t i=0;i<options.size();++i) {
                            if(std::get<0>(live[i].key)!=std::get<0>(options[i].key) ||
                               std::get<1>(live[i].key)!=std::get<1>(options[i].key) ||
                               std::get<2>(live[i].key)!=std::get<2>(options[i].key) ||
                               std::get<3>(live[i].key)!=std::get<3>(options[i].key) ||
                               live[i].surface!=options[i].surface) {
                                accepted=false;break;
                            }
                        }
                    }
                    if(accepted)selected=static_cast<std::size_t>(site_index);
                }
            } else {
                const auto& key=options.at(selected).key;
                if(f[3]=="BOARD")
                    accepted=world.planetary_player_shuttle_action(
                        owner,p.home_ship,key,PlanetaryPlayerAction::Board,proof);
                else if(f[3]=="DESCEND")
                    accepted=world.launch_planetary_shuttle(
                        owner,p.home_ship,key,proof+1,false);
                else if(f[3]=="DISEMBARK")
                    accepted=world.planetary_player_shuttle_action(
                        owner,p.home_ship,key,PlanetaryPlayerAction::Disembark,proof);
                else if(f[3]=="ASCEND")
                    accepted=world.launch_planetary_shuttle(
                        owner,p.home_ship,key,proof+1,true);
                else if(f[3]=="EXIT")
                    accepted=world.planetary_player_shuttle_action(
                        owner,p.home_ship,key,PlanetaryPlayerAction::Exit,proof);
            }
            if(!accepted) {
                denial(request,"original-transport-authority-refused");
                continue;
            }
            // The single-use native action must already be atomically journaled.
            // SELECT changes no source and emits receipt=0.
            if(action && f[3]=="DISEMBARK") {
                const auto& ashore=world.world().player_people().at(owner);
                if(ashore.state!=PlayerPersonState::AtPlanetarySurface ||
                   !ashore.planet_descent_receipt)
                    throw std::runtime_error("committed disembark lacks original arrival");
                // Emit one verified handoff receipt before closing this process;
                // Godot MUST stop this writer before launching --planet.
                std::cout<<"TLAND|1|"<<session<<'|'<<request<<'|'<<ashore.home_ship
                    <<'|'<<ashore.planet_body<<'|'<<world.world().tick()<<'|'
                    <<world.generation()<<'|'<<proof<<'\n'<<std::flush;
                return 0;
            }
            if(action && f[3]=="EXIT") {
                const auto& aboard=world.world().player_people().at(owner);
                if(aboard.state!=PlayerPersonState::AboardOwnShip ||
                   aboard.physical_ship!=aboard.home_ship)
                    throw std::runtime_error("committed return lacks original ship");
                std::cout<<"TRETURN|1|"<<session<<'|'<<request<<'|'<<aboard.home_ship
                    <<'|'<<world.world().tick()<<'|'<<world.generation()
                    <<'|'<<proof<<'\n'<<std::flush;
                return 0;
            }
            emit(request,action?proof:0);
        }
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"Original planet transfer refused: "<<e.what()<<'\n';
        return 3;
    }
}
} // namespace galaxy::installed_planet_transfer
