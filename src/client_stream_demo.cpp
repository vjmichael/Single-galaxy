#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
using namespace galaxy;
int main(int argc,char**argv) {
    const auto path=(argc>1?argv[1]:"galaxy_b3_demo.sqlite");
    if(std::filesystem::exists(path)){std::cerr<<"Refusing to overwrite existing demo database: "<<path<<'\n';return 2;}
    Simulation seed(123);
    auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({500,0},20);
    PersistentWorld world(path,seed);
    ServerViewBridge server(world.world(),40001);ClientView client;
    client.apply(decode_frame(encode_frame(server.snapshot())));
    Command c;c.id=1;c.execute_tick=1;c.issuer=7;c.actor=ship;c.target=rock;c.action=Action::StartMining;c.amount=4;
    if(!world.submit(c))throw std::runtime_error("start mining command rejected");
    for(int i=0;i<5;++i){
        world.step();
        auto d=decode_frame(encode_frame(server.after_committed_step(world.world())));
        if(client.apply(d)!=ApplyResult::Applied)throw std::runtime_error("view update failed");
        const auto& state=client.world();
        std::cout<<"tick="<<state.tick<<" cargo="<<state.entities.at(ship).cargo
                 <<"/10 rock_ore="<<state.entities.at(rock).ore;
        for(const auto& note:client.recent_notifications()){
            if(note.kind==ChangeKind::Activity && note.activity.kind==ActivityKind::MiningStopped)
                std::cout << " notification=MINING_STOPPED:"<<to_string(note.activity.result);
        }
        std::cout<<'\n';
    }
    std::cout<<"saved="<<path<<'\n';
}
