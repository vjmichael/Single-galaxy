#pragma once
// STAGE6-B: bounded, read-only 3D render tiles of ORIGINAL planetary survey
// cells. No duplicate terrain inventory, resource generation, collision,
// docking, landing, transport or authority. A tile is never a game command.
#include "galaxy/Stage6PlanetSurface.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace galaxy::stage6 {
// The original physical geoid remains in CosmicPosition/physical_radius_mm.
// These are UNIT-SPHERE local render coordinates (not physical galaxy meters).
// Game clients must apply their own floating-origin visual transforms after
// accepting the original body ID/position. Never feed rendered coordinates
// back as a ship, surface, resource or NPC location.
struct RenderVertex {
    double x=0, y=0, z=0;
    std::int16_t original_elevation=0; // exact authoritative survey score
    std::uint16_t latitude=0, longitude=0;
};
struct RenderTileAddress {
    std::uint8_t latitude_band=0,longitude_band=0;
    std::uint8_t stride=1; // 1 = full available survey cells; 2 = coarse LOD
    bool operator<(const RenderTileAddress& o) const noexcept {
        if(latitude_band!=o.latitude_band)return latitude_band<o.latitude_band;
        if(longitude_band!=o.longitude_band)return longitude_band<o.longitude_band;
        return stride<o.stride;
    }
};
struct RenderTile {
    RenderTileAddress address{};
    NativePlanetSurface observed{};
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> triangles;
    std::size_t latitude_vertices=0,longitude_vertices=0;
    bool still_current(const PersistentWorld& world) const {
        return observed.still_current(world);
    }
};
constexpr unsigned kLatitudeCellsPerTile=7;
constexpr unsigned kLongitudeCellsPerTile=6;
constexpr unsigned kLatitudeBands=5; // 35 edges, 36 native sampled rings
constexpr unsigned kLongitudeBands=12; // 72 native cells wrap at seam
constexpr unsigned kMaxTilesPerRequest=4;
// Strict numerical domain; this factor is *visual exaggeration*, not the
// radius, atmosphere, tide, tectonics, or valid landing elevation.
constexpr double kVisualHeightPerSurveyUnit=0.000001;
constexpr double kPi=3.141592653589793238462643383279502884;

inline std::vector<unsigned> sampled_axis(unsigned start,unsigned end,
                                          unsigned stride){
    std::vector<unsigned> out;
    for(unsigned at=start;at<end;at+=stride)out.push_back(at);
    out.push_back(end);
    return out; // always includes the exact neighboring tile edge
}
inline RenderVertex visual_vertex(const planetary::GeographicSite& s,
                                  unsigned unwrapped_lon){
    const auto lat=static_cast<unsigned>(s.key.latitude);
    const auto lon=static_cast<unsigned>(s.key.longitude);
    // Cell-center latitudes keep polar rings finite until an actual polar-cap
    // reconstruction gate. Longitude 72 is EXACTLY longitude 0.
    const double phi=(-90.0+(double(lat)+0.5)*5.0)*(kPi/180.0);
    // At the longitude seam, the final right-edge sample is 72 (not 0).
    // 182.5 degrees and -177.5 degrees occupy one same physical meridian;
    // do not create a triangle crossing the entire globe at the wrap.
    const double lambda=(-180.0+(double(unwrapped_lon)+0.5)*5.0)*(kPi/180.0);
    const auto h=s.landscape.region.elevation;
    const double r=1.0+double(h)*kVisualHeightPerSurveyUnit;
    return {r*std::cos(phi)*std::cos(lambda),
            r*std::sin(phi),
            r*std::cos(phi)*std::sin(lambda),
            h,s.key.latitude,s.key.longitude};
}
// The supplied world, owner, ship and original body are checked again for
// EVERY sampled source cell. This prevents a client from passing a valid
// first tile and using fabricated adjacent heights or foreign provenance.
inline std::optional<RenderTile> make_original_render_tile(
    const PersistentWorld& native,PlayerId owner,EntityId ship,
    std::uint64_t body,const RenderTileAddress& address){
    if(address.latitude_band>=kLatitudeBands ||
       address.longitude_band>=kLongitudeBands ||
       (address.stride!=1 && address.stride!=2))
        return std::nullopt;
    const unsigned lat_start=unsigned(address.latitude_band)*kLatitudeCellsPerTile;
    const unsigned lon_start=unsigned(address.longitude_band)*kLongitudeCellsPerTile;
    const auto latitudes=sampled_axis(lat_start,lat_start+kLatitudeCellsPerTile,
                                      address.stride);
    const auto longitudes=sampled_axis(lon_start,lon_start+kLongitudeCellsPerTile,
                                       address.stride);
    RenderTile result{};
    result.address=address;
    result.latitude_vertices=latitudes.size();
    result.longitude_vertices=longitudes.size();
    for(const auto lat:latitudes)for(const auto unwrapped_lon:longitudes){
        const auto lon=unwrapped_lon%planetary::kWatershedColumns;
        const auto sample=observe_planet_surface(native,owner,ship,body,
                              std::uint16_t(lat),std::uint16_t(lon));
        if(!sample)return std::nullopt;
        if(result.vertices.empty())result.observed=*sample;
        else if(sample->world_generation!=result.observed.world_generation ||
                sample->state_hash!=result.observed.state_hash ||
                sample->original_body_id!=result.observed.original_body_id ||
                sample->surveyed_cell.key.revision!=
                    result.observed.surveyed_cell.key.revision)
            return std::nullopt;
        result.vertices.push_back(visual_vertex(sample->surveyed_cell,unwrapped_lon));
    }
    const auto width=result.longitude_vertices;
    for(std::size_t y=0;y+1<result.latitude_vertices;++y){
        for(std::size_t x=0;x+1<width;++x){
            const auto a=static_cast<std::uint32_t>(y*width+x);
            const auto b=static_cast<std::uint32_t>((y+1)*width+x);
            result.triangles.insert(result.triangles.end(),
                                    {a,b,a+1,b,b+1,a+1});
        }
    }
    if(!result.still_current(native))return std::nullopt;
    return result;
}
// Stream no more than FOUR independent tile meshes per request. The caller
// schedules subsequent batches and frees old meshes; no hidden per-frame
// generation, background worker, unbounded cache or second authority.
inline std::optional<std::vector<RenderTile>> stream_original_render_tiles(
    const PersistentWorld& native,PlayerId owner,EntityId ship,
    std::uint64_t body,const std::vector<RenderTileAddress>& requested){
    if(requested.empty() || requested.size()>kMaxTilesPerRequest)
        return std::nullopt;
    std::set<RenderTileAddress> distinct;
    std::vector<RenderTile> out;
    for(const auto& address:requested){
        if(!distinct.insert(address).second)return std::nullopt;
        auto tile=make_original_render_tile(native,owner,ship,body,address);
        if(!tile)return std::nullopt;
        out.push_back(std::move(*tile));
    }
    return out;
}
} // namespace galaxy::stage6
