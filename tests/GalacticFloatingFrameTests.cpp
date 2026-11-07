#include "galaxy/GalacticFloatingFrame.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
using namespace galaxy::galactic;
int main(){
    const Point origin{{946073000,kSectorEdgeMm-25},
                       {-946073000,4},{17,kSectorEdgeMm-30}};
    FloatingFrame frame{origin,1000000000};
    assert(frame.valid());
    const auto moved=advance(origin,Velocity{70,-90,100});
    assert(moved && moved->x.sector==946073001 &&
           moved->y.sector==-946073001 && moved->z.sector==18);
    const auto local=local_from_galactic(frame,*moved);
    assert((local && *local==Local3{70,-90,100}));
    assert(galactic_from_local(frame,*local)==moved);
    const auto rebased=rebase(frame,*moved);
    assert((rebased && local_from_galactic(*rebased,*moved)==Local3{0,0,0}));
    assert((galactic_from_local(*rebased,Local3{-70,90,-100})==origin));
    // A distant target is outside a short-range frame, NOT absent from
    // original space, which still retains its exact galactic coordinates.
    const auto far=advance(*moved,Velocity{2000000000LL,0,0});
    assert(far && !local_from_galactic(frame,*far));
    const auto far_frame=rebase(frame,*far);
    assert(far_frame && locally_visible(*far_frame,*far));
    assert(!locally_visible(frame,*far));
    // Malformed and overflowed sectors fail rather than wrapping.
    assert(!local_from_galactic(FloatingFrame{origin,0},origin));
    const Point at_max{{std::numeric_limits<std::int64_t>::max(),
                        kSectorEdgeMm-1},{0,0},{0,0}};
    assert(!advance(at_max,Velocity{1,0,0}));
    assert((!galactic_from_local(frame,Local3{1000000001LL,0,0})));
    std::cout<<"PASS SPACE-04B floating local frame geometry across x/y/z sectors; "
                "no galaxy motion clamp or duplicated matter\n";
}
