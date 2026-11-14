#pragma once
// FP3D-ENV-01H: true native station blockers for explicitly opted-in v125
// worlds. 340mm-radius avatar against pictured 3D box/cylinder obstacles.
// Older native save versions retain their earlier movement rules.
#include <cstdint>
namespace galaxy::station_walk {
constexpr std::int64_t kRadius=340;
inline bool box(std::int64_t x,std::int64_t z,
                std::int64_t cx,std::int64_t cz,
                std::int64_t half_x,std::int64_t half_z) noexcept {
    return x>=cx-half_x-kRadius && x<=cx+half_x+kRadius &&
           z>=cz-half_z-kRadius && z<=cz+half_z+kRadius;
}
inline bool clear(std::int64_t x,std::int64_t z) noexcept {
    if(x< -8000||x>8000||z< -8000||z>8000)return false;
    // Two overlapping refinery-area blocks: terminal and cargo crate.
    if(box(x,z,3000,0,325,325)||box(x,z,3000,0,450,450))return false;
    if(box(x,z,-2300,-2500,650,650))return false; // crate #01
    const auto dz=z+5000;
    if(x*x+dz*dz <= 720*720)return false; // 0.38m native NPC marker
    if(box(x,z,-5000,-7000,200,14000)||
       box(x,z,5000,-7000,200,14000))return false; // hallway walls
    return true;
}
} // namespace galaxy::station_walk
