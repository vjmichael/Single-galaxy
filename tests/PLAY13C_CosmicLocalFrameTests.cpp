// PLAY-13C.1 standalone C++ assertions (no modifications to legacy physics).
#include "galaxy/CosmicLocalFrame.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
using namespace galaxy;
int main() {
    const LocalSpaceFrame frame{17, (std::uint64_t(17)<<32)|3,
        {200000000000000LL,-300000000000000LL,900000000000000LL}};
    const LocalOffset3 d{123456789,-999999999999LL,500000000000LL};
    auto cosmic=local_to_cosmic(frame,d);
    assert(cosmic.has_value());
    auto back=cosmic_to_local(frame,*cosmic);
    assert(back.has_value() && back->x_mm==d.x_mm &&
           back->y_mm==d.y_mm && back->z_mm==d.z_mm);
    assert(!local_to_legacy_plane(d).has_value());
    assert(local_to_legacy_plane({50,-50,0}).has_value());
    // Boundary is inclusive; out-of-frame positions are not silently clipped.
    assert(local_to_cosmic(frame,{kLocalFrameLimitMm,-kLocalFrameLimitMm,0}));
    assert(!local_to_cosmic(frame,{kLocalFrameLimitMm+1,0,0}));
    assert(!cosmic_to_local(frame,{frame.origin.x_mm+kLocalFrameLimitMm+1,
                                  frame.origin.y_mm,frame.origin.z_mm}));
    // Signed overflow/underflow is explicitly rejected, including INT64_MIN.
    constexpr auto hi=std::numeric_limits<std::int64_t>::max();
    constexpr auto lo=std::numeric_limits<std::int64_t>::min();
    assert(!checked_cosmic_add(hi,1));
    assert(!checked_cosmic_add(lo,-1));
    assert(!checked_cosmic_sub(hi,-1));
    assert(!checked_cosmic_sub(lo,1));
    assert(!cosmic_to_local({1,1,{lo,0,0}},{hi,0,0}));
    assert(!local_to_cosmic({1,1,{hi,0,0}},{1,0,0}));
    // Same local coordinates must resolve to different cosmos in different frames.
    auto other=local_to_cosmic({18,(std::uint64_t(18)<<32)|3,
                               {300000000000000LL,0,0}},{50,0,0});
    assert(other && other->x_mm!=cosmic->x_mm);
}
