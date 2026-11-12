#pragma once
// S6-F5-B: deterministic body-relative geodetic navigation over the ORIGINAL
// PGF watershed's 36 x 72 indexed source elevations. This file has no
// inventory, player registry, simulation worker, renderer, or transport writer.
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <limits>
namespace galaxy::planetary::continuous_surface {
constexpr std::int64_t kDegree=1000000000000LL; // signed picodegrees
constexpr std::int64_t kCellSpan=5*kDegree;
constexpr std::int64_t kPole=90*kDegree;
constexpr std::int64_t kHalfTurn=180*kDegree;
constexpr std::int64_t kFullTurn=360*kDegree;
constexpr std::uint64_t kPdegPerRadian=57295779513082ULL; // nearest integer 180e12/pi
constexpr std::int64_t kWeight=1000000;
constexpr std::int64_t kHeightMmPerSourceUnit=1000;
constexpr std::int64_t kMaxNativeStepRiseMm=250;
constexpr std::uint64_t kMinWalkableBodyRadiusMm=100000000ULL; // 100km
constexpr std::uint64_t kMaxWalkableBodyRadiusMm=1000000000000000ULL;
// Fixed integer table. No libm, long double, renderer transform or platform
// trigonometric rounding is involved in committed movement.
constexpr std::array<std::uint32_t,36> kCosCellCenterPpm{{
    43619,130526,216440,300706,382683,461749,537300,608761,675590,
    737277,793353,843391,887011,923880,953717,976296,991445,999048,
    999048,991445,976296,953717,923880,887011,843391,793353,737277,
    675590,608761,537300,461749,382683,300706,216440,130526,43619
}};
struct GeoPoint {
    std::int64_t latitude_pdeg=0,longitude_pdeg=0;
};
struct SourceCell {
    std::uint16_t latitude=0,longitude=0;
    constexpr bool operator==(const SourceCell& x) const noexcept {
        return latitude==x.latitude&&longitude==x.longitude;
    }
};
struct Sample {
    std::int32_t elevation=0;
    bool ocean=false;
};
struct Ground {
    GeoPoint point{};
    SourceCell cell{};
    std::int64_t height_mm=0;
    bool ocean=false;
};
struct Weights {
    SourceCell a{},b{},c{},d{}; // a=(lat0,lon0), b=(lat1,lon0), c=(lat0,lon1), d=(lat1,lon1)
    std::int64_t north=0,east=0; // 0...1000000 fractional cell
};
inline std::int64_t positive_mod(std::int64_t x,std::int64_t m) noexcept {
    const auto r=x%m;
    return r<0?r+m:r;
}
inline std::int64_t normalize_lon(std::int64_t lon) noexcept {
    return positive_mod(lon+kHalfTurn,kFullTurn)-kHalfTurn;
}
inline bool canonical(GeoPoint p) noexcept {
    return p.latitude_pdeg>=-kPole&&p.latitude_pdeg<=kPole&&
           p.longitude_pdeg>=-kHalfTurn&&p.longitude_pdeg<kHalfTurn;
}
inline std::optional<GeoPoint> cell_center(SourceCell c) {
    if(c.latitude>=36||c.longitude>=72)return std::nullopt;
    return GeoPoint{
        (-175LL+10LL*c.latitude)*(kDegree/2),
        (-355LL+10LL*c.longitude)*(kDegree/2)};
}
inline SourceCell source_region(GeoPoint p) noexcept {
    const auto lat=std::clamp<std::int64_t>(
        (p.latitude_pdeg+kPole)/kCellSpan,0,35);
    const auto lon=positive_mod(p.longitude_pdeg+kHalfTurn,kFullTurn)/kCellSpan;
    return {static_cast<std::uint16_t>(lat),
            static_cast<std::uint16_t>(lon)};
}
inline Weights source_triangles(GeoPoint p) noexcept {
    // Cell-center to cell-center interpolation is the SAME diagonal as
    // Stage6PlanetTiles: [a,b,c] and [b,d,c]. Source pixels are NOT ocean kg.
    const auto lat_delta=p.latitude_pdeg+175*(kDegree/2);
    std::int64_t lat0=0,north=0;
    if(lat_delta<=0)lat0=0;
    else if(lat_delta>=35*kCellSpan)lat0=35;
    else {
        lat0=lat_delta/kCellSpan;
        north=(lat_delta%kCellSpan)/(kCellSpan/kWeight);
    }
    const auto lon_offset=positive_mod(
        p.longitude_pdeg+355*(kDegree/2),kFullTurn);
    const auto lon0=lon_offset/kCellSpan;
    const auto east=(lon_offset%kCellSpan)/(kCellSpan/kWeight);
    const auto lat1=std::min<std::int64_t>(35,lat0+1);
    const auto lon1=(lon0+1)%72;
    return {
       {static_cast<std::uint16_t>(lat0),static_cast<std::uint16_t>(lon0)},
       {static_cast<std::uint16_t>(lat1),static_cast<std::uint16_t>(lon0)},
       {static_cast<std::uint16_t>(lat0),static_cast<std::uint16_t>(lon1)},
       {static_cast<std::uint16_t>(lat1),static_cast<std::uint16_t>(lon1)},
       north,east};
}
inline std::uint32_t cosine_ppm(std::int64_t lat) noexcept {
    // Linear source-ring interpolation; smooth across every 5-degree cell
    // boundary. At poles a conservative 1/1000 scale avoids singularity.
    const auto shifted=lat+175*(kDegree/2);
    if(shifted<=0||shifted>=35*kCellSpan)
        return std::max<std::uint32_t>(1000,kCosCellCenterPpm[
            shifted<=0?0:35]);
    const auto index=shifted/kCellSpan;
    const auto fraction=(shifted%kCellSpan)/(kCellSpan/kWeight);
    const auto left=std::int64_t(kCosCellCenterPpm[index]);
    const auto right=std::int64_t(kCosCellCenterPpm[index+1]);
    const auto value=(left*(kWeight-fraction)+right*fraction)/kWeight;
    return std::max<std::uint32_t>(1000,static_cast<std::uint32_t>(value));
}
inline std::optional<GeoPoint> native_offset(GeoPoint from,
            std::int64_t east_mm,std::int64_t north_mm,
            std::uint64_t radius_mm) noexcept {
    // Also used once to migrate a v132 <=32-metre original landing-pad
    // odometer. It NEVER admits a client destination or a game command.
    if(!canonical(from)||
       radius_mm<kMinWalkableBodyRadiusMm||
       radius_mm>kMaxWalkableBodyRadiusMm||
       east_mm< -32000||east_mm>32000||
       north_mm< -32000||north_mm>32000)
        return std::nullopt;
    const auto absolute=[](std::int64_t x)->std::uint64_t {
        return static_cast<std::uint64_t>(x<0?-x:x);
    };
    // Max numerator 32,000*57,295,779,513,082 < 2^61. No 128-bit /
    // compiler-dependent intrinsics or platform floating-point rounding.
    const auto north_step=static_cast<std::int64_t>(
        (absolute(north_mm)*kPdegPerRadian+radius_mm/2)/radius_mm);
    const auto east_angle=(
        absolute(east_mm)*kPdegPerRadian+radius_mm/2)/radius_mm;
    const auto cosine=cosine_ppm(from.latitude_pdeg);
    const auto east_step=static_cast<std::int64_t>(
        (east_angle*1000000ULL+cosine/2)/cosine);
    auto lat=from.latitude_pdeg+(north_mm<0?-north_step:north_step);
    auto lon=from.longitude_pdeg+(east_mm<0?-east_step:east_step);
    if(lat>kPole){lat=2*kPole-lat;lon+=kHalfTurn;}
    if(lat< -kPole){lat= -2*kPole-lat;lon+=kHalfTurn;}
    const GeoPoint result{lat,normalize_lon(lon)};
    return canonical(result)?std::optional<GeoPoint>(result):std::nullopt;
}
inline std::optional<GeoPoint> native_step(GeoPoint from,
            std::int64_t east_mm,std::int64_t north_mm,
            std::uint64_t radius_mm) noexcept {
    if(east_mm< -300||east_mm>300||
       north_mm< -300||north_mm>300||
       (east_mm==0&&north_mm==0)||
       east_mm*east_mm+north_mm*north_mm>90000)
        return std::nullopt;
    return native_offset(from,east_mm,north_mm,radius_mm);
}
inline std::uint64_t wrapped_longitude_gap(GeoPoint a,GeoPoint b) noexcept {
    const auto delta=static_cast<std::uint64_t>(
        a.longitude_pdeg>b.longitude_pdeg?
        a.longitude_pdeg-b.longitude_pdeg:
        b.longitude_pdeg-a.longitude_pdeg);
    return std::min<std::uint64_t>(delta,kFullTurn-delta);
}
inline std::uint64_t angular_proximity_score(GeoPoint a,
                                                 GeoPoint b) noexcept {
    // Monotone local contact/retreat metric, not a global orbital distance.
    // Split before multiplication to avoid overflow on opposite meridians.
    const auto lat=static_cast<std::uint64_t>(
        a.latitude_pdeg>b.latitude_pdeg?
        a.latitude_pdeg-b.latitude_pdeg:b.latitude_pdeg-a.latitude_pdeg);
    const auto lon=wrapped_longitude_gap(a,b);
    const auto cos=cosine_ppm((a.latitude_pdeg+b.latitude_pdeg)/2);
    const auto projected=(lon/1000000ULL)*cos+
                         ((lon%1000000ULL)*cos)/1000000ULL;
    return lat+projected;
}
inline bool within_radius(GeoPoint a,GeoPoint b,
                          std::uint64_t radius_mm,
                          std::uint64_t limit_mm) noexcept {
    if(!canonical(a)||!canonical(b)||
       radius_mm<kMinWalkableBodyRadiusMm||
       radius_mm>kMaxWalkableBodyRadiusMm||limit_mm>5000)return false;
    const auto allowed=(limit_mm*kPdegPerRadian+radius_mm-1)/radius_mm;
    const auto lat_diff=static_cast<std::uint64_t>(
        a.latitude_pdeg>b.latitude_pdeg?
        a.latitude_pdeg-b.latitude_pdeg:
        b.latitude_pdeg-a.latitude_pdeg);
    if(lat_diff>allowed)return false;
    const auto cos=cosine_ppm((a.latitude_pdeg+b.latitude_pdeg)/2);
    const auto lon_diff=wrapped_longitude_gap(a,b);
    if(lon_diff>(allowed*1000000ULL)/cos+1)return false;
    const auto lon_projected=(lon_diff/1000000ULL)*cos+
                             ((lon_diff%1000000ULL)*cos)/1000000ULL;
    return lat_diff*lat_diff+lon_projected*lon_projected<=allowed*allowed;
}
template<class Fetch>
std::optional<Ground> source_ground(GeoPoint p,Fetch&& source) {
    if(!canonical(p))return std::nullopt;
    const auto current=source_region(p);
    const auto c=source(current);
    if(!c)return std::nullopt;
    const auto w=source_triangles(p);
    const auto a=source(w.a),b=source(w.b),x=source(w.c),d=source(w.d);
    if(!a||!b||!x||!d)return std::nullopt;
    const auto elevation=[&](const Sample& v)->std::int64_t {
        // Avoid interpolating underwater bathymetry into a dry shoreline.
        return v.ocean?c->elevation:v.elevation;
    };
    const auto e00=elevation(*a),e10=elevation(*b),
               e01=elevation(*x),e11=elevation(*d);
    const auto north=w.north,east=w.east;
    std::int64_t height=0;
    if(north+east<=kWeight){
        height=(e00*(kWeight-north-east)+e10*north+e01*east);
    }else {
        height=(e10*(kWeight-east)+e01*(kWeight-north)+
                e11*(north+east-kWeight));
    }
    height=height*kHeightMmPerSourceUnit/kWeight;
    // The final 2.5 degrees blend to ONE existing original cap-ring source.
    // The pole itself is a physical point, not 72 distinct longitude peaks.
    const auto abs_lat=p.latitude_pdeg<0?-p.latitude_pdeg:p.latitude_pdeg;
    const auto ring_lat=175*(kDegree/2);
    if(abs_lat>ring_lat){
        const SourceCell cap{static_cast<std::uint16_t>(
            p.latitude_pdeg>0?35:0),0};
        const auto reference=source(cap);
        if(!reference)return std::nullopt;
        const auto fraction=std::min<std::int64_t>(
            kWeight,(abs_lat-ring_lat)/(kDegree*5/(2*kWeight)));
        height=(height*(kWeight-fraction)+
                std::int64_t(reference->elevation)*
                    kHeightMmPerSourceUnit*fraction)/kWeight;
    }
    return Ground{p,current,height,c->ocean};
}
} // namespace galaxy::planetary::continuous_surface
