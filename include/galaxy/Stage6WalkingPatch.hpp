#pragma once
// S6-F5-C: a bounded *visual* walking patch read from the same original
// deterministic continuous-surface source triangles as the real host walker.
// This is NOT landing, motion, collision, atmosphere or equipment authority.
// Every coordinate is local to the original committed player's odometer.
#include "galaxy/Persistence.hpp"
#include "galaxy/PlanetaryContinuousSurface.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
namespace galaxy::stage6 {
inline std::optional<std::string> original_walking_patch(
       const PersistentWorld& world,PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key) {
    namespace grid=planetary::continuous_surface;
    constexpr std::int64_t kGridStepMm=5000;
    constexpr unsigned kGridWidth=17;
    const auto& original=world.world();
    const auto player=original.player_people().find(owner);
    const auto site=original.planetary_sites().find(key);
    const auto shuttle=original.planetary_shuttles().find(key);
    const auto* vessel=original.find(ship);
    const auto body=std::get<0>(key);
    if(player==original.player_people().end() ||
       site==original.planetary_sites().end() ||
       shuttle==original.planetary_shuttles().end() || !vessel ||
       player->second.state!=PlayerPersonState::AtPlanetarySurface ||
       player->second.home_ship!=ship || player->second.physical_ship ||
       player->second.planet_body!=body ||
       player->second.planet_latitude!=std::get<1>(key) ||
       player->second.planet_longitude!=std::get<2>(key) ||
       player->second.planet_revision!=std::get<3>(key) ||
       !player->second.planet_descent_receipt ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       !site->second.surface_location || site->second.spaceport_phase!=3 ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=ship ||
       shuttle->second.flight_phase!=2 ||
       shuttle->second.descent_receipt!=player->second.planet_descent_receipt ||
       vessel->owner_id!=owner || vessel->type!=EntityType::Ship ||
       vessel->hull<=0)return std::nullopt;
    const auto radius=original.celestial_navigation_anchor(owner,ship,body);
    if(!radius || !radius->physical_radius_mm ||
       (radius->kind!=CelestialKind::Planet &&
        radius->kind!=CelestialKind::Moon))return std::nullopt;
    const auto home=grid::cell_center(
        {std::get<1>(key),std::get<2>(key)});
    if(!home)return std::nullopt;
    const auto& source_pose=player->second.surface_pose;
    if(source_pose.last_move_receipt && !original.continuous_native_planet_surface())
        return std::nullopt;
    const auto center=source_pose.last_move_receipt?
        grid::GeoPoint{source_pose.latitude_pdeg,source_pose.longitude_pdeg}:*home;
    if(!grid::canonical(center))return std::nullopt;
    std::map<std::pair<std::uint16_t,std::uint16_t>,grid::Sample> cached;
    const auto sample=[&](grid::SourceCell cell)->std::optional<grid::Sample>{
        const auto k=std::make_pair(cell.latitude,cell.longitude);
        const auto present=cached.find(k);
        if(present!=cached.end())return present->second;
        const auto survey=original.scan_planetary_geography(
            owner,ship,body,cell.latitude,cell.longitude);
        if(!survey || survey->key.body!=body ||
           survey->key.revision!=std::get<3>(key))return std::nullopt;
        const grid::Sample value{
            survey->landscape.region.elevation,
            survey->landscape.region.surface_water};
        cached.emplace(k,value);
        return value;
    };
    const auto zero=grid::source_ground(center,sample);
    if(!zero || zero->ocean)return std::nullopt;
    std::ostringstream out;
    out<<std::setprecision(12);
    out<<"{\"schema\":\"SG-STAGE6-WALKPATCH-01\","
       <<"\"body\":\""<<body<<"\","
       <<"\"owner\":\""<<owner<<"\","
       <<"\"ship\":\""<<ship<<"\","
       <<"\"surface_location\":\""<<site->second.surface_location<<"\","
       <<"\"native_generation\":\""<<world.generation()<<"\","
       <<"\"native_tick\":\""<<original.tick()<<"\","
       <<"\"native_move_receipt\":\""<<source_pose.last_move_receipt<<"\","
       <<"\"center_x_mm\":\""<<source_pose.x_mm<<"\","
       <<"\"center_y_mm\":\""<<zero->height_mm<<"\","
       <<"\"center_z_mm\":\""<<source_pose.z_mm<<"\","
       <<"\"stride_mm\":"<<kGridStepMm<<","
       <<"\"width\":"<<kGridWidth<<","
       <<"\"vertices\":[";
    for(unsigned row=0;row<kGridWidth;++row)
        for(unsigned col=0;col<kGridWidth;++col){
            const auto dx=(std::int64_t(col)-8)*kGridStepMm;
            const auto dz=(std::int64_t(row)-8)*kGridStepMm;
            const auto geodetic=grid::native_offset(
                center,dx,-dz,radius->physical_radius_mm);
            if(!geodetic)return std::nullopt;
            const auto ground=grid::source_ground(*geodetic,sample);
            if(!ground || ground->ocean)return std::nullopt;
            if(row||col)out<<',';
            // Original signed mm -> visual metres near a floating origin.
            out<<'['<<double(dx)/1000.0<<','
               <<double(ground->height_mm-zero->height_mm)/1000.0<<','
               <<double(dz)/1000.0<<']';
        }
    out<<"],\"triangles\":[";
    bool first=true;
    for(unsigned row=0;row+1<kGridWidth;++row)
        for(unsigned col=0;col+1<kGridWidth;++col){
            const auto a=row*kGridWidth+col,b=(row+1)*kGridWidth+col;
            for(const auto index:std::array<unsigned,6>{a,b,a+1,b,b+1,a+1}){
                if(!first)out<<',';
                first=false;out<<index;
            }
        }
    out<<"]}";
    return out.str();
}
} // namespace galaxy::stage6
