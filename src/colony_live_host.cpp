// B7.0b: graphical colony-first local host. Authoritative C++/SQLite owns every
// action, world state, player identity, construction and geological inventory.
// stdout SGVIEW frames; stderr bounded COLONY telemetry for the local relay.
#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/OrbitalShuttleCommands.hpp"
#include "galaxy/Galaxy.hpp"
#include "galaxy/ClientContract.hpp"
#include "galaxy/FleetSelection.hpp"
#include "galaxy/ColonySafety.hpp"
#include "galaxy/NpcCyberSupervisor.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <poll.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace galaxy;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
namespace {
// All native world writers (including galaxy_shipping_status_bridge) use
// this exact sidecar lease. The integrated relay must not launch another writer.
struct WorldWriterLease {
#ifdef _WIN32
    HANDLE fd=INVALID_HANDLE_VALUE;
    explicit WorldWriterLease(const fs::path& world){
        const auto p=(fs::weakly_canonical(world).string()+".host.lock");
        fd=CreateFileA(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(fd==INVALID_HANDLE_VALUE)throw std::runtime_error("another game writer owns this world");
    }
    ~WorldWriterLease(){if(fd!=INVALID_HANDLE_VALUE)CloseHandle(fd);}
#else
    int fd=-1;
    explicit WorldWriterLease(const fs::path& world){
        const auto p=fs::weakly_canonical(world).string()+".host.lock";
        int flags=O_CREAT|O_RDWR;
#ifdef O_CLOEXEC
        flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        fd=::open(p.c_str(),flags,0600);
        struct stat st{};
        if(fd<0)throw std::runtime_error("cannot open world host lock");
        if(fstat(fd,&st)||!S_ISREG(st.st_mode)||flock(fd,LOCK_EX|LOCK_NB)){
            close(fd);fd=-1;throw std::runtime_error("another game writer owns this world");
        }
    }
    ~WorldWriterLease(){if(fd>=0){flock(fd,LOCK_UN);close(fd);}}
#endif
    WorldWriterLease(const WorldWriterLease&)=delete;
    WorldWriterLease& operator=(const WorldWriterLease&)=delete;
};
constexpr PlayerId owner=7;
constexpr std::int64_t sensor_mm=Simulation::kLocalSurveyRangeMm;
std::string pending;
std::vector<std::string> commands;
// UI-only correlation. The actual command/effect and recovery receipts are
// owned by the canonical PersistentWorld; this map never grants authority.
std::map<std::uint64_t,unsigned> awaiting_cyber_results;
void poll_input(){
    char data[1024];
    for(int n=0;n<4;++n){
        int size=0;
#ifdef _WIN32
        HANDLE h=GetStdHandle(STD_INPUT_HANDLE);DWORD avail=0,got=0;
        if(h==INVALID_HANDLE_VALUE || GetFileType(h)!=FILE_TYPE_PIPE ||
           !PeekNamedPipe(h,nullptr,0,nullptr,&avail,nullptr) || !avail)break;
        if(!ReadFile(h,data,static_cast<DWORD>(std::min<std::size_t>(sizeof(data),avail)),&got,nullptr))break;
        size=static_cast<int>(got);
#else
        struct pollfd p{STDIN_FILENO,POLLIN,0};
        if(::poll(&p,1,0)!=1 || !(p.revents&POLLIN))break;
        size=static_cast<int>(::read(STDIN_FILENO,data,sizeof(data)));
#endif
        if(size<=0)break;
        for(int k=0;k<size;++k){
            if(data[k]=='\n'){
                if(!pending.empty() && pending.size()<=192 && commands.size()<32)
                    commands.push_back(pending);
                pending.clear();
            }else if(data[k]>=32 && data[k]<=126 && pending.size()<193)pending+=data[k];
            else pending.clear();
        }
    }
}
std::vector<std::string> split(const std::string& s){
    std::istringstream in(s);std::vector<std::string> parts;std::string item;
    while(std::getline(in,item,'|'))parts.push_back(item);
    return parts;
}
std::int64_t integer(const std::string& s){
    // Body IDs use the entire positive signed-64 range, not the old
    // nine-digit parser limit. Every command still validates its own bounds.
    if(s.empty()||s.size()>20||s[0]=='+'||
       (s[0]=='-' && (s.size()==1||s[1]=='0'))||
       (s.size()>1 && s[0]=='0'))
        throw std::invalid_argument("bad number");
    for(std::size_t i=s[0]=='-'?1:0;i<s.size();++i)
        if(s[i]<'0'||s[i]>'9')throw std::invalid_argument("bad number");
    std::size_t consumed=0;
    const auto value=std::stoll(s,&consumed,10);
    if(consumed!=s.size())throw std::invalid_argument("bad number");
    return value;
}
void announce(const Simulation& w,const std::string& text){
    std::cerr<<"COLONY|1|"<<w.tick()<<'|'<<text<<'\n'<<std::flush;
}
// SHIP-STATUS-05: read-only cockpit from one authoritative physical ship.
// A frame displays actual fitted hardware and effective native module state,
// never a client-assumed equipment grant or a speculative attack outcome.
void cyber_status(const Simulation& w,EntityId ship) {
    const auto* hull=w.find(ship);
    if(!hull || hull->type!=EntityType::Ship || hull->owner_id!=owner)return;
    const auto until=hull->last_hack_tick &&
        w.tick()<hull->last_hack_tick+Simulation::kHackCooldownTicks?
        hull->last_hack_tick+Simulation::kHackCooldownTicks-w.tick():0;
    announce(w,"CYBER_STATUS|"+std::to_string(ship)+"|"+
        std::to_string(hull->fitting.intrusion_emitter()?1:0)+"|"+
        std::to_string(hull->fitting.cyber_firewall()?1:0)+"|"+
        std::to_string(until)+"|"+
        std::to_string(w.colony_ship(ship)?1:0));
    for(std::uint8_t slot=0;slot<2;++slot) {
        const auto hw=hull->fitting.hardpoints[slot];
        announce(w,"CYBER_MODULE|"+std::to_string(ship)+"|"+
            std::to_string(unsigned(slot))+"|"+std::to_string(unsigned(hw))+"|"+
            std::to_string(hull->hardpoint_requested_on[slot]?1:0)+"|"+
            std::to_string(w.hardpoint_effective_on(ship,slot)?1:0)+"|"+
            std::to_string(hull->hardpoint_isolated[slot]?1:0));
    }
    // Defensive read model is derived from the current original-world
    // effects, not the intended button state or an uncommitted attack.
    std::uint64_t remaining=0,source=0,exposed=0;
    for(const auto& effect:w.status_effects().records()){
        if(effect.target_ship!=ship ||
           w.tick()<effect.started_tick || w.tick()>=effect.expires_tick)
            continue;
        remaining=effect.expires_tick-w.tick();
        source=effect.source_ship;
        exposed=effect.targets.size();
        break; // the native kernel does not permit overlapping effects
    }
    announce(w,"CYBER_DEFENSE|"+std::to_string(ship)+"|"+
        std::to_string(remaining)+"|"+std::to_string(source)+"|"+
        std::to_string(exposed));
    bool scanner_online=false;
    for(std::uint8_t slot=0;slot<2;++slot)
        if(hull->fitting.hardpoints[slot]==1 &&
           w.hardpoint_effective_on(ship,slot))scanner_online=true;
    announce(w,"CYBER_SENSORS|"+std::to_string(ship)+"|"+
        std::to_string(hull->fitting.stealth_baffle()?1:0)+"|"+
        std::to_string(hull->fitting.counter_ecm_array()?1:0)+"|"+
        std::to_string(scanner_online?1:0));
    announce(w,"CYBER_TARGETS_RESET|"+std::to_string(ship));
    for(const auto& [id,target]:w.entities()){
        if(id==ship || target.type!=EntityType::Ship || target.training_drone ||
           !target.owner_id || target.owner_id==hull->owner_id ||
           target.system_id!=hull->system_id ||
           !w.same_local_frame(ship,id) ||
           target.dock_state!=DockState::InSpace)continue;
        const auto dx=double(target.position.x)-hull->position.x;
        const auto dy=double(target.position.y)-hull->position.y;
        const auto distance=std::hypot(dx,dy);
        if(distance>double(Simulation::kHackRangeMm) ||
           distance>double(ship_sensors::detection_range_mm(
               w,*hull,target)))continue;
        announce(w,"CYBER_TARGET|"+std::to_string(ship)+"|"+
            std::to_string(id)+"|"+std::to_string(
                static_cast<std::uint64_t>(distance)));
    }
}
void status(const Simulation& w,EntityId ship,bool replay_discoveries=false){
    const auto* s=w.find(ship);const auto* c=w.colony_ship(ship);
    if(!s || !c)throw std::runtime_error("colony ship missing");
    if(const auto f=w.ship_frames().find(ship);f!=w.ship_frames().end())
        announce(w,"LOCAL_FRAME|"+std::to_string(f->second.system)+"|"+
            std::to_string(f->second.anchor_body));
    if(const auto p=w.assess_expedition(s->owner_id,ship)) {
        // Read-only local opportunity and *existing* life-support buffer.
        // Each period is exactly one 20-tick consumption cycle, not ETA.
        announce(w,"EXPEDITION|"+std::to_string(p->body)+"|"+
            std::to_string(p->ore_tonnes)+"|"+
            std::to_string(p->nickel_iron_kg)+"|"+
            std::to_string(p->plagioclase_kg)+"|"+
            std::to_string(p->food_periods)+"|"+
            std::to_string(p->water_periods)+"|"+
            std::to_string(p->oxygen_periods)+"|"+
            std::to_string(unsigned(p->hazard))+"|"+
            std::to_string(p->local_supply_site?1:0));
    }
    if(const auto frame=w.ship_frames().find(ship);
       frame!=w.ship_frames().end()&&frame->second.anchor_body) {
        const auto reserve=w.oasis_reserves().find(frame->second.anchor_body);
        if(reserve!=w.oasis_reserves().end())
            announce(w,"OASIS_SOURCE|"+std::to_string(reserve->first)+"|"+
                std::to_string(reserve->second.water_kg)+"|"+
                std::to_string(reserve->second.nutrients_kg));
    }
    if(const auto rig=w.oasis_extractors().find(ship);
        rig!=w.oasis_extractors().end()) {
        const auto& e=rig->second;
        announce(w,"OASIS_EXTRACTOR|"+std::to_string(e.ship)+"|"+
            std::to_string(e.body)+"|"+std::to_string(e.energy_units)+"|"+
            std::to_string(unsigned(e.assigned_workers))+"|"+
            std::to_string(unsigned(e.condition)));
    }
    announce(w,"HEADING|"+std::to_string(s->heading_deg));
    announce(w,"PROBE_RESET");
    for(const auto& [id,p]:w.survey_probes()){
        if(p.owner!=s->owner_id || p.carrier!=ship)continue;
        if(p.drone_phase && p.system==s->system_id){
            // A geological drone keeps flying autonomously out of radio range,
            // but live status is available only while its carrier is within 10 km.
            const auto dx=p.position.x-s->position.x;
            const auto dy=p.position.y-s->position.y;
            const auto r=Simulation::kDroneControlRangeMm;
            if(dx<-r || dx>r || dy<-r || dy>r || dx*dx+dy*dy>r*r)continue;
        }else if(p.drone_phase && p.system!=s->system_id)continue;
        announce(w,"SURVEY_PROBE|"+std::to_string(id)+"|"+std::to_string(p.system)+"|"+
            std::to_string(p.position.x)+"|"+std::to_string(p.position.y)+"|"+
            std::to_string(p.drone_phase)+"|"+std::to_string(p.drone_target)+"|"+
            std::to_string(p.sample_ore)+"|"+std::to_string(p.taxonomy)+"|"+
            std::to_string(p.focus_site)+"|"+std::to_string(p.focus_waypoint.x)+"|"+
            std::to_string(p.focus_waypoint.y));
    }
    announce(w,"PROBE_STOCK|"+std::to_string(s->survey_probes_carried)+"|"+
        std::to_string(s->fitting.probe_launcher()?1:0));
    announce(w,"FITTING|"+std::to_string(s->fitting.exploration_scanner()?1:0)+"|"+
        std::to_string(s->fitting.hardpoints.size())+"|"+
        std::to_string(s->fitting.support.size())+"|"+
        std::to_string(s->fitting.subsystems.size())+"|"+
        std::to_string(s->fitting.rigging.size()));
    EntityId closest=0;
    std::int64_t closest_squared=Simulation::kProximitySafetyRangeMm*
        Simulation::kProximitySafetyRangeMm+1;
    for(const auto& [id,e]:w.entities()){
        if(id==ship || e.type!=EntityType::Ship || e.system_id!=s->system_id)continue;
        const auto dx=e.position.x-s->position.x,dy=e.position.y-s->position.y;
        const auto r=Simulation::kProximitySafetyRangeMm;
        if(dx < -r || dx > r || dy < -r || dy > r)continue;
        const auto square=dx*dx+dy*dy;
        if(square<closest_squared || (square==closest_squared && id<closest)){
            closest_squared=square;closest=id;
        }
    }
    announce(w,"PROXIMITY|"+std::to_string(closest)+"|"+
        std::to_string(closest?static_cast<std::uint64_t>(std::sqrt(
            static_cast<double>(closest_squared))):0));
    if(replay_discoveries)for(const auto& [key,d]:w.discoveries()){
        if(key.first!=s->owner_id)continue;
        announce(w,"DISCOVERY|"+std::to_string(d.site)+"|"+std::to_string(d.system)+"|"+
            std::to_string(d.location.x)+"|"+std::to_string(d.location.y)+"|"+
            std::to_string(d.first_tick)+"|"+std::to_string(d.geological?1:0)+"|"+
            std::to_string(d.survey_taxonomy));
    }
    for(const auto& [id,e]:w.entities()){
        if(e.type==EntityType::Wreck && e.owner_id==8 &&
           e.system_id==s->system_id && !w.colony_ship(id)){
            announce(w,"SALVAGE_WRECK|"+std::to_string(id)+"|"+std::to_string(e.cargo));
        }
    }
    announce(w,"COMBAT_SHIP|"+std::to_string(s->hull)+"|"+
        std::to_string(s->weapon_charges));
    for(const auto& [id,e]:w.entities()){
        if(!e.training_drone || e.type!=EntityType::Ship ||
           e.system_id!=s->system_id)continue;
        announce(w,"COMBAT_TARGET|"+std::to_string(id)+"|"+
            std::to_string(e.hull));
    }
    announce(w,"STATUS|"+std::to_string(c->crew)+"|"+std::to_string(s->cargo)+"|"+
        std::to_string(c->refined)+"|"+std::to_string(c->modules)+"|"+
        std::to_string(w.built_outposts().size())+"|"+std::to_string(w.total_ore()));
    if(const auto* l=w.life_support(ship)){
        const auto safety=colony_safety::assess(w,ship,ship);
        announce(w,"SAFETY|"+std::to_string(ship)+"|"+
            std::to_string(static_cast<int>(safety.risk))+"|"+
            std::to_string(safety.remaining.food_periods)+"|"+
            std::to_string(safety.remaining.water_periods)+"|"+
            std::to_string(safety.remaining.oxygen_periods)+"|"+
            std::to_string(safety.population));
        const auto response=colony_safety::plan_response(safety);
        // Read-only proposal telemetry, NOT an executed repair/rescue receipt.
        // Never label a searched-for vessel as an arrived rescue.
        announce(w,"SAFETY_RESPONSE|"+std::to_string(response.site)+"|"+
            std::to_string(static_cast<int>(response.level))+"|"+
            std::to_string(response.request_supply?1:0)+"|"+
            std::to_string(response.seek_physical_repairs?1:0)+"|"+
            std::to_string(response.search_verified_departure?1:0)+"|"+
            std::to_string(response.activate_distress?1:0)+"|"+
            std::to_string(response.rescue_completed?1:0));
        const auto v=w.assess_colony(ship);
        announce(w,"LIFE|"+std::to_string(l->food_kg)+"|"+std::to_string(l->water_kg)+"|"+
            std::to_string(l->oxygen_kg)+"|"+std::to_string(l->nutrients_kg)+"|"+
            std::to_string(l->co2_kg)+"|"+std::to_string(l->crops_kg)+"|"+
            std::to_string(l->compost_kg)+"|"+std::to_string(l->wastewater_kg)+"|"+
            std::to_string(l->cycles)+"|"+std::to_string(l->shortages)+"|"+
            std::to_string(v.reserve_cycles)+"|"+viability_stage_name(v.stage)+"|"+
            std::to_string(l->crops_ready_tick));
    }
    for(const auto& [id,outpost]:w.built_outposts()){
        if(outpost.source_colony_ship!=ship)continue;
        announce(w,"OUTPOST|"+std::to_string(id)+"|"+std::to_string(w.farm(id)?1:0));
    }
    for(const auto& [id,f]:w.farms()){
        if(f.source_ship!=ship)continue;
        const auto recent=f.recent_food_kg[0]+f.recent_food_kg[1]+f.recent_food_kg[2];
        const auto& l=f.life;
        announce(w,"FARM|"+std::to_string(id)+"|"+std::to_string(f.workers)+"|"+
            std::to_string(l.food_kg)+"|"+std::to_string(l.water_kg)+"|"+
            std::to_string(l.oxygen_kg)+"|"+std::to_string(l.nutrients_kg)+"|"+
            std::to_string(l.co2_kg)+"|"+std::to_string(l.crops_kg)+"|"+
            std::to_string(l.harvested_food_kg)+"|"+std::to_string(recent)+"|"+
            std::to_string(l.cycles)+"|"+std::to_string(l.shortages)+"|"+
            std::to_string(f.production_cycles));
    }
    for(const auto& [id,h]:w.habitats()){
        if(h.source_ship!=ship)continue;
        const auto* f=w.farm(id);
        const auto staffed=f?f->workers:0;
        announce(w,"HABITAT|"+std::to_string(id)+"|"+std::to_string(h.residents)+"|"+
            std::to_string(h.capacity)+"|"+std::to_string(h.residents+staffed)+"|"+
            std::to_string(h.life.food_kg)+"|"+std::to_string(h.life.water_kg)+"|"+
            std::to_string(h.life.oxygen_kg)+"|"+std::to_string(h.cycles)+"|"+
            std::to_string(h.consecutive_comfort_cycles)+"|"+std::to_string(h.life.shortages));
    }
    for(const auto& [id,u]:w.utilities()){
        if(u.source_ship!=ship)continue;
        announce(w,"UTILITY|"+std::to_string(id)+"|"+
            std::to_string(u.condition)+"|"+std::to_string(u.stored_energy)+"|"+
            std::to_string(u.powered?1:0)+"|"+
            std::to_string(u.cycles)+"|"+
            std::to_string(u.consecutive_powered_cycles)+"|"+
            std::to_string(u.outages)+"|"+std::to_string(u.repairs));
        announce(w,"MAINTENANCE|"+std::to_string(id)+"|"+
            std::to_string(u.spare_modules)+"|"+std::to_string(u.technicians)+"|"+
            std::to_string(u.delegated_maintenance?1:0)+"|"+
            std::to_string(u.delegated_repairs)+"|"+
            std::to_string(u.blocked_maintenance_cycles));
    }
    for(const auto& [id,c]:w.clinics()){
        if(c.source_ship!=ship)continue;
        announce(w,"CLINIC|"+std::to_string(id)+"|"+
            std::to_string(c.medics)+"|"+std::to_string(c.water_kg)+"|"+
            std::to_string(c.substrate_kg)+"|"+std::to_string(c.active_cases)+"|"+
            std::to_string(c.cycles)+"|"+std::to_string(c.consecutive_care_cycles)+"|"+
            std::to_string(c.shortages)+"|"+std::to_string(c.treated_cases)+"|"+
            std::to_string(c.recorded_cases));
    }
    for(const auto& [id,c]:w.communities()){
        if(c.source_ship!=ship)continue;
        announce(w,"COMMUNITY|"+std::to_string(id)+"|"+
            std::to_string(c.attendants)+"|"+std::to_string(c.food_kg)+"|"+
            std::to_string(c.water_kg)+"|"+std::to_string(c.cycles)+"|"+
            std::to_string(c.consecutive_service_cycles)+"|"+
            std::to_string(c.shortages)+"|"+std::to_string(c.served_visits));
    }
    const auto v=w.assess_colony(ship);
    announce(w,"UTILITY_ASSESS|"+std::to_string(v.powered_outposts)+"|"+
        std::to_string(v.unpowered_outposts)+"|"+
        std::to_string(v.utilities_sustained?1:0));
    announce(w,"MAINTENANCE_ASSESS|"+std::to_string(v.maintenance_spare_modules)+"|"+
        std::to_string(v.assigned_technicians)+"|"+
        std::to_string(v.maintenance_blocked_sites)+"|"+
        std::to_string(v.delegated_maintenance_ready?1:0));
    announce(w,"HAB_ASSESS|"+std::to_string(v.habitat_residents)+"|"+
        std::to_string(v.housed_colonists)+"|"+std::to_string(v.housing_capacity)+"|"+
        std::to_string(v.homeless_outpost_workers)+"|"+
        std::to_string(v.housing_provisioned?1:0)+"|"+viability_stage_name(v.stage));
    announce(w,"ASSESS|"+std::to_string(v.crew_total)+"|"+
        std::to_string(v.farm_workers)+"|"+std::to_string(v.recent_food_production_kg)+"|"+
        std::to_string(v.recent_food_demand_kg)+"|"+
        std::to_string(v.sustained_food_surplus?1:0));
    const auto risk=w.assess_resilience(ship);
    announce(w,"RESILIENCE|"+std::to_string(risk.onboard_reserve_cycles)+"|"+
        std::to_string(risk.lowest_site_reserve_cycles)+"|"+
        std::to_string(risk.occupied_sites)+"|"+
        std::to_string(risk.sites_with_reserves)+"|"+
        std::to_string(risk.serviced_sites)+"|"+
        std::to_string(risk.maintained_sites)+"|"+
        std::to_string(risk.spare_modules)+"|"+
        std::to_string(risk.nonmaintenance_residents)+"|"+
        std::to_string(risk.food_buffer_proven?1:0)+"|"+
        std::to_string(risk.operational_delegation_candidate?1:0)+"|"+
        std::to_string(risk.autonomous_growth_ready?1:0)+"|"+
        std::to_string(risk.blocker_mask));
    for(const auto& [site,p]:w.reclaimers()){
        if(p.source_ship!=ship)continue;
        announce(w,"RECLAIMER|"+std::to_string(site)+"|"+
            std::to_string(p.water_recovered_total_kg)+"|"+
            std::to_string(p.oxygen_generated_total_kg)+"|"+
            std::to_string(p.sludge_kg)+"|"+std::to_string(p.carbon_residue_kg)+"|"+
            std::to_string(p.cycles)+"|"+std::to_string(p.consecutive_powered_cycles));
    }
    for(const auto& [site,p]:w.waterworks_sites()){
        if(p.source_ship!=ship)continue;
        announce(w,"WATERWORKS|"+std::to_string(site)+"|"+
            std::to_string(p.raw_feed_kg)+"|"+std::to_string(p.brine_kg)+"|"+
            std::to_string(p.purified_total_kg)+"|"+
            std::to_string(p.cycles)+"|"+std::to_string(p.consecutive_service_cycles));
    }
    for(const auto& p:w.assess_policies(ship)){
        announce(w,"POLICY_AUDIT|"+std::to_string(p.site)+"|"+
            std::to_string(static_cast<int>(p.domain))+"|"+
            std::to_string(static_cast<int>(p.declared))+"|"+
            std::to_string(p.assigned_inspectors)+"|"+
            std::to_string(p.required_inspectors)+"|"+
            std::to_string(p.operational_evidence?1:0)+"|"+
            std::to_string(p.oversight_staffed?1:0)+"|"+
            std::to_string(p.assurance_supported?1:0));
    }
    const auto water=w.assess_water_supply(ship);
    announce(w,"WATER_SUPPLY|"+std::to_string(water.occupied_sites)+"|"+
        std::to_string(water.equipped_sites)+"|"+
        std::to_string(water.measured_sites)+"|"+
        std::to_string(water.potable_output_last3_kg)+"|"+
        std::to_string(water.water_demand_last3_kg)+"|"+
        std::to_string(water.short_window_coverage?1:0)+"|"+
        std::to_string(water.indefinitely_renewable?1:0));
    const auto recovery=w.assess_life_recovery(ship);
    announce(w,"LIFE_RECOVERY|"+std::to_string(recovery.occupied_sites)+"|"+
        std::to_string(recovery.equipped_sites)+"|"+
        std::to_string(recovery.measured_sites)+"|"+
        std::to_string(recovery.water_recovered_last3_kg)+"|"+
        std::to_string(recovery.oxygen_generated_last3_kg)+"|"+
        std::to_string(recovery.water_demand_last3_kg)+"|"+
        std::to_string(recovery.oxygen_demand_last3_kg)+"|"+
        std::to_string(recovery.sludge_kg)+"|"+
        std::to_string(recovery.carbon_residue_kg)+"|"+
        std::to_string(recovery.local_recycling_coverage?1:0)+"|"+
        std::to_string(recovery.independent_renewal_proven?1:0));
    announce(w,"CLINIC_ASSESS|"+std::to_string(risk.clinic_supported_sites)+"|"+
        std::to_string(risk.occupied_sites)+"|"+
        std::to_string(risk.active_clinical_cases)+"|"+
        std::to_string(risk.treated_clinical_cases)+"|"+
        std::to_string(risk.basic_clinic_access_proven?1:0));
    announce(w,"COMMUNITY_ASSESS|"+std::to_string(risk.community_supported_sites)+"|"+
        std::to_string(risk.occupied_sites)+"|"+
        std::to_string(risk.restorative_leisure_proven?1:0));
    // Emit a complete, bounded inventory refresh only on change; avoid
    // filling the SSE queue during idle ticks. Only allowlisted names emitted.
    std::string stocks;
    for(const auto& [name,kg]:c->minerals_kg)if(kg>0)stocks+="MINERAL|"+name+"|"+std::to_string(kg)+"\n";
    for(const auto& [name,kg]:c->goods_kg)if(kg>0)stocks+="GOOD|"+name+"|"+std::to_string(kg)+"\n";
    stocks+="TAILINGS|"+std::to_string(c->tailings_kg)+"\n";
    static std::string last;
    if(stocks!=last){
        announce(w,"STOCK_CLEAR");
        std::istringstream rows(stocks);std::string row;
        while(std::getline(rows,row))announce(w,"STOCK|"+row);
        last=stocks;
    }
}
void scan(const Simulation& w,EntityId ship){
    const auto* s=w.find(ship);if(!s)return;
    const auto frame=w.ship_frames().find(ship);
    if(frame!=w.ship_frames().end() && frame->second.anchor_body) {
        const auto region=w.local_environments().find(frame->second.anchor_body);
        if(region!=w.local_environments().end()) {
            const auto& e=region->second;
            announce(w,"LOCAL_ENVIRONMENT|"+std::to_string(frame->second.anchor_body)+
                "|"+std::to_string(unsigned(e.zone))+
                "|"+std::to_string(unsigned(e.hazard)));
        }
    }
    int found=0;
    for(const auto& [id,e]:w.entities()){
        if(e.type!=EntityType::Asteroid || e.system_id!=s->system_id ||
           !w.same_local_frame(ship,id))continue;
        auto dx=e.position.x-s->position.x,dy=e.position.y-s->position.y;
        if(dx*dx+dy*dy>sensor_mm*sensor_mm)continue;
        ++found;
        if(const auto* g=w.geology(id)){
            announce(w,"ROCK|"+std::to_string(id)+"|"+std::to_string(static_cast<int>(g->taxonomy))+"|"+
                std::to_string(g->radius_m)+"|"+std::to_string(g->density_kg_m3)+"|"+
                std::to_string(g->original_tonnes)+"|"+std::to_string(e.ore));
            for(const auto& mineral:mineral_profile(g->taxonomy))
                announce(w,"MINERAL|"+std::to_string(id)+"|"+mineral.name+"|"+std::to_string(mineral.permille));
        }else {
            announce(w,"SMALL_ROCK|"+std::to_string(id)+"|"+std::to_string(e.ore));
            if(const auto taxonomy=w.local_deposit_taxonomy(id))
                announce(w,"LOCAL_DEPOSIT|"+std::to_string(id)+"|"+
                    std::to_string(unsigned(*taxonomy)));
        }
    }
    announce(w,"SCAN_DONE|"+std::to_string(found));
}
EntityId nearest(const Simulation& w, EntityId ship){
    const auto* s=w.find(ship);if(!s)return 0;
    EntityId chosen=0;std::int64_t best=std::numeric_limits<std::int64_t>::max();
    for(const auto& [id,e]:w.entities())if(e.type==EntityType::Asteroid &&
        w.same_local_frame(ship,id)){
        auto dx=e.position.x-s->position.x,dy=e.position.y-s->position.y;
        const auto dist=dx*dx+dy*dy;
        if(dist<best){best=dist;chosen=id;}
    }
    return chosen;
}
void apply(PersistentWorld& world,EntityId ship,FleetSelection& fleet,const std::string& line,std::uint64_t& nonce,PlayerId principal,std::set<std::uint64_t>& pending_scans,std::set<std::uint64_t>& pending_probe_scans,SurveyArchive* knowledge){
    if(principal!=owner){announce(world.world(),"ERROR|UNAUTHORIZED_PRINCIPAL");return;}
    const auto v=split(line);if(v.empty())return;
    if(v.size()==1 && v[0]=="SCAN"){
        Command c{};c.id=++nonce;c.sequence=c.id;c.execute_tick=world.world().tick()+1;
        c.issuer=principal;c.actor=ship;c.action=Action::SurveyLocal;
        if(world.submit(c))pending_scans.insert(c.id);
        else announce(world.world(),"ERROR|INVALID_INPUT");
        return;
    }
    // WORK-01A: one owner-authorized universal work coordinator command
    // boundary. All physical effects still go through native task authority.
    if(!v.empty() && (v[0]=="WORK_DELEGATE_SHIP" ||
                       v[0]=="WORK_CANCEL" || v[0]=="WORK_STATUS")){
        try{
            std::uint64_t changed_id=0;
            bool changed=false;
            if(v[0]=="WORK_DELEGATE_SHIP" && v.size()==7){
                const auto id=integer(v[1]),contract=integer(v[2]),
                    first=integer(v[3]),second=integer(v[4]),
                    provisions=integer(v[5]),dependency=integer(v[6]);
                if(id<=0 || id>=(std::int64_t{1}<<50) ||
                   contract<=0 || contract>=(std::int64_t{1}<<50) ||
                   first<0 || second<0 || provisions<0 ||
                   first>=(std::int64_t{1}<<50) ||
                   second>=(std::int64_t{1}<<50) ||
                   dependency<0 || dependency>=(std::int64_t{1}<<50) ||
                   !world.delegate_customer_ship_order(principal,ship,
                       std::uint64_t(id),std::uint64_t(contract),
                       {{std::uint64_t(first),std::uint64_t(second)}},
                       provisions,std::uint64_t(dependency)))
                    throw std::invalid_argument("delegation not permitted");
                changed=true;changed_id=std::uint64_t(id);
            }else if(v[0]=="WORK_CANCEL" && v.size()==2){
                const auto id=integer(v[1]);
                if(id<=0 || id>=(std::int64_t{1}<<50) ||
                   !world.cancel_work_directive(principal,std::uint64_t(id)))
                    throw std::invalid_argument("not a cancellable owner work order");
                changed=true;changed_id=std::uint64_t(id);
            }else if(v[0]!="WORK_STATUS" || v.size()!=1)
                throw std::invalid_argument("invalid universal work command");
            if(changed)
                announce(world.world(),"WORK_COMMITTED|"+
                    std::to_string(changed_id)+"|"+
                    std::to_string(world.generation()));
            std::uint64_t count=0;
            for(const auto& entry:world.world().universal_work_board(
                    principal,ship)){
                if(count>=64)break;
                announce(world.world(),"WORK_ITEM|"+
                    std::to_string(unsigned(entry.kind))+"|"+
                    std::to_string(entry.source_order)+"|"+
                    std::to_string(unsigned(entry.phase))+"|"+
                    std::to_string(entry.progress)+"|"+
                    std::to_string(entry.required)+"|"+
                    std::to_string(entry.delegated_id)+"|"+
                    std::to_string(world.generation()));
                ++count;
            }
            announce(world.world(),"WORK_DONE|"+std::to_string(count));
            std::uint64_t direct_count=0;
            for(const auto& [id,job]:world.world().work_directives()){
                if(direct_count>=64)break;
                if(job.owner!=principal || job.colony_ship!=ship)continue;
                announce(world.world(),"WORK_DIRECTIVE|"+
                    std::to_string(id)+"|"+
                    std::to_string(job.source_order)+"|"+
                    std::to_string(unsigned(job.status))+"|"+
                    std::to_string(unsigned(job.blocked))+"|"+
                    std::to_string(job.requires_directive)+"|"+
                    std::to_string(job.commissioning_crew[0])+"|"+
                    std::to_string(job.commissioning_crew[1])+"|"+
                    std::to_string(job.provisions_kg_each)+"|"+
                    std::to_string(job.posted_tick)+"|"+
                    std::to_string(job.resolved_tick)+"|"+
                    std::to_string(world.generation()));
                ++direct_count;
            }
            announce(world.world(),"WORK_DIRECTIVES_DONE|"+
                std::to_string(direct_count));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|WORK_DENIED");
        }
        return;
    }
    // SHIP-01F: one physical intercolony shipyard contract, not a free
    // factory. The buyer requests, the supplier independently accepts, six
    // existing modules change custody at a real berth, the ordinary twelve-
    // tick native build finishes, and the empty hull transfers only when
    // the customer ship returns to its real location.
    if(!v.empty() && (v[0]=="YARD_CONTRACT_POST" ||
                       v[0]=="YARD_CONTRACT_ACCEPT" ||
                       v[0]=="YARD_CONTRACT_FUND" ||
                       v[0]=="YARD_CONTRACT_DELIVER" ||
                       v[0]=="YARD_CONTRACT_STATUS")){
        try{
            std::uint64_t id=0;
            bool changed=false;
            if(v[0]=="YARD_CONTRACT_POST" && v.size()==4){
                const auto request=integer(v[1]),yard=integer(v[2]),
                           build=integer(v[3]);
                if(request<=0 || yard<=0 || build<=0 ||
                   request>=(std::int64_t{1}<<50) ||
                   build>=(std::int64_t{1}<<50) ||
                   !world.post_shipyard_customer_order(principal,ship,
                       std::uint64_t(yard),std::uint64_t(build),
                       std::uint64_t(request)))
                    throw std::invalid_argument("shipyard customer request denied");
                id=std::uint64_t(request);changed=true;
            }else if(v.size()==2 && v[0]!="YARD_CONTRACT_STATUS"){
                const auto raw=integer(v[1]);
                if(raw<=0 || raw>=(std::int64_t{1}<<50))
                    throw std::invalid_argument("invalid ship order id");
                id=std::uint64_t(raw);
                const auto it=world.world().shipyard_customer_orders().find(id);
                if(it==world.world().shipyard_customer_orders().end() ||
                   (it->second.customer_owner!=principal &&
                    it->second.supplier_owner!=principal))
                    throw std::invalid_argument("foreign shipyard contract");
                if(v[0]=="YARD_CONTRACT_ACCEPT")
                    changed=world.accept_shipyard_customer_order(principal,id);
                else if(v[0]=="YARD_CONTRACT_FUND")
                    changed=world.fund_shipyard_customer_order(principal,id);
                else if(v[0]=="YARD_CONTRACT_DELIVER")
                    changed=world.deliver_shipyard_customer_order(principal,id);
                if(!changed)throw std::invalid_argument("shipyard transition denied");
            }else if(v[0]!="YARD_CONTRACT_STATUS" || v.size()!=1)
                throw std::invalid_argument("invalid shipyard request shape");
            if(changed)
                announce(world.world(),"YARD_CONTRACT_COMMITTED|"+
                    std::to_string(id)+"|"+std::to_string(world.generation()));
            std::uint64_t count=0;
            for(const auto& [oid,contract]:world.world().shipyard_customer_orders()){
                if(count>=64)break;
                if(contract.customer_owner!=principal &&
                   contract.supplier_owner!=principal)continue;
                const auto job=world.world().freighter_build_orders().find(
                    contract.build_order);
                const auto progress=job==world.world().freighter_build_orders().end()?
                    0:job->second.progress;
                const auto required=job==world.world().freighter_build_orders().end()?
                    12:job->second.required;
                const auto hull=job==world.world().freighter_build_orders().end()?
                    0:job->second.launched_ship;
                announce(world.world(),"YARD_CONTRACT_ITEM|"+
                    std::to_string(oid)+"|"+
                    std::to_string(contract.yard_order)+"|"+
                    std::to_string(contract.build_order)+"|"+
                    std::to_string(contract.customer_ship)+"|"+
                    std::to_string(contract.supplier_ship)+"|"+
                    std::to_string(contract.customer_owner)+"|"+
                    std::to_string(contract.supplier_owner)+"|"+
                    std::to_string(unsigned(contract.stage))+"|"+
                    std::to_string(progress)+"|"+
                    std::to_string(required)+"|"+
                    std::to_string(hull)+"|"+
                    std::to_string(contract.delivered_ship)+"|"+
                    std::to_string(world.generation()));
                ++count;
            }
            announce(world.world(),"YARD_CONTRACT_DONE|"+std::to_string(count));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|YARD_CONTRACT_DENIED");
        }
        return;
    }
    // SHIP-01A.1 player-built real shipyard; never trust the UI to claim
    // ownership, worker identity, resource consumption or completion.
    if(!v.empty() && (v[0]=="SHIPYARD_BUILD" ||
                     v[0]=="SHIPYARD_ASSIGN" || v[0]=="SHIPYARD_STATUS")){
        try{
            if(v[0]=="SHIPYARD_BUILD" && v.size()==4){
                const auto order=integer(v[1]),a=integer(v[2]),b=integer(v[3]);
                if(order<=0 || order>=(std::int64_t{1}<<50) ||
                   a<=0 || b<=0 || a==b ||
                   !world.start_shipyard_construction(principal,ship,
                       std::uint64_t(order),
                       {{std::uint64_t(a),std::uint64_t(b)}})){
                    announce(world.world(),"ERROR|SHIPYARD_DENIED");
                    return;
                }
                announce(world.world(),"SHIPYARD_STARTED|"+
                    std::to_string(order)+"|"+std::to_string(world.generation()));
            }else if(v[0]=="SHIPYARD_ASSIGN" && v.size()==4){
                const auto order=integer(v[1]),a=integer(v[2]),b=integer(v[3]);
                if(order<=0 || order>=(std::int64_t{1}<<50) ||
                   a<=0 || b<=0 || a==b ||
                   !world.assign_shipyard_builders(principal,
                       std::uint64_t(order),
                       {{std::uint64_t(a),std::uint64_t(b)}})){
                    announce(world.world(),"ERROR|SHIPYARD_DENIED");
                    return;
                }
                announce(world.world(),"SHIPYARD_ASSIGNED|"+
                    std::to_string(order)+"|"+std::to_string(world.generation()));
            }else if(v[0]!="SHIPYARD_STATUS" || v.size()!=1){
                announce(world.world(),"ERROR|SHIPYARD_DENIED");return;
            }
            std::uint64_t count=0;
            for(const auto& [id,yard]:world.world().shipyard_constructions()){
                if(yard.owner!=principal || yard.colony_ship!=ship || count>=64)
                    continue;
                announce(world.world(),"SHIPYARD_ITEM|"+
                    std::to_string(id)+"|"+std::to_string(yard.colony_ship)+"|"+
                    std::to_string(yard.workers[0])+"|"+
                    std::to_string(yard.workers[1])+"|"+
                    std::to_string(yard.progress)+"|"+
                    std::to_string(yard.required)+"|"+
                    std::to_string(yard.stalled_ticks)+"|"+
                    std::to_string(world.generation()));
                ++count;
            }
            announce(world.world(),"SHIPYARD_DONE|"+std::to_string(count));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|SHIPYARD_DENIED");
        }
        return;
    }
    // SHIP-01D: selecting a cockpit changes only which physical hull receives
    // flight commands. All industrial / colony work commands still use ship,
    // the original owner resource authority. Foreign and uncommissioned
    // vessels are rejected by FleetSelection against the real world.
    if(!v.empty() && (v[0]=="FLEET_STATUS" || v[0]=="FLEET_SELECT")){
        try{
            if(v[0]=="FLEET_SELECT"){
                if(v.size()!=2)throw std::invalid_argument("bad fleet selection");
                const auto requested=integer(v[1]);
                if(requested<=0)
                    throw std::invalid_argument("invalid selected ship");
                // Reject a busy carrier BEFORE mutating the session cockpit;
                // a denied click cannot silently switch away from the ship
                // the player was already flying.
                for(const auto& [id,leg]:world.world().intercolony_freight()) {
                    (void)id;
                    if(leg.carrier_ship==EntityId(requested) &&
                       leg.stage!=IntercolonyFreightStage::Delivered)
                        throw std::invalid_argument("active freight carrier reserved");
                }
                if(!fleet.select(world.world(),EntityId(requested)))
                    throw std::invalid_argument("ship is not pilot-ready");
            }else if(v.size()!=1)
                throw std::invalid_argument("bad fleet status request");
            announce(world.world(),"FLEET_RESET");
            std::size_t count=0;
            for(const auto& item:fleet.list(world.world())){
                announce(world.world(),"FLEET_ITEM|"+
                    std::to_string(item.ship)+"|"+
                    std::to_string(item.manufacture_order)+"|"+
                    std::to_string(item.home?1:0)+"|"+
                    std::to_string(item.ready?1:0)+"|"+
                    std::to_string(item.crew)+"|"+
                    std::to_string(item.anchor_body)+"|"+
                    std::to_string(item.food_kg)+"|"+
                    std::to_string(item.water_kg)+"|"+
                    std::to_string(item.oxygen_kg));
                ++count;
            }
            announce(world.world(),"FLEET_DONE|"+std::to_string(count));
            announce(world.world(),"FLEET_SELECTED|"+
                std::to_string(fleet.piloted())+"|"+
                std::to_string(fleet.home())+"|"+
                std::to_string(world.generation()));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|FLEET_SELECTION_DENIED");
        }
        return;
    }
    // SHIP-01E: assign a real *non-piloted* manufactured carrier to an
    // existing planetary NPC pickup -> intercolony freight work order. This
    // uses the same canonical post_intercolony_work_order transaction as
    // the original work board. No second AI job queue or virtual cargo.
    if(!v.empty() && v[0]=="FLEET_WORK_POST"){
        try {
            if(v.size()!=9)throw std::invalid_argument("fleet work arity");
            const auto order=integer(v[1]),carrier_id=integer(v[2]),
                receiver_id=integer(v[3]),body=integer(v[4]),
                latitude=integer(v[5]),longitude=integer(v[6]),
                kg=integer(v[8]);
            if(order<=0 || order>=(std::int64_t{1}<<50) ||
               carrier_id<=0 || receiver_id<=0 ||
               body<=0 || latitude<0 || latitude>=36 ||
               longitude<0 || longitude>=72 || kg<=0 || kg>1000)
                throw std::invalid_argument("invalid fleet work fields");
            const auto carrier=EntityId(carrier_id);
            const auto receiver=EntityId(receiver_id);
            if(!fleet_freight_eligible(world.world(),fleet,
                    principal,carrier,receiver))
                throw std::invalid_argument("carrier busy, player-piloted or invalid");
            ShuttleCargo cargo{};
            if(v[7]=="WATER")cargo=ShuttleCargo::Water;
            else if(v[7]=="FOOD")cargo=ShuttleCargo::Food;
            else if(v[7]=="NUTRIENTS")cargo=ShuttleCargo::Nutrients;
            else throw std::invalid_argument("unsupported fleet material");
            // Authoritative PersistentWorld performs one copied-state,
            // all-or-nothing SQLite commit. An order never creates stock.
            if(!world.post_intercolony_work_order(
                    principal,ship,
                    PlanetarySiteKey{std::uint64_t(body),
                        std::uint16_t(latitude),
                        std::uint16_t(longitude),1},
                    cargo,kg,std::uint64_t(order),carrier,receiver))
                throw std::invalid_argument("physical work order rejected");
            announce(world.world(),"FLEET_WORK_ASSIGNED|"+
                std::to_string(order)+"|"+std::to_string(carrier)+"|"+
                std::to_string(receiver)+"|"+std::to_string(world.generation()));
            // Existing NPC shuttle and physical freighter scheduler take over
            // ONLY after their separately committed material handoffs.
        }catch(const std::exception&){
            announce(world.world(),"ERROR|FLEET_WORK_DENIED");
        }
        return;
    }
    // SHIP-01C: pilot a specifically manufactured/commissioned hull without
    // rebinding colony infrastructure, work board or founding ship authority.
    // Only exact owner/physical hull/real crew + immutable build receipt admit
    // commands. Request IDs are host-allocated, never browser-controlled.
    if(!v.empty() && (v[0]=="FREIGHTER_PILOT_STATUS" ||
                      v[0]=="FREIGHTER_THRUST" ||
                      v[0]=="FREIGHTER_YAW" ||
                      v[0]=="FREIGHTER_TRAVEL")){
        try{
            if(v.size()<2)throw std::invalid_argument("missing freighter order");
            const auto raw=integer(v[1]);
            if(raw<=0 || raw>=(std::int64_t{1}<<50))
                throw std::invalid_argument("bad freighter order");
            const auto order_id=std::uint64_t(raw);
            const auto job=world.world().freighter_build_orders().find(order_id);
            if(job==world.world().freighter_build_orders().end() ||
               job->second.owner!=principal ||
               job->second.source_ship!=ship ||
               !job->second.commissioned())
                throw std::invalid_argument("uncommissioned or foreign hull");
            const auto hull_id=job->second.launched_ship;
            const auto* hull=world.world().find(hull_id);
            const auto* crew=world.world().colony_ship(hull_id);
            const auto* life=world.world().life_support(hull_id);
            const auto frame=world.world().ship_frames().find(hull_id);
            if(!hull || hull->type!=EntityType::Ship ||
               hull->owner_id!=principal ||
               hull->manufactured_order_id!=order_id ||
               !crew || crew->crew<1 || !life ||
               frame==world.world().ship_frames().end() ||
               !hull->fitting.exploration_scanner())
                throw std::invalid_argument("freighter not operational");
            if(v[0]=="FREIGHTER_THRUST" && v.size()==4){
                const auto x=integer(v[2]),y=integer(v[3]);
                if(x<-1 || x>1 || y<-1 || y>1)
                    throw std::invalid_argument("invalid thrust vector");
                Command command{};
                command.id=++nonce;command.sequence=command.id;
                command.execute_tick=world.world().tick()+1;
                command.issuer=principal;command.actor=hull_id;
                command.action=Action::Thrust;
                command.thrust_x=int(x);command.thrust_y=int(y);
                if(!world.submit(command))
                    throw std::invalid_argument("freighter thrust rejected");
                announce(world.world(),"FREIGHTER_PILOT_QUEUED|"+
                    std::to_string(order_id)+"|"+std::to_string(hull_id)+"|"+
                    std::to_string(command.id)+"|"+
                    std::to_string(world.generation()));
            }else if(v[0]=="FREIGHTER_YAW" && v.size()==3){
                const auto delta=integer(v[2]);
                if(delta!=15 && delta!=-15)
                    throw std::invalid_argument("invalid hull yaw");
                Command command{};
                command.id=++nonce;command.sequence=command.id;
                command.execute_tick=world.world().tick()+1;
                command.issuer=principal;command.actor=hull_id;
                command.action=Action::Thrust;command.amount=delta;
                if(!world.submit(command))
                    throw std::invalid_argument("freighter yaw rejected");
                announce(world.world(),"FREIGHTER_PILOT_QUEUED|"+
                    std::to_string(order_id)+"|"+std::to_string(hull_id)+"|"+
                    std::to_string(command.id)+"|"+
                    std::to_string(world.generation()));
            }else if(v[0]=="FREIGHTER_TRAVEL" && v.size()==3){
                const auto target=integer(v[2]);
                if(target<=0)throw std::invalid_argument("invalid body");
                const auto request=++nonce;
                if(!world.begin_celestial_transit(principal,hull_id,
                        std::uint64_t(target),request))
                    throw std::invalid_argument("freighter transit rejected");
                announce(world.world(),"FREIGHTER_TRANSIT_STARTED|"+
                    std::to_string(order_id)+"|"+std::to_string(hull_id)+"|"+
                    std::to_string(request)+"|"+std::to_string(target)+"|"+
                    std::to_string(world.generation()));
            }else if(v[0]!="FREIGHTER_PILOT_STATUS" || v.size()!=2){
                throw std::invalid_argument("unsupported freighter control");
            }
            // Read back the actual committed/native state; a pending thrust
            // is explicitly not reported as already executed movement.
            const auto* current=world.world().find(hull_id);
            const auto& custody=world.world().ship_frames().at(hull_id);
            const auto* onboard=world.world().life_support(hull_id);
            announce(world.world(),"FREIGHTER_PILOT_ITEM|"+
                std::to_string(order_id)+"|"+std::to_string(hull_id)+"|"+
                std::to_string(current->heading_deg)+"|"+
                std::to_string(current->position.x)+"|"+
                std::to_string(current->position.y)+"|"+
                std::to_string(custody.anchor_body)+"|"+
                std::to_string(world.world().colony_ship(hull_id)->crew)+"|"+
                std::to_string(onboard->food_kg)+"|"+
                std::to_string(onboard->water_kg)+"|"+
                std::to_string(onboard->oxygen_kg)+"|"+
                std::to_string(world.generation()));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|FREIGHTER_PILOT_DENIED");
        }
        return;
    }
    // SHIP-01B.1: real finite-hull production, separate from free genesis.
    // The client can request an existing player's yard but cannot assert
    // completion, choose an entity ID, or claim a crew and cargo.
    if(!v.empty() && (v[0]=="FREIGHTER_BUILD" || v[0]=="FREIGHTER_COMMISSION" || v[0]=="FREIGHTER_STATUS")){
        try{
            if(v[0]=="FREIGHTER_BUILD" && v.size()==3){
                const auto yard=integer(v[1]),order=integer(v[2]);
                const auto site=world.world().shipyard_constructions().find(
                    std::uint64_t(yard>0?yard:0));
                if(yard<=0 || order<=0 || order>=(std::int64_t{1}<<50) ||
                   site==world.world().shipyard_constructions().end() ||
                   site->second.owner!=principal ||
                   site->second.colony_ship!=ship ||
                   !world.queue_shipyard_freighter(principal,
                       std::uint64_t(yard),std::uint64_t(order))){
                    announce(world.world(),"ERROR|FREIGHTER_DENIED");return;
                }
                announce(world.world(),"FREIGHTER_QUEUED|"+
                    std::to_string(order)+"|"+std::to_string(yard)+"|"+
                    std::to_string(world.generation()));
            }else if(v[0]=="FREIGHTER_COMMISSION" && v.size()==5){
                const auto order=integer(v[1]),one=integer(v[2]),
                    two=integer(v[3]),kg=integer(v[4]);
                const auto build=world.world().freighter_build_orders().find(
                    std::uint64_t(order>0?order:0));
                if(order<=0 || order>=(std::int64_t{1}<<50) ||
                   one<=0 || two<=0 || one==two ||
                   kg<10 || kg>1000 ||
                   build==world.world().freighter_build_orders().end() ||
                   !((build->second.owner==principal &&
                      build->second.source_ship==ship) ||
                     std::any_of(world.world().shipyard_customer_orders().begin(),
                         world.world().shipyard_customer_orders().end(),
                         [&](const auto& entry){
                            const auto& c=entry.second;
                            return c.build_order==std::uint64_t(order) &&
                               c.customer_owner==principal &&
                               c.customer_ship==ship &&
                               c.stage==ShipyardCustomerStage::Delivered;
                         })) ||
                   !world.commission_shipyard_freighter(principal,
                       std::uint64_t(order),
                       {{std::uint64_t(one),std::uint64_t(two)}},kg)){
                    announce(world.world(),"ERROR|FREIGHTER_DENIED");return;
                }
            }else if(v[0]!="FREIGHTER_STATUS" || v.size()!=1){
                announce(world.world(),"ERROR|FREIGHTER_DENIED");return;
            }
            std::uint64_t count=0;
            for(const auto& [id,order]:world.world().freighter_build_orders()){
                bool local_build=order.owner==principal &&
                                 order.source_ship==ship;
                if(!local_build){
                    for(const auto& [cid,contract]:world.world().shipyard_customer_orders()){
                        (void)cid;
                        if(contract.build_order==id &&
                           contract.customer_owner==principal &&
                           contract.customer_ship==ship &&
                           contract.stage==ShipyardCustomerStage::Delivered){
                            local_build=true;break;
                        }
                    }
                }
                if(!local_build || count>=64)continue;
                announce(world.world(),"FREIGHTER_ITEM|"+
                    std::to_string(id)+"|"+std::to_string(order.yard_order)+"|"+
                    std::to_string(order.progress)+"|"+
                    std::to_string(order.required)+"|"+
                    std::to_string(order.launched_ship)+"|"+
                    std::to_string(world.generation()));
                if(order.commissioned())
                    announce(world.world(),"FREIGHTER_COMMISSIONED|"+
                        std::to_string(id)+"|"+
                        std::to_string(order.launched_ship)+"|"+
                        std::to_string(order.commissioning_crew[0])+"|"+
                        std::to_string(order.commissioning_crew[1])+"|"+
                        std::to_string(order.provisions_kg_each)+"|"+
                        std::to_string(world.generation()));
                ++count;
            }
            announce(world.world(),"FREIGHTER_DONE|"+std::to_string(count));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|FREIGHTER_DENIED");
        }
        return;
    }
    if(v.size()==2 && v[0]=="OASIS_INSTALL") {
        try {
            const auto id=integer(v[1]);
            if(id<=0 || !world.install_oasis_extractor(principal,ship,
                static_cast<std::uint64_t>(id)))
                announce(world.world(),"ERROR|INVALID_INPUT");
            else {
                announce(world.world(),"OASIS_INSTALLED|"+std::to_string(id));
                status(world.world(),ship);
            }
        }catch(const std::exception&) {
            announce(world.world(),"ERROR|INVALID_INPUT");
        }
        return;
    }
    if(v.size()==4 && v[0]=="OASIS_COLLECT") {
        try {
            const auto id=integer(v[1]);
            const auto kg=integer(v[3]);
            if(id<=0 || kg<=0 || (v[2]!="WATER" && v[2]!="NUTRIENTS"))
                throw std::invalid_argument("bad native oasis collection");
            if(!world.collect_oasis_resource(principal,ship,
                static_cast<std::uint64_t>(id),v[2]=="WATER",kg)) {
                announce(world.world(),"ERROR|INVALID_INPUT");
            }else {
                announce(world.world(),"OASIS_COLLECTED|"+std::to_string(id)+"|"+
                    v[2]+"|"+std::to_string(kg));
                status(world.world(),ship);
            }
        }catch(const std::exception&) {
            announce(world.world(),"ERROR|INVALID_INPUT");
        }
        return;
    }
    if(v.size()==2 && v[0]=="FRAME_TRAVEL") {
        try {
            // The client supplies only a known body ID. The host resolves
            // the authenticated player/ship and allocates its own request ID.
            const auto raw=integer(v[1]);
            if(raw<=0)throw std::invalid_argument("invalid celestial target");
            const auto body=static_cast<std::uint64_t>(raw);
            const auto request=++nonce;
            if(!fleet.is_ready(world.world(),fleet.piloted()) ||
               !world.begin_celestial_transit(principal,fleet.piloted(),body,request))
                announce(world.world(),"ERROR|FRAME_TRAVEL_DENIED");
            else announce(world.world(),"FRAME_TRANSIT_STARTED|"+
                std::to_string(request)+"|"+std::to_string(body));
        }catch(const std::exception&) {
            announce(world.world(),"ERROR|FRAME_TRAVEL_DENIED");
        }
        return;
    }
    // Orbital NPC delegation: player never descends. Crew and project status
    // are native read-models; construction advances only in committed ticks.
    if(v.size()==1 && v[0]=="PLANET_CREW"){
        for(const auto& [id,n]:world.world().npc_records()){
            if(n.home_ship!=ship || n.location!=ship ||
               n.role!=NpcRole::Ship || n.assigned_site!=ship ||
               !world.world().npc_available_for_duty(id))continue;
            bool guardian=false;
            for(const auto& [dependent,child]:world.world().physical_dependents()){
                (void)dependent;
                if(child.guardian_id==id){guardian=true;break;}
            }
            if(!guardian)announce(world.world(),
                "PLANET_CREW_AVAILABLE|"+std::to_string(id));
        }
        announce(world.world(),"PLANET_CREW_DONE");
        return;
    }
    if(v.size()==4 && v[0]=="PLANET_SITE_STATUS"){
        try{
            const auto body=integer(v[1]),lat=integer(v[2]),lon=integer(v[3]);
            if(body<=0 || lat<0 || lat>=36 || lon<0 || lon>=72)
                throw std::invalid_argument("invalid planet site status");
            const auto key=PlanetarySiteKey{
                std::uint64_t(body),std::uint16_t(lat),std::uint16_t(lon),1};
            const auto it=world.world().planetary_sites().find(key);
            if(it==world.world().planetary_sites().end() ||
               it->second.owner!=principal)
                throw std::invalid_argument("planet site not owned");
            const auto& site=it->second;
            announce(world.world(),"PLANET_SITE_STATUS|"+
                std::to_string(body)+"|"+std::to_string(lat)+"|"+
                std::to_string(lon)+"|"+std::to_string(site.spaceport_phase)+"|"+
                std::to_string(site.construction_progress)+"|"+
                std::to_string(site.construction_required)+"|"+
                std::to_string(site.assigned_workers)+"|"+
                std::to_string(site.food_kg)+"|"+
                std::to_string(site.water_kg)+"|"+
                std::to_string(site.oxygen_kg)+"|"+
                std::to_string(site.delegated_npcs[0])+"|"+
                std::to_string(site.delegated_npcs[1])+"|"+
                std::to_string(unsigned(site.workforce_policy))+"|"+
                std::to_string(unsigned(site.equipment_condition))+"|"+
                std::to_string(site.autonomous_cycles)+"|"+
                std::to_string(site.stalled_cycles)+"|"+
                std::to_string(site.auto_extracted_kg)+"|"+
                std::to_string(site.repair_cycles)+
                (site.nutrients_kg>0?"|"+std::to_string(site.nutrients_kg):""));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|PLANET_SITE_STATUS_DENIED");
        }
        return;
    }
    // PLAY-13D.4G. Physical source -> existing crewed carrier -> destination.
    // These commands move NO goods except through the actual native
    // PersistentWorld load/unload transactions. The celestial leg reuses
    // authenticated ship transit and its independently committed arrival.
    if(!v.empty() && (v[0]=="INTERFREIGHT_LOAD" ||
                      v[0]=="INTERFREIGHT_DISPATCH" ||
                      v[0]=="INTERFREIGHT_DELIVER" ||
                      v[0]=="INTERFREIGHT_STATUS")) {
        try {
            const auto& records=world.world().intercolony_freight();
            if(v[0]=="INTERFREIGHT_STATUS" && v.size()==1) {
                std::uint64_t count=0;
                for(const auto& [id,leg]:records) {
                    if(leg.owner!=principal || leg.source_ship!=ship || count>=64)continue;
                    const auto good=leg.cargo==ShuttleCargo::Water?"WATER":
                        leg.cargo==ShuttleCargo::Food?"FOOD":"NUTRIENTS";
                    announce(world.world(),"INTERFREIGHT_ITEM|"+
                        std::to_string(id)+"|"+std::to_string(leg.carrier_ship)+"|"+
                        std::to_string(leg.destination_ship)+"|"+good+"|"+
                        std::to_string(leg.kilograms)+"|"+
                        std::to_string(unsigned(leg.stage))+"|"+
                        std::to_string(leg.arrival_receipt));
                    ++count;
                }
                announce(world.world(),"INTERFREIGHT_DONE|"+std::to_string(count));
            }else if(v[0]=="INTERFREIGHT_LOAD" && v.size()==4) {
                const auto order=integer(v[1]),carrier=integer(v[2]),
                           destination=integer(v[3]);
                const auto job=world.world().local_delivery_board().find(
                    std::uint64_t(order>0?order:0));
                if(order<=0 || order>=(std::int64_t{1}<<50) ||
                   carrier<=0 || destination<=0 ||
                   job==world.world().local_delivery_board().end() ||
                   job->second.board_ship!=ship ||
                   !world.load_intercolony_freight(principal,
                       std::uint64_t(order),EntityId(carrier),
                       EntityId(destination)))
                    announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
                else announce(world.world(),"INTERFREIGHT_LOADED|"+
                    std::to_string(order)+"|"+std::to_string(carrier)+"|"+
                    std::to_string(destination)+"|"+std::to_string(world.generation()));
            }else if(v[0]=="INTERFREIGHT_DISPATCH" && v.size()==2) {
                const auto order=integer(v[1]);
                const auto it=records.find(std::uint64_t(order>0?order:0));
                if(order<=0 || it==records.end() ||
                   it->second.owner!=principal || it->second.source_ship!=ship ||
                   it->second.stage!=IntercolonyFreightStage::Loaded) {
                    announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
                }else{
                    const auto carrier=it->second.carrier_ship;
                    const auto dest=it->second.destination_ship;
                    const auto at=world.world().ship_frames().find(dest);
                    const auto request=++nonce;
                    if(at==world.world().ship_frames().end() ||
                       !at->second.anchor_body ||
                       !world.begin_celestial_transit(
                           principal,carrier,at->second.anchor_body,request))
                        announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
                    else announce(world.world(),"INTERFREIGHT_DEPARTED|"+
                        std::to_string(order)+"|"+std::to_string(request)+"|"+
                        std::to_string(carrier)+"|"+std::to_string(dest));
                }
            }else if(v[0]=="INTERFREIGHT_DELIVER" && v.size()==2) {
                const auto order=integer(v[1]);
                const auto it=records.find(std::uint64_t(order>0?order:0));
                if(order<=0 || it==records.end() ||
                   it->second.source_ship!=ship ||
                   !world.deliver_intercolony_freight(principal,std::uint64_t(order)))
                    announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
                else announce(world.world(),"INTERFREIGHT_DELIVERED|"+
                    std::to_string(order)+"|"+std::to_string(world.generation()));
            }else announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
        }catch(const std::exception&) {
            announce(world.world(),"ERROR|INTERFREIGHT_DENIED");
        }
        return;
    }
    // The owner's LOCAL work board: player posts a finite pickup contract;
    // the named NPC pilot then fulfills it via existing physical shuttle
    // flights and stock transfers on committed simulation ticks.
    if(!v.empty() && (v[0]=="LOCAL_WORK_POST" ||
                       v[0]=="LOCAL_WORK_POST_INTERCOLONY" ||
                       v[0]=="LOCAL_WORK_CANCEL" ||
                       v[0]=="LOCAL_WORK_STATUS")) {
        try {
            if(v[0]=="LOCAL_WORK_STATUS" && v.size()==1) {
                // Show unfinished player orders first; durable old history
                // must not displace the actual work the NPCs still have to do.
                // No second job ledger: only references to canonical records.
                const auto& board=world.world().local_delivery_board();
                std::uint64_t count=0;
                const auto emit=[&](std::uint64_t id,
                                    const LocalDeliveryOrder& job) {
                    const auto kind=job.cargo==ShuttleCargo::Water?"WATER":
                                    job.cargo==ShuttleCargo::Food?"FOOD":"NUTRIENTS";
                    announce(world.world(),"LOCAL_WORK_ITEM|"+std::to_string(id)+"|"+
                        std::to_string(std::get<0>(job.site))+"|"+
                        std::to_string(std::get<1>(job.site))+"|"+
                        std::to_string(std::get<2>(job.site))+"|"+kind+"|"+
                        std::to_string(job.kilograms)+"|"+
                        std::to_string(unsigned(job.stage))+"|"+
                        std::to_string(job.assigned_pilot));
                    ++count;
                };
                std::vector<std::pair<std::uint64_t,const LocalDeliveryOrder*>> history;
                for(const auto& [id,job]:board) {
                    if(job.owner!=principal || job.board_ship!=ship)continue;
                    if(job.stage==LocalDeliveryStage::Complete ||
                       job.stage==LocalDeliveryStage::Cancelled)
                        history.emplace_back(id,&job);
                    else if(count<64)emit(id,job);
                }
                // The newest *posted* completed jobs take remaining display
                // slots; active orders cannot be hidden behind old history.
                std::sort(history.begin(),history.end(),
                    [](const auto& a,const auto& b) {
                        if(a.second->created_tick!=b.second->created_tick)
                            return a.second->created_tick>b.second->created_tick;
                        return a.first>b.first;
                    });
                for(const auto& [id,job]:history) {
                    if(count>=64)break;
                    emit(id,*job);
                }
                announce(world.world(),"LOCAL_WORK_DONE|"+std::to_string(count));
            }else if(v[0]=="LOCAL_WORK_POST_INTERCOLONY" && v.size()==9) {
                const auto body=integer(v[1]),lat=integer(v[2]),lon=integer(v[3]);
                const auto kg=integer(v[5]),id=integer(v[6]),
                           carrier=integer(v[7]),destination=integer(v[8]);
                if(body<=0 || lat<0 || lat>=36 || lon<0 || lon>=72 ||
                   kg<=0 || kg>1000 || id<=0 ||
                   id>=(std::int64_t{1}<<50) ||
                   carrier<=0 || destination<=0)
                    throw std::invalid_argument("invalid intercolony work contract");
                ShuttleCargo cargo{};
                if(v[4]=="WATER")cargo=ShuttleCargo::Water;
                else if(v[4]=="FOOD")cargo=ShuttleCargo::Food;
                else if(v[4]=="NUTRIENTS")cargo=ShuttleCargo::Nutrients;
                else throw std::invalid_argument("unsupported freight resource");
                if(!world.post_intercolony_work_order(principal,ship,
                    PlanetarySiteKey{std::uint64_t(body),std::uint16_t(lat),
                                     std::uint16_t(lon),1},
                    cargo,kg,std::uint64_t(id),EntityId(carrier),
                    EntityId(destination))) {
                    announce(world.world(),"ERROR|LOCAL_WORK_DENIED");
                    return;
                }
                announce(world.world(),"LOCAL_WORK_POSTED|"+std::to_string(id)+"|"+
                         std::to_string(world.generation()));
                announce(world.world(),"INTERFREIGHT_PLANNED|"+
                    std::to_string(id)+"|"+std::to_string(carrier)+"|"+
                    std::to_string(destination)+"|"+std::to_string(world.generation()));
            }else if(v[0]=="LOCAL_WORK_POST" && v.size()==7) {
                const auto body=integer(v[1]),lat=integer(v[2]),lon=integer(v[3]);
                const auto kg=integer(v[5]),id=integer(v[6]);
                if(body<=0 || lat<0 || lat>=36 || lon<0 || lon>=72 ||
                   kg<=0 || kg>1000 || id<=0 ||
                   id>=(std::int64_t{1}<<50))
                    throw std::invalid_argument("invalid local transport contract");
                ShuttleCargo cargo{};
                if(v[4]=="WATER")cargo=ShuttleCargo::Water;
                else if(v[4]=="FOOD")cargo=ShuttleCargo::Food;
                else if(v[4]=="NUTRIENTS")cargo=ShuttleCargo::Nutrients;
                else throw std::invalid_argument("unsupported contract resource");
                if(!world.post_local_delivery_order(principal,ship,
                    PlanetarySiteKey{std::uint64_t(body),std::uint16_t(lat),
                                     std::uint16_t(lon),1},
                    cargo,kg,std::uint64_t(id))) {
                    announce(world.world(),"ERROR|LOCAL_WORK_DENIED");
                    return;
                }
                announce(world.world(),"LOCAL_WORK_POSTED|"+std::to_string(id)+"|"+
                         std::to_string(world.generation()));
            }else if(v[0]=="LOCAL_WORK_CANCEL" && v.size()==2) {
                const auto id=integer(v[1]);
                if(id<=0 || !world.cancel_local_delivery_order(
                    principal,std::uint64_t(id))) {
                    announce(world.world(),"ERROR|LOCAL_WORK_DENIED");
                    return;
                }
                announce(world.world(),"LOCAL_WORK_CANCELLED|"+std::to_string(id)+"|"+
                         std::to_string(world.generation()));
            }else throw std::invalid_argument("bad local transport command");
        }catch(const std::exception&){
            announce(world.world(),"ERROR|LOCAL_WORK_DENIED");
        }
        return;
    }
    // PLAY-13D.4C.4: authenticated, owner-scoped native orbital shuttle
    // commands. The relay supplies strictly typed fields; native validates again.
    if(!v.empty() && v[0]=="PLANET_SHUTTLE") {
        try {
            if(v.size()<5 || v.size()>8)
                throw std::invalid_argument("invalid shuttle command arity");
            const auto body=integer(v[1]),lat=integer(v[2]),lon=integer(v[3]);
            if(body<=0 || lat<0 || lat>=36 || lon<0 || lon>=72)
                throw std::invalid_argument("invalid shuttle region");
            const PlanetarySiteKey key{std::uint64_t(body),std::uint16_t(lat),
                std::uint16_t(lon),1};
            OrbitalShuttleCommand command{};
            command.owner=principal;
            command.ship=ship;
            command.site=key;
            const auto action=v[4];
            if(action=="STATUS" && v.size()==5)
                command.action=OrbitalShuttleAction::Status;
            else if(action=="BUILD" && v.size()==5)
                command.action=OrbitalShuttleAction::Build;
            else if(action=="PILOT" && v.size()==6) {
                command.action=OrbitalShuttleAction::AssignPilot;
                const auto npc=integer(v[5]);
                if(npc<=0)throw std::invalid_argument("invalid pilot");
                command.npc=std::uint64_t(npc);
            } else if((action=="BOARD" || action=="RELEASE") &&
                      v.size()==6) {
                command.action=action=="BOARD"?
                    OrbitalShuttleAction::BookPassenger:
                    OrbitalShuttleAction::ReleasePassenger;
                const auto npc=integer(v[5]);
                if(npc<=0)throw std::invalid_argument("invalid passenger");
                command.npc=std::uint64_t(npc);
            } else if((action=="DISEMBARK" || action=="REBOARD") &&
                      v.size()==6) {
                command.action=action=="DISEMBARK"?
                    OrbitalShuttleAction::Disembark:
                    OrbitalShuttleAction::Reboard;
                const auto npc=integer(v[5]);
                if(npc<=0)throw std::invalid_argument("invalid surface visitor");
                command.npc=std::uint64_t(npc);
            } else if((action=="DUTY_ON" || action=="DUTY_OFF") &&
                      v.size()==6) {
                command.action=action=="DUTY_ON"?
                    OrbitalShuttleAction::AssignVisitorDuty:
                    OrbitalShuttleAction::ReleaseVisitorDuty;
                const auto npc=integer(v[5]);
                if(npc<=0)throw std::invalid_argument("invalid visitor duty");
                command.npc=std::uint64_t(npc);
            } else if((action=="LOAD" || action=="UNLOAD") &&
                      v.size()==7) {
                command.action=action=="LOAD"?OrbitalShuttleAction::Load:
                    OrbitalShuttleAction::Unload;
                if(v[5]!="WATER" && v[5]!="FOOD" && v[5]!="NUTRIENTS")
                    throw std::invalid_argument("invalid cargo");
                command.water=v[5]=="WATER";
                command.cargo=v[5]=="NUTRIENTS"?
                    ShuttleCargo::Nutrients:
                    (command.water?ShuttleCargo::Water:ShuttleCargo::Food);
                command.kilograms=integer(v[6]);
            } else if((action=="DESCEND" || action=="ASCEND") &&
                      v.size()==6) {
                command.action=action=="DESCEND"?OrbitalShuttleAction::Descend:
                    OrbitalShuttleAction::Ascend;
                const auto request=integer(v[5]);
                if(request<=0)throw std::invalid_argument("invalid flight ID");
                command.request_id=std::uint64_t(request);
            } else throw std::invalid_argument("invalid shuttle action");
            const auto result=execute_orbital_shuttle(world,command);
            if(!result.accepted)
                throw std::invalid_argument("shuttle refused");
            if(result.mutated)
                announce(world.world(),"PLANET_SHUTTLE_COMMITTED|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|"+action+"|"+
                    std::to_string(result.generation));
            if(!result.present) {
                announce(world.world(),"PLANET_SHUTTLE_STATUS|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|0|0|0|0|0|0|0|0|0|"+
                    std::to_string(result.generation));
            } else {
                const auto& sh=result.shuttle;
                announce(world.world(),"PLANET_SHUTTLE_STATUS|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|1|"+std::to_string(sh.flight_phase)+"|"+
                    std::to_string(sh.pilot_npc)+"|"+std::to_string(sh.water_kg)+"|"+
                    std::to_string(sh.food_kg)+"|"+std::to_string(sh.arrival_tick)+"|"+
                    std::to_string(sh.flight_request)+"|"+
                    std::to_string(sh.descent_receipt)+"|"+
                    std::to_string(sh.ascent_receipt)+"|"+
                    std::to_string(result.generation)+
                    (sh.nutrients_kg>0?"|"+std::to_string(sh.nutrients_kg):""));
            }
            // Canonical sorted manifest of real NPC identities, not an
            // invented second passenger roster in the temporary UI.
            if(result.present) {
                for(const auto id:result.shuttle.passenger_ids)
                    announce(world.world(),"PLANET_SHUTTLE_PASSENGER|"+
                        std::to_string(body)+"|"+std::to_string(lat)+"|"+
                        std::to_string(lon)+"|"+std::to_string(id));
                announce(world.world(),"PLANET_SHUTTLE_MANIFEST_DONE|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|"+
                    std::to_string(result.shuttle.passenger_ids.size()));
                for(const auto id:result.shuttle.disembarked_ids)
                    announce(world.world(),"PLANET_SHUTTLE_VISITOR|"+
                        std::to_string(body)+"|"+std::to_string(lat)+"|"+
                        std::to_string(lon)+"|"+std::to_string(id));
                announce(world.world(),"PLANET_SHUTTLE_VISITORS_DONE|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|"+
                    std::to_string(result.shuttle.disembarked_ids.size()));
                for(const auto id:result.shuttle.duty_ids)
                    announce(world.world(),"PLANET_SHUTTLE_WORKER|"+
                        std::to_string(body)+"|"+std::to_string(lat)+"|"+
                        std::to_string(lon)+"|"+std::to_string(id));
                announce(world.world(),"PLANET_SHUTTLE_WORKERS_DONE|"+
                    std::to_string(body)+"|"+std::to_string(lat)+"|"+
                    std::to_string(lon)+"|"+
                    std::to_string(result.shuttle.duty_ids.size()));
            }
        }catch(const std::exception&){
            announce(world.world(),"ERROR|PLANET_SHUTTLE_DENIED");
        }
        return;
    }
    // PLAY-13D.4B: owner administrative policy; unlike a physical transfer
    // it remains available after the ship leaves this planet.
    if(v.size()==5 && v[0]=="PLANET_POLICY") {
        try {
            const auto body=integer(v[1]),lat=integer(v[2]),
                       lon=integer(v[3]),policy=integer(v[4]);
            if(body<=0 || lat<0 || lat>=36 || lon<0 || lon>=72 ||
               policy<0 || policy>4)
                throw std::invalid_argument("invalid planetary policy");
            if(!world.set_planetary_workforce_policy(
                principal,std::uint64_t(body),std::uint16_t(lat),
                std::uint16_t(lon),std::uint8_t(policy))) {
                announce(world.world(),"ERROR|PLANET_POLICY_DENIED");
                return;
            }
            announce(world.world(),"PLANET_POLICY_COMMITTED|"+
                std::to_string(body)+"|"+std::to_string(lat)+"|"+
                std::to_string(lon)+"|"+std::to_string(policy)+"|"+
                std::to_string(world.generation()));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|PLANET_POLICY_DENIED");
        }
        return;
    }
    // PLAY-13D.3: region-scoped physical actions. Player must have
    // previously observed this exact region/revision in the private archive.
    if(!v.empty() && (v[0]=="PLANET_BUILD" || v[0]=="PLANET_EQUIP" ||
       v[0]=="PLANET_SUPPLY" || v[0]=="PLANET_GROW" ||
       v[0]=="PLANET_EXTRACT" || v[0]=="PLANET_RETRIEVE" ||
       v[0]=="PLANET_SUPPORT")) {
        try {
            const auto type=v[0];
            const bool build=type=="PLANET_BUILD";
            const bool equip=type=="PLANET_EQUIP";
            const bool grow=type=="PLANET_GROW";
            if(v.size()!=(build?(v.size()==6?6:4):(equip||grow)?5:6))
                throw std::invalid_argument("planet operation arity");
            const auto body=integer(v[1]);
            const auto latitude=integer(v[2]),longitude=integer(v[3]);
            if(body<=0 || latitude<0 || latitude>=planetary::kWatershedRows ||
               longitude<0 || longitude>=planetary::kWatershedColumns ||
               !knowledge)
                throw std::invalid_argument("planet operation coordinates");
            const auto site=world.world().scan_planetary_geography(
                principal,ship,std::uint64_t(body),
                std::uint16_t(latitude),std::uint16_t(longitude));
            if(!site)throw std::invalid_argument("planet operation unauthorized");
            const auto known=knowledge->known_planetary_region(
                principal,std::uint32_t(std::uint64_t(body)>>32),
                std::uint64_t(body),std::uint16_t(latitude),
                std::uint16_t(longitude),site->key.revision);
            if(!known || known->elevation!=site->landscape.region.elevation ||
               known->ocean!=std::uint8_t(site->landscape.region.surface_water) ||
               known->terrain!=std::uint8_t(site->landscape.region.terrain) ||
               known->watercourse!=std::uint8_t(site->landscape.watercourse) ||
               known->drainage!=site->catchment_runoff)
                throw std::invalid_argument("unsurveyed planetary region");
            bool success=false;
            if(build) {
                if(v.size()==6) {
                    const auto first=integer(v[4]),second=integer(v[5]);
                    if(first<=0 || second<=0)
                        throw std::invalid_argument("invalid worker selection");
                    success=world.build_planetary_site(
                        principal,ship,std::uint64_t(body),
                        std::uint16_t(latitude),std::uint16_t(longitude),
                        std::uint64_t(first),std::uint64_t(second));
                } else success=world.build_planetary_site(
                    principal,ship,std::uint64_t(body),
                    std::uint16_t(latitude),std::uint16_t(longitude));
            }
            else if(equip) {
                if(v[4]!="FARM" && v[4]!="EXTRACT")
                    throw std::invalid_argument("invalid equipment");
                success=world.equip_planetary_site(
                    principal,ship,std::uint64_t(body),
                    std::uint16_t(latitude),std::uint16_t(longitude),
                    v[4]=="FARM");
            } else if(grow) {
                success=world.cultivate_planetary_site(principal,ship,
                    std::uint64_t(body),std::uint16_t(latitude),
                    std::uint16_t(longitude),integer(v[4]));
            } else {
                const auto kind=v[4],kg=integer(v[5]);
                if(type=="PLANET_SUPPORT") {
                    if(kind!="FOOD" && kind!="OXYGEN")
                        throw std::invalid_argument("invalid surface crew supply material");
                    success=world.support_planetary_crew(principal,ship,
                        std::uint64_t(body),std::uint16_t(latitude),
                        std::uint16_t(longitude),kind=="OXYGEN",kg);
                } else if(type=="PLANET_SUPPLY") {
                    if(kind!="WATER" && kind!="NUTRIENTS")
                        throw std::invalid_argument("invalid supply material");
                    success=world.supply_planetary_site(principal,ship,
                        std::uint64_t(body),std::uint16_t(latitude),
                        std::uint16_t(longitude),kind=="WATER",kg);
                } else if(type=="PLANET_EXTRACT") {
                    if(kind!="WATER" && kind!="NUTRIENTS")
                        throw std::invalid_argument("invalid extraction material");
                    success=world.extract_planetary_site(principal,ship,
                        std::uint64_t(body),std::uint16_t(latitude),
                        std::uint16_t(longitude),kind=="WATER",kg);
                } else {
                    if(kind!="FOOD" && kind!="WATER")
                        throw std::invalid_argument("invalid retrieval material");
                    success=world.retrieve_planetary_site(principal,ship,
                        std::uint64_t(body),std::uint16_t(latitude),
                        std::uint16_t(longitude),kind=="FOOD",kg);
                }
            }
            if(!success){announce(world.world(),"ERROR|PLANET_OPERATION_DENIED");return;}
            announce(world.world(),"PLANET_OPERATION_COMMITTED|"+
                std::to_string(body)+"|"+std::to_string(latitude)+"|"+
                std::to_string(longitude)+"|"+type+"|"+
                std::to_string(world.generation()));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|PLANET_OPERATION_DENIED");
        }
        return;
    }
    // PGF-01B.2: native authorized planetary regional survey; map-backed
    // terrain is the same result exposed by Simulation to other consumers.
    if(v.size()==4 && v[0]=="PLANET_SURVEY"){
        try{
            const auto body=integer(v[1]);
            const auto latitude=integer(v[2]);
            const auto longitude=integer(v[3]);
            if(body<=0 || latitude<0 || latitude>=planetary::kWatershedRows ||
               longitude<0 || longitude>=planetary::kWatershedColumns)
                throw std::invalid_argument("invalid planetary region");
            const auto site=world.world().scan_planetary_geography(
                principal,ship,static_cast<std::uint64_t>(body),
                static_cast<std::uint16_t>(latitude),
                static_cast<std::uint16_t>(longitude));
            if(!site){announce(world.world(),"ERROR|PLANET_SURVEY_DENIED");return;}
            // A region is reported as discovered only after its private
            // immutable geography is written to the durable survey index.
            if(!knowledge){announce(world.world(),"ERROR|PLANET_SURVEY_ARCHIVE_UNAVAILABLE");return;}
            knowledge->record_planetary_region(principal,
                static_cast<std::uint32_t>(body>>32),*site,
                world.world().tick()+1);
            const auto& r=site->landscape.region;
            announce(world.world(),"PLANET_REGION|"+std::to_string(body)+"|"+
                std::to_string(latitude)+"|"+std::to_string(longitude)+"|"+
                std::to_string(r.elevation)+"|"+std::to_string(r.surface_water?1:0)+"|"+
                std::to_string(static_cast<unsigned>(r.terrain))+"|"+
                std::to_string(static_cast<unsigned>(site->landscape.watercourse))+"|"+
                std::to_string(site->catchment_runoff)+"|"+
                std::to_string(site->downstream_latitude)+"|"+
                std::to_string(site->downstream_longitude)+"|"+
                std::to_string(site->key.revision));
        }catch(const std::exception&){
            announce(world.world(),"ERROR|PLANET_SURVEY_DENIED");
        }
        return;
    }
    if(v.size()==1 && v[0]=="SYSTEM_CELESTIAL"){
        const auto profile=world.world().scan_celestial_profile(principal,ship);
        if(!profile){announce(world.world(),"ERROR|INVALID_INPUT");return;}
        if(knowledge)try {
            const auto* carrier=world.world().find(ship);
            const auto system=carrier?carrier->system_id:0;
            if(!system)throw std::runtime_error("missing active survey system");
            knowledge->record_system_catalogue(principal,system,world.world().tick()+1,*profile);
            const auto history=knowledge->known_system_catalogue(principal,system);
            if(history)
                announce(world.world(),"CELESTIAL_HISTORY|"+
                    std::to_string(history->first_tick)+"|"+
                    std::to_string(history->last_tick));
        }catch(const std::exception& ex){
            std::cerr<<"PLAY13B_CATALOGUE_WRITE_ERROR "<<ex.what()<<'\n';
        }
        announce(world.world(),"CELESTIAL_HEADER|"+
            std::to_string(static_cast<unsigned>(profile->architecture))+"|"+
            std::to_string(static_cast<unsigned>(profile->morphology))+"|"+
            std::to_string(profile->star_count)+"|"+
            std::to_string(profile->orbital_bands.size()));
        for(const auto& band:profile->orbital_bands)
            announce(world.world(),"CELESTIAL_BAND|"+
                std::to_string(band.ordinal)+"|"+
                std::to_string(band.radius_milli_au)+"|"+
                std::to_string(band.eccentricity_permille)+"|"+
                std::to_string(static_cast<unsigned>(band.zone)));
        const bodies=world.world().scan_physical_celestials(principal,ship);
        for(const auto& body:bodies)
            announce(world.world(),"CELESTIAL_BODY|"+std::to_string(body.id)+"|"+
                std::to_string(body.parent_id)+"|"+
                std::to_string(static_cast<unsigned>(body.kind))+"|"+
                std::to_string(body.orbital_radius_milli_au)+"|"+
                std::to_string(body.position.x_mm)+"|"+
                std::to_string(body.position.y_mm)+"|"+
                std::to_string(body.position.z_mm)+"|"+
                std::to_string(body.physical_radius_mm));
        // Rare viable *prospects*, not free supplies or a guarantee of safety.
        for(const auto& oasis:world.world().survey_oasis_prospects(principal,ship))
            announce(world.world(),"OASIS_PROSPECT|"+std::to_string(oasis.body)+"|"+
                std::to_string(unsigned(oasis.water))+"|"+
                std::to_string(unsigned(oasis.energy))+"|"+
                std::to_string(unsigned(oasis.nutrients))+"|"+
                std::to_string(unsigned(oasis.shelter)));
        // A fitted scanner can locate a finite reserve's surface waypoint.
        // This is exploration metadata, never authorization to harvest.
        for(const auto& oasis:world.world().survey_oasis_prospects(principal,ship)){
            const auto source=world.world().planetary_oasis_source_region(
                principal,ship,oasis.body);
            if(source)announce(world.world(),"OASIS_REGION|"+
                std::to_string(oasis.body)+"|"+
                std::to_string(source->latitude)+"|"+
                std::to_string(source->longitude)+"|"+
                std::to_string(source->revision));
        }
        announce(world.world(),"CELESTIAL_DONE|"+
            std::to_string(profile->orbital_bands.size()));
        return;
    }
    if(v.size()==1 && v[0]=="SYSTEM_ENVIRONMENT"){
        const auto reading=world.world().scan_environment(principal,ship);
        if(!reading){announce(world.world(),"ERROR|INVALID_INPUT");return;}
        announce(world.world(),"ENVIRONMENT_STATUS|"+
            std::to_string(reading->system)+"|"+
            std::to_string(reading->magnetized_region?1:0)+"|"+
            std::to_string(reading->dust_region?1:0)+"|"+
            std::to_string(reading->ship_speed_mm_per_tick));
        return;
    }
    if(v.size()==2 && v[0]=="SIGNATURES"){
        announce(world.world(),"SIGNATURE_RESET");
        try{
        const auto radius=integer(v[1]);
        const auto found=world.world().coarse_probe_signatures(principal,ship,radius);
        for(const auto& item:found)
            announce(world.world(),"SIGNATURE_CONTACT|"+std::to_string(item.id)+"|"+
                std::to_string(item.coarse.x)+"|"+std::to_string(item.coarse.y)+"|"+
                std::to_string(item.uncertainty_mm));
        announce(world.world(),"SIGNATURE_DONE|"+std::to_string(found.size()));
        }catch(const std::exception&){announce(world.world(),"ERROR|INVALID_INPUT");}
        return;
    }
    if((v.size()==3 && v[0]=="PROBE_FOCUS") ||
       (v.size()==2 && v[0]=="PROBE_CANCEL")){
        try {
            const auto id=integer(v[1]);
            const auto site=v.size()==3?integer(v[2]):0;
            if(id<1||id>1000000000000000LL||
               (v.size()==3&&(site<1||site>100000000)))
                throw std::invalid_argument("invalid probe lock-on");
            Command command{};
            command.id=++nonce;command.sequence=command.id;
            command.execute_tick=world.world().tick()+1;
            command.issuer=principal;command.actor=ship;
            command.target=static_cast<EntityId>(id);
            command.amount=site;
            command.action=v.size()==3?Action::FocusSurveyProbe:Action::CancelSurveyFocus;
            if(!world.submit(command))
                announce(world.world(),"ERROR|INVALID_INPUT");
        }catch(const std::exception&){
            announce(world.world(),"ERROR|INVALID_INPUT");
        }
        return;
    }
    if(v.size()==2 && v[0]=="PROBE_SCAN"){
        try{
            const auto id=integer(v[1]);
            if(id<1 || id>1000000000000000LL)throw std::invalid_argument("bad probe id");
            Command c{};c.id=++nonce;c.sequence=c.id;c.execute_tick=world.world().tick()+1;
            c.issuer=principal;c.actor=ship;c.target=static_cast<EntityId>(id);
            c.action=Action::ScanSurveyProbe;
            if(world.submit(c))pending_probe_scans.insert(c.id);
            else announce(world.world(),"ERROR|INVALID_INPUT");
        }catch(const std::exception&){announce(world.world(),"ERROR|INVALID_INPUT");}
        return;
    }
    if(v.size()==3 && v[0]=="SAFETY"){
        try{
            const auto site=integer(v[1]);
            if(site<=0||site>100000000)throw std::invalid_argument("invalid site");
            if(v[2]=="ASSESS"){
                const auto a=colony_safety::assess(world.world(),ship,static_cast<EntityId>(site));
                announce(world.world(),"SAFETY_CHECK|"+std::to_string(a.site)+"|"+
                    std::to_string(static_cast<int>(a.risk)));
                return;
            }
            colony_safety::Order order{};
            if(v[2]=="PRODUCTION_PREFLIGHT")order=colony_safety::Order::SetLocalProduction;
            else if(v[2]=="SEAL_EXIT")order=colony_safety::Order::SealOccupiedExit;
            else if(v[2]=="DISABLE_DISTRESS")order=colony_safety::Order::DisableDistress;
            else if(v[2]=="DUMP_ATMOSPHERE")order=colony_safety::Order::DumpOccupiedAtmosphere;
            else if(v[2]=="BLOCK_REPAIR")order=colony_safety::Order::PreventEmergencyRepair;
            else throw std::invalid_argument("unknown safety order");
            const auto d=world.record_colony_safety_preflight(ship,static_cast<EntityId>(site),principal,order);
            announce(world.world(),"SAFETY_ORDER|"+std::to_string(d.assessment.site)+"|"+
                (d.allowed?"PREFLIGHT_ONLY":"REFUSED")+"|"+
                std::to_string(static_cast<int>(d.assessment.risk)));
            return;
        }catch(const std::exception&){announce(world.world(),"ERROR|INVALID_SAFETY_ORDER");return;}
    }
    Command c{};c.issuer=principal;c.actor=ship;c.execute_tick=world.world().tick()+1;
    c.id=++nonce;c.sequence=c.id;
    try {
        if(v.size()==3 && v[0]=="THRUST"){
            auto x=integer(v[1]),y=integer(v[2]);
            if(x<-1||x>1||y<-1||y>1)throw std::invalid_argument("bad thrust");
            if(!fleet.is_ready(world.world(),fleet.piloted()))throw std::invalid_argument("pilot unavailable");
            c.actor=fleet.piloted();
            c.action=Action::Thrust;c.thrust_x=static_cast<int>(x);c.thrust_y=static_cast<int>(y);
        }else if(v.size()==3 && v[0]=="SALVAGE_WRECK"){
            const auto target=integer(v[1]),amount=integer(v[2]);
            if(target<=0 || target>100000000 || amount<1 || amount>10)
                throw std::invalid_argument("invalid wreck salvage");
            c.action=Action::SalvageWreck;
            c.target=static_cast<EntityId>(target);c.amount=amount;
        }else if(v.size()==2 && v[0]=="DRONE_LAUNCH"){
            const auto target=integer(v[1]);
            if(target<1 || target>100000000)
                throw std::invalid_argument("invalid geological drone target");
            c.action=Action::LaunchGeologicalDrone;
            c.target=static_cast<EntityId>(target);
        }else if(v.size()==1 && v[0]=="PROBE_DEPLOY"){
            c.action=Action::DeploySurveyProbe;
        }else if(v.size()==2 && (v[0]=="PROBE_SCAN" || v[0]=="PROBE_RECOVER")){
            const auto target=integer(v[1]);
            if(target<1 || target>1000000000000000LL)
                throw std::invalid_argument("invalid deployed survey probe");
            c.target=static_cast<EntityId>(target);
            c.action=v[0]=="PROBE_SCAN"?Action::ScanSurveyProbe:Action::RecoverSurveyProbe;
        }else if(v.size()==2 && v[0]=="FIT_SCANNER"){
            const auto mode=integer(v[1]);
            if(mode!=1 && mode!=-1)throw std::invalid_argument("invalid scanner fit mode");
            c.action=Action::ConfigureExplorationScanner;c.amount=mode;
        }else if(v.size()==2 && v[0]=="CYBER_FIT"){
            const auto mode=integer(v[1]);
            if(mode!=3 && mode!=-3 && mode!=4 && mode!=-4 &&
               mode!=5 && mode!=-5 && mode!=6 && mode!=-6)
                throw std::invalid_argument("invalid cyber/stealth fitting");
            if(!fleet.is_ready(world.world(),fleet.piloted()))
                throw std::invalid_argument("cockpit unavailable");
            c.actor=fleet.piloted();
            c.action=Action::ConfigureCyberModule;c.amount=mode;
        }else if(v.size()==3 &&
                 (v[0]=="CYBER_POWER" || v[0]=="CYBER_ISOLATE")){
            const auto slot=integer(v[1]),enabled=integer(v[2]);
            if(slot<0 || slot>1 || (enabled!=0 && enabled!=1))
                throw std::invalid_argument("invalid cyber control");
            if(!fleet.is_ready(world.world(),fleet.piloted()))
                throw std::invalid_argument("cockpit unavailable");
            c.actor=fleet.piloted();
            c.action=v[0]=="CYBER_POWER"?Action::SetHardpointPower:
                Action::SetCyberIsolation;
            c.amount=slot*2+enabled;
        }else if(v.size()==2 && v[0]=="CYBER_ATTACK"){
            const auto target=integer(v[1]);
            if(target<=0 || target>1000000000000000LL)
                throw std::invalid_argument("invalid cyber target");
            if(!fleet.is_ready(world.world(),fleet.piloted()))
                throw std::invalid_argument("cockpit unavailable");
            c.actor=fleet.piloted();c.target=static_cast<EntityId>(target);
            c.action=Action::HackConfuse;
        }else if(v.size()==2 && v[0]=="FIRE_TRAINING"){
            const auto id=integer(v[1]);
            if(id<=0||id>100000000)throw std::invalid_argument("invalid combat target");
            c.action=Action::FireTrainingWeapon;c.target=static_cast<EntityId>(id);
        }else if(v.size()==1 && v[0]=="DOCK_BASE"){
            const auto* self=world.world().find(ship);
            if(!self)throw std::invalid_argument("ship missing");
            EntityId nearest_base=0;
            std::int64_t best=std::numeric_limits<std::int64_t>::max();
            for(const auto& [site,facility]:world.world().built_outposts()){
                (void)facility;
                const auto* e=world.world().find(site);
                if(!e || e->owner_id!=principal || e->system_id!=self->system_id)continue;
                const auto dx=e->position.x-self->position.x;
                const auto dy=e->position.y-self->position.y;
                const auto distance=dx*dx+dy*dy;
                if(!nearest_base || distance<best || (distance==best && site<nearest_base)){
                    nearest_base=site;best=distance;
                }
            }
            if(!nearest_base)throw std::invalid_argument("no owned outpost in system");
            c.action=Action::RequestDock;c.target=nearest_base;
        }else if(v.size()==1 && (v[0]=="UNDOCK_BASE" ||
                                v[0]=="UNLOAD_BASE" || v[0]=="LOAD_BASE")){
            const auto* self=world.world().find(ship);
            if(!self)throw std::invalid_argument("ship missing");
            const auto site=self->dock_station;
            const auto* e=world.world().find(site);
            if(v[0]!="UNDOCK_BASE" && (!e || e->owner_id!=principal ||
               !world.world().built_outposts().count(site)))
                throw std::invalid_argument("not docked at owned outpost");
            c.target=v[0]=="UNDOCK_BASE"?0:site;
            c.amount=v[0]=="UNDOCK_BASE"?0:1000; // load/unload only; native custody limits
            c.action=v[0]=="UNDOCK_BASE"?Action::RequestUndock:
                     v[0]=="UNLOAD_BASE"?Action::UnloadOre:Action::LoadOre;
        }else if(v.size()==2 && v[0]=="YAW"){
            const auto delta=integer(v[1]);
            if(delta!=15 && delta!=-15)throw std::invalid_argument("bad yaw");
            if(!fleet.is_ready(world.world(),fleet.piloted()))throw std::invalid_argument("pilot unavailable");
            c.actor=fleet.piloted();
            c.action=Action::Thrust;c.amount=delta;
        }else if(v.size()==2 && v[0]=="MINE"){
            c.amount=integer(v[1]);if(c.amount<1||c.amount>100)throw std::invalid_argument("bad mine amount");
            c.action=Action::Mine;c.target=nearest(world.world(),ship);
        }else if(v.size()==3 && v[0]=="MINE_TARGET"){
            c.target=static_cast<EntityId>(integer(v[1]));
            c.amount=integer(v[2]);
            if(!c.target||c.target>100000000 || c.amount<1||c.amount>100)
                throw std::invalid_argument("bad mineral target");
            c.action=Action::Mine;
        }else if(v.size()==3 && v[0]=="PROCESS"){
            c.amount=integer(v[2]);
            if(c.amount<1||c.amount>100000)throw std::invalid_argument("bad processing quantity");
            c.target=0;
            for(std::uint64_t i=1;i<=11;++i)if(v[1]==mineral_name(i))c.target=i;
            if(!c.target||c.target==9)throw std::invalid_argument("unrefinable mineral");
            c.action=Action::RefineMineral;
        }else if(v.size()==3 && v[0]=="FABRICATE_MATERIAL"){
            c.amount=integer(v[2]);
            if(c.amount<1||c.amount>1000 || (v[1]!="alloy" && v[1]!="aluminum"))
                throw std::invalid_argument("bad material recipe");
            c.target=v[1]=="alloy"?1:2;c.action=Action::FabricateMaterialModule;
        }else if(v.size()==2 && (v[0]=="PLANT" || v[0]=="HARVEST" ||
             v[0]=="RECYCLE_WATER" || v[0]=="RECYCLE_COMPOST" ||
             v[0]=="OXYGEN" || v[0]=="LIFE_WATER")) {
            c.amount=integer(v[1]);
            if(c.amount<1 || c.amount>(v[0]=="LIFE_WATER"?100:10000))
                throw std::invalid_argument("invalid life-support batch");
            c.action=v[0]=="PLANT"?Action::PlantCrops:
              v[0]=="HARVEST"?Action::HarvestCrops:
              v[0]=="RECYCLE_WATER"?Action::RecycleWater:
              v[0]=="RECYCLE_COMPOST"?Action::RecycleCompost:
              v[0]=="OXYGEN"?Action::ProduceOxygen:Action::LoadLifeWater;
        }else if(v.size()==2 && v[0]=="FARM_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad outpost");
            c.action=Action::ConstructFarm;c.amount=1;
        }else if(v.size()==3 && v[0]=="FARM_STAFF"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000 || c.amount==0 ||
               c.amount<-8 || c.amount>8)throw std::invalid_argument("bad farm assignment");
            c.action=Action::AssignFarmWorkers;
        }else if(v.size()==4 && v[0]=="FARM_STOCK"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[3]);
            if(!c.target || c.target>100000000 || c.amount==0 ||
               c.amount<-10000 || c.amount>10000)throw std::invalid_argument("bad stock amount");
            const char* stocks[]={"food","water","oxygen","nutrients","co2"};
            for(int i=0;i<5;++i)if(v[2]==stocks[i])c.thrust_x=i+1;
            if(!c.thrust_x)throw std::invalid_argument("bad stock resource");
            c.action=Action::TransferFarmStock;
        }else if(v.size()==4 && (v[0]=="POLICY_SET" || v[0]=="POLICY_STAFF")){
            const auto id=integer(v[1]),domain=integer(v[2]),value=integer(v[3]);
            if(id<1 || id>100000000 || domain<0 || domain>3 || value<0 || value>3)
                throw std::invalid_argument("invalid policy request");
            c.target=static_cast<EntityId>(id);c.thrust_x=static_cast<int>(domain);
            c.amount=value;
            c.action=v[0]=="POLICY_SET"?Action::SetPolicyAssurance:Action::AssignPolicyInspectors;
        }else if(v.size()==2 && v[0]=="WATERWORKS_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad waterworks site");
            c.action=Action::ConstructWaterworks;c.amount=1;
        }else if(v.size()==3 && v[0]=="WATERWORKS_FEED"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000 || !c.amount ||
               c.amount%1000 || c.amount<-10000 || c.amount>10000)
                throw std::invalid_argument("bad sealed water delivery");
            c.action=Action::TransferWaterworksFeed;
        }else if(v.size()==2 && v[0]=="RECLAIMER_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad reclaimer site");
            c.action=Action::ConstructReclaimer;c.amount=1;
        }else if(v.size()==2 && v[0]=="CLINIC_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad clinic site");
            c.action=Action::ConstructClinic;c.amount=1;
        }else if(v.size()==3 && v[0]=="CLINIC_STAFF"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000 || c.amount<0 || c.amount>4)
                throw std::invalid_argument("bad clinic medics");
            c.action=Action::AssignClinicStaff;
        }else if(v.size()==4 && v[0]=="CLINIC_STOCK"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[3]);
            if(!c.target || c.target>100000000 || !c.amount ||
               c.amount<-10000 || c.amount>10000)throw std::invalid_argument("bad care supply count");
            c.thrust_x=v[2]=="water"?1:v[2]=="substrate"?2:0;
            if(!c.thrust_x)throw std::invalid_argument("bad care resource");
            c.action=Action::TransferClinicStock;
        }else if(v.size()==2 && v[0]=="COMMUNITY_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad hall site");
            c.action=Action::ConstructCommunity;c.amount=1;
        }else if(v.size()==3 && v[0]=="COMMUNITY_STAFF"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000 || c.amount<0 || c.amount>4)
                throw std::invalid_argument("bad hall staff");
            c.action=Action::AssignCommunityStaff;
        }else if(v.size()==4 && v[0]=="COMMUNITY_STOCK"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[3]);
            if(!c.target || c.target>100000000 || !c.amount ||
               c.amount<-10000 || c.amount>10000)throw std::invalid_argument("bad hall stock");
            c.thrust_x=v[2]=="food"?1:v[2]=="water"?2:0;
            if(!c.thrust_x)throw std::invalid_argument("bad hall resource");
            c.action=Action::TransferCommunityStock;
        }else if(v.size()==2 && (v[0]=="UTILITY_BUILD" || v[0]=="UTILITY_REPAIR")){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad utility site");
            c.action=v[0]=="UTILITY_BUILD"?Action::ConstructUtility:Action::RepairUtility;
            c.amount=1;
        }else if(v.size()==3 &&
           (v[0]=="UTILITY_SPARES" || v[0]=="UTILITY_STAFF" || v[0]=="UTILITY_AUTO")){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad utility site");
            if(v[0]=="UTILITY_SPARES"){
                if(!c.amount || c.amount<-32 || c.amount>32)
                    throw std::invalid_argument("bad spare delivery count");
                c.action=Action::TransferUtilitySpares;
            }else if(v[0]=="UTILITY_STAFF"){
                if(c.amount<0 || c.amount>8)
                    throw std::invalid_argument("bad technician assignment");
                c.action=Action::AssignUtilityTechnicians;
            }else{
                if(c.amount<0 || c.amount>1)throw std::invalid_argument("bad maintenance policy");
                c.action=Action::SetUtilityMaintenance;
            }
        }else if(v.size()==2 && v[0]=="HAB_BUILD"){
            c.target=static_cast<EntityId>(integer(v[1]));
            if(!c.target || c.target>100000000)throw std::invalid_argument("bad habitat site");
            c.action=Action::ConstructHabitat;c.amount=1;
        }else if(v.size()==3 && v[0]=="HAB_RESIDENTS"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[2]);
            if(!c.target || c.target>100000000 || !c.amount ||
               c.amount<-8 || c.amount>8)throw std::invalid_argument("bad residents");
            c.action=Action::AssignHabitatResidents;
        }else if(v.size()==4 && v[0]=="HAB_STOCK"){
            c.target=static_cast<EntityId>(integer(v[1]));c.amount=integer(v[3]);
            if(!c.target || c.target>100000000 || !c.amount ||
               c.amount<-10000 || c.amount>10000)throw std::invalid_argument("bad habitat stock");
            const char* stocks[]={"food","water","oxygen"};
            for(int i=0;i<3;++i)if(v[2]==stocks[i])c.thrust_x=i+1;
            if(!c.thrust_x)throw std::invalid_argument("bad habitat resource");
            c.action=Action::TransferHabitatStock;
        }else if(v.size()==2 && (v[0]=="REFINE"||v[0]=="FABRICATE")){
            c.amount=integer(v[1]);if(c.amount<1||c.amount>1000)throw std::invalid_argument("bad amount");
            c.action=v[0]=="REFINE"?Action::RefineOre:Action::FabricateModule;
        }else if(v.size()==1 && v[0]=="BUILD"){
            c.action=Action::ConstructOutpost;c.amount=1;
        }else throw std::invalid_argument("bad command");
        if(!world.submit(c))throw std::invalid_argument("rejected submission");
        const unsigned cyber_kind=
            c.action==Action::ConfigureCyberModule?1:
            (c.action==Action::SetHardpointPower ||
             c.action==Action::SetCyberIsolation)?2:
            c.action==Action::HackConfuse?3:0;
        if(cyber_kind)awaiting_cyber_results.emplace(c.id,cyber_kind);
        // The host emits the result only after the next committed tick.
    }catch(const std::exception&){announce(world.world(),"ERROR|INVALID_INPUT");}
}
}
int main(int argc,char** argv){
    if(argc!=4 && (argc!=5 || std::string(argv[4])!="--authenticated-transport")){
        std::cerr<<"usage: galaxy_colony_live_host SAVE.sqlite TICK_MS MAX_TICKS [--authenticated-transport]\n";return 2;
    }
    const bool authenticated_transport=argc==5;
    try{
#ifdef _WIN32
        if(_setmode(_fileno(stdout),_O_BINARY)==-1)throw std::runtime_error("binary stdout failed");
#endif
        auto ms=integer(argv[2]),max_ticks=integer(argv[3]);
        if(ms<20||ms>5000||max_ticks<5||max_ticks>1200)throw std::invalid_argument("invalid run limits");
        WorldWriterLease single_writer(argv[1]);
        Simulation genesis(0xB70B);
        const auto initial_ship=genesis.add_colony_ship(owner,{0,0},60,24);
        EntityId ship=initial_ship;
        genesis.provision_colony_genesis(initial_ship,1000,3000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.provision_starter_exploration_scanner(initial_ship);
        genesis.provision_starter_probe_kit(initial_ship);
        genesis.add_asteroid({2000,1000},40); // small reachable starter resource
        genesis.add_asteroid({15000,1000},30);
        genesis.add_training_drone({8000,0}); // uncrewed training opponent; no colony NPCs
        genesis.add_geological_asteroid({140000,0},AsteroidClass::Carbonaceous,70,1700);
        genesis.add_geological_asteroid({320000,90000},AsteroidClass::Silicate,95,2900);
        genesis.add_geological_asteroid({-220000,-90000},AsteroidClass::Metallic,55,5700);
        // No stations, other ships, markets, factions or gates at start.
        const bool was_saved=fs::exists(argv[1]);
        PersistentWorld world(argv[1],std::move(genesis));
        // v68: translate the nominal 10-second report into durable game ticks
        // using this host session's actual fixed-step duration.
        world.configure_probe_report_tick_ms(static_cast<std::uint64_t>(ms));
        // Existing saves are upgraded atomically on first new-host start.
        world.ensure_physical_celestial_systems();
        // PLAY-13C.2B: migrate current ships to durable legacy-local frames.
        world.ensure_ship_frame_registry();
        // PLAY-13C.4: durable, idempotent region seeding for pre-v73 arrivals.
        world.ensure_visited_local_regions();
        // PLAY-13C.5: v73 worlds migrate to validated v74 mineral profiles.
        world.ensure_local_environment_profiles();
        if(!world.world().colony_ship(ship)||!world.world().find(ship)||world.world().find(ship)->owner_id!=owner){
            // A populated production world can have stations before its first
            // player-owned colony ship. Do not assume the ship has entity ID 1.
            ship=0;
            for(const auto& [id,e]:world.world().entities())
                if(e.owner_id==owner&&world.world().colony_ship(id)){
                    if(!ship||id<ship)ship=id;
                }
            if(!ship)throw std::runtime_error("no colony ship for authenticated owner");
        }
        // PLAY-13B: the v67 world is the authoritative discovery source.
        // This sidecar is an owner-scoped, rebuildable navigation/survey index.
        // Every host restart replays committed native discoveries idempotently.
        std::unique_ptr<SurveyArchive> survey_knowledge;
        auto sync_survey_knowledge=[&]() {
            if(!survey_knowledge)return;
            try {
                std::set<std::uint32_t> systems;
                for(const auto& [key,discovery]:world.world().discoveries()) {
                    (void)key;
                    if(discovery.owner==owner)systems.insert(discovery.system);
                }
                for(const auto system:systems)
                    survey_knowledge->import_native_discoveries(world.world(),owner,system);
            }catch(const std::exception& e) {
                std::cerr<<"PLAY13B_SURVEY_INDEX_ERROR "<<e.what()<<'\n';
                survey_knowledge.reset(); // never discard the committed world
            }
        };
        try {
            survey_knowledge=std::make_unique<SurveyArchive>(
                std::string(argv[1])+".survey.sqlite");
            sync_survey_knowledge();
        }catch(const std::exception& e) {
            std::cerr<<"PLAY13B_SURVEY_INDEX_UNAVAILABLE "<<e.what()<<'\n';
            survey_knowledge.reset();
        }
        std::random_device rd;auto stream=(std::uint64_t(rd())<<32)^rd();if(!stream)stream=1;
        FleetSelection fleet(owner,ship);
        ServerViewBridge bridge(world.world(),stream,owner,fleet.piloted());
        std::cout<<encode_frame(bridge.snapshot())<<std::flush;
        announce(world.world(),was_saved?"RESUMED":"NEW_COLONY");
        status(world.world(),ship,true);
        cyber_status(world.world(),fleet.piloted());
        // The initial selection is always the actual home ship. A browser
        // may restore a prior preference, but native selection revalidates it.
        std::uint64_t nonce=900000000000ULL+world.generation();
        std::set<std::uint64_t> pending_scans,pending_probe_scans;
        std::size_t known_discoveries=world.world().discoveries().size();
        const auto report_count=[&](){
            return std::count_if(world.world().discoveries().begin(),
                world.world().discoveries().end(),
                [](const auto& record){return record.second.survey_taxonomy!=0;});
        };
        std::size_t known_reports=report_count();
        const auto period=std::chrono::milliseconds(ms);
        for(int i=0;i<max_ticks;++i){
            const auto start=Clock::now();
            poll_input();
            const auto to_apply=std::move(commands);commands.clear();
            for(const auto& line:to_apply){
                if(!authenticated_transport){apply(world,ship,fleet,line,nonce,owner,pending_scans,pending_probe_scans,survey_knowledge.get());continue;}
                // The trusted relay supplies a resolved account ID over our
                // private stdin pipe; unprefixed/forged/foreign commands fail
                // closed even if relay validation is accidentally bypassed.
                const auto first=line.find('|'),second=first==std::string::npos?first:line.find('|',first+1);
                try{
                    if(line.substr(0,first)!="AUTH"||second==std::string::npos)
                        throw std::invalid_argument("unauthenticated native command");
                    const auto principal=integer(line.substr(first+1,second-first-1));
                    if(principal!=static_cast<std::int64_t>(owner))
                        throw std::invalid_argument("foreign colony command");
                    apply(world,ship,fleet,line.substr(second+1),nonce,static_cast<PlayerId>(principal),pending_scans,pending_probe_scans,survey_knowledge.get());
                }catch(const std::exception&){announce(world.world(),"ERROR|UNAUTHORIZED_PRINCIPAL");}
            }
            const auto old=world.world().events().size();
            std::map<PlanetarySiteKey,std::uint8_t> prior_shuttle_phases;
            for(const auto& [key,sh]:world.world().planetary_shuttles())
                prior_shuttle_phases.emplace(key,sh.flight_phase);
            std::map<std::uint64_t,std::pair<std::uint64_t,EntityId>> prior_build;
            for(const auto& [id,order]:world.world().freighter_build_orders())
                if(order.owner==owner && order.source_ship==ship)
                    prior_build.emplace(id,std::make_pair(
                        order.progress,order.launched_ship));
            std::map<std::uint64_t,std::uint64_t> prior_yard;
            for(const auto& [id,yard]:world.world().shipyard_constructions())
                if(yard.owner==owner && yard.colony_ship==ship)
                    prior_yard.emplace(id,yard.progress);
            std::map<std::uint64_t,LocalDeliveryStage> prior_work;
            for(const auto& [id,job]:world.world().local_delivery_board())
                if(job.owner==owner && job.board_ship==ship)
                    prior_work.emplace(id,job.stage);
            std::map<std::uint64_t,IntercolonyFreightStage> prior_freight;
            for(const auto& [id,leg]:world.world().intercolony_freight())
                if(leg.owner==owner && leg.source_ship==ship)
                    prior_freight.emplace(id,leg.stage);
            world.step();
            // Run the same finite, original-world cyber command path on
            // committed ticks. There are no autonomous host-owned attackers
            // at genesis; only genuinely constructed NPC colonies with an
            // existing predatory government and fabricated emitter may act.
            const auto cyber_pulse=npc_cyber::supervise(world,owner);
            if(cyber_pulse.approaches){
                announce(world.world(),"NPC_CYBER_APPROACH_QUEUED|"+
                    std::to_string(cyber_pulse.last_approaching_ship)+"|"+
                    std::to_string(cyber_pulse.last_approach_target)+"|"+
                    std::to_string(cyber_pulse.last_flight));
            }
            if(cyber_pulse.queued){
                announce(world.world(),"NPC_CYBER_QUEUED|"+
                    std::to_string(cyber_pulse.last_attacking_ship)+"|"+
                    std::to_string(cyber_pulse.last_target_ship)+"|"+
                    std::to_string(cyber_pulse.last_command));
            }
            if(fleet.reconcile(world.world())) {
                announce(world.world(),"FLEET_FALLBACK|"+
                    std::to_string(fleet.piloted())+"|"+
                    std::to_string(fleet.home())+"|"+
                    std::to_string(world.generation()));
            }
            bridge.set_focus_ship(fleet.piloted());
            // Published only after atomic world-step commit. Each status
            // comes from the real work board; no guessed completion.
            for(const auto& [id,job]:world.world().local_delivery_board()) {
                const auto prev=prior_work.find(id);
                if(job.owner!=owner || job.board_ship!=ship ||
                   (prev!=prior_work.end() && prev->second==job.stage))continue;
                const auto good=job.cargo==ShuttleCargo::Water?"WATER":
                    job.cargo==ShuttleCargo::Food?"FOOD":"NUTRIENTS";
                announce(world.world(),"LOCAL_WORK_ITEM|"+std::to_string(id)+"|"+
                    std::to_string(std::get<0>(job.site))+"|"+
                    std::to_string(std::get<1>(job.site))+"|"+
                    std::to_string(std::get<2>(job.site))+"|"+good+"|"+
                    std::to_string(job.kilograms)+"|"+
                    std::to_string(unsigned(job.stage))+"|"+
                    std::to_string(job.assigned_pilot));
            }
            // Publish the actual sealed-hold transfer only after the
            // atomic authoritative world-step has committed. A planned job
            // is not itself proof of cargo custody or an arrival receipt.
            for(const auto& [id,leg]:world.world().intercolony_freight()) {
                const auto prev=prior_freight.find(id);
                if(leg.owner!=owner || leg.source_ship!=ship ||
                   (prev!=prior_freight.end() && prev->second==leg.stage))
                    continue;
                const auto good=leg.cargo==ShuttleCargo::Water?"WATER":
                    leg.cargo==ShuttleCargo::Food?"FOOD":"NUTRIENTS";
                announce(world.world(),"INTERFREIGHT_ITEM|"+
                    std::to_string(id)+"|"+std::to_string(leg.carrier_ship)+"|"+
                    std::to_string(leg.destination_ship)+"|"+good+"|"+
                    std::to_string(leg.kilograms)+"|"+
                    std::to_string(unsigned(leg.stage))+"|"+
                    std::to_string(leg.arrival_receipt));
                if(leg.stage==IntercolonyFreightStage::Loaded)
                    announce(world.world(),"INTERFREIGHT_LOADED|"+
                        std::to_string(id)+"|"+std::to_string(leg.carrier_ship)+"|"+
                        std::to_string(leg.destination_ship)+"|"+
                        std::to_string(world.generation()));
            }
            // Status is published only after this exact world.step's durable
            // SQLite commit. A new hull ID is evidence of a created Entity,
            // not a speculative construction bar in the temporary browser.
            for(const auto& [id,order]:world.world().freighter_build_orders()){
                if(order.owner!=owner || order.source_ship!=ship)continue;
                const auto old=prior_build.find(id);
                if(old!=prior_build.end() && old->second.first==order.progress &&
                   old->second.second==order.launched_ship)continue;
                announce(world.world(),"FREIGHTER_ITEM|"+
                    std::to_string(id)+"|"+std::to_string(order.yard_order)+"|"+
                    std::to_string(order.progress)+"|"+
                    std::to_string(order.required)+"|"+
                    std::to_string(order.launched_ship)+"|"+
                    std::to_string(world.generation()));
                if(order.finished())
                    announce(world.world(),"FREIGHTER_LAUNCHED|"+
                        std::to_string(id)+"|"+
                        std::to_string(order.launched_ship)+"|"+
                        std::to_string(world.generation()));
            }
            // Shipyard construction progress comes only from the committed
            // native tick, never from a guessed GUI progress timer.
            for(const auto& [id,yard]:world.world().shipyard_constructions()){
                const auto old=prior_yard.find(id);
                if(yard.owner!=owner || yard.colony_ship!=ship ||
                   (old!=prior_yard.end() && old->second==yard.progress))
                    continue;
                announce(world.world(),"SHIPYARD_ITEM|"+
                    std::to_string(id)+"|"+std::to_string(yard.colony_ship)+"|"+
                    std::to_string(yard.workers[0])+"|"+
                    std::to_string(yard.workers[1])+"|"+
                    std::to_string(yard.progress)+"|"+
                    std::to_string(yard.required)+"|"+
                    std::to_string(yard.stalled_ticks)+"|"+
                    std::to_string(world.generation()));
            }
            // Native world ticks complete shuttle flights automatically.
            // Publish only the committed phase transition to the owning player.
            for(const auto& [key,sh]:world.world().planetary_shuttles()) {
                const auto prior=prior_shuttle_phases.find(key);
                if(prior==prior_shuttle_phases.end() ||
                   prior->second==sh.flight_phase || sh.owner!=owner)continue;
                announce(world.world(),"PLANET_SHUTTLE_STATUS|"+
                    std::to_string(std::get<0>(key))+"|"+
                    std::to_string(std::get<1>(key))+"|"+
                    std::to_string(std::get<2>(key))+"|1|"+
                    std::to_string(sh.flight_phase)+"|"+
                    std::to_string(sh.pilot_npc)+"|"+
                    std::to_string(sh.water_kg)+"|"+
                    std::to_string(sh.food_kg)+"|"+
                    std::to_string(sh.arrival_tick)+"|"+
                    std::to_string(sh.flight_request)+"|"+
                    std::to_string(sh.descent_receipt)+"|"+
                    std::to_string(sh.ascent_receipt)+"|"+
                    std::to_string(world.generation())+
                    (sh.nutrients_kg>0?"|"+std::to_string(sh.nutrients_kg):""));
            }
            // Autonomy for explicitly authorized, actual carrier assignments:
            // no route is attempted until the finite planetary pickup is
            // unloaded into the origin ship and physically loaded on the
            // existing freighter. Every subsequent departure uses the same
            // native journal as the player's manual celestial navigation.
            std::vector<std::uint64_t> ready_freight;
            for(const auto& [order,leg]:world.world().intercolony_freight()){
                // Never let an NPC work scheduler commandeer the vessel
                // currently under the player's direct flight controls.
                if(leg.owner!=owner || leg.source_ship!=ship ||
                   leg.stage!=IntercolonyFreightStage::Loaded ||
                   leg.carrier_ship==fleet.piloted() ||
                   !fleet.is_ready(world.world(),leg.carrier_ship))continue;
                // A crew reassignment/death after accepting a job cannot
                // grant an uncrewed autonomous flight. Leave the finite
                // sealed cargo aboard until the ship is physically serviceable.
                const auto* operating=world.world().colony_ship(leg.carrier_ship);
                const auto* provisions=world.world().life_support(leg.carrier_ship);
                if(!operating || !provisions ||
                   provisions->food_kg<2*operating->crew ||
                   provisions->water_kg<2*operating->crew ||
                   provisions->oxygen_kg<2*operating->crew)continue;
                const auto at=world.world().ship_frames().find(leg.carrier_ship);
                if(at==world.world().ship_frames().end() ||
                   at->second.anchor_body!=leg.origin_body)continue;
                bool has_flight=false;
                for(const auto& [id,p]:world.world().pending_celestial_transits()){
                    (void)id;
                    if(p.ship==leg.carrier_ship){has_flight=true;break;}
                }
                if(!has_flight)ready_freight.push_back(order);
            }
            for(const auto order:ready_freight){
                const auto it=world.world().intercolony_freight().find(order);
                if(it==world.world().intercolony_freight().end())continue;
                const auto carrier=it->second.carrier_ship;
                const auto destination=it->second.destination_ship;
                const auto target=world.world().ship_frames().find(destination);
                if(target==world.world().ship_frames().end() ||
                   !target->second.anchor_body)continue;
                const auto request=++nonce;
                if(world.begin_celestial_transit(
                        owner,carrier,target->second.anchor_body,request))
                    announce(world.world(),"INTERFREIGHT_DEPARTED|"+
                        std::to_string(order)+"|"+std::to_string(request)+"|"+
                        std::to_string(carrier)+"|"+std::to_string(destination));
            }
            // Mature transit requests are committed separately; no half-
            // applied position is ever published. Catch-up on restart is
            // automatic because due requests live in the world snapshot.
            std::vector<std::uint64_t> due_frame_transits;
            for(const auto& [request,p]:world.world().pending_celestial_transits())
                if(p.due_tick<=world.world().tick())due_frame_transits.push_back(request);
            for(const auto request:due_frame_transits) {
                const auto pending=world.world().pending_celestial_transits().at(request);
                if(world.complete_celestial_transit(request)) {
                    announce(world.world(),"FRAME_TRANSIT_ARRIVED|"+
                        std::to_string(request)+"|"+
                        std::to_string(pending.destination_body));
                    // Refresh frame-specific local sensors and inventory only
                    // after the destination is durably committed.
                    status(world.world(),ship,true);
                    scan(world.world(),ship);
                }
                else
                    announce(world.world(),"ERROR|FRAME_TRANSIT_BLOCKED");
            }
            // Intercolony freight only completes after a separate, committed
            // transit and actual destination berth. If a target moved away,
            // the cargo stays sealed aboard its real carrier until recovered.
            std::vector<std::uint64_t> landed_freight;
            for(const auto& [order,leg]:world.world().intercolony_freight())
                if(leg.owner==owner && leg.source_ship==ship &&
                   leg.stage==IntercolonyFreightStage::Loaded)
                    landed_freight.push_back(order);
            for(const auto order:landed_freight)
                if(world.deliver_intercolony_freight(owner,order))
                    announce(world.world(),"INTERFREIGHT_DELIVERED|"+
                        std::to_string(order)+"|"+std::to_string(world.generation()));
            sync_survey_knowledge(); // only after durable native world step
            // EXIT-01D: every eighth committed world tick, host (not the
            // player input UI) automatically observes a bounded rotation of
            // real inhabited colonies. No receiving-owner permission is
            // fabricated: without a broker-approved offer the supervisor
            // preserves individual interest without secretly boarding a ferry.
            if(world.world().tick()>=8 && world.world().tick()%8==0){
                const auto& colonies=world.world().colony_ships();
                if(!colonies.empty()){
                    constexpr std::size_t max_colonies_per_cycle=4;
                    const auto count=colonies.size();
                    const auto cursor=((world.world().tick()/8)%count *
                        (max_colonies_per_cycle%count))%count;
                    const auto batch=std::min(count,max_colonies_per_cycle);
                    for(std::size_t c=0;c<batch;++c){
                        const auto slot=(cursor+c)%count;
                        auto it=colonies.begin();
                        std::advance(it,static_cast<std::ptrdiff_t>(slot));
                        PersistentWorld::HostVoluntaryDepartureSchedule request;
                        request.origin=it->first;
                        request.scan_budget=8;
                        const auto report=world.supervise_voluntary_migration(request);
                        if(report.selected_npc &&
                           report.status==
                            PersistentWorld::HostVoluntaryDepartureStatus::SeekingDestination)
                            std::cerr<<"COLONY|MIGRATION_INTEREST|"<<report.origin
                                <<"|"<<report.selected_npc
                                <<"|TICK|"<<report.tick<<'\n';
                    }
                }
            }
            bool scanned=false;
            for(std::size_t ix=old;ix<world.world().events().size();++ix){
                const auto& ev=world.world().events()[ix];
                if(pending_scans.erase(ev.command_id) && ev.result==Outcome::Applied){
                    scan(world.world(),ship);
                    scanned=true;
                }
                // A successful probe order queues a withheld native report.
                // Do not mark discovery complete until the committed journal
                // actually gains the observation after the response interval.
                pending_probe_scans.erase(ev.command_id);
                // Original immutable NPC operator receipt proves this was
                // an actual foreign cyber attempt on the piloted physical
                // ship. Never label a queued threat as an applied effect.
                if(const auto it=world.world().npc_cyber_operators().find(
                       ev.command_id);
                   it!=world.world().npc_cyber_operators().end() &&
                   it->second.target_ship==fleet.piloted()){
                    announce(world.world(),"CYBER_INCOMING|"+
                        std::to_string(it->second.target_ship)+"|"+
                        std::to_string(it->second.attacking_ship)+"|"+
                        std::to_string(ev.command_id)+"|"+
                        to_string(ev.result));
                }
                announce(world.world(),"RESULT|"+std::to_string(ev.command_id)+"|"+
                    to_string(ev.result)+"|"+std::to_string(ev.transferred));
                if(const auto cyber=awaiting_cyber_results.find(ev.command_id);
                   cyber!=awaiting_cyber_results.end()){
                    announce(world.world(),"CYBER_RESULT|"+
                        std::to_string(ev.command_id)+"|"+
                        std::to_string(cyber->second)+"|"+to_string(ev.result));
                    awaiting_cyber_results.erase(cyber);
                }
            }
            const auto reports_now=report_count();
            if(world.world().discoveries().size()!=known_discoveries ||
               reports_now!=known_reports){
                // Native geology report arrived over a live 10 km link,
                // even when its target was already discovered previously.
                scanned=true;
                known_discoveries=world.world().discoveries().size();
                known_reports=reports_now;
            }
            std::cout<<encode_frame(bridge.after_committed_step(world.world()))<<std::flush;
            status(world.world(),ship,scanned);
            cyber_status(world.world(),fleet.piloted());
            // Player cockpit is not the home colony's management actor.
            // Publish these independently so camera, heading and supplies
            // describe the hull actually selected for flight.
            if(const auto* e=world.world().find(fleet.piloted())) {
                const auto* c=world.world().colony_ship(fleet.piloted());
                const auto* life=world.world().life_support(fleet.piloted());
                const auto frame=world.world().ship_frames().find(fleet.piloted());
                if(c && life && frame!=world.world().ship_frames().end())
                    announce(world.world(),"FLEET_PILOT_ITEM|"+
                        std::to_string(fleet.piloted())+"|"+
                        std::to_string(e->heading_deg)+"|"+
                        std::to_string(e->position.x)+"|"+
                        std::to_string(e->position.y)+"|"+
                        std::to_string(frame->second.anchor_body)+"|"+
                        std::to_string(c->crew)+"|"+
                        std::to_string(life->food_kg)+"|"+
                        std::to_string(life->water_kg)+"|"+
                        std::to_string(life->oxygen_kg)+"|"+
                        std::to_string(world.generation()));
            }
            std::this_thread::sleep_until(start+period);
        }
        std::cerr<<"B7_0E COLONY HOST COMPLETED generation="<<world.generation()<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"B7_0E COLONY HOST FAILED "<<e.what()<<'\n';return 1;}
}
