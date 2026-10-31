#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace galaxy;
int main(int argc, char** argv) {
    if(argc!=2) { std::cerr<<"usage: galaxy_client_wire_producer NEW_SQLITE_PATH\n"; return 2; }
    try {
        const std::filesystem::path db=argv[1];
        if(std::filesystem::exists(db)) throw std::runtime_error("refusing to replace existing save");
        Simulation initial(0xB3);
        const auto ship=initial.add_ship(7,{0,0},10);
        const auto rock=initial.add_asteroid({500,0},20);
        PersistentWorld w(db.string(), initial);
        ServerViewBridge publisher(w.world(), 2026103001ULL);
        std::cout << encode_frame(publisher.snapshot());
        Command c; c.id=10;c.execute_tick=1;c.issuer=7;c.actor=ship;
        c.target=rock;c.action=Action::StartMining;c.amount=4;
        if(!w.submit(c))throw std::runtime_error("cannot submit mining command");
        for(int i=0;i<5;++i){
            w.step();
            std::cout << encode_frame(publisher.after_committed_step(w.world()));
        }
        std::cout.flush();
    } catch(const std::exception& ex) {std::cerr<<"producer: "<<ex.what()<<'\n';return 1;}
}
