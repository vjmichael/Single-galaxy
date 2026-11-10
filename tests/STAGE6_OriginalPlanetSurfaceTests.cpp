#include "galaxy/Stage6PlanetTiles.hpp"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    std::string("STAGE6-A line ")+std::to_string(__LINE__)+": " #x);}while(0)
int main(){
 try{
    const auto dir=fs::temp_directory_path()/
        ("sg_stage6_native_"+std::to_string(std::chrono::steady_clock::now().
                                      time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup {fs::path path;~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{dir};
    const auto db=(dir/"authoritative.sqlite").string();
    EntityId ship=0;
    std::uint64_t planet=0;
    stage6::NativePlanetSurface before{};
    std::uint64_t original_material=0;
    {
        Simulation genesis(777,1);
        ship=genesis.add_colony_ship(7,{0,0},50,5);
        genesis.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
        // Original named worker roster must exist BEFORE the physical
        // celestial catalogue upgrades the snapshot to modern NPC format.
        genesis.enable_individual_workforce();
        genesis.provision_starter_exploration_scanner(ship);
        PersistentWorld native(db,std::move(genesis));
        native.ensure_physical_celestial_systems();
        for(const auto& body:native.world().physical_celestial_systems().at(1))
            if(body.kind==CelestialKind::Planet){planet=body.id;break;}
        CHECK(planet!=0);
        original_material=native.world().total_material_kg();
        const auto hash=native.world().state_hash();
        const auto generation=native.generation();
        CHECK(!stage6::observe_planet_surface(native,8,ship,planet,17,22));
        CHECK(!stage6::observe_planet_surface(native,7,ship,planet+999999,17,22));
        CHECK(!stage6::observe_planet_surface(native,7,ship,planet,36,22));
        CHECK(!stage6::observe_planet_surface(native,7,ship,planet,17,72));
        CHECK(!stage6::observe_planet_surface(native,7,ship,0,17,22));
        CHECK(!stage6::observe_planet_surface(native,7,999999,planet,17,22));
        auto first=stage6::observe_planet_surface(native,7,ship,planet,17,22);
        CHECK(first);
        before=*first;
        const auto repeat=stage6::observe_planet_surface(
            native,7,ship,planet,17,22);
        CHECK(repeat && repeat->original_body_id==planet &&
              repeat->original_body.physical_radius_mm==
                  first->original_body.physical_radius_mm &&
              repeat->surveyed_cell.landscape.region.elevation==
                  first->surveyed_cell.landscape.region.elevation &&
              repeat->surveyed_cell.catchment_runoff==
                  first->surveyed_cell.catchment_runoff);
        CHECK(first->still_current(native));
        // STAGE6-B: two native-authenticated 3D tiles meet at an exact
        // original sampled meridian. No invented 3D world ID or local cargo.
        const auto batch=stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,0,1},{0,1,1}});
        CHECK(batch && batch->size()==2);
        const auto& left=batch->at(0);
        const auto& right=batch->at(1);
        CHECK(left.vertices.size()==left.latitude_vertices*left.longitude_vertices);
        CHECK(right.vertices.size()==right.latitude_vertices*right.longitude_vertices);
        CHECK(left.triangles.size()==(left.latitude_vertices-1)*
            (left.longitude_vertices-1)*6);
        for(std::size_t row=0;row<left.latitude_vertices;++row){
            const auto& a=left.vertices[row*left.longitude_vertices+
                                        (left.longitude_vertices-1)];
            const auto& z=right.vertices[row*right.longitude_vertices];
            CHECK(a.latitude==z.latitude && a.longitude==z.longitude);
            CHECK(std::abs(a.x-z.x)<1e-12 &&
                  std::abs(a.y-z.y)<1e-12 &&
                  std::abs(a.z-z.z)<1e-12 &&
                  a.original_elevation==z.original_elevation);
        }
        // At the 72 -> 0 longitude seam geometry must not cross the globe.
        const auto seam=stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,11,2},{0,0,2}});
        CHECK(seam && seam->size()==2);
        for(std::size_t row=0;row<seam->at(0).latitude_vertices;++row){
            const auto& a=seam->at(0).vertices[
                row*seam->at(0).longitude_vertices+
                seam->at(0).longitude_vertices-1];
            const auto& z=seam->at(1).vertices[
                row*seam->at(1).longitude_vertices];
            CHECK(a.longitude==0 && z.longitude==0);
            CHECK(std::abs(a.x-z.x)<1e-12 &&
                  std::abs(a.y-z.y)<1e-12 &&
                  std::abs(a.z-z.z)<1e-12);
        }
        CHECK(!stage6::stream_original_render_tiles(
            native,8,ship,planet,{{0,0,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet+999999,{{0,0,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,0,1},{0,0,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,0,1},{0,1,1},{0,2,1},
                                  {0,3,1},{0,4,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{{5,0,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,12,1}}));
        CHECK(!stage6::stream_original_render_tiles(
            native,7,ship,planet,{{0,0,3}}));
        CHECK(native.world().state_hash()==hash &&
              native.generation()==generation &&
              native.world().total_material_kg()==original_material);
        native.step(); // real authoritative game tick invalidates the view
        CHECK(!first->still_current(native));
        CHECK(!batch->front().still_current(native));
        const auto refreshed=stage6::observe_planet_surface(
            native,7,ship,planet,17,22);
        CHECK(refreshed && refreshed->still_current(native) &&
              refreshed->surveyed_cell.landscape.region.elevation==
                  first->surveyed_cell.landscape.region.elevation);
        const auto archived=(dir/"verified-copy.sqlite").string();
        PersistentWorld::recover_copy(db,archived);
        {
            PersistentWorld copied(archived,Simulation(777,1));
            const auto loaded=stage6::observe_planet_surface(
                copied,7,ship,planet,17,22);
            CHECK(loaded && loaded->still_current(copied));
            const auto recovered_tile=stage6::make_original_render_tile(
                copied,7,ship,planet,{0,0,1});
            CHECK(recovered_tile && recovered_tile->still_current(copied));
            CHECK(recovered_tile->vertices.size()==batch->front().vertices.size());
            for(std::size_t i=0;i<recovered_tile->vertices.size();++i){
                const auto& a=recovered_tile->vertices[i];
                const auto& z=batch->front().vertices[i];
                CHECK(a.original_elevation==z.original_elevation &&
                    std::abs(a.x-z.x)<1e-12 &&
                    std::abs(a.y-z.y)<1e-12 &&
                    std::abs(a.z-z.z)<1e-12);
            }
            CHECK(loaded->original_body_id==before.original_body_id &&
                  loaded->original_body.physical_radius_mm==
                      before.original_body.physical_radius_mm &&
                  loaded->surveyed_cell.landscape.region.elevation==
                      before.surveyed_cell.landscape.region.elevation &&
                  loaded->surveyed_cell.spill_height==
                      before.surveyed_cell.spill_height &&
                  copied.world().total_material_kg()==original_material);
        }
    }
    std::cout<<"STAGE6-A/B original physical planet, bounded 3D terrain tiles, "
                "longitude seam, owner-gated read, stale refusal, SQLite restart PASS\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"STAGE6-A FAIL: "<<e.what()<<'\n';return 1;
 }
}
