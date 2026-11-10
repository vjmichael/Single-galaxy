// Read-only developer bridge from the original installed SQLite world to
// Godot 4 rendering. Does not alter the world or grant player landing.
#include "galaxy/Stage6GodotPacket.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::invalid_argument(
            "usage: stage6_godot_export <existing_world.sqlite> <owner> "
            "<ship_id> <original_body_id> <out.json>");
        const std::filesystem::path source=argv[1];
        if(!std::filesystem::is_regular_file(source))
            throw std::runtime_error("original SQLite world is missing");
        const auto owner=static_cast<galaxy::PlayerId>(std::stoull(argv[2]));
        const auto ship=static_cast<galaxy::EntityId>(std::stoull(argv[3]));
        const auto body=std::stoull(argv[4]);
        // An existing, independently verified game world is mandatory.
        galaxy::PersistentWorld world(source.string(),galaxy::Simulation(1));
        const auto packet=galaxy::stage6::original_godot_mesh_packet(
            world,owner,ship,body,{{0,0,1},{0,1,1},{0,2,2},{0,3,2}});
        if(!packet)throw std::runtime_error(
            "owner/body/planetary survey not authorized in original world");
        std::ofstream target(argv[5],std::ios::binary|std::ios::trunc);
        if(!target)throw std::runtime_error("cannot write Godot mesh packet");
        target<<*packet;
        target.flush();
        if(!target)throw std::runtime_error("Godot mesh packet write failed");
        std::cout<<"STAGE6-D original-owner terrain mesh exported without world mutation\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"STAGE6-D: "<<e.what()<<'\n';return 1;
    }
}
