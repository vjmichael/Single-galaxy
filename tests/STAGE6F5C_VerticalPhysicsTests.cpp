#include "galaxy/PlanetaryVerticalPhysics.hpp"
#include <cassert>
using namespace galaxy::planetary::vertical;
int main(){
    Source s{};
    s.ground_height_mm=1000;
    assert(!advance({1000,0,1000,0,true},s,100,false));
    s.gravity_mm_s2=9800;
    assert(!advance({1000,0,1000,0,true},s,0,false));
    assert(!advance({1500,0,1000,0,true},s,100,false));
    // Verified native ground, bounded jump, airborne gravity, no double jump.
    s.breathable_atmosphere=true;
    State p{1000,0,1000,0,true};
    auto start=advance(p,s,100,true);
    assert(start && !start->next.grounded && start->next.height_mm>1000);
    p=start->next;
    bool landed=false;
    for(int i=0;i<50;++i){
        auto f=advance(p,s,100,true);
        assert(f);
        p=f->next;
        if(f->impact){landed=true;break;}
    }
    assert(landed&&p.grounded&&p.height_mm==1000);
    // Verified original nonbreathable source drains only supplied oxygen.
    s.breathable_atmosphere=false;
    s.verified_oxygen_supply=true;
    s.oxygen_use_ml_s=30;
    s.hazard=galaxy::planetary::PhysicalHazard::Cold;
    auto cold=advance(p,s,100,false);
    assert(cold&&cold->next.oxygen_ml==997&&cold->next.exposure_ms==100);
    assert(cold->thermal_unprotected&&!cold->oxygen_exhausted);
    s.verified_thermal_protection=true;
    auto shielded=advance(cold->next,s,100,false);
    assert(shielded&&shielded->next.exposure_ms==100);
    s.verified_oxygen_supply=false;
    assert(!advance(p,s,100,false)); // absent proof cannot fake breathable air
    // Fall impact is only an observation: health authority still pending.
    s.verified_oxygen_supply=true;
    s.breathable_atmosphere=true;
    s.hazard=galaxy::planetary::PhysicalHazard::None;
    auto impact=advance({2000,-8000,500,0,false},s,150,false);
    assert(impact&&impact->impact&&impact->impact_speed_mm_s>=kHardImpactMmS);
}
