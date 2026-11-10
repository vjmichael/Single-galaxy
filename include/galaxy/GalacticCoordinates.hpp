#pragma once
// SPACE-04A: galaxy-scale EXACT 3D coordinate math without floating-point
// drift and without the ±1-light-year limit of a single signed int64 mm.
// Each axis is a signed high-order sector plus a normalized sub-sector mm
// offset. No EVE grid wall; the sector is purely numeric representation.
// This does NOT register star-system galaxy origins or move a native ship.
#include "galaxy/Simulation.hpp"
#include "galaxy/GalacticPhysicalPosition.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace galaxy::galactic {
// A system must have a real registered Galactic Point from world creation.
// This conversion supplies geometry only; it cannot invent a star location.
inline std::optional<Point> at_system_origin(
    Point registered_system_origin,CosmicPosition astronomical_offset){
    const auto x=normalize(registered_system_origin.x,astronomical_offset.x_mm);
    const auto y=normalize(registered_system_origin.y,astronomical_offset.y_mm);
    const auto z=normalize(registered_system_origin.z,astronomical_offset.z_mm);
    if(!x||!y||!z)return std::nullopt;
    return Point{*x,*y,*z};
}
struct Cell {
    std::int64_t sector_x=0,sector_y=0,sector_z=0;
    std::int32_t within_x=0,within_y=0,within_z=0;
    bool operator==(const Cell& r)const{
        return sector_x==r.sector_x && sector_y==r.sector_y &&
               sector_z==r.sector_z && within_x==r.within_x &&
               within_y==r.within_y && within_z==r.within_z;
    }
};
inline std::optional<Cell> cell(Point p){
    if(!p.valid())return std::nullopt;
    return Cell{p.x.sector,p.y.sector,p.z.sector,
        static_cast<std::int32_t>(p.x.millimeters/kCellMm),
        static_cast<std::int32_t>(p.y.millimeters/kCellMm),
        static_cast<std::int32_t>(p.z.millimeters/kCellMm)};
}
inline std::string scope(Cell c){
    if(c.within_x<0||c.within_y<0||c.within_z<0||
       c.within_x>=kCellsPerSector ||
       c.within_y>=kCellsPerSector ||
       c.within_z>=kCellsPerSector)
        throw std::invalid_argument("noncanonical galaxy cell");
    return "galactic/v1/s"+std::to_string(c.sector_x)+","+
        std::to_string(c.sector_y)+","+std::to_string(c.sector_z)+
        "/c"+std::to_string(c.within_x)+","+
        std::to_string(c.within_y)+","+std::to_string(c.within_z);
}
// For native close-range interactions only. Distant systems never need an
// enormous, overflowing raw millimeter subtraction to calculate range.
// Avoid premature narrowing; the only representable differences are
// same or adjacent sector, <=2e15 mm comfortably within signed int64.
inline std::optional<std::int64_t> nearby_delta(Axis a,Axis b) {
    if(!a.valid()||!b.valid())return std::nullopt;
    if(a.sector==b.sector)return a.millimeters-b.millimeters;
    if(a.sector<std::numeric_limits<std::int64_t>::max() &&
       a.sector+1==b.sector)
        return a.millimeters-b.millimeters-kSectorEdgeMm;
    if(b.sector<std::numeric_limits<std::int64_t>::max() &&
       b.sector+1==a.sector)
        return a.millimeters-b.millimeters+kSectorEdgeMm;
    return std::nullopt; // genuinely far, not an invisible physics wall
}
inline std::optional<CosmicPosition> nearby_displacement(Point a,Point b){
    const auto x=nearby_delta(a.x,b.x);
    const auto y=nearby_delta(a.y,b.y);
    const auto z=nearby_delta(a.z,b.z);
    if(!x||!y||!z)return std::nullopt;
    return CosmicPosition{*x,*y,*z};
}
} // namespace galaxy::galactic
