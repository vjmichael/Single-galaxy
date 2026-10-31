#include "galaxy/ClientContract.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
int main() {
    ClientView client;
    std::string line, packet;
    try {
        while(std::getline(std::cin,line)) {
            if(packet.size()+line.size()+1>4*1024*1024)throw std::runtime_error("oversize packet");
            packet+=line;packet+='\n';
            if(line!="END")continue;
            const auto frame=decode_frame(packet);
            if(client.apply(frame)!=ApplyResult::Applied)throw std::runtime_error("missing or conflicting frame; snapshot required");
            std::int64_t ship_cargo=-1, ship_capacity=-1, remaining_ore=-1;
            for(const auto& [id,e] : client.world().entities) {
                (void)id;
                if(e.type==EntityType::Ship){ship_cargo=e.cargo;ship_capacity=e.cargo_capacity;}
                if(e.type==EntityType::Asteroid)remaining_ore=e.ore;
            }
            std::cout<<"client tick="<<client.world().tick<<" seq="<<client.sequence()
                     <<" cargo="<<ship_cargo<<'/'<<ship_capacity<<" ore="<<remaining_ore;
            for(const auto& note : client.recent_notifications()) {
                if(note.kind==ChangeKind::Activity && note.activity.kind==ActivityKind::MiningStopped)
                    std::cout<<" event=MINING_STOPPED:"<<to_string(note.activity.result);
                if(note.kind==ChangeKind::Activity && note.activity.kind==ActivityKind::AsteroidDespawned)
                    std::cout<<" event=ASTEROID_DESPAWNED:"<<note.activity.target;
            }
            std::cout<<'\n';packet.clear();
        }
        if(!packet.empty() || !client.initialized()) throw std::runtime_error("incomplete or empty stream");
    } catch(const std::exception& ex) {std::cerr<<"consumer: "<<ex.what()<<'\n';return 1;}
}
