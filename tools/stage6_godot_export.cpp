// Read-only operator bridge from genuine original saved-world receipts to
// Godot terrain display. No new simulation state, landings or material grants.
#include "galaxy/Stage6GodotPacket.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs=std::filesystem;
namespace {
std::uint8_t decimal_byte(const std::string& part) {
    if(part.empty() ||
       !std::all_of(part.begin(),part.end(),[](unsigned char c){
           return c>='0' && c<='9';
       }))throw std::invalid_argument("tile coordinates must be unsigned decimal");
    const auto n=std::stoul(part);
    if(n>255)throw std::invalid_argument("tile coordinate exceeds byte range");
    return static_cast<std::uint8_t>(n);
}
galaxy::stage6::RenderTileAddress parse_tile(const std::string& value) {
    const auto a=value.find(',');
    const auto b=a==std::string::npos?a:value.find(',',a+1);
    if(a==std::string::npos || b==std::string::npos ||
       value.find(',',b+1)!=std::string::npos)
        throw std::invalid_argument("tile format is LAT,LON,STRIDE");
    return {decimal_byte(value.substr(0,a)),
            decimal_byte(value.substr(a+1,b-a-1)),
            decimal_byte(value.substr(b+1))};
}
void publish_packet(const fs::path& destination,const std::string& content) {
    fs::path staging=destination;
    staging += ".stage6."+std::to_string(std::chrono::steady_clock::now().
        time_since_epoch().count())+".tmp";
    struct Cleanup {fs::path p;~Cleanup(){
        std::error_code ec;fs::remove(p,ec);
    }} cleanup{staging};
    {
        std::ofstream target(staging,std::ios::binary|std::ios::trunc);
        if(!target)throw std::runtime_error("cannot stage Godot mesh packet");
        target<<content;
        target.flush();
        if(!target)throw std::runtime_error("Godot mesh packet write failed");
    }
#ifdef _WIN32
    if(!MoveFileExW(staging.c_str(),destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot atomically replace Godot packet");
#else
    fs::rename(staging,destination);
#endif
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc<6 || argc>10)throw std::invalid_argument(
            "usage: stage6_godot_export <existing_world.sqlite> <owner> "
            "<ship_id> <original_body_id> <out.json> [LAT,LON,STRIDE ... up to 4]");
        const fs::path source=argv[1], output=argv[5];
        if(!fs::is_regular_file(source))
            throw std::runtime_error("original SQLite world is missing");
        // Never truncate the ORIGINAL database (including aliases or hardlinks)
        // or its transactional sidecars while exporting a visual packet.
        const auto canonical_source=fs::weakly_canonical(source);
        const auto canonical_output=fs::weakly_canonical(output);
        fs::path wal=canonical_source,shm=canonical_source,journal=canonical_source;
        wal += "-wal";
        shm += "-shm";
        journal += "-journal";
        if(canonical_source==canonical_output ||
           (fs::exists(output) && fs::equivalent(source,output)) ||
           canonical_output==wal || canonical_output==shm ||
           canonical_output==journal)
            throw std::invalid_argument("refusing to overwrite original SQLite data");
        const auto owner=static_cast<galaxy::PlayerId>(std::stoull(argv[2]));
        const auto ship=static_cast<galaxy::EntityId>(std::stoull(argv[3]));
        const auto body=std::stoull(argv[4]);
        std::vector<galaxy::stage6::RenderTileAddress> selected;
        if(argc==6)selected={{0,0,1},{0,1,1},{0,2,2},{0,3,2}};
        else for(int i=6;i<argc;++i)selected.push_back(parse_tile(argv[i]));
        // The original native authority validates every tile address, body,
        // ship, owner and the common exact tick/generation/hash. A batch is
        // bounded to four tiles; independent requests page the real survey.
        galaxy::PersistentWorld world(source.string(),galaxy::Simulation(1));
        const auto packet=galaxy::stage6::original_godot_mesh_packet(
            world,owner,ship,body,selected);
        if(!packet)throw std::runtime_error(
            "owner/body/terrain tile selection not authorized in original world");
        publish_packet(output,*packet);
        std::cout<<"STAGE6-D original-owner bounded tile batch published atomically; "
                    "no world mutation or player landing\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"STAGE6-D: "<<e.what()<<'\n';return 1;
    }
}
