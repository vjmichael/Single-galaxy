#include "galaxy/Persistence.hpp"
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace galaxy;
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: galaxy_persistence_demo <sqlite-path> init|crash-init|resume\n";
        return 2;
    }
    try {
        Simulation initial(20261030);
        initial.add_station({0,0});
        initial.add_ship(7, {0,0}, 50);
        initial.add_asteroid({2000,0}, 80);
        PersistentWorld persistent(argv[1], std::move(initial));
        if (std::string(argv[2]) == "init" || std::string(argv[2]) == "crash-init") {
            if (persistent.world().tick() != 0) throw std::runtime_error("init requested on existing progressed world");
            Command cmd{};
            cmd.id=1; cmd.execute_tick=1; cmd.issuer=7; cmd.actor=2; cmd.target=3;
            cmd.action=Action::Mine; cmd.amount=12;
            if (!persistent.submit(cmd)) throw std::runtime_error("initial command rejected");
            std::cout << "INIT saved pending command=1; tick=0\n";
            if (std::string(argv[2]) == "crash-init") {
                std::cout << "INJECT abrupt process termination after committed submit\n";
                std::cout.flush();
                std::_Exit(77); // deliberately skip destructors and graceful shutdown
            }
        } else if (std::string(argv[2]) == "resume") {
            if (persistent.world().tick() != 0) throw std::runtime_error("resume expects pending command at tick=0");
            persistent.step();
            if (persistent.world().find(2)->cargo != 12 || persistent.world().find(3)->ore != 68)
                throw std::runtime_error("unexpected resource outcome");
            std::cout << "RESUME mined=12; cargo=12; asteroid=68; conserved=80\n";
        } else throw std::runtime_error("unknown operation");
        std::cout << "generation=" << persistent.generation() << " hash=" << persistent.world().state_hash() << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL " << ex.what() << '\n';
        return 1;
    }
}
