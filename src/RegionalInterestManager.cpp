#include "galaxy/RegionalInterestManager.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <algorithm>
namespace galaxy::regional_events {
namespace {
void valid(PositionMeters p){
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
        throw std::invalid_argument("interest location must have finite coordinates");
}
}
RegionalInterestManager::RegionalInterestManager(InterestPolicy p):policy_(p){
    if(!std::isfinite(p.activate_m)||!std::isfinite(p.deactivate_m)||
       p.activate_m<0||p.deactivate_m<=p.activate_m)
        throw std::invalid_argument("interest requires finite positive hysteresis");
}
RegionMode RegionalInterestManager::mode(const std::string& id) const {
    const auto it=regions_.find(id);
    return it==regions_.end()?RegionMode::Cold:it->second.mode;
}
InterestDecision RegionalInterestManager::consider(const RegionBounds& region,
  const std::vector<PositionMeters>& observers,std::uint64_t tick){
    if(region.id.empty()||!std::isfinite(region.radius_m)||region.radius_m<0)
        throw std::invalid_argument("invalid region bounds");
    valid(region.center);
    const auto old=regions_.find(region.id);
    if(old!=regions_.end()&&old->second.initialized&&tick<old->second.changed_tick)
        throw std::invalid_argument("interest clock cannot move backwards");
    double nearest=std::numeric_limits<double>::infinity();
    for(auto p:observers){
        valid(p);
        const double d=std::hypot(p.x-region.center.x,
                                  p.y-region.center.y,p.z-region.center.z);
        // Distance to the edge of the region, not center. Large region radii
        // therefore do not cause detailed mode to unload while player is inside.
        const double surface=std::max(0.0,d-region.radius_m);
        nearest=std::min(nearest,surface);
    }
    const RegionMode previous=mode(region.id);
    RegionMode desired=previous;
    if(previous==RegionMode::Cold&&nearest<=policy_.activate_m)
        desired=RegionMode::Detailed;
    if(previous==RegionMode::Detailed&&nearest>=policy_.deactivate_m)
        desired=RegionMode::Cold;
    if(old!=regions_.end()&&old->second.initialized && desired!=previous){
        if(tick-old->second.changed_tick<policy_.minimum_dwell_ticks)
            desired=previous;
    }
    return {previous,desired,nearest,desired!=previous};
}
void RegionalInterestManager::acknowledge(const std::string& id,RegionMode state,
                                          std::uint64_t tick){
    if(id.empty())throw std::invalid_argument("empty region id");
    auto& s=regions_[id];
    if(s.initialized&&tick<s.changed_tick)
        throw std::invalid_argument("interest ack time rollback");
    s.mode=state;s.changed_tick=tick;s.initialized=true;
}
} // namespace galaxy::regional_events
