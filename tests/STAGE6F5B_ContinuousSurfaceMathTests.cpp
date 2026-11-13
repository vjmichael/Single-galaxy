#include "galaxy/PlanetaryContinuousSurface.hpp"
#include "galaxy/Stage6WalkingPatch.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
using namespace galaxy::planetary::continuous_surface;
#define CHECK(x) do{if(!(x))throw std::runtime_error(     "S6-F5-B line "+std::to_string(__LINE__)+": " #x);}while(0)
int main(){
try{
    const auto sample=[](SourceCell c)->std::optional<Sample>{
        if(c.latitude>=36||c.longitude>=72)return std::nullopt;
        return Sample{std::int32_t(-250+3*c.latitude+int(c.longitude)),false};
    };
    constexpr std::uint64_t earth_radius=6371000000ULL;
    constexpr std::uint64_t moon_radius=1737400000ULL;
    const auto initial=cell_center({17,36});
    CHECK(initial && canonical(*initial));
    const SourceCell expected_region{17,36};
    CHECK(source_region(*initial)==expected_region);
    const auto contact=source_ground(*initial,sample);
    CHECK(contact && !contact->ocean);
    CHECK(contact->height_mm==(-250+3*17+36)*1000);
    const auto accepted=native_step(*initial,300,0,earth_radius);
    CHECK(accepted && accepted->longitude_pdeg>initial->longitude_pdeg);
    CHECK(accepted->latitude_pdeg==initial->latitude_pdeg);
    CHECK(within_radius(*initial,*accepted,earth_radius,500));
    CHECK(!native_step(*initial,301,0,earth_radius));
    CHECK(!native_step(*initial,300,300,earth_radius));
    CHECK(!native_step(*initial,300,0,1)); // no impossible body physics
    // The source-only 3x3 visual atlas spans +/-64 metres, while no actual
    // planetary movement may exceed the original +/-32m native offset bound.
    CHECK(!native_offset(*initial,64000,0,earth_radius));
    const auto visual_east=galaxy::stage6::visual_tile_offset(
        *initial,64000,0,earth_radius);
    const auto visual_west=galaxy::stage6::visual_tile_offset(
        *initial,-64000,0,earth_radius);
    const auto visual_north=galaxy::stage6::visual_tile_offset(
        *initial,0,64000,earth_radius);
    CHECK(visual_east && visual_west && visual_north);
    CHECK(visual_east->longitude_pdeg>initial->longitude_pdeg);
    CHECK(visual_west->longitude_pdeg<initial->longitude_pdeg);
    CHECK(visual_north->latitude_pdeg>initial->latitude_pdeg);
    CHECK(galaxy::stage6::visual_tile_offset(
        *initial,64000,64000,earth_radius));
    CHECK(!galaxy::stage6::visual_tile_offset(
        *initial,64002,0,earth_radius));
    CHECK(!galaxy::stage6::visual_tile_offset(
        *initial,0,64001,earth_radius));
    // Crossing both 5-degree source-region seams is a native geometry
    // operation, not teleport between two colony-site IDs.
    const GeoPoint left{-85*kDegree,-175*kDegree-1000000};
    const GeoPoint right{-85*kDegree,-175*kDegree+1000000};
    CHECK(source_region(left).longitude==0);
    CHECK(source_region(right).longitude==1);
    CHECK(source_region(left).latitude==1);
    CHECK(source_region(right).latitude==1);
    const auto l=source_ground(left,sample),r=source_ground(right,sample);
    CHECK(l && r && std::llabs(l->height_mm-r->height_mm)<=5);
    const GeoPoint south{-85*kDegree-1000000,-170*kDegree};
    const GeoPoint north{-85*kDegree+1000000,-170*kDegree};
    CHECK(source_region(south).latitude==0);
    CHECK(source_region(north).latitude==1);
    const auto h_s=source_ground(south,sample),h_n=source_ground(north,sample);
    CHECK(h_s && h_n && std::llabs(h_s->height_mm-h_n->height_mm)<=5);
    // Longitude 71 -> 0 is one source triangle seam, not an extra column.
    const GeoPoint seam_a{0,kHalfTurn-1000000};
    const GeoPoint seam_b{0,-kHalfTurn+1000000};
    CHECK(source_region(seam_a).longitude==71);
    CHECK(source_region(seam_b).longitude==0);
    CHECK(within_radius(seam_a,seam_b,earth_radius,500));
    const auto wrap=native_step(seam_a,300,0,earth_radius);
    CHECK(wrap && wrap->longitude_pdeg<0);
    const auto seam_h_a=source_ground(seam_a,sample);
    const auto seam_h_b=source_ground(seam_b,sample);
    CHECK(seam_h_a && seam_h_b);
    CHECK(std::llabs(seam_h_a->height_mm-seam_h_b->height_mm)<=5);
    // Physical polar reflection changes meridian by half a turn, while the
    // common original-source cap height is independent of longitude.
    const GeoPoint just_before_pole{kPole-100000,0};
    const auto over=native_step(just_before_pole,0,300,moon_radius);
    CHECK(over && over->latitude_pdeg<kPole);
    CHECK(over->longitude_pdeg== -kHalfTurn);
    const GeoPoint cap_a{kPole,0};
    const GeoPoint cap_b{kPole,123*kDegree};
    CHECK(within_radius(cap_a,cap_b,moon_radius,5));
    const auto ca=source_ground(cap_a,sample),cb=source_ground(cap_b,sample);
    CHECK(ca && cb && ca->height_mm==cb->height_mm);
    CHECK(cosine_ppm(kPole)==1000);
    // A true ocean is represented by existing source geography alone.
    const auto wet=[](SourceCell c)->std::optional<Sample>{
        return Sample{0,c.latitude==17&&c.longitude==36};
    };
    const auto flooded=source_ground(*initial,wet);
    CHECK(flooded && flooded->ocean);
    const auto absent=[](SourceCell)->std::optional<Sample>{
        return std::nullopt;
    };
    CHECK(!source_ground(*initial,absent));
    // Deterministic same-input result and complete original globe traversal
    // are independent of floating-origin/Godot state.
    CHECK(native_step(*initial,200,-200,earth_radius)==
          native_step(*initial,200,-200,earth_radius));
    CHECK(source_region({0,-kHalfTurn}).longitude==0);
    CHECK(source_region({0,kHalfTurn-1}).longitude==71);
    std::cout<<"S6-F5-B fixed-point globe seam, cell continuity, "
             <<"polar cap, source ocean and swept input bounds PASS\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"S6-F5-B FAIL: "<<e.what()<<'\n';return 1;
}
}
