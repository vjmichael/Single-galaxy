// SPACE-04E native arithmetic oracle: exact 128-bit reference is
// test-only (GCC/Clang), not used by shipping Windows physics.
#include "galaxy/GalacticPhysicalPosition.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using galaxy::galactic::Axis;
using galaxy::galactic::normalize;
using galaxy::galactic::kSectorEdgeMm;
__extension__ typedef __int128 i128;
static void check(Axis a,std::int64_t delta){
    const auto got=normalize(a,delta);
    const i128 full=static_cast<i128>(a.sector)*kSectorEdgeMm+
        a.millimeters+static_cast<i128>(delta);
    i128 q=full/kSectorEdgeMm,r=full%kSectorEdgeMm;
    if(r<0){--q;r+=kSectorEdgeMm;}
    const bool valid_sector=q>=std::numeric_limits<std::int64_t>::min() &&
        q<=std::numeric_limits<std::int64_t>::max();
    if(got.has_value()!=valid_sector)
        throw std::runtime_error("galactic sector overflow mismatch");
    if(got && (got->sector!=static_cast<std::int64_t>(q) ||
               got->millimeters!=r))
        throw std::runtime_error("galactic absolute axis mismatch");
}
int main(){
    std::mt19937_64 rng(20261107);
    for(int i=0;i<200000;++i){
        const auto sec=static_cast<std::int64_t>(rng());
        const auto mm=static_cast<std::int64_t>(rng()%kSectorEdgeMm);
        const auto shift=static_cast<std::int64_t>(rng());
        check({sec,mm},shift);
    }
    check({std::numeric_limits<std::int64_t>::max(),kSectorEdgeMm-1},1);
    check({std::numeric_limits<std::int64_t>::min(),0},-1);
    check({7,kSectorEdgeMm-100},250);
    check({7,0},-250);
    // Regression: quotient=-1 plus carry=1 is a legal net zero sector
    // move at INT64_MIN; intermediate overflow must not reject it.
    check({std::numeric_limits<std::int64_t>::min(),kSectorEdgeMm-1},-1);
    check({std::numeric_limits<std::int64_t>::min(),kSectorEdgeMm-1},-2);
    check({std::numeric_limits<std::int64_t>::min(),kSectorEdgeMm/2},-1);
    check({std::numeric_limits<std::int64_t>::min(),1},-1);
    check({std::numeric_limits<std::int64_t>::min(),kSectorEdgeMm-1},
          -kSectorEdgeMm+1);
    std::cout<<"PASS 200009 original high-order galactic axis normalizations "
                "against independent 128-bit oracle\n";
}
