#pragma once
// WO3-A: read-only settlement capacity and owner-confirmed native facility
// planning. Original Simulation, named NPCs and SQLite are the ONLY owners of
// construction, people, inventory, power and receipts. A proposed Command is
// never an executable entitlement; the trusted host must authenticate its
// issuer and original PersistentWorld::submit + tick must revalidate it.
#include "galaxy/Simulation.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::colony03 {

enum class Facility : std::uint8_t {
    Farm, Habitat, Utility, Community, Clinic,
    Reclaimer, Waterworks, Workshop, FrameFinisher, Storage
};
enum class ConstructionGate : std::uint8_t {
    Ready, InvalidOwner, MissingOriginalOutpost, ForeignSite, AlreadyBuilt,
    NotAtSite, InsufficientCrew, InsufficientModules, MissingHabitat,
    MissingUtility, MissingWorkshop
};
struct FacilityPlan {
    Facility facility=Facility::Farm;
    Action original_action=Action::ConstructFarm;
    EntityId source_ship=0,site=0;
    std::int64_t modules=0;
    ConstructionGate gate=ConstructionGate::MissingOriginalOutpost;
    bool can_request() const noexcept {return gate==ConstructionGate::Ready;}
};
struct OutpostAudit {
    EntityId source_ship=0,site=0;
    PlayerId owner=0;
    std::int64_t original_crew_aboard=0;
    std::int64_t modules_aboard=0;
    std::uint64_t physically_present_named=0,habitat_workers_named=0;
    std::uint64_t farm_workers_named=0,temporary_visitors_named=0;
    std::int64_t housing_capacity=0,housed_residents=0,local_farm_workers=0;
    std::int64_t food_kg=0,water_kg=0,oxygen_kg=0;
    std::int64_t utility_energy=0;
    bool utility_installed=false,utility_powered=false;
    bool farm_installed=false,workshop_installed=false;
    bool clinic_installed=false,community_installed=false;
    bool local_housing_installed=false;
    // This is NOT a self-sufficiency, growth, migration, or health certificate.
    bool aggregate_habitat_headcount_matches_named=false;
    bool aggregate_farm_headcount_matches_named=false;
};
inline bool original_owner(const Simulation& world,PlayerId owner,
                           EntityId ship) {
    const auto* e=world.find(ship);
    return owner && e && e->type==EntityType::Ship &&
           e->owner_id==owner && world.colony_ship(ship)!=nullptr &&
           world.life_support(ship)!=nullptr;
}
inline bool original_site(const Simulation& world,EntityId ship,EntityId site) {
    const auto* e=world.find(site);
    const auto it=world.built_outposts().find(site);
    return e && e->type==EntityType::Station &&
           it!=world.built_outposts().end() &&
           it->second.source_colony_ship==ship;
}
inline std::optional<OutpostAudit> audit_original_outpost(
    const Simulation& world,PlayerId owner,EntityId ship,EntityId site) {
    if(!original_owner(world,owner,ship) ||
       !original_site(world,ship,site))return std::nullopt;
    const auto* source=world.find(ship);
    const auto* outpost=world.find(site);
    if(!source || !outpost || outpost->owner_id!=owner)return std::nullopt;
    OutpostAudit a{};
    a.owner=owner;a.source_ship=ship;a.site=site;
    a.original_crew_aboard=world.colony_ship(ship)->crew;
    a.modules_aboard=world.colony_ship(ship)->modules;
    const auto* h=world.habitat(site);
    const auto* f=world.farm(site);
    const auto* u=world.utility(site);
    a.local_housing_installed=h && h->source_ship==ship;
    a.farm_installed=f && f->source_ship==ship;
    a.utility_installed=u && u->source_ship==ship;
    a.workshop_installed=world.workshop(site)!=nullptr;
    a.clinic_installed=world.clinic(site)!=nullptr;
    a.community_installed=world.community(site)!=nullptr;
    if(a.local_housing_installed) {
        a.housing_capacity=h->capacity;
        a.housed_residents=h->residents;
        a.food_kg=h->life.food_kg;
        a.water_kg=h->life.water_kg;
        a.oxygen_kg=h->life.oxygen_kg;
    }
    if(a.farm_installed)a.local_farm_workers=f->workers;
    if(a.utility_installed){
        a.utility_energy=u->stored_energy;
        a.utility_powered=u->powered && u->condition>0;
    }
    // Physical presence, legal residence and assigned duty are distinct.
    // Do not infer new settlers from aggregate workforce or visitor data.
    for(const auto& [id,n]:world.npc_records()){
        (void)id;
        if(n.location!=site)continue;
        ++a.physically_present_named;
        if(n.home_ship!=ship || n.assigned_site!=site){
            ++a.temporary_visitors_named;
            continue;
        }
        if(n.role==NpcRole::Habitat)++a.habitat_workers_named;
        else if(n.role==NpcRole::Farm)++a.farm_workers_named;
        else ++a.temporary_visitors_named;
    }
    a.aggregate_habitat_headcount_matches_named=
        a.local_housing_installed &&
        a.housed_residents==static_cast<std::int64_t>(a.habitat_workers_named);
    a.aggregate_farm_headcount_matches_named=
        a.farm_installed &&
        a.local_farm_workers==static_cast<std::int64_t>(a.farm_workers_named);
    return a;
}
struct FacilityDescriptor {
    Facility kind;
    Action original_action;
    std::int64_t modules;
    bool needs_habitat,needs_utility,needs_workshop;
};
// Costs and prerequisites mirror the original Simulation::execute paths.
// Proposed INDUSTRY-01 institutions not in this table are NOT buildable.
inline constexpr std::array<FacilityDescriptor,10> kNativeFacilities{{ 
    {Facility::Farm,Action::ConstructFarm,2,false,false,false},
    {Facility::Habitat,Action::ConstructHabitat,2,false,false,false},
    {Facility::Utility,Action::ConstructUtility,2,false,false,false},
    {Facility::Community,Action::ConstructCommunity,2,true,false,false},
    {Facility::Clinic,Action::ConstructClinic,2,true,false,false},
    {Facility::Reclaimer,Action::ConstructReclaimer,2,true,true,false},
    {Facility::Waterworks,Action::ConstructWaterworks,2,true,true,false},
    {Facility::Workshop,Action::ConstructWorkshop,1,false,false,false},
    {Facility::FrameFinisher,Action::ConstructWorkshopFinisher,1,false,false,true},
    {Facility::Storage,Action::ConstructStorage,1,false,false,false}
}};
inline bool installed(const Simulation& world,EntityId site,Facility kind) {
    switch(kind){
        case Facility::Farm: return world.farm(site)!=nullptr;
        case Facility::Habitat: return world.habitat(site)!=nullptr;
        case Facility::Utility: return world.utility(site)!=nullptr;
        case Facility::Community: return world.community(site)!=nullptr;
        case Facility::Clinic: return world.clinic(site)!=nullptr;
        case Facility::Reclaimer: return world.reclaimer(site)!=nullptr;
        case Facility::Waterworks: return world.waterworks(site)!=nullptr;
        case Facility::Workshop: return world.workshop(site)!=nullptr;
        case Facility::FrameFinisher:
            return world.workshop(site) && world.workshop(site)->finisher_installed;
        case Facility::Storage: return world.storage(site)!=nullptr;
    }
    return false;
}
inline FacilityPlan plan_facility(const Simulation& world,PlayerId owner,
    EntityId ship,EntityId site,Facility kind) {
    FacilityPlan plan{};plan.facility=kind;
    plan.source_ship=ship;plan.site=site;
    const FacilityDescriptor* d=nullptr;
    for(const auto& entry:kNativeFacilities)if(entry.kind==kind){d=&entry;break;}
    if(!d)return plan; // invalid enum has no offer
    plan.original_action=d->original_action;plan.modules=d->modules;
    if(!original_owner(world,owner,ship)){
        plan.gate=ConstructionGate::InvalidOwner;return plan;
    }
    if(!world.find(site) || !world.built_outposts().count(site)){
        plan.gate=ConstructionGate::MissingOriginalOutpost;return plan;
    }
    if(!original_site(world,ship,site) ||
       world.find(site)->owner_id!=owner){
        plan.gate=ConstructionGate::ForeignSite;return plan;
    }
    if(installed(world,site,kind)){
        plan.gate=ConstructionGate::AlreadyBuilt;return plan;
    }
    const auto* source=world.find(ship);
    const auto* destination=world.find(site);
    const long double dx=static_cast<long double>(source->position.x)-
                         static_cast<long double>(destination->position.x);
    const long double dy=static_cast<long double>(source->position.y)-
                         static_cast<long double>(destination->position.y);
    constexpr long double range=2500.0L;
    if(source->system_id!=destination->system_id ||
       source->jump_state!=ShipJumpState::Ready ||
       (source->dock_state!=DockState::InSpace &&
        source->dock_station!=site) ||
       dx*dx+dy*dy>range*range){
        plan.gate=ConstructionGate::NotAtSite;return plan;
    }
    if(d->needs_habitat && !world.habitat(site)){
        plan.gate=ConstructionGate::MissingHabitat;return plan;
    }
    if(d->needs_utility && !world.utility(site)){
        plan.gate=ConstructionGate::MissingUtility;return plan;
    }
    if(d->needs_workshop && !world.workshop(site)){
        plan.gate=ConstructionGate::MissingWorkshop;return plan;
    }
    const auto* colony=world.colony_ship(ship);
    if(colony->crew<2){
        plan.gate=ConstructionGate::InsufficientCrew;return plan;
    }
    if(colony->modules<d->modules){
        plan.gate=ConstructionGate::InsufficientModules;return plan;
    }
    plan.gate=ConstructionGate::Ready;return plan;
}
inline std::array<FacilityPlan,10> plan_native_facilities(
    const Simulation& world,PlayerId owner,EntityId ship,EntityId site) {
    std::array<FacilityPlan,10> out{};
    for(std::size_t n=0;n<out.size();++n)
        out[n]=plan_facility(world,owner,ship,site,kNativeFacilities[n].kind);
    return out;
}
inline std::optional<Command> approved_original_facility(
    const Simulation& world,PlayerId owner,EntityId ship,EntityId site,
    Facility kind,std::uint64_t command_id,bool explicit_owner_approval) {
    if(!explicit_owner_approval || !command_id ||
       world.has_command_id(command_id) ||
       world.tick()==std::numeric_limits<std::uint64_t>::max())return std::nullopt;
    const auto plan=plan_facility(world,owner,ship,site,kind);
    if(!plan.can_request())return std::nullopt;
    Command c{};c.id=command_id;c.sequence=command_id;
    c.execute_tick=world.tick()+1;
    c.issuer=owner;c.actor=ship;c.target=site;
    c.action=plan.original_action;c.amount=1;
    return c;
}
// Physical surface sites are NOT original station outposts. WO3-B adds the
// separate physically built first habitat, not generic fully supplied
// WO1 industry institutions or an independent second planetary simulation.
struct PlanetarySiteAudit {
    PlanetarySiteKey key{};
    EntityId source_ship=0;
    PlayerId owner=0;
    std::uint8_t spaceport_phase=0,workforce_policy=0;
    std::int64_t water_kg=0,food_kg=0,nutrients_kg=0,oxygen_kg=0;
    std::uint64_t declared_workers=0,physical_delegates=0;
    std::uint64_t named_surface_visitors=0;
    std::int64_t current_surface_consumers=0; // delegated crew plus physically present shuttle people
    bool shuttle_installed=false,permanent_housing_installed=false;
    bool staffed_by_original_named_npcs=false;
    std::uint8_t physically_delivered_modules=0,housing_phase=0;
    std::uint64_t housing_progress=0,housing_order_id=0;
    std::uint64_t housing_stalled_ticks=0;
    std::uint64_t housing_completed_tick=0;
    std::uint64_t housing_supported_cycles=0,housing_shortages=0;
    bool housing_paused=false;
    std::uint8_t utility_phase=0;
    std::uint64_t utility_progress=0,utility_supervisor=0;
    std::int64_t stored_electricity=0,brine_kg=0;
    std::uint64_t utility_powered_cycles=0,hvac_cycles=0,utility_outages=0;
    std::int64_t recycled_water_kg=0,recycled_oxygen_kg=0;
    std::int64_t unused_housing_capacity=0; // 4 initial beds; no future free population
    std::uint64_t original_permanent_residents=0;
    bool support_healthy_now=false; // site has >=2kg of each local vital stock
};
inline std::optional<PlanetarySiteAudit> audit_planetary_surface(
    const Simulation& world,PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key) {
    if(!original_owner(world,owner,ship))return std::nullopt;
    const auto it=world.planetary_sites().find(key);
    if(it==world.planetary_sites().end() ||
       it->second.owner!=owner || it->second.source_ship!=ship)
        return std::nullopt;
    const auto& s=it->second;
    PlanetarySiteAudit a{};
    a.key=key;a.owner=owner;a.source_ship=ship;
    a.spaceport_phase=s.spaceport_phase;
    a.workforce_policy=s.workforce_policy;
    a.water_kg=s.water_kg;a.food_kg=s.food_kg;
    a.nutrients_kg=s.nutrients_kg;a.oxygen_kg=s.oxygen_kg;
    a.declared_workers=s.assigned_workers>0?
        static_cast<std::uint64_t>(s.assigned_workers):0;
    a.shuttle_installed=world.planetary_shuttles().count(key)!=0;
    a.physically_delivered_modules=s.housing_modules;
    a.housing_phase=s.housing_phase;
    a.housing_progress=s.housing_progress;
    a.housing_order_id=s.housing_order_id;
    a.housing_stalled_ticks=s.housing_stalled_ticks;
    a.housing_completed_tick=s.housing_completed_tick;
    a.housing_paused=s.housing_paused;
    a.housing_supported_cycles=s.housing_supported_cycles;
    a.housing_shortages=s.housing_shortages;
    a.utility_phase=s.utility_phase;
    a.utility_progress=s.utility_progress;
    a.utility_supervisor=s.utility_supervisor;
    a.stored_electricity=s.stored_electricity;
    a.brine_kg=s.brine_kg;
    a.utility_powered_cycles=s.utility_powered_cycles;
    a.hvac_cycles=s.hvac_cycles;
    a.utility_outages=s.utility_outages;
    a.recycled_water_kg=s.water_recycled_kg;
    a.recycled_oxygen_kg=s.oxygen_recycled_kg;
    a.permanent_housing_installed=s.housing_phase==2;
    std::int64_t next_tick_people=2; // real delegated work team
    const auto shuttle=world.planetary_shuttles().find(key);
    if(shuttle!=world.planetary_shuttles().end()) {
        const auto& sh=shuttle->second;
        next_tick_people+=static_cast<std::int64_t>(sh.disembarked_ids.size());
        if(sh.flight_phase==2)
            next_tick_people+=1+static_cast<std::int64_t>(sh.passenger_ids.size());
    }
    a.current_surface_consumers=next_tick_people;
    a.support_healthy_now=s.food_kg>=next_tick_people &&
        s.water_kg>=next_tick_people &&
        s.oxygen_kg>=next_tick_people;
    for(const auto npc_id:s.delegated_npcs){
        if(!npc_id)continue;
        const auto it_n=world.npc_records().find(npc_id);
        if(it_n!=world.npc_records().end() &&
           it_n->second.home_ship==ship &&
           it_n->second.location==s.surface_location &&
           it_n->second.role==NpcRole::PlanetarySurface)
            ++a.physical_delegates;
    }
    for(const auto& [id,npc]:world.npc_records()){
        (void)id;
        if(a.permanent_housing_installed &&
           npc.legal_residence()==transport::Endpoint{
               transport::LocationKind::PlanetarySite,s.surface_location})
            ++a.original_permanent_residents;
        if(npc.location==s.surface_location &&
           npc.role==NpcRole::PlanetarySurface &&
           npc.home_ship==ship)++a.named_surface_visitors;
    }
    a.staffed_by_original_named_npcs=
        a.physical_delegates==a.declared_workers && a.declared_workers!=0;
    // Original physically stationed constructors occupy beds regardless
    // of whether an approved legal residency request has been submitted.
    a.unused_housing_capacity=a.permanent_housing_installed?
        std::max<std::int64_t>(0,4-static_cast<std::int64_t>(
            std::max(a.original_permanent_residents,a.physical_delegates))):0;
    return a;
}

// WO3-C/F: conservative physical-service report, not a second resource
// scheduler. An installed shelter is not proof of electricity, independence,
// food renewal or a functioning return shuttle.
enum class SurfaceSupplyGate : std::uint8_t {
    NoOriginalSite, NotYetHoused, NamedWorkersUnavailable,
    FoodShortage, WaterShortage, OxygenShortage, ImportDependent
};
struct SurfaceSupply {
    SurfaceSupplyGate gate=SurfaceSupplyGate::NoOriginalSite;
    std::int64_t baseline_ten_tick_food_periods=0;
    std::int64_t baseline_ten_tick_water_periods=0;
    std::int64_t baseline_ten_tick_oxygen_periods=0;
    std::int64_t baseline_ten_tick_reserve_periods=0;
    std::uint64_t permanent_named_residents=0;
    std::int64_t current_surface_consumers=0; // conservative next provisioning window
    std::uint64_t past_supported_cycles=0,past_shortage_cycles=0;
    bool shuttle_installed=false,shuttle_on_surface=false;
    bool food_production_installed=false,water_extractor_installed=false;
    bool independent_power_proven=false,closed_water_loop_proven=false;
    bool hvac_ever_operated=false,loss_bearing_recycling_operated=false;
    std::int64_t retained_recycling_brine_kg=0;
    bool independently_sustainable=false;
};
inline SurfaceSupply assess_surface_supplies(
    const Simulation& world,PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key) {
    SurfaceSupply out{};
    const auto site=world.planetary_sites().find(key);
    const auto audit=audit_planetary_surface(world,owner,ship,key);
    if(!audit || site==world.planetary_sites().end())return out;
    const auto& s=site->second;
    out.permanent_named_residents=audit->original_permanent_residents;
    out.current_surface_consumers=audit->current_surface_consumers;
    out.past_supported_cycles=s.housing_supported_cycles;
    out.past_shortage_cycles=s.housing_shortages;
    out.shuttle_installed=audit->shuttle_installed;
    const auto shuttle=world.planetary_shuttles().find(key);
    out.shuttle_on_surface=shuttle!=world.planetary_shuttles().end() &&
                           shuttle->second.flight_phase==2;
    out.food_production_installed=s.farm;
    out.water_extractor_installed=s.extractor;
    out.independent_power_proven=s.utility_phase==2 &&
        s.utility_powered_cycles>0;
    out.hvac_ever_operated=s.utility_phase==2 && s.hvac_cycles>0;
    out.loss_bearing_recycling_operated=s.utility_phase==2 &&
        (s.water_recycled_kg>0 || s.oxygen_recycled_kg>0);
    out.retained_recycling_brine_kg=s.brine_kg;
    // Original builders use two kg per ten ticks; passengers, landed pilot
    // and disembarked visitors additionally consume real stock every 20
    // ticks. Charging every current person for *each* 10-tick forecast
    // period deliberately underestimates runway rather than promising an
    // unsupported margin near the next 20-tick shuttle ration event.
    const auto reserve_ration=std::max<std::int64_t>(
        2,out.current_surface_consumers);
    out.baseline_ten_tick_food_periods=s.food_kg/reserve_ration;
    out.baseline_ten_tick_water_periods=s.water_kg/reserve_ration;
    out.baseline_ten_tick_oxygen_periods=s.oxygen_kg/reserve_ration;
    out.baseline_ten_tick_reserve_periods=std::min({
        out.baseline_ten_tick_food_periods,
        out.baseline_ten_tick_water_periods,
        out.baseline_ten_tick_oxygen_periods});
    if(s.housing_phase!=2)out.gate=SurfaceSupplyGate::NotYetHoused;
    else if(!audit->staffed_by_original_named_npcs)
        out.gate=SurfaceSupplyGate::NamedWorkersUnavailable;
    else if(s.food_kg<reserve_ration)out.gate=SurfaceSupplyGate::FoodShortage;
    else if(s.water_kg<reserve_ration)out.gate=SurfaceSupplyGate::WaterShortage;
    else if(s.oxygen_kg<reserve_ration)out.gate=SurfaceSupplyGate::OxygenShortage;
    else out.gate=SurfaceSupplyGate::ImportDependent;
    // Solar electricity and HVAC are now physical installed services.
    // Recycling retains brine/solid residue and cannot claim perfect closure.
    // Farms and extractors still consume finite inputs, so solar electricity
    // alone cannot certify a self-sustaining colony.
    return out;
}

} // namespace galaxy::colony03
