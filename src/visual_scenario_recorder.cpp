#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace galaxy;
namespace fs = std::filesystem;

static Command request(std::uint64_t id, std::uint64_t tick, EntityId ship,
                       EntityId target, Action action, std::int64_t amount = 0) {
    Command c{};
    c.id = id; c.execute_tick = tick; c.issuer = 7; c.actor = ship;
    c.target = target; c.action = action; c.amount = amount;
    return c;
}
static void submit_or_throw(PersistentWorld& w, const Command& c) {
    if (!w.submit(c)) throw std::runtime_error("scenario command rejected");
}
static void checked_step(PersistentWorld& w, ServerViewBridge& bridge) {
    w.step(); // Persistence must commit before any display frame is published.
    std::cout << encode_frame(bridge.after_committed_step(w.world()));
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: galaxy_visual_scenario_recorder ten|hold|motion NEW_SQLITE_PATH\n";
        return 2;
    }
    try {
#ifdef _WIN32
        // Preserve B3 exact LF bytes on Windows (CRT text mode would insert CR).
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) throw std::runtime_error("cannot set binary stdout");
#endif
        const std::string scenario = argv[1];
        const fs::path path = argv[2];
        if (fs::exists(path)) throw std::runtime_error("refusing to replace existing SQLite save");
        if (scenario != "ten" && scenario != "hold" && scenario != "motion")
            throw std::invalid_argument("unknown scenario");

        Simulation initial(0xB40);
        EntityId ship = 0;
        EntityId rock = 0;
        std::vector<EntityId> rocks;
        if (scenario == "ten") {
            ship = initial.add_ship(7, {0,0}, 100);
            initial.add_station({-2500,-2500});
            for (int i = 0; i < 10; ++i) {
                // Ten original, deterministic positions, all inside 5,000 mm mining range.
                const std::int64_t x = -2200 + (i % 5) * 1100;
                const std::int64_t y = 1500 + (i / 5) * 900;
                rocks.push_back(initial.add_asteroid({x,y},10));
            }
        } else if (scenario == "hold") {
            ship = initial.add_ship(7, {0,0},10);
            initial.add_station({-3000,-2100});
            rock = initial.add_asteroid({2000,1000},20);
        } else {
            ship = initial.add_ship(7, {-3000,-800},10);
            initial.add_station({-3000,2100});
            initial.add_asteroid({2700,2300},20);
        }

        PersistentWorld w(path.string(), initial);
        const std::uint64_t epoch = scenario == "ten" ? 2026103010ULL :
                                    scenario == "hold" ? 2026103020ULL : 2026103030ULL;
        ServerViewBridge bridge(w.world(), epoch);
        std::cout << encode_frame(bridge.snapshot());
        if (scenario == "ten") {
            for (int i = 0; i < 10; ++i) {
                submit_or_throw(w, request(100 + i, i + 1, ship, rocks[i], Action::Mine, 10));
                checked_step(w, bridge);
            }
            if(w.world().find(ship)->cargo != 100 || w.world().total_ore()!=100)
                throw std::runtime_error("ten-rock conservation failure");
            for (EntityId id : rocks) if(w.world().find(id))
                throw std::runtime_error("asteroid failed to despawn");
        } else if (scenario == "hold") {
            submit_or_throw(w, request(200, 1, ship, rock, Action::StartMining, 4));
            for(int i=0;i<7;++i) checked_step(w, bridge);
            if(w.world().find(ship)->cargo != 10 || w.world().find(rock)->ore != 10 ||
               !w.world().active_mining().empty() || w.world().total_ore()!=20)
                throw std::runtime_error("hold-full scenario mismatch");
        } else {
            auto c = request(300,1,ship,0,Action::Thrust);
            c.thrust_x=1; c.thrust_y=1;
            submit_or_throw(w,c);
            c = request(301,3,ship,0,Action::Thrust);
            c.thrust_x=1;c.thrust_y=0;
            submit_or_throw(w,c);
            for(int i=0;i<12;++i) checked_step(w,bridge);
            if(w.world().find(ship)->position.x <= -3000)
                throw std::runtime_error("motion scenario did not move");
        }
        std::cout.flush();
        if(!std::cout) throw std::runtime_error("output pipe/write failure");
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "visual recorder: " << ex.what() << '\n';
        return 1;
    }
}
