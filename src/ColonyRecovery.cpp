#include "galaxy/Simulation.hpp"
#include <set>
namespace galaxy {
ColonyLifeRecovery Simulation::assess_life_recovery(EntityId ship) const {
    ColonyLifeRecovery a{};
    if(!colony_ship(ship) || !life_support(ship))return a;
    std::set<EntityId> sites;
    for(const auto& [id,f]:farms_)if(f.source_ship==ship && f.workers)sites.insert(id);
    for(const auto& [id,h]:habitats_)if(h.source_ship==ship && h.residents)sites.insert(id);
    a.occupied_sites=static_cast<std::int64_t>(sites.size());
    bool all_covered=!sites.empty();
    for(const auto id:sites){
        const auto it=reclaimers_.find(id);
        if(it==reclaimers_.end() || it->second.source_ship!=ship){all_covered=false;continue;}
        const auto& r=it->second;++a.equipped_sites;
        a.sludge_kg+=r.sludge_kg;a.carbon_residue_kg+=r.carbon_residue_kg;
        if(r.cycles<3 || r.consecutive_powered_cycles<3){all_covered=false;continue;}
        ++a.measured_sites;
        for(int i=0;i<3;++i){
            a.water_recovered_last3_kg+=r.water_recent[i];
            a.oxygen_generated_last3_kg+=r.oxygen_recent[i];
            a.water_demand_last3_kg+=r.water_demand_recent[i];
            a.oxygen_demand_last3_kg+=r.oxygen_demand_recent[i];
            if(!r.water_demand_recent[i] || !r.oxygen_demand_recent[i] ||
               r.water_recent[i]<r.water_demand_recent[i] ||
               r.oxygen_recent[i]<r.oxygen_demand_recent[i])all_covered=false;
        }
    }
    a.local_recycling_coverage=all_covered;
    // These processes run on finite local waste and produce losses. Even high
    // measured coverage must not imply unlimited replenishment or no imports.
    a.independent_renewal_proven=false;
    return a;
}
} // namespace galaxy
