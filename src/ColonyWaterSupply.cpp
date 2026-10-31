#include "galaxy/Simulation.hpp"
#include <set>
namespace galaxy {
ColonyWaterSupply Simulation::assess_water_supply(EntityId ship) const {
    ColonyWaterSupply a{};
    if(!colony_ship(ship) || !life_support(ship))return a;
    std::set<EntityId> sites;
    for(const auto& [id,f]:farms_)if(f.source_ship==ship && f.workers)sites.insert(id);
    for(const auto& [id,h]:habitats_)if(h.source_ship==ship && h.residents)sites.insert(id);
    a.occupied_sites=static_cast<std::int64_t>(sites.size());
    bool coverage=!sites.empty();
    for(const auto id:sites){
        const auto it=waterworks_.find(id);
        if(it==waterworks_.end() || it->second.source_ship!=ship){coverage=false;continue;}
        ++a.equipped_sites;
        const auto& p=it->second;
        if(p.cycles<3 || p.consecutive_service_cycles<3){coverage=false;continue;}
        ++a.measured_sites;
        for(int j=0;j<3;++j){
            a.potable_output_last3_kg+=p.recent_output[j];
            a.water_demand_last3_kg+=p.recent_demand[j];
            if(!p.recent_demand[j] || p.recent_output[j]<p.recent_demand[j])coverage=false;
        }
    }
    a.short_window_coverage=coverage;
    a.indefinitely_renewable=false; // finite mineral deposits and imports
    return a;
}
} // namespace galaxy
