// FP3D-02B.0: local child-process feed from the ACTUAL C++ persistent world.
// This is READ ONLY to the client: stdin is never interpreted as a command.
// SGDESK v1 is a bounded view of the independently SGVIEW-validated projection.
// Ship horizontal coordinates (mm) are not an authoritative on-foot XYZ pose.
#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
using namespace galaxy;
namespace fs=std::filesystem;

static std::uint64_t parse_ticks(const char* s) {
    const std::string v(s);
    if(v.empty()||v.size()>4||v.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("ticks must be digits 1..1000");
    const auto n=std::stoull(v);
    if(n<1||n>1000)throw std::invalid_argument("ticks must be 1..1000");
    return n;
}
static std::uint64_t new_epoch() {
    std::random_device rd;
    const auto seed=(std::uint64_t(rd())<<32)|std::uint64_t(rd());
    return seed?seed:1;
}
static const Entity* selected_ship(const ClientProjection& state) {
    for(const auto& [id,e]:state.entities) {
        (void)id;
        if(e.type==EntityType::Ship && e.owner_id==7 && e.system_id==state.system_id)return &e;
    }
    return nullptr;
}
static void present(const ClientView& verified) {
    const auto& s=verified.world();
    const Entity* ship=selected_ship(s);
    if(!ship)throw std::runtime_error("no verified owned ship in current observed system");
    // The projection already enforces native owner masking, stream continuity and hash.
    // Only 64 objects are exposed per packet. SGDESK never claims native Z/height.
    std::size_t count=0;
    for(const auto& [id,e]:s.entities) {
        (void)id;
        if(e.system_id==s.system_id && count<64)++count;
    }
    std::cout<<"SGDESK|1|"<<verified.stream_id()<<'|'<<verified.sequence()<<'|'
             <<s.tick<<'|'<<s.system_id<<'|'<<ship->id<<'|'<<ship->owner_id<<'|'
             <<ship->position.x<<'|'<<ship->position.y<<'|'
             <<ship->velocity.x<<'|'<<ship->velocity.y<<'|'<<count<<'\n';
    std::size_t emitted=0;
    for(const auto& [id,e]:s.entities) {
        if(e.system_id!=s.system_id)continue;
        if(emitted++>=64)break;
        std::cout<<"CONTACT|"<<id<<'|'<<int(e.type)<<'|'<<e.owner_id<<'|'
                 <<e.position.x<<'|'<<e.position.y<<'|'<<e.ore<<'\n';
    }
    std::cout<<"END\n"<<std::flush;
    if(!std::cout)throw std::runtime_error("client pipe closed");
}
int main(int argc,char**argv) {
    if(argc!=3){std::cerr<<"Usage: galaxy_desktop_readonly_host SQLITE_SAVE_PATH MAX_TICKS\n";return 2;}
    try {
        const fs::path save(argv[1]);
        if(save.empty() || save.string().size()>1024 || (!save.parent_path().empty() && !fs::exists(save.parent_path())))
            throw std::invalid_argument("invalid save path");
        const auto ticks=parse_ticks(argv[2]);
        Simulation seed(0xD357);
        const auto home=seed.add_station({0,0});
        seed.spawn_ship_docked(7,home,24,60);
        seed.add_asteroid({2500,700},40);
        seed.add_station({15000,0});
        PersistentWorld world(save.string(),std::move(seed));
        ServerViewBridge server(world.world(),new_epoch(),7);
        ClientView verified;
        auto apply=[&](const ViewFrame& f) {
            const auto encoded=encode_frame(f);
            const auto decoded=decode_frame(encoded);
            if(verified.apply(decoded)!=ApplyResult::Applied)
                throw std::runtime_error("native SGVIEW frame rejected by continuity verifier");
            present(verified);
        };
        apply(server.snapshot());
        for(std::uint64_t i=0;i<ticks;++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            world.step(); // sole authoritative SQLite journal and simulation writer
            apply(server.after_committed_step(world.world()));
        }
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"SGDESK native host refused operation: "<<e.what()<<'\n';return 1;
    }
}
