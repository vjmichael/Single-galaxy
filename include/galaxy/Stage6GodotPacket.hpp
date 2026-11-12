#pragma once
// STAGE6-D: transport only previously authorized original-world terrain into
// a Godot ArrayMesh/ConcavePolygonShape3D. This is NEVER a simulation command,
// a terrain authority, a player-spawn grant, or a collision-to-world conversion.
#include "galaxy/Stage6PlanetTiles.hpp"
#include "galaxy/Stage6AuthorizedLanding.hpp"
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace galaxy::stage6 {
inline std::optional<std::string> original_godot_mesh_packet(
    const PersistentWorld& native, PlayerId owner, EntityId ship,
    std::uint64_t body, const std::vector<RenderTileAddress>& addresses,
    const std::optional<PlanetarySiteKey>& landing_key=std::nullopt) {
    const auto tiles=stream_original_render_tiles(
        native,owner,ship,body,addresses);
    if(!tiles || tiles->empty())return std::nullopt;
    const auto& first=tiles->front().observed;
    if(!first.still_current(native) || !first.original_body.physical_radius_mm)
        return std::nullopt;
    std::optional<NativeLandingObservation> landed;
    std::optional<NativePlayerSurfaceObservation> player_landed;
    if(landing_key){
        if(std::get<0>(*landing_key)!=body)return std::nullopt;
        landed=observe_native_landed_shuttle(native,owner,ship,*landing_key);
        if(!landed || !landed->still_current(native))
            return std::nullopt;
        player_landed=observe_native_landed_player(
            native,owner,ship,*landing_key);
        if(player_landed && !player_landed->still_current(native))
            return std::nullopt;
    }
    std::ostringstream out;
    out<<std::setprecision(17);
    // Identifiers/hashes are strings: GDScript JSON numbers can lose 64-bit
    // precision and therefore must not be reused as simulation authorities.
    out<<"{\"schema\":\"SG-STAGE6-GODOT-01\","
       <<"\"body_id\":\""<<body<<"\","
       <<"\"owner\":\""<<owner<<"\","
       <<"\"source_ship\":\""<<ship<<"\","
       <<"\"generation\":\""<<first.world_generation<<"\","
       <<"\"world_hash\":\""<<first.state_hash<<"\","
       <<"\"physical_radius_mm\":\""
       <<first.original_body.physical_radius_mm<<"\","
       <<"\"landing_verified\":"<<(landed?"true":"false")<<","
       <<"\"player_surface_verified\":"<<(player_landed?"true":"false")<<","
       <<"\"player_disembark_receipt\":\""
       <<(player_landed?player_landed->player_disembark_receipt:0)<<"\","
       <<"\"original_descent_receipt\":\""
       <<(landed?landed->original_descent_receipt:0)<<"\","
       <<"\"landing_lat\":"<<(landed?std::get<1>(landed->key):0)<<","
       <<"\"landing_lon\":"<<(landed?std::get<2>(landed->key):0)<<","
       <<"\"surface_location\":\""
       <<(landed?landed->original_surface_location:0)<<"\","
       <<"\"shuttle_pilot_npc\":\""
       <<(landed?landed->pilot_npc:0)<<"\","
       <<"\"tiles\":[";
    for(std::size_t t=0;t<tiles->size();++t) {
        const auto& tile=tiles->at(t);
        if(!tile.still_current(native) ||
           tile.observed.world_generation!=first.world_generation ||
           tile.observed.state_hash!=first.state_hash ||
           tile.observed.original_body_id!=body)return std::nullopt;
        if(t)out<<',';
        out<<"{\"lat_band\":"<<unsigned(tile.address.latitude_band)
           <<",\"lon_band\":"<<unsigned(tile.address.longitude_band)
           <<",\"stride\":"<<unsigned(tile.address.stride)
           <<",\"vertices\":[";
        for(std::size_t i=0;i<tile.vertices.size();++i){
            const auto& v=tile.vertices[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
                return std::nullopt;
            if(i)out<<',';
            out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';
        }
        out<<"],\"triangles\":[";
        for(std::size_t i=0;i<tile.triangles.size();++i){
            if(tile.triangles[i]>=tile.vertices.size())return std::nullopt;
            if(i)out<<',';
            out<<tile.triangles[i];
        }
        out<<"]}";
    }
    out<<"]}";
    if(!first.still_current(native))return std::nullopt;
    return out.str();
}
} // namespace galaxy::stage6
