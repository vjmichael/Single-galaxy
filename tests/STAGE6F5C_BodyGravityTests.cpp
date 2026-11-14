#include "galaxy/PlanetaryBodyGravity.hpp"
#include <cassert>
#include <cstdint>
using namespace galaxy::planetary::gravity;
int main(){
    // Original generated planet/moon source values (mm radius + kg/m^3
    // generator bulk density). Fixed point must be host/OS reproducible.
    const auto planet_mass=mass_exatons(6000000000ULL,5200ULL);
    const auto moon_mass=mass_exatons(1700000000ULL,3300ULL);
    assert(planet_mass&&moon_mass&&*planet_mass>*moon_mass);
    const auto p=surface_gravity_mm_s2(6000000000ULL,*planet_mass);
    const auto m=surface_gravity_mm_s2(1700000000ULL,*moon_mass);
    assert(p && m && *p>7000 && *p<13000 && *m>700 && *m<3000);
    assert(!mass_exatons(0,5200));
    assert(!mass_exatons(6000000000ULL,0));
    assert(!surface_gravity_mm_s2(6000000000ULL,0));
    assert(!surface_gravity_mm_s2(0,*planet_mass));
}
