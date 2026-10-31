#include "galaxy/Simulation.hpp"
#include <algorithm>

namespace galaxy {
const char* policy_domain_name(PolicyDomain domain){
    switch(domain){
      case PolicyDomain::Maintenance:return "maintenance";
      case PolicyDomain::Healthcare:return "healthcare";
      case PolicyDomain::Community:return "community";
      case PolicyDomain::WaterSupply:return "water";
    }
    return "invalid";
}
std::int64_t Simulation::assigned_policy_inspectors(EntityId site) const{
    const auto it=policy_sites_.find(site);
    if(it==policy_sites_.end())return 0;
    std::int64_t total=0;
    for(const auto& p:it->second.domains)total+=p.inspectors;
    return total;
}
std::vector<PolicyAudit> Simulation::assess_policies(EntityId ship) const{
    std::vector<PolicyAudit> rows;
    if(!colony_ship(ship))return rows;
    for(const auto& [site,p]:policy_sites_){
        if(p.source_ship!=ship)continue;
        for(int d=0;d<4;++d){
            PolicyAudit r{};
            r.site=site;r.domain=static_cast<PolicyDomain>(d);
            r.declared=p.domains[d].declared;
            r.assigned_inspectors=p.domains[d].inspectors;
            r.required_inspectors=static_cast<int>(r.declared);
            r.oversight_staffed=r.assigned_inspectors>=r.required_inspectors;
            if(d==0){
                auto it=utilities_.find(site);
                r.operational_evidence=it!=utilities_.end() && it->second.source_ship==ship &&
                    it->second.technicians>0 && it->second.delegated_maintenance &&
                    it->second.spare_modules>0 && it->second.condition>0 &&
                    it->second.consecutive_powered_cycles>=3;
            }else if(d==1){
                auto it=clinics_.find(site);
                r.operational_evidence=it!=clinics_.end() && it->second.source_ship==ship &&
                    it->second.medics>0 && it->second.water_kg>0 &&
                    it->second.substrate_kg>0 && it->second.active_cases==0 &&
                    it->second.consecutive_care_cycles>=3;
            }else if(d==2){
                auto it=communities_.find(site);
                r.operational_evidence=it!=communities_.end() && it->second.source_ship==ship &&
                    it->second.attendants>0 && it->second.food_kg>0 &&
                    it->second.water_kg>0 && it->second.consecutive_service_cycles>=3;
            }else{
                auto it=waterworks_.find(site);
                const auto u=utilities_.find(site);
                r.operational_evidence=it!=waterworks_.end() && it->second.source_ship==ship &&
                    u!=utilities_.end() && u->second.powered &&
                    it->second.raw_feed_kg>0 && it->second.consecutive_service_cycles>=3;
            }
            // None means the player has not requested this assurance program.
            // Never treat "no inspectors required" as proof that a service works.
            r.assurance_supported=r.required_inspectors>0 &&
                r.oversight_staffed && r.operational_evidence;
            rows.push_back(r);
        }
    }
    return rows;
}
} // namespace galaxy
