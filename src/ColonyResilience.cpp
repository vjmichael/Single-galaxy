#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace galaxy {
namespace {
std::int64_t stored_cycles(const ColonyLifeSupport& life, std::int64_t consumers) {
    if(consumers<=0)return 0;
    return std::min({life.food_kg,life.water_kg,life.oxygen_kg})/consumers;
}
}
ColonyResilience Simulation::assess_resilience(EntityId ship) const {
    ColonyResilience r{};
    const auto* colony=colony_ship(ship);
    const auto* onboard=life_support(ship);
    if(!colony || !onboard)return r;
    constexpr std::int64_t kBufferCycles=6; // diagnostic threshold, not a policy lock
    r.onboard_reserve_cycles=stored_cycles(*onboard,colony->crew);
    if(r.onboard_reserve_cycles<kBufferCycles)r.blocker_mask|=CrewReservesLow;
    if(onboard->consecutive_supplied_cycles<3)r.blocker_mask|=RecentShortage;
    std::set<EntityId> sites;
    for(const auto& [id,f]:farms_)if(f.source_ship==ship && f.workers>0)sites.insert(id);
    for(const auto& [id,h]:habitats_)if(h.source_ship==ship && h.residents>0)sites.insert(id);
    r.occupied_sites=static_cast<std::int64_t>(sites.size());
    if(!sites.empty())r.blocker_mask&=~NoOccupiedSettlement;
    r.lowest_site_reserve_cycles=sites.empty()?0:std::numeric_limits<std::int64_t>::max();
    for(auto id:sites){
        const auto fit=farms_.find(id);
        const auto hit=habitats_.find(id);
        const auto uit=utilities_.find(id);
        const auto ci=communities_.find(id);
        const auto clinic=clinics_.find(id);
        const auto workers=fit!=farms_.end()&&fit->second.source_ship==ship?fit->second.workers:0;
        const auto residents=hit!=habitats_.end()&&hit->second.source_ship==ship?hit->second.residents:0;
        std::int64_t site_cycles=std::numeric_limits<std::int64_t>::max();
        if(workers>0){
            site_cycles=std::min(site_cycles,stored_cycles(fit->second.life,workers));
            if(fit->second.life.consecutive_supplied_cycles<3)r.blocker_mask|=RecentShortage;
        }
        if(residents>0){
            site_cycles=std::min(site_cycles,stored_cycles(hit->second.life,residents));
            if(hit->second.life.consecutive_supplied_cycles<3 ||
               hit->second.consecutive_comfort_cycles<3)r.blocker_mask|=RecentShortage;
        }
        r.lowest_site_reserve_cycles=std::min(r.lowest_site_reserve_cycles,site_cycles);
        if(site_cycles>=kBufferCycles)++r.sites_with_reserves;
        else r.blocker_mask|=SiteReservesLow;
        if(uit==utilities_.end() || uit->second.source_ship!=ship ||
           !uit->second.powered || uit->second.consecutive_powered_cycles<3)
            r.blocker_mask|=UnreliablePower;
        else ++r.serviced_sites;
        if(uit==utilities_.end() || uit->second.source_ship!=ship ||
           !uit->second.delegated_maintenance || uit->second.spare_modules<2 ||
           uit->second.condition==0 || residents==0 ||
           uit->second.technicians==0 || uit->second.technicians>residents ||
           hit->second.life.consecutive_supplied_cycles<3)
            r.blocker_mask|=MaintenanceUnsupported;
        else ++r.maintained_sites;
        if(uit!=utilities_.end() && uit->second.source_ship==ship){
            r.spare_modules+=uit->second.spare_modules;
            // A staffing proxy: at most half of housed residents may have
            // a standing maintenance assignment. Not actual work shifts.
            if(uit->second.technicians*2>residents)r.blocker_mask|=StaffOvercommitted;
        }
        const auto assigned=uit!=utilities_.end() && uit->second.source_ship==ship
            ?uit->second.technicians:0;
        const auto attendants=(ci!=communities_.end() && ci->second.source_ship==ship)
            ?ci->second.attendants:0;
        const auto medics=clinic!=clinics_.end() && clinic->second.source_ship==ship
            ?clinic->second.medics:0;
        const auto inspectors=assigned_policy_inspectors(id);
        if(attendants+assigned+medics+inspectors>residents)r.blocker_mask|=StaffOvercommitted;
        r.nonmaintenance_residents+=std::max<std::int64_t>(0,residents-assigned-attendants-medics-inspectors);
        if(ci!=communities_.end() && ci->second.source_ship==ship &&
           ci->second.consecutive_service_cycles>=3 && attendants>0 &&
           attendants*4>=residents+workers)++r.community_supported_sites;
        else r.blocker_mask|=CommunityServicesMissing;
        if(clinic!=clinics_.end() && clinic->second.source_ship==ship){
            r.active_clinical_cases+=clinic->second.active_cases;
            r.treated_clinical_cases+=static_cast<std::int64_t>(clinic->second.treated_cases);
            if(clinic->second.consecutive_care_cycles>=3 && clinic->second.active_cases==0 &&
               clinic->second.water_kg>0 && clinic->second.substrate_kg>0 && medics>0)
                ++r.clinic_supported_sites;
        }
    }
    if(r.occupied_sites>0 && r.community_supported_sites==r.occupied_sites){
        r.restorative_leisure_proven=true;
        r.blocker_mask&=~CommunityServicesMissing;
    }
    r.basic_clinic_access_proven=r.occupied_sites>0 &&
        r.clinic_supported_sites==r.occupied_sites && r.active_clinical_cases==0;
    // This basic first-aid proxy does NOT remove HealthRestUnmodeled:
    // treatment is aggregate and rest, medicines and individual health are not modeled.
    // Food must cover every current colonist in EACH recent needs interval,
    // with a 25% production buffer. Initial stockpiles cannot fake a surplus.
    const auto viability=assess_colony(ship);
    r.food_buffer_proven=viability.has_operational_farm && viability.crew_total>0;
    const auto required=viability.crew_total+(viability.crew_total+3)/4;
    for(int period=0;period<3;++period){
        std::int64_t produced=0;
        for(const auto& [id,f]:farms_){(void)id;
            if(f.source_ship==ship)produced+=f.recent_food_kg[period];
        }
        if(produced<required)r.food_buffer_proven=false;
    }
    if(!r.food_buffer_proven)r.blocker_mask|=FoodBufferUnproven;
    // Local, staffed operational continuity is a candidate for future
    // delegated policy, NEVER a grant of server-offline simulation or growth.
    constexpr std::uint32_t operational_blockers=(1u<<8)-1;
    r.operational_delegation_candidate=(r.blocker_mask&operational_blockers)==0;
    // Water recovery history is cumulative (not a per-cycle renewal proof),
    // healthcare, water-renewal proof and governance remain unverified.
    // Keep the last 3 blockers pending until real capabilities are verified.
    r.autonomous_growth_ready=false;
    return r;
}
} // namespace galaxy
