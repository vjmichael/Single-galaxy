#pragma once
// SPACE-04C: independent authoritative high-order physical location type.
// Simulation owns these values; a local render projection can never replace them.
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
namespace galaxy::galactic {
constexpr std::int64_t kSectorEdgeMm=1000000000000000LL; // 1e15mm (~6.68 AU)
constexpr std::int64_t kCellMm=100000000LL; // same 100km cell edge
static_assert(kSectorEdgeMm%kCellMm==0,"cell must divide sector exactly");
constexpr std::int64_t kCellsPerSector=kSectorEdgeMm/kCellMm;
struct Axis {
    std::int64_t sector=0;
    std::int64_t millimeters=0; // always 0..kSectorEdgeMm-1
    bool valid()const {
        return millimeters>=0 && millimeters<kSectorEdgeMm;
    }
    bool operator==(const Axis& r) const {
        return sector==r.sector && millimeters==r.millimeters;
    }
};
inline std::optional<std::int64_t> checked_add(std::int64_t a,
                                               std::int64_t b){
    constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    constexpr auto minimum=std::numeric_limits<std::int64_t>::min();
    if(b>0 && a>maximum-b)return std::nullopt;
    if(b<0 && a<minimum-b)return std::nullopt;
    return a+b;
}
inline std::optional<Axis> normalize(Axis initial,std::int64_t delta_mm){
    if(!initial.valid())return std::nullopt;
    // No sector*edge multiplication: that can overflow for INT64_MIN.
    auto quotient=delta_mm/kSectorEdgeMm;
    auto remainder=delta_mm%kSectorEdgeMm;
    if(remainder<0){--quotient;remainder+=kSectorEdgeMm;}
    // initial.mm + remainder lies in [0,2*edge); int64 safe.
    const auto total=initial.millimeters+remainder;
    const auto carry=total>=kSectorEdgeMm?1:0;
    // Combine the normalized sector delta before checking overflow.
    // At INT64_MIN, a negative quotient and positive carry can cancel;
    // rejecting the intermediate sum would incorrectly forbid legal motion.
    const auto shifted=checked_add(initial.sector,quotient+carry);
    if(!shifted)return std::nullopt;
    return Axis{*shifted,total-carry*kSectorEdgeMm};
}
struct Point {
    Axis x{},y{},z{};
    bool valid()const{return x.valid()&&y.valid()&&z.valid();}
    bool operator==(const Point& r)const{
        return x==r.x && y==r.y && z==r.z;
    }
};
struct Velocity {
    std::int64_t x_mm_per_tick=0,y_mm_per_tick=0,z_mm_per_tick=0;
};
inline std::optional<Point> advance(Point p,Velocity v){
    const auto x=normalize(p.x,v.x_mm_per_tick);
    const auto y=normalize(p.y,v.y_mm_per_tick);
    const auto z=normalize(p.z,v.z_mm_per_tick);
    if(!x||!y||!z)return std::nullopt;
    return Point{*x,*y,*z};
}
} // namespace galaxy::galactic
