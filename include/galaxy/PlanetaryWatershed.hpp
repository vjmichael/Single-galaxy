#pragma once
// PLAY-13D PGF-01B. Versioned, deterministic coarse planetary drainage.
// Candidate hydrology ONLY: rainfall units represent a modelled climate budget,
// not physical water inventory. Extraction must use world-genesis custody.
#include "galaxy/PlanetaryGenerationFramework.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <queue>
#include <vector>
#include <limits>
#include <stdexcept>

namespace galaxy::planetary {
constexpr int kWatershedRows=36;
constexpr int kWatershedColumns=72;
constexpr int kWatershedCells=kWatershedRows*kWatershedColumns;
enum class DrainageKind : std::uint8_t { Dry, Arroyo, SeasonalStream, PerennialRiver, Ocean };
struct WatershedCell {
    std::int32_t elevation=0;     // signed coarse altitude units
    std::int32_t spill_height=0;  // priority-flood depression-fill potential
    std::int32_t downstream=-1;   // -1 ocean / terminal inland basin
    std::uint32_t rainfall=0;     // abstract annual climate units; not kg
    std::uint64_t drainage=0;     // summed rainfall potential from catchment
    DrainageKind flow=DrainageKind::Dry;
    bool ocean=false;
};
struct WatershedMap {
    PlanetaryWorldKey key{};
    WorldClimate climate=WorldClimate::LifeSupporting;
    std::uint8_t water_coverage=0;
    std::int16_t average_celsius=0;
    std::array<WatershedCell,kWatershedCells> cells{};
    std::uint32_t ocean_count=0;
    std::uint64_t total_rainfall=0, terminal_discharge=0;
    const WatershedCell& at(int lat,int lon) const {
        if(lat<0 || lat>=kWatershedRows || lon<0 || lon>=kWatershedColumns)
            throw std::out_of_range("watershed location");
        return cells[lat*kWatershedColumns+lon];
    }
};
namespace watershed_detail {
inline std::uint64_t hash(std::uint64_t seed,int y,int x,int salt) {
    return astronomy_mix(seed ^ astronomy_mix(
        std::uint64_t(y+1)*0x9e3779b97f4a7c15ULL ^
        std::uint64_t(x+1)*0xbf58476d1ce4e5b9ULL ^
        std::uint64_t(salt+1)*0x94d049bb133111ebULL));
}
inline int grid_noise(std::uint64_t seed,int y,int x,int scale,int salt) {
    // Bilinear interpolation of fixed-point corner values, seam continuous.
    const int gy=y/scale, fy=y%scale, gx=x/scale, fx=x%scale;
    const int nx=(kWatershedColumns+scale-1)/scale;
    const auto corner=[&](int yy,int xx) {
        return int(hash(seed,yy,xx%nx,salt)%2001)-1000;
    };
    const std::int64_t a=std::int64_t(corner(gy,gx))*(scale-fx)+
                         std::int64_t(corner(gy,gx+1))*fx;
    const std::int64_t b=std::int64_t(corner(gy+1,gx))*(scale-fx)+
                         std::int64_t(corner(gy+1,gx+1))*fx;
    return int((a*(scale-fy)+b*fy)/(scale*scale));
}
inline std::array<int,4> adjacent(int index) {
    const int y=index/kWatershedColumns,x=index%kWatershedColumns;
    const int left=y*kWatershedColumns+(x+kWatershedColumns-1)%kWatershedColumns;
    const int right=y*kWatershedColumns+(x+1)%kWatershedColumns;
    // Caps join across longitude but do not use toroidal latitude wrapping.
    return {{y>0?index-kWatershedColumns:-1,
              y<kWatershedRows-1?index+kWatershedColumns:-1,
              left,right}};
}
} // namespace watershed_detail

inline WatershedMap generate_watersheds(
    const PlanetaryWorldKey& key,const PlanetaryEnvelope& envelope) {
    validate_planetary_envelope(key,envelope);
    WatershedMap out;
    out.key=key;
    out.climate=envelope.climate;
    out.water_coverage=envelope.water_coverage;
    out.average_celsius=envelope.average_celsius;
    const auto p=derive_region_profile(key,envelope);
    // Global relief with 3 continuous spatial scales. Note: the final
    // longitude seam is neighbor-connected but not guaranteed C1 smooth.
    std::vector<std::pair<std::int32_t,int>> sorted;
    sorted.reserve(kWatershedCells);
    for(int i=0;i<kWatershedCells;++i) {
        const int y=i/kWatershedColumns,x=i%kWatershedColumns;
        const int continental=watershed_detail::grid_noise(p.seed,y,x,12,1);
        const int regional=watershed_detail::grid_noise(p.seed,y,x,6,2);
        const int detail=watershed_detail::grid_noise(p.seed,y,x,3,3);
        const int elevation=(continental*6+regional*3+detail)/10;
        out.cells[i].elevation=elevation;
        sorted.emplace_back(elevation,i);
    }
    std::sort(sorted.begin(),sorted.end());
    // Rank-based sea level yields reproducible coverage; airless worlds with
    // 'zero water' retain terrain relief but receive no invented oceans.
    const int marine=int(kWatershedCells)*int(envelope.water_coverage)/100;
    for(int i=0;i<marine;++i) {
        out.cells[sorted[i].second].ocean=true;
        out.cells[sorted[i].second].flow=DrainageKind::Ocean;
        ++out.ocean_count;
    }
    struct Node {
        int height,index;
        bool operator>(const Node& rhs)const{
            return height!=rhs.height?height>rhs.height:index>rhs.index;
        }
    };
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> frontier;
    std::array<bool,kWatershedCells> visited{};
    std::vector<int> order;
    order.reserve(kWatershedCells);
    for(int i=0;i<kWatershedCells;++i)if(out.cells[i].ocean) {
        visited[i]=true;
        out.cells[i].spill_height=out.cells[i].elevation;
        frontier.push({out.cells[i].spill_height,i});
    }
    // No surface ocean: keep an inland terminal basin at global lowest
    // elevation, allowing endorheic catchments instead of imaginary seas.
    if(frontier.empty()) {
        const int outlet=sorted.front().second;
        visited[outlet]=true;
        out.cells[outlet].spill_height=out.cells[outlet].elevation;
        frontier.push({out.cells[outlet].spill_height,outlet});
    }
    while(!frontier.empty()){
        const Node n=frontier.top();frontier.pop();
        order.push_back(n.index);
        for(const int next:watershed_detail::adjacent(n.index)){
            if(next<0 || visited[next])continue;
            visited[next]=true;
            auto& cell=out.cells[next];
            cell.downstream=n.index;
            cell.spill_height=std::max(cell.elevation,n.height);
            frontier.push({cell.spill_height,next});
        }
    }
    // Orographic approximation: aridity varies with latitude, relief and
    // longitudinal exposure. Full winds / atmospheric mass balance PGF-01C.
    for(int i=0;i<kWatershedCells;++i){
        const int y=i/kWatershedColumns,x=i%kWatershedColumns;
        auto& c=out.cells[i];
        const int latitude=std::abs(2*y-(kWatershedRows-1));
        const int wet_base=envelope.climate==WorldClimate::Arid?6:
              envelope.climate==WorldClimate::AirlessRock?0:
              envelope.climate==WorldClimate::Frozen?5:32;
        const int climate_band=std::max(0,18-latitude/2);
        const int exposure=watershed_detail::grid_noise(p.seed,y,x,9,4)/100;
        const int upland=c.elevation>350?12:0;
        const int rain_shadow=(x>0 && out.cells[i-1].elevation>c.elevation+150)?10:0;
        c.rainfall=std::uint32_t(std::max(0,wet_base+climate_band+exposure+upland-rain_shadow));
        c.drainage=c.rainfall;
        out.total_rainfall+=c.rainfall;
    }
    // Reverse of the priority-flood discovery order is a topological order
    // because each new cell is assigned to an earlier popped parent.
    for(auto it=order.rbegin();it!=order.rend();++it){
        const int i=*it;
        const int down=out.cells[i].downstream;
        if(down>=0){
            if(out.cells[down].drainage>std::numeric_limits<std::uint64_t>::max()-out.cells[i].drainage)
                throw std::overflow_error("watershed drainage overflow");
            out.cells[down].drainage+=out.cells[i].drainage;
        } else out.terminal_discharge+=out.cells[i].drainage;
    }
    // Classification is candidate flow only; physical river volume needs
    // authoritative world-water allocation and seasonal runoff generation.
    for(auto& c:out.cells){
        if(c.ocean)continue;
        if(c.drainage>=3000 && c.rainfall>=12) c.flow=DrainageKind::PerennialRiver;
        else if(c.drainage>=650) c.flow=DrainageKind::SeasonalStream;
        else if(c.drainage>=140) c.flow=DrainageKind::Arroyo;
    }
    return out;
}
} // namespace galaxy::planetary
