// B4 fixed-step prototype. Uses the same committed-world client projection as B3.
// Deliberately no network, assets, replay spoofing, or graphics in this binary.
#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace galaxy;
namespace fs=std::filesystem;
using clock_type=std::chrono::steady_clock;

static Command cmd(std::uint64_t id, std::uint64_t tick, EntityId ship,
                   EntityId target, Action action, std::int64_t amount=0) {
    Command c{};
    c.id=id; c.execute_tick=tick; c.issuer=7; c.actor=ship;
    c.target=target; c.action=action; c.amount=amount;
    return c;
}
static void submit(PersistentWorld& w, const Command& c) {
    if(!w.submit(c)) throw std::runtime_error("command rejected before step");
}
static void write(const ViewFrame& f) {
    std::cout << encode_frame(f) << std::flush;
    if(!std::cout) throw std::runtime_error("SGVIEW output failed");
}
static std::uint64_t new_stream_id() {
    std::random_device rd;
    // A different stream/epoch is required for each host process. This is not an authentication token.
    std::uint64_t v=(std::uint64_t(rd())<<32)^std::uint64_t(rd());
    v^=std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    return v ? v : 1;
}
int main(int argc, char** argv) {
    if(argc<3 || argc>4) {
        std::cerr << "Usage: galaxy_live_scenario_host ten|hold|motion NEW_SQLITE_PATH [tick_ms=100]\\n";
        return 2;
    }
    try {
#ifdef _WIN32
        if(_setmode(_fileno(stdout), _O_BINARY)==-1) throw std::runtime_error("stdout binary mode failed");
#endif
        const std::string scenario=argv[1];
        if(scenario!="ten"&&scenario!="hold"&&scenario!="motion")
            throw std::invalid_argument("unknown scenario");
        const fs::path db=argv[2];
        if(fs::exists(db)) throw std::runtime_error("refusing to overwrite an existing save");
        unsigned ms=100;
        if(argc==4) {
            const std::string value=argv[3];
            if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
                throw std::invalid_argument("invalid tick milliseconds");
            const unsigned long parsed=std::stoul(value);
            if(parsed<10||parsed>5000) throw std::invalid_argument("tick milliseconds must be 10..5000");
            ms=static_cast<unsigned>(parsed);
        }
        Simulation initial(0xB40);
        EntityId ship=0, rock=0;
        std::vector<EntityId> rocks;
        if(scenario=="ten") {
            ship=initial.add_ship(7,{0,0},100);
            initial.add_station({-2500,-2500});
            for(int i=0;i<10;++i) {
                const auto x=std::int64_t(-2200+(i%5)*1100);
                const auto y=std::int64_t(1500+(i/5)*900);
                rocks.push_back(initial.add_asteroid({x,y},10));
            }
        } else if(scenario=="hold") {
            ship=initial.add_ship(7,{0,0},10);
            initial.add_station({-3000,-2100});
            rock=initial.add_asteroid({2000,1000},20);
        } else {
            ship=initial.add_ship(7,{-3000,-800},10);
            initial.add_station({-3000,2100});
            initial.add_asteroid({2700,2300},20);
        }
        PersistentWorld w(db.string(), std::move(initial));
        ServerViewBridge bridge(w.world(),new_stream_id());
        write(bridge.snapshot());
        // Prepare future commands before the real-time loop: all are durably accepted.
        if(scenario=="ten") {
            for(int i=0;i<10;++i) submit(w,cmd(100+i,i+1,ship,rocks[i],Action::Mine,10));
        } else if(scenario=="hold") {
            submit(w,cmd(200,1,ship,rock,Action::StartMining,4));
        } else {
            Command c=cmd(300,1,ship,0,Action::Thrust);
            c.thrust_x=1;c.thrust_y=1;submit(w,c);
            c=cmd(301,3,ship,0,Action::Thrust);
            c.thrust_x=1;c.thrust_y=0;submit(w,c);
        }
        const std::uint64_t ticks=scenario=="ten"?10:scenario=="hold"?7:12;
        const auto period=std::chrono::milliseconds(ms);
        const auto started=clock_type::now();
        auto max_late=std::chrono::microseconds::zero();
        for(std::uint64_t i=1;i<=ticks;++i) {
            // Fixed-step: never scale physics by varying wall-clock time.
            const auto deadline=started+period*i;
            std::this_thread::sleep_until(deadline);
            const auto late=std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now()-deadline);
            if(late>max_late) max_late=late;
            w.step(); // COMMIT must succeed before publishing the delta
            write(bridge.after_committed_step(w.world()));
        }
        const auto& world=w.world();
        if(scenario=="ten") {
            if(world.find(ship)->cargo!=100 || world.total_ore()!=100)
                throw std::runtime_error("ten-rock resource conservation mismatch");
            for(EntityId id:rocks) if(world.find(id)) throw std::runtime_error("asteroid not despawned");
        } else if(scenario=="hold") {
            if(world.find(ship)->cargo!=10 || world.find(rock)->ore!=10 ||
                !world.active_mining().empty() || world.total_ore()!=20)
                throw std::runtime_error("hold-full resource conservation mismatch");
        }
        std::cerr << "B4 LIVE PASS scenario=" << scenario << " ticks=" << ticks
                  << " period_ms=" << ms << " max_late_us=" << max_late.count() << '\n';
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "B4 LIVE FAIL: " << e.what() << '\n';
        return 1;
    }
}
