#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C3: ")+#x+" @"+std::to_string(__LINE__));}while(false)
int main(){
    const auto path=fs::temp_directory_path()/
      ("sg_play13c3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    try {
        EntityId ship=0,other=0,rock=0;
        std::uint64_t body=0;
        {
            Simulation seed(13003);
            ship=seed.add_colony_ship(7,{0,0},60,8);
            other=seed.add_ship(8,{100,0},30);
            rock=seed.add_asteroid({1000,0},20);
            seed.provision_starter_exploration_scanner(ship);
            PersistentWorld w(path.string(),std::move(seed));
            w.ensure_physical_celestial_systems();
            w.ensure_ship_frame_registry();
            body=w.world().physical_celestial_systems().at(1).front().id;
            CHECK(w.world().same_local_frame(ship,other));
            CHECK(w.world().coarse_probe_signatures(7,ship).size()==1);
            ServerViewBridge bridge(w.world(),8512,7);
            ClientView client;
            CHECK(client.apply(decode_frame(encode_frame(bridge.snapshot())))==ApplyResult::Applied);
            CHECK(client.world().entities.count(ship)==1);
            CHECK(client.world().entities.count(other)==1);
            CHECK(client.world().entities.count(rock)==1);
            CHECK(w.begin_celestial_transit(7,ship,body,9001));
            for(int i=0;i<4;++i){
                w.step();
                if(i==3)CHECK(w.complete_celestial_transit(9001));
                auto frame=decode_frame(encode_frame(bridge.after_committed_step(w.world())));
                CHECK(client.apply(frame)==ApplyResult::Applied);
            }
            CHECK(client.world().entities.count(ship)==1);
            CHECK(client.world().entities.count(other)==0);
            CHECK(client.world().entities.count(rock)==0);
            CHECK(!w.world().same_local_frame(ship,other));
            CHECK(w.world().coarse_probe_signatures(7,ship).empty());
            CHECK(w.world().total_ore()==20);
            CHECK(w.world().colony_ship(ship)->crew==8);
            // A newly attached bridge never leaks old-frame objects on snapshot.
            ServerViewBridge fresh(w.world(),8513,7);
            ClientView reader;
            CHECK(reader.apply(decode_frame(encode_frame(fresh.snapshot())))==ApplyResult::Applied);
            CHECK(reader.world().entities.size()==1);
            CHECK(reader.world().entities.count(ship)==1);
        }
        {
            PersistentWorld w(path.string(),Simulation(2));
            CHECK(w.world().ship_frames().at(ship).anchor_body==body);
            ServerViewBridge after_restart(w.world(),8514,7);
            ClientView reader;
            CHECK(reader.apply(decode_frame(encode_frame(after_restart.snapshot())))==ApplyResult::Applied);
            CHECK(reader.world().entities.count(ship)==1);
            CHECK(!reader.world().entities.count(other));
            CHECK(!reader.world().entities.count(rock));
        }
        fs::remove(path);
        std::cout<<"PASS PLAY-13C.3 frame-local snapshot/delta/scans/restart\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY-13C.3 "<<e.what()<<'\n';
        std::error_code ec;fs::remove(path,ec);return 1;
    }
}
