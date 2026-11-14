// Read-only operator bridge from genuine original saved-world receipts to
// Godot terrain display. No new simulation state, landings or material grants.
#include "galaxy/Stage6GodotPacket.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
std::uint64_t decimal_identity(const std::string& value) {
    if(value.empty() || !std::all_of(value.begin(),value.end(),
        [](unsigned char ch){return ch>='0'&&ch<='9';}))
        throw std::invalid_argument("world identity must be unsigned decimal");
    const auto result=std::stoull(value);
    if(!result)throw std::invalid_argument("original world identity must be positive");
    return result;
}
galaxy::PlanetarySiteKey parse_landing(
    std::uint64_t body,const std::string& selection) {
    const auto a=selection.find(',');
    const auto b=a==std::string::npos?a:selection.find(',',a+1);
    if(a==std::string::npos || b==std::string::npos ||
       selection.find(',',b+1)!=std::string::npos)
        throw std::invalid_argument("landing site format is LAT,LON,REV");
    const auto lat=decimal_byte(selection.substr(0,a));
    const auto lon=decimal_byte(selection.substr(a+1,b-a-1));
    const auto revision=decimal_byte(selection.substr(b+1));
    if(lat>=galaxy::planetary::kWatershedRows ||
       lon>=galaxy::planetary::kWatershedColumns || revision!=1)
        throw std::invalid_argument("landing site outside original region/revision");
    return galaxy::PlanetarySiteKey{body,lat,lon,revision};
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
        if(argc<6 || argc>11)throw std::invalid_argument(
            "usage: stage6_godot_export <existing_world.sqlite> <owner> "
            "<ship_id> <original_body_id> <out.json> "
            "[LAT,LON,STRIDE ... up to 4] [--landed=LAT,LON,REV]");
        const fs::path source=argv[1], output=argv[5];
        if(!fs::is_regular_file(source))
            throw std::runtime_error("original SQLite world is missing");
        // Never truncate the ORIGINAL database, its sidecars, or an alias.
        const auto canonical_source=fs::weakly_canonical(source);
        const auto canonical_output=fs::weakly_canonical(output);
        fs::path wal=canonical_source,shm=canonical_source,journal=canonical_source;
        wal += "-wal"; shm += "-shm"; journal += "-journal";
        if(canonical_source==canonical_output ||
           (fs::exists(output) && fs::equivalent(source,output)) ||
           canonical_output==wal || canonical_output==shm ||
           canonical_output==journal)
            throw std::invalid_argument("refusing to overwrite original SQLite data");
        const auto owner=static_cast<galaxy::PlayerId>(decimal_identity(argv[2]));
        const auto ship=static_cast<galaxy::EntityId>(decimal_identity(argv[3]));
        const auto body=decimal_identity(argv[4]);
        std::vector<galaxy::stage6::RenderTileAddress> selected;
        std::optional<galaxy::PlanetarySiteKey> landed;
        for(int i=6;i<argc;++i) {
            const std::string choice(argv[i]);
            if(choice.rfind("--landed=",0)==0) {
                if(landed)throw std::invalid_argument("duplicate landing observation");
                landed=parse_landing(body,choice.substr(9));
            }else{
                selected.push_back(parse_tile(choice));
            }
        }
        if(selected.empty()) {
            if(landed) {
                // A genuine original shuttle descent chooses the displayed
                // regional band. This is NOT a player-spawn authorization.
                const auto lat=std::get<1>(*landed);
                const auto lon=std::get<2>(*landed);
                selected.push_back({std::uint8_t(std::min<unsigned>(lat/7,galaxy::stage6::kLatitudeBands-1)),
                                    std::uint8_t(lon/6),1});
            }else selected={{0,0,1},{0,1,1},{0,2,2},{0,3,2}};
        }
        if(selected.size()>galaxy::stage6::kMaxTilesPerRequest)
            throw std::invalid_argument("at most four verified tiles per batch");
        // Read a recovered, generation-consistent COPY. Never open the live
        // original save through a potentially mutating PersistentWorld writer.
        const auto unique=std::chrono::steady_clock::now().
            time_since_epoch().count();
        const fs::path scratch=fs::temp_directory_path()/
            ("sg-stage6f-"+std::to_string(unique));
        fs::create_directory(scratch);
        struct CleanupCopy {fs::path path;~CleanupCopy(){
            std::error_code ec;fs::remove_all(path,ec);
        }} cleanup{scratch};
        const auto copy=(scratch/"source-copy.sqlite").string();
        galaxy::PersistentWorld::recover_copy(source.string(),copy);
        galaxy::PersistentWorld world(copy,galaxy::Simulation(1));
        const auto packet=galaxy::stage6::original_godot_mesh_packet(
            world,owner,ship,body,selected,landed);
        if(!packet)throw std::runtime_error(
            "original owner/scanner/body or completed shuttle descent not verified");
        publish_packet(output,*packet);
        std::cout<<"S6-F original-owner source tiles published from recovered copy; "
                    "shuttle descent observational only; no player landing or cargo change\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"STAGE6-D: "<<e.what()<<'\n';return 1;
    }
}
