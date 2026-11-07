#include "galaxy/Industry01Catalog.hpp"
#include "galaxy/Simulation.hpp"
#include "galaxy/ShipStatusFitting.hpp"
#include "galaxy/ShipSensorProfile.hpp"
#include "galaxy/WorkPlanning.hpp"
#include "galaxy/Navigation.hpp"
#include "galaxy/ShipInteractionPreflight.hpp"
#include "galaxy/CelestialFrameTransition.hpp"
#include "galaxy/ShipyardConstructionPreflight.hpp"
#include "galaxy/TransportIdentity.hpp"
#include "galaxy/NpcTransport.hpp"
#include "galaxy/ColonySafety.hpp"
#include "galaxy/QuantifiedProduction.hpp"
#include "galaxy/FactoryLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace galaxy {
namespace {
constexpr int kPlacementBound=1000000; // coordinate safety, not room size
std::uint32_t placement_node(std::int64_t n){
    return n==-1?kWorkshopConveyorNode:static_cast<std::uint32_t>(n);
}
industry::FactoryLayout factory_devices(const OutpostStorage& st){
    industry::FactoryLayout layout;
    for(const auto& [node,where]:st.equipment_positions){
        if(where.x < -kPlacementBound || where.x > kPlacementBound ||
           where.y < -kPlacementBound || where.y > kPlacementBound)
            throw std::invalid_argument("factory coordinate outside safety range");
        if(node==kWorkshopConveyorNode){
            layout.place_machine(node,{where.x,where.y},1,
                {"good/aluminum"},{"good/aluminum_panels","good/aluminum_frames","good/aluminum_scrap"});
        }else{
            if(node && !st.extra_bins.count(node))
                throw std::invalid_argument("placement references unbuilt container");
            layout.place_storage(static_cast<std::uint64_t>(node)+1,{where.x,where.y},1,1,4000);
        }
    }
    return layout;
}
void validate_site_placements(const OutpostStorage& st){(void)factory_devices(st);}
// A route cell is real built infrastructure; path plans may be incomplete but
// may never cross equipment, another installed/pending conveyor, or themselves.
// The FactoryLayout domain validates the complete endpoint/port topology.
void validate_site_conveyors(const OutpostStorage& st){
    auto layout=factory_devices(st);
    std::set<industry::GridCell> occupied;
    for(const auto& [id,link]:st.links){
        if(!link.route_required){
            if(link.route_ready || !link.route_cells.empty())
                throw std::invalid_argument("unowned conveyor route cells");
            continue; // v27-v29 legacy logical links remain functional
        }
        const auto a=st.equipment_positions.find(link.source);
        const auto b=st.equipment_positions.find(link.destination);
        if(a==st.equipment_positions.end() || b==st.equipment_positions.end() ||
           link.route_cells.size()>4096 || (link.route_ready && link.route_cells.empty()))
            throw std::invalid_argument("conveyor endpoints missing installed equipment");
        std::vector<industry::GridCell> path;
        for(const auto& cell:link.route_cells){
            if(cell.x<-kPlacementBound || cell.x>kPlacementBound ||
               cell.y<-kPlacementBound || cell.y>kPlacementBound)
                throw std::invalid_argument("conveyor cell out of coordinate bounds");
            industry::GridCell pos{cell.x,cell.y};
            if(!occupied.insert(pos).second)
                throw std::invalid_argument("conveyors overlap or intersect themselves");
            for(const auto& [node,d]:layout.devices()){
                (void)node;
                if(pos.x>=d.origin.x && pos.x<d.origin.x+d.width &&
                   pos.y>=d.origin.y && pos.y<d.origin.y+d.height)
                    throw std::invalid_argument("conveyor crosses installed machine");
            }
            if(!path.empty()){
                const auto prev=path.back();
                if(std::abs(pos.x-prev.x)+std::abs(pos.y-prev.y)!=1)
                    throw std::invalid_argument("conveyor path is not orthogonally continuous");
            }
            path.push_back(pos);
        }
        if(!path.empty()){
            const auto& device=layout.devices().at(link.source==kWorkshopConveyorNode?
                                                     link.source:static_cast<std::uint64_t>(link.source)+1);
            const auto& p=path.front();
            const bool adjacent=((p.x==device.origin.x-1 || p.x==device.origin.x+device.width) &&
                   p.y>=device.origin.y && p.y<device.origin.y+device.height) ||
                   ((p.y==device.origin.y-1 || p.y==device.origin.y+device.height) &&
                   p.x>=device.origin.x && p.x<device.origin.x+device.width);
            if(!adjacent)throw std::invalid_argument("conveyor does not start at output port");
        }
        if(link.route_ready){
            static const char* goods[]={"good/aluminum","good/aluminum_panels","good/aluminum_frames","good/aluminum_scrap"};
            layout.add_conveyor(id,link.source==kWorkshopConveyorNode?link.source:static_cast<std::uint64_t>(link.source)+1,
                link.destination==kWorkshopConveyorNode?link.destination:static_cast<std::uint64_t>(link.destination)+1,
                goods[link.item_index],link.units_per_tick,std::move(path));
        }
    }
}
} // namespace

namespace {
std::int64_t clamp_velocity(std::int64_t v) {
    return std::clamp(v, -Simulation::kMaxVelocityMmPerTick, Simulation::kMaxVelocityMmPerTick);
}
}

std::vector<MineralFraction> mineral_profile(AsteroidClass taxonomy) {
    switch(taxonomy) {
      case AsteroidClass::Carbonaceous:
        return {{"serpentine",550},{"saponite",220},{"magnetite",55},
                {"pyrrhotite",65},{"carbonates",35},{"olivine",20},
                {"pyroxene",15},{"nickel_iron",10},{"other",30}};
      case AsteroidClass::Silicate:
        // Feldspar-rich basaltic/collisional-fragment gameplay profile: aluminum
        // feed is deliberately plentiful where the local geology supports it.
        return {{"olivine",300},{"pyroxene",180},{"plagioclase",400},
                {"nickel_iron",70},{"troilite",25},{"other",25}};
      case AsteroidClass::Metallic:
        return {{"nickel_iron",860},{"troilite",80},{"olivine",30},
                {"pyroxene",15},{"other",15}};
    }
    throw std::invalid_argument("unknown asteroid class");
}
// Codes are fixed, not user-supplied strings serialized into world state.
const char* mineral_name(std::uint64_t code) {
    static const char* const names[]={"", "serpentine", "saponite", "magnetite",
        "pyrrhotite", "carbonates", "olivine", "pyroxene", "nickel_iron",
        "other", "plagioclase", "troilite"};
    return code>0 && code<sizeof(names)/sizeof(names[0])?names[code]:nullptr;
}
bool is_known_mineral(const std::string& name) {
    for(std::uint64_t i=1;i<=11;++i)if(name==mineral_name(i))return true;
    return false;
}
bool is_known_good(const std::string& name) {
    return name=="freight_water" || name=="freight_food" ||
           name=="freight_nutrients" ||
           name=="structural_alloy" || name=="aluminum" || name=="aluminum_panels" || name=="aluminum_scrap" || name=="aluminum_frames" || name=="ceramic" ||
           name=="glass" || name=="water" || name=="sulfur" || name=="carbon_feed";
}
// Physical prototype recipes. The yields are deliberately playable estimates,
// NOT claims of measured metal extraction from individual asteroid minerals.
struct SeparationRecipe { const char* a; int a_permille; const char* b; int b_permille; };
SeparationRecipe separation(const std::string& name) {
    if(name=="nickel_iron")return {"structural_alloy",900,nullptr,0};
    if(name=="magnetite")return {"structural_alloy",800,nullptr,0};
    if(name=="troilite" || name=="pyrrhotite")return {"structural_alloy",700,"sulfur",200};
    if(name=="plagioclase")return {"aluminum",200,"glass",600};
    if(name=="serpentine" || name=="saponite")return {"ceramic",700,"water",100};
    if(name=="olivine" || name=="pyroxene")return {"ceramic",800,nullptr,0};
    if(name=="carbonates")return {"ceramic",800,"carbon_feed",100};
    return {nullptr,0,nullptr,0}; // 'other' requires later processing technology
}

const char* viability_stage_name(ColonyViability::Stage stage) {
    switch(stage){
      case ColonyViability::Stage::Survival:return "SURVIVAL";
      case ColonyViability::Stage::Stable:return "STABLE";
      case ColonyViability::Stage::Livable:return "LIVABLE";
      case ColonyViability::Stage::SelfSufficient:return "SELF_SUFFICIENT";
      case ColonyViability::Stage::Prosperous:return "PROSPEROUS";
    }
    return "UNKNOWN";
}

std::int64_t asteroid_mass_tonnes(std::int64_t radius_m, std::int64_t density_kg_m3) {
    // Spherical-equivalent model. Real asteroids are irregular and often porous.
    if(radius_m<10 || radius_m>350 || density_kg_m3<900 || density_kg_m3>8000)
        throw std::invalid_argument("implausible test asteroid geometry");
    const long double volume=(4.0L/3.0L)*3.14159265358979323846264338327950288L*
         radius_m*radius_m*radius_m;
    const long double mass=volume*density_kg_m3/1000.0L;
    if(mass>1000000000.0L)throw std::invalid_argument("asteroid over current resource limit");
    return static_cast<std::int64_t>(std::llround(mass));
}

Simulation::Simulation(std::uint64_t seed, std::uint32_t system_id)
    : system_id_(system_id), rng_state_(seed) {
    if (system_id == 0) throw std::invalid_argument("system_id must not be zero");
    systems_.insert(system_id);
}

bool Simulation::same_local_frame(EntityId a,EntityId b) const {
    const auto* x=find(a);
    const auto* y=find(b);
    if(!x||!y||x->system_id!=y->system_id)return false;
    const auto ax=ship_frames_.find(a),ay=ship_frames_.find(b);
    const auto li=local_entity_anchors_.find(a);
    const auto ri=local_entity_anchors_.find(b);
    const auto left=ax==ship_frames_.end()?
        (li==local_entity_anchors_.end()?0:li->second):ax->second.anchor_body;
    const auto right=ay==ship_frames_.end()?
        (ri==local_entity_anchors_.end()?0:ri->second):ay->second.anchor_body;
    return left==right;
}
void Simulation::enable_native_3d_physics() {
    // Opt-in migration preserves all original entity IDs, resources and
    // legacy x/y state. The host persists the v119 change via its existing
    // original world snapshot transaction before any physical turn.
    snapshot_version_=std::max(snapshot_version_,119);
}
bool Simulation::valid_position(Vector2 p) const {
    return p.x >= -kWorldBoundMm && p.x <= kWorldBoundMm &&
           p.y >= -kWorldBoundMm && p.y <= kWorldBoundMm;
}

EntityId Simulation::add_entity(Entity entity) {
    if (!valid_position(entity.position)) throw std::invalid_argument("entity out of system bounds");
    if (next_entity_id_ == std::numeric_limits<EntityId>::max()) throw std::overflow_error("entity IDs exhausted");
    entity.id = next_entity_id_++;
    entity.system_id = system_id_;
    const auto id = entity.id;
    entities_.emplace(id, entity);
    if(snapshot_version_>=71 && entity.type==EntityType::Ship)
        ship_frames_.emplace(id,ShipFrameCustody{entity.system_id,0,{}});
    return id;
}

EntityId Simulation::add_ship(PlayerId owner, Vector2 p, std::int64_t capacity) {
    if (owner == 0 || capacity <= 0 || capacity > 1000000000LL)
        throw std::invalid_argument("invalid owner or capacity");
    Entity entity;
    entity.type = EntityType::Ship;
    entity.owner_id = owner;
    entity.position = p;
    entity.cargo_capacity = capacity;
    entity.hull=100;
    entity.weapon_charges=12;
    return add_entity(entity);
}

void Simulation::provision_starter_exploration_scanner(EntityId id){
    if(tick_ || !submitted_ids_.empty() || !commands_.empty() ||
       !colony_ships_.count(id) || entities_.at(id).fitting.exploration_scanner())
        throw std::invalid_argument("starter scanner requires unfitted genesis colony ship");
    entities_.at(id).fitting.hardpoints[0]=1;
    snapshot_version_=std::max(snapshot_version_,64);
}
void Simulation::provision_starter_probe_kit(EntityId id){
    if(tick_ || !submitted_ids_.empty() || !commands_.empty() ||
       !colony_ships_.count(id) || entities_.at(id).fitting.probe_launcher() ||
       entities_.at(id).survey_probes_carried)
        throw std::invalid_argument("starter probe kit requires pristine colony ship");
    auto& ship=entities_.at(id);
    if(ship.fitting.hardpoints[1])throw std::invalid_argument("starter probe launcher socket occupied");
    ship.fitting.hardpoints[1]=2;
    ship.survey_probes_carried=1; // one recoverable, physically conserved probe
    snapshot_version_=std::max(snapshot_version_,65);
}
EntityId Simulation::add_training_drone(Vector2 p){
    if(tick_!=0 || !submitted_ids_.empty() || !commands_.empty())
        throw std::invalid_argument("training drone must be provisioned at genesis");
    const auto id=add_ship(8,p,1);
    auto& e=entities_.at(id);
    e.training_drone=true;
    e.hull=45;
    e.weapon_charges=0;
    e.cargo_capacity=6; e.cargo=6; // pre-existing recoverable bulk stock, never generated on kill
    snapshot_version_=std::max(snapshot_version_,62);
    return id;
}
EntityId Simulation::add_colony_ship(PlayerId owner, Vector2 p,
                                      std::int64_t capacity, std::int64_t crew) {
    if(tick_!=0 || !submitted_ids_.empty() || owner==0 || owner==kServerAuthority ||
       crew<1 || crew>1000 || capacity<8 || capacity>1000000000LL)
        throw std::invalid_argument("invalid starting colony ship/crew");
    const auto id=add_ship(owner,p,capacity);
    colony_ships_.emplace(id,ColonyShip{crew,0,0,{},{},0});
    snapshot_version_=std::max(snapshot_version_,7);
    return id;
}
void Simulation::provision_colony_genesis(EntityId ship, std::int64_t food,
   std::int64_t water, std::int64_t oxygen, std::int64_t nutrients, std::int64_t co2) {
    if(tick_ || !submitted_ids_.empty() || !colony_ships_.count(ship) ||
       food<100 || water<100 || oxygen<100 || nutrients<100 || co2<100 ||
       food>100000 || water>100000 || oxygen>100000 || nutrients>100000 || co2>100000 ||
       (food+water+oxygen+nutrients+co2)%1000)
       throw std::invalid_argument("invalid genesis life-support provisioning");
    ColonyLifeSupport life{};
    life.food_kg=food;life.water_kg=water;life.oxygen_kg=oxygen;
    life.nutrients_kg=nutrients;life.co2_kg=co2;
    if(!life_support_.emplace(ship,life).second)
        throw std::invalid_argument("duplicate life-support seed");
    snapshot_version_=std::max(snapshot_version_,10);
}
const FarmFacility* Simulation::farm(EntityId outpost) const {
    const auto it=farms_.find(outpost);
    return it==farms_.end()?nullptr:&it->second;
}
const WaterworksFacility* Simulation::waterworks(EntityId site) const {
    const auto it=waterworks_.find(site);
    return it==waterworks_.end()?nullptr:&it->second;
}
const ReclaimerFacility* Simulation::reclaimer(EntityId site) const {
    const auto it=reclaimers_.find(site);return it==reclaimers_.end()?nullptr:&it->second;
}
const ColonyLifeSupport* Simulation::life_support(EntityId ship) const {
    const auto it=life_support_.find(ship);
    return it==life_support_.end()?nullptr:&it->second;
}
ColonyViability Simulation::assess_colony(EntityId ship) const {
    ColonyViability r{};
    const auto* c=colony_ship(ship);const auto* l=life_support(ship);
    if(!c || !l)return r;
    r.crew_total=c->crew;
    std::int64_t total_food=l->food_kg,total_water=l->water_kg,total_oxygen=l->oxygen_kg;
    // Surface-delegated adults remain real colonists, not missing crew.
    for(const auto& [key,site]:planetary_sites_) {
        (void)key;
        if(site.source_ship!=ship)continue;
        r.crew_total+=site.assigned_workers;
        total_food+=site.food_kg;
        total_water+=site.water_kg;
        total_oxygen+=site.oxygen_kg;
    }
    for(const auto& [id,f]:farms_){(void)id;
        if(f.source_ship!=ship)continue;
        r.farm_workers+=f.workers;r.crew_total+=f.workers;
        total_food+=f.life.food_kg;total_water+=f.life.water_kg;
        total_oxygen+=f.life.oxygen_kg;
        r.recent_food_production_kg+=f.recent_food_kg[0]+f.recent_food_kg[1]+f.recent_food_kg[2];
        if(f.workers && f.production_cycles>=3)r.has_operational_farm=true;
    }
    bool all_housing_provisioned=true,any_housing_occupied=false;
    for(const auto& [id,h]:habitats_){
        if(h.source_ship!=ship)continue;
        const auto f=farms_.find(id);
        const auto staff=f==farms_.end()?0:f->second.workers;
        const auto occupied=h.residents+staff;
        r.crew_total+=h.residents;
        r.habitat_residents+=h.residents;
        r.housed_colonists+=occupied;
        r.housing_capacity+=h.capacity;
        total_food+=h.life.food_kg;total_water+=h.life.water_kg;
        total_oxygen+=h.life.oxygen_kg;
        if(occupied){
            any_housing_occupied=true;
            if(h.consecutive_comfort_cycles<3)all_housing_provisioned=false;
        }
    }
    r.housing_provisioned=any_housing_occupied && all_housing_provisioned;
    // All occupied sites must have working services; one generator must not
    // certify an unrelated, unpowered settlement.
    std::set<EntityId> occupied_sites;
    for(const auto& [id,f]:farms_)if(f.source_ship==ship && f.workers)occupied_sites.insert(id);
    for(const auto& [id,h]:habitats_)if(h.source_ship==ship && h.residents)occupied_sites.insert(id);
    bool all_powered=true;
    for(const auto id:occupied_sites){
        const auto it=utilities_.find(id);
        if(it==utilities_.end() || !it->second.powered){
            ++r.unpowered_outposts;all_powered=false;
        } else {
            ++r.powered_outposts;
            if(it->second.consecutive_powered_cycles<3)all_powered=false;
        }
    }
    r.utilities_sustained=!occupied_sites.empty() && all_powered;
    bool has_colony_utilities=false,all_maintenance_ready=true;
    for(const auto& [id,u]:utilities_){
        if(u.source_ship!=ship)continue;
        has_colony_utilities=true;
        r.maintenance_spare_modules+=u.spare_modules;
        r.assigned_technicians+=u.technicians;
        const auto hi=habitats_.find(id);
        const bool local_labor=hi!=habitats_.end() && u.technicians>0 &&
            hi->second.residents>=u.technicians &&
            hi->second.life.consecutive_supplied_cycles>=3;
        if(!u.delegated_maintenance || !local_labor || u.spare_modules<1 || !u.powered){
            ++r.maintenance_blocked_sites;all_maintenance_ready=false;
        }
    }
    r.delegated_maintenance_ready=has_colony_utilities && all_maintenance_ready;
    for(const auto& [id,f]:farms_){
        if(f.source_ship!=ship)continue;
        if(!habitats_.count(id))r.homeless_outpost_workers+=f.workers;
    }
    if(r.crew_total<=0)return r;
    r.reserve_cycles=std::min({total_food,total_water,total_oxygen})/r.crew_total;
    // Last 3 complete needs intervals, not an all-time harvest counter.
    r.recent_food_demand_kg=r.crew_total*3;
    r.sustained_food_surplus=r.has_operational_farm;
    // Every one of the last three completed periods must cover demand;
    // a single bumper harvest cannot disguise two failed periods.
    for(int period=0;period<3;++period){
        std::int64_t produced=0;
        for(const auto& [id,f]:farms_){(void)id;
            if(f.source_ship==ship)produced+=f.recent_food_kg[period];
        }
        if(produced<r.crew_total)r.sustained_food_surplus=false;
    }
    r.renewable_food_proven=r.sustained_food_surplus;
    r.renewable_water_proven=l->recovered_water_kg>=r.crew_total*3;
    r.recent_shortage=(l->consecutive_supplied_cycles<3);
    for(const auto& [id,f]:farms_){(void)id;
        if(f.source_ship==ship && f.workers && f.life.consecutive_supplied_cycles<3)
            r.recent_shortage=true;
    }
    for(const auto& [id,h]:habitats_){(void)id;
        if(h.source_ship==ship && h.residents &&
           h.life.consecutive_supplied_cycles<3)r.recent_shortage=true;
    }
    if(r.reserve_cycles<3 || l->cycles<3 || r.recent_shortage)
        r.stage=ColonyViability::Stage::Survival;
    else {
        r.stage=ColonyViability::Stage::Stable;
        if(r.housed_colonists>0 && !r.homeless_outpost_workers &&
           r.housing_provisioned && r.housing_capacity>=r.housed_colonists &&
           (!has_colony_utilities || r.utilities_sustained))
            r.stage=ColonyViability::Stage::Livable;
    }
    r.sufficient_quality_of_life=r.stage>=ColonyViability::Stage::Livable;
    // A food surplus does not prove water/oxygen renewability, good living
    // conditions, equipment replacement, or permission to expand autonomously.
    return r;
}

const OutpostWorkshop* Simulation::workshop(EntityId site) const {
    const auto it=workshops_.find(site);
    return it==workshops_.end()?nullptr:&it->second;
}
bool Simulation::pending_workshop_shift(EntityId site,bool finisher,
                                        std::uint64_t command_id) const {
    if(!command_id)return false;
    const auto shop=workshops_.find(site);
    if(shop==workshops_.end() ||
       shop->second.cancelled_pending_shifts.count(command_id))return false;
    for(const auto& [due,orders]:commands_){
        if(due<=tick_ || due-tick_<=1)continue; // cancellation must commit first
        for(const auto& c:orders)
            if(c.id==command_id && c.target==site &&
               c.actor==shop->second.source_ship &&
               c.action==Action::StartWorkshopShift &&
               c.thrust_x==(finisher?2:1))return true;
    }
    return false;
}
bool Simulation::pending_workshop_worker_duty(
        std::uint64_t npc_id,std::uint64_t from_tick) const {
    if(!npc_id)return false;
    for(const auto& [due,orders]:commands_){
        if(due<from_tick)continue;
        for(const auto& c:orders){
            if(c.action!=Action::StartWorkshopShift ||
               c.amount!=static_cast<std::int64_t>(npc_id))continue;
            const auto shop=workshops_.find(c.target);
            if(shop==workshops_.end() || shop->second.source_ship!=c.actor ||
               shop->second.cancelled_pending_shifts.count(c.id))continue;
            return true;
        }
    }
    return false;
}
const UtilityFacility* Simulation::utility(EntityId site) const {
    const auto it=utilities_.find(site);
    return it==utilities_.end()?nullptr:&it->second;
}
const ClinicFacility* Simulation::clinic(EntityId site) const {
    const auto it=clinics_.find(site);return it==clinics_.end()?nullptr:&it->second;
}
const CommunityFacility* Simulation::community(EntityId site) const {
    const auto it=communities_.find(site);
    return it==communities_.end()?nullptr:&it->second;
}
const HabitatFacility* Simulation::habitat(EntityId outpost) const {
    auto it=habitats_.find(outpost);
    return it==habitats_.end()?nullptr:&it->second;
}
void Simulation::enable_individual_workforce(){
    if(snapshot_version_>=20 || !npcs_.empty() || tick_!=0 || !submitted_ids_.empty())
        throw std::invalid_argument("individual workforce migration requires unused genesis world");
    npc_next_seen_=true;
    for(const auto& [ship,c]:colony_ships_){
        for(std::int64_t n=0;n<c.crew;++n){
            auto id=next_npc_id_++;
            npcs_.emplace(id,NpcRecord{id,ship,ship,NpcRole::Ship,0,0,0,ship});
        }
        for(const auto& [site,f]:farms_)if(f.source_ship==ship)
            for(std::int64_t n=0;n<f.workers;++n){
                auto id=next_npc_id_++;
                npcs_.emplace(id,NpcRecord{id,ship,site,NpcRole::Farm,0,0,0,site});
            }
        for(const auto& [site,h]:habitats_)if(h.source_ship==ship)
            for(std::int64_t n=0;n<h.residents;++n){
                auto id=next_npc_id_++;
                npcs_.emplace(id,NpcRecord{id,ship,site,NpcRole::Habitat,0,0,0,site});
            }
    }
    if(npcs_.empty())throw std::invalid_argument("no colony population to materialize");
    snapshot_version_=31;
}
std::optional<transport::NpcTransport> Simulation::npc_transport_arrival(
        std::uint64_t receipt,std::uint64_t npc_id) const {
    if(!receipt || !npc_id)return std::nullopt;
    const auto person=npcs_.find(npc_id);
    if(person==npcs_.end() || person->second.transport_id)return std::nullopt;
    if(const auto voyage=voyages_.find(receipt);voyage!=voyages_.end()) {
        const auto& v=voyage->second;
        if(v.status!=2 || !v.origin || !v.destination ||
           person->second.location!=v.destination ||
           !transport::contains_person(v.passengers,npc_id))
            return std::nullopt;
        transport::NpcTransport arrival{receipt,transport::Mode::Ferry,
            {transport::LocationKind::ColonyShip,v.origin},
            {transport::LocationKind::ColonyShip,v.destination},
            v.passengers.size(),v.passengers,transport::TransportState::Arrived};
        if(transport::completed_transport(arrival,npc_id))return arrival;
        return std::nullopt;
    }
    // An NPC who has physically disembarked stays at the surface, even
    // after the vehicle has returned to orbit. A currently boarded
    // passenger/pilot is NOT settlement-arrived.
    for(const auto& [key,sh]:planetary_shuttles_) {
        const auto individual=sh.visitor_arrival_receipts.find(npc_id);
        // Legacy v84 visitor arrivals have no immutable personal receipt.
        // They are allowed to reboard, but a migration writer must reject
        // the legacy fallback until a new descent has been attested.
        const auto prior_receipt=individual==sh.visitor_arrival_receipts.end()?
            (snapshot_version_<85?sh.descent_receipt:0):individual->second;
        if(prior_receipt!=receipt ||
           !shuttle_flight_request_ids_.count(receipt) ||
           !transport::contains_person(sh.disembarked_ids,npc_id))continue;
        const auto site=planetary_sites_.find(key);
        if(site==planetary_sites_.end() ||
           person->second.home_ship!=sh.source_ship ||
           person->second.assigned_site!=sh.source_ship ||
           person->second.role!=NpcRole::Ship ||
           person->second.location!=site->second.surface_location)
            return std::nullopt;
        transport::NpcTransport arrival{receipt,transport::Mode::PlanetaryShuttle,
            {transport::LocationKind::ColonyShip,sh.source_ship},
            {transport::LocationKind::PlanetarySite,site->second.surface_location},
            64,sh.disembarked_ids,transport::TransportState::Arrived};
        if(transport::completed_transport(arrival,npc_id))return arrival;
        return std::nullopt;
    }
    return std::nullopt;
}

NpcCustodyView Simulation::npc_custody(std::uint64_t id) const {
    const auto it=npcs_.find(id);
    if(it==npcs_.end())throw std::out_of_range("npc custody: unknown identity");
    const auto& n=it->second;
    // Reuse the existing transport-facing NPC custody projection. The stored
    // NpcRecord remains a single authoritative person; its ferry transport_id
    // is not overloaded with an unrelated shuttle voyage identity.
    if(snapshot_version_>=81)
        for(const auto& [key,sh]:planetary_shuttles_) {
            (void)key;
            if((sh.pilot_npc==id ||
                std::binary_search(sh.passenger_ids.begin(),
                                   sh.passenger_ids.end(),id)) &&
                (sh.flight_phase==1 || sh.flight_phase==3))
                return {n.id,n.home_ship,n.location,n.role,sh.flight_request,true};
        }
    return {n.id,n.home_ship,n.location,n.role,n.transport_id,n.transport_id!=0};
}
std::size_t Simulation::children_physically_aboard(EntityId ship) const {
    std::size_t count=0;
    for(const auto& [id,child]:physical_dependents_){
        (void)id;
        if(child.location==ship)++count;
    }
    return count;
}
std::size_t Simulation::people_physically_aboard(EntityId ship) const {
    std::size_t count=0;
    for(const auto& [id,n]:npcs_){
        (void)id;
        if(n.location==ship){
            if(count==std::numeric_limits<std::size_t>::max())
                throw std::overflow_error("physical occupancy overflow");
            ++count;
        }
    }
    // v98: the player is a distinct physical occupant, not an NPC clone.
    // Existing life-support accounting uses this SAME physical population.
    if(snapshot_version_>=98)
        for(const auto& [owner,person]:player_people_){
            (void)owner;
            if(person.physical_ship==ship){
                if(count==std::numeric_limits<std::size_t>::max())
                    throw std::overflow_error("physical player occupancy overflow");
                ++count;
            }
        }
    return count;
}
std::size_t Simulation::passengers_aboard(EntityId ship) const {
    std::size_t count=0;
    for(const auto& [id,n]:npcs_){
        (void)id;
        if(n.location==ship && n.transport_id!=0)++count;
    }
    return count;
}
Simulation Simulation::migrate_legacy_active_voyages(
    const std::string& snapshot,
    const std::vector<LegacyVoyageManifest>& manifests){
    if(snapshot.rfind("galaxy-kernel-v32|",0)!=0)
        throw std::runtime_error("migration: v32 snapshot required");
    if(manifests.empty())throw std::runtime_error("migration: verified manifests required");
    // Transform in memory only. The v33 parser validates custody, ships,
    // passenger identity, manifest ordering, and orphaned passengers.
    std::string upgraded=snapshot;
    upgraded.replace(0,17,"galaxy-kernel-v33");
    std::set<std::uint64_t> voyage_ids, passenger_ids;
    std::string records;
    for(const auto& m:manifests){
        if(!m.transport_id || !m.ferry || !m.origin || m.ferry==m.origin ||
           m.passenger_ids.empty() || m.passenger_ids.size()>10000 ||
           !voyage_ids.insert(m.transport_id).second)
            throw std::runtime_error("migration: invalid/duplicate voyage");
        records+="voyage|"+std::to_string(m.transport_id)+"|"+
            std::to_string(m.ferry)+"|"+std::to_string(m.origin)+"|1|"+
            std::to_string(m.passenger_ids.size());
        std::uint64_t prev=0;
        for(auto id:m.passenger_ids){
            if(!id || id<=prev || !passenger_ids.insert(id).second)
                throw std::runtime_error("migration: unsorted/duplicate passenger");
            prev=id;records+="|"+std::to_string(id);
        }
        records+='\n';
    }
    auto insertion=upgraded.find("\naccepted|");
    if(insertion==std::string::npos)
        throw std::runtime_error("migration: missing command receipts");
    upgraded.insert(insertion+1,records);
    auto result=Simulation::from_snapshot(upgraded);
    // Reject extraneous manifests: every listed passenger must be in active
    // custody, and the v33 parser has already checked the converse.
    std::size_t active=0;
    for(const auto& [id,n]:result.npc_records()){
        (void)id;
        if(n.transport_id)++active;
    }
    if(active!=passenger_ids.size())
        throw std::runtime_error("migration: manifest does not match active custody");
    return result;
}
void Simulation::upgrade_passenger_snapshot_format(){
    if(snapshot_version_<20)throw std::logic_error("individual NPC records required");
    for(const auto& [id,n]:npcs_){
        (void)id;
        if(n.transport_id!=0)throw std::logic_error("active passenger migration unsupported");
    }
    snapshot_version_=std::max(snapshot_version_,31);
}
void Simulation::enable_passenger_life_support(){
    if(!player_people_.empty() || tick_ || !submitted_ids_.empty() || !commands_.empty() ||
       snapshot_version_<32 || npcs_.empty())
        throw std::invalid_argument("passenger life support requires new named-crew genesis world");
    snapshot_version_=57; // Versioned, durable; never silently alter old-world replay.
}
void Simulation::upgrade_assignment_snapshot_format(){
    if(snapshot_version_<20)throw std::logic_error("individual NPC records required");
    for(auto& [id,n]:npcs_){(void)id;if(!n.assigned_site)n.assigned_site=n.location;}
    snapshot_version_=std::max(snapshot_version_,32);
}
void Simulation::upgrade_legal_residence_snapshot_format(){
    if(snapshot_version_<85 || snapshot_version_>86 || npcs_.empty())
        throw std::logic_error("legal residence upgrade requires v85 named population");
    if(snapshot_version_==86)return;
    for(auto& [id,n]:npcs_) {
        (void)id;
        if(!n.home_ship || (n.residence_id && n.residence_id!=n.home_ship) ||
           n.residence_kind!=transport::LocationKind::ColonyShip)
            throw std::logic_error("legal residence upgrade requires canonical ship homes");
        n.residence_id=n.home_ship;
    }
    snapshot_version_=86;
}
void Simulation::enable_medical_workforce_rules(){
    if(!player_people_.empty() || tick_ || !submitted_ids_.empty() || !commands_.empty() ||
       snapshot_version_<32 || npcs_.empty())
        throw std::invalid_argument("v40 medical workforce opt-in is genesis-only");
    snapshot_version_=40;
}
void Simulation::move_npc_records(EntityId ship,EntityId site,NpcRole role,std::int64_t delta){
    if(snapshot_version_<20 || !delta)return;
    const auto source=delta>0?ship:site;
    const auto target=delta>0?site:ship;
    const auto source_role=delta>0?NpcRole::Ship:role;
    const auto target_role=delta>0?role:NpcRole::Ship;
    auto qty=delta>0?delta:-delta;
    for(auto& [id,n]:npcs_){
        (void)id;
        if(n.home_ship!=ship || n.location!=source || n.role!=source_role)continue;
        if(snapshot_version_>=40 && delta>0 && !npc_available_for_duty(id))continue;
        // A resident reassigned away from this outpost cannot remain its
        // machine operator. Reserved material remains owned by the workshop.
        if(source!=ship){
            auto wi=workshops_.find(source);
            if(wi!=workshops_.end()){
                if(wi->second.operator_npc_id==id)wi->second.operator_npc_id=0;
                if(wi->second.finisher_npc_id==id)wi->second.finisher_npc_id=0;
            }
        }
        n.location=target;n.assigned_site=target;n.role=target_role;
        // A departed mentor or apprentice must not leave phantom training.
        for(auto& [other,trainee]:npcs_)if(trainee.mentor_id==n.id)trainee.mentor_id=0;
        n.mentor_id=0;
        if(--qty==0)break;
    }
    if(qty)throw std::logic_error("NPC count out of sync with staff ledger");
    if(role==NpcRole::Farm && farm_team_xp_.count(site))
        farm_team_xp_[site]=farm_team_xp_[site]*2/3; // team turnover erodes shared coordination
}
void Simulation::award_farm_experience(EntityId site){
    if(snapshot_version_<20)return;
    const auto f=farms_.find(site);
    if(f==farms_.end() || f->second.workers==0)return;
    for(auto& [id,n]:npcs_){
        (void)id;
        if(n.role!=NpcRole::Farm || n.assigned_site!=site || n.location!=site)continue;
        if(snapshot_version_>=40 && !npc_available_for_duty(id))continue;
        std::uint64_t bonus=0;
        if(n.mentor_id){
            auto mentor=npcs_.find(n.mentor_id);
            if(mentor!=npcs_.end() && mentor->second.assigned_site==site && mentor->second.location==site &&
               mentor->second.role==NpcRole::Farm && mentor->second.farm_xp>n.farm_xp)
                bonus=1;
        }
        n.farm_xp=std::min<std::uint64_t>(1000000,n.farm_xp+1+bonus);
        // The training relationship ends once the apprentice has caught up.
        if(n.mentor_id){
            const auto mentor=npcs_.find(n.mentor_id);
            if(mentor==npcs_.end() || n.farm_xp>=mentor->second.farm_xp)
                n.mentor_id=0;
        }
    }
    std::int64_t available=f->second.workers;
    if(snapshot_version_>=40){
        available=0;
        for(const auto& [id,n]:npcs_)if(n.role==NpcRole::Farm &&
            n.assigned_site==site && npc_available_for_duty(id))++available;
    }
    if(available>=2)farm_team_xp_[site]=std::min<std::uint64_t>(1000000,farm_team_xp_[site]+1);
}
FarmWorkforceAssessment Simulation::assess_farm_workforce(EntityId site) const {
    FarmWorkforceAssessment r{};
    const auto f=farms_.find(site);
    if(f==farms_.end())return r;
    r.workers=f->second.workers;
    if(auto it=farm_team_xp_.find(site);it!=farm_team_xp_.end())r.cooperative_cycles=it->second;
    r.coordination=static_cast<std::int64_t>(std::min<std::uint64_t>(100,r.cooperative_cycles/10));
    for(const auto& [id,n]:npcs_){(void)id;
        if(n.assigned_site==site && n.location==site && n.role==NpcRole::Farm)r.experience_total+=n.farm_xp;
    }
    return r;
}
const ColonyShip* Simulation::colony_ship(EntityId id) const {
    const auto it=colony_ships_.find(id);
    return it==colony_ships_.end()?nullptr:&it->second;
}
inventory::StackStore Simulation::ship_inventory_snapshot(EntityId ship_id) const {
    const auto* ship=find(ship_id);
    // Wreck inventory remains physical and conserved. This is a read-only
    // snapshot; accepting a wreck here does not authorize any ship action.
    if(!ship || (ship->type!=EntityType::Ship &&
       !(ship->type==EntityType::Wreck && colony_ships_.count(ship_id))))
        throw std::invalid_argument("not a ship or colony wreck");
    if(ship->cargo_capacity<0 || ship->cargo_capacity>1000000000000LL)
        throw std::overflow_error("invalid ship cargo capacity");
    inventory::StackStore store(inventory::Catalogue::standard(),ship->cargo_capacity*1000);
    store.load("ore",ship->cargo);
    if(const auto* c=colony_ship(ship_id)) {
        store.load("refined",c->refined);
        store.load("construction_module",c->modules);
        store.load("tailings",c->tailings_kg);
        for(const auto& [code,units]:c->minerals_kg)
            store.load("mineral/"+code,units);
        for(const auto& [code,units]:c->goods_kg)
            store.load("good/"+code,units);
    }
    return store;
}
const Vector2* Simulation::factory_equipment_position(EntityId site,std::uint32_t node) const {
    const auto st=storages_.find(site);
    if(st==storages_.end())return nullptr;
    const auto it=st->second.equipment_positions.find(node);
    return it==st->second.equipment_positions.end()?nullptr:&it->second;
}
const OutpostStorage* Simulation::storage(EntityId site) const {
    auto it=storages_.find(site);return it==storages_.end()?nullptr:&it->second;
}
inventory::StackStore Simulation::storage_inventory_snapshot(EntityId site) const {
    auto it=storages_.find(site);
    if(it==storages_.end())throw std::invalid_argument("storage not built");
    inventory::StackStore out(inventory::Catalogue::standard(),4000);
    for(const auto& [name,qty]:it->second.goods_kg)out.load("good/"+name,qty);
    return out;
}
inventory::StackStore Simulation::storage_node_inventory_snapshot(EntityId site,std::uint32_t node) const {
    if(node==0)return storage_inventory_snapshot(site);
    auto it=storages_.find(site);
    if(it==storages_.end() || node>static_cast<std::uint32_t>(std::numeric_limits<int>::max()/4) || !it->second.extra_bins.count(node))
        throw std::invalid_argument("storage node not constructed");
    inventory::StackStore out(inventory::Catalogue::standard(),4000);
    for(const auto& [name,qty]:it->second.extra_bins.at(node))out.load("good/"+name,qty);
    return out;
}
inventory::StackStore Simulation::workshop_bin_inventory_snapshot(EntityId site, int item) const {
    const auto it=workshops_.find(site);
    if(it==workshops_.end() || item<0 || item>3)
        throw std::invalid_argument("invalid workshop bin");
    const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
    const auto& w=it->second;
    const std::int64_t amounts[]={w.aluminum_kg,w.panels_kg,w.frames_kg,w.scrap_kg};
    // Existing site-local production buffers remain 1000 units each.
    inventory::StackStore bin(inventory::Catalogue::standard(),1000);
    bin.load(std::string("good/")+names[item],amounts[item]);
    return bin;
}
std::int64_t Simulation::loaded_cargo_liters(const Entity& ship) const {
    return ship_inventory_snapshot(ship.id).used_liters();
}
std::int64_t Simulation::ship_cargo_volume_liters(EntityId ship_id) const {
    return ship_inventory_snapshot(ship_id).used_liters();
}
void Simulation::transfer_geological_minerals(EntityId ship, EntityId rock, std::int64_t tonnes){
    auto c=colony_ships_.find(ship);
    const auto g=geology_.find(rock);
    const auto local=local_deposit_taxonomy(rock);
    if(c==colony_ships_.end() || (g==geology_.end()&&!local))
        throw std::runtime_error("mineral transfer requires colony ship and classified deposit");
    const auto taxonomy=g==geology_.end()?*local:g->second.taxonomy;
    for(const auto phase:mineral_profile(taxonomy))
        c->second.minerals_kg[phase.name] += tonnes*phase.permille;
    snapshot_version_=std::max(snapshot_version_,9);
}

EntityId Simulation::add_asteroid(Vector2 p, std::int64_t ore) {
    if (ore <= 0 || ore > 1000000000LL) throw std::invalid_argument("invalid asteroid ore");
    Entity entity;
    entity.type = EntityType::Asteroid;
    entity.position = p;
    entity.ore = ore;
    return add_entity(entity);
}

EntityId Simulation::add_geological_asteroid(Vector2 p, AsteroidClass taxonomy,
                                                   std::int64_t radius_m, std::int64_t density_kg_m3) {
    if(tick_ || !submitted_ids_.empty()) throw std::invalid_argument("geology can only be seeded at genesis");
    if(static_cast<unsigned>(taxonomy)>2)throw std::invalid_argument("invalid asteroid taxonomy");
    const auto mass=asteroid_mass_tonnes(radius_m,density_kg_m3);
    const auto id=add_asteroid(p,mass);
    geology_.emplace(id,AsteroidGeology{taxonomy,radius_m,density_kg_m3,mass});
    snapshot_version_=std::max(snapshot_version_,8);
    return id;
}
const AsteroidGeology* Simulation::geology(EntityId id) const {
    const auto it=geology_.find(id);
    return it==geology_.end()?nullptr:&it->second;
}

EntityId Simulation::add_station(Vector2 p) {
    Entity entity;
    entity.type = EntityType::Station;
    entity.position = p;
    return add_entity(entity);
}

void Simulation::add_system(std::uint32_t system) {
    if (!system || tick_ || !submitted_ids_.empty())
        throw std::invalid_argument("systems can only be added at world creation");
    if (!systems_.insert(system).second) throw std::invalid_argument("duplicate system");
    snapshot_version_ = std::max(snapshot_version_,5);
}
EntityId Simulation::add_station_in_system(std::uint32_t system, Vector2 position) {
    if (tick_ || !systems_.count(system)) throw std::invalid_argument("unknown or locked system");
    Entity entity; entity.type=EntityType::Station;entity.position=position;
    const auto id=add_entity(entity);entities_.at(id).system_id=system;
    snapshot_version_=std::max(snapshot_version_,5);return id;
}
EntityId Simulation::add_asteroid_in_system(std::uint32_t system, Vector2 position, std::int64_t ore) {
    if (tick_ || !systems_.count(system) || ore<=0 || ore>1000000000LL)
        throw std::invalid_argument("unknown system or ore");
    Entity entity;entity.type=EntityType::Asteroid;entity.position=position;entity.ore=ore;
    const auto id=add_entity(entity);entities_.at(id).system_id=system;
    snapshot_version_=std::max(snapshot_version_,5);return id;
}
EntityId Simulation::add_gate(std::uint32_t origin, Vector2 position,
                              std::uint32_t destination, Vector2 arrival) {
    // A gate must link two *registered* real systems; no invisible teleport target.
    if (tick_ || origin==destination || !systems_.count(origin) || !systems_.count(destination) ||
        !valid_position(arrival) ||
        arrival.x>kWorldBoundMm-12000 || arrival.x<-kWorldBoundMm+12000)
        throw std::invalid_argument("invalid stargate link");
    Entity gate;gate.type=EntityType::Stargate;gate.position=position;
    gate.gate_destination=destination;gate.gate_arrival=arrival;gate.gate_online=true;
    const auto id=add_entity(gate);entities_.at(id).system_id=origin;
    snapshot_version_=std::max(snapshot_version_,5);return id;
}
void Simulation::configure_gate_operations(EntityId first, EntityId second,
                                            std::int64_t condition,
                                            std::int64_t spare_parts,
                                            std::int64_t technicians) {
    const auto *a=find(first), *b=find(second);
    if(tick_ || !submitted_ids_.empty() || !a || !b || first==second ||
       a->type!=EntityType::Stargate || b->type!=EntityType::Stargate ||
       a->system_id!=b->gate_destination || b->system_id!=a->gate_destination ||
       condition<0 || condition>1000 || spare_parts<0 || spare_parts>1000000 ||
       technicians<0 || technicians>16)
        throw std::invalid_argument("invalid initial gate maintenance");
    const auto pair=std::minmax(first,second);
    if(!gate_ops_.emplace(pair, GateOperations{condition,spare_parts,technicians,0,0}).second)
        throw std::invalid_argument("duplicate gate maintenance");
    if(!condition){entities_.at(first).gate_online=false;entities_.at(second).gate_online=false;}
    snapshot_version_=std::max(snapshot_version_,6);
}
const GateOperations* Simulation::gate_operations(EntityId first, EntityId second) const {
    const auto it=gate_ops_.find(std::minmax(first,second));
    return it==gate_ops_.end()?nullptr:&it->second;
}
bool Simulation::protected_from_attack(EntityId id) const {
    const auto* e=find(id);
    return e && e->type==EntityType::Ship && e->jump_state!=ShipJumpState::Ready;
}

EntityId Simulation::spawn_ship_docked(PlayerId owner, EntityId station_id,
                                       std::int64_t ship_capacity, std::int64_t hangar_capacity) {
    if (tick_ != 0 || !submitted_ids_.empty())
        throw std::invalid_argument("docked spawning is initial-world setup only");
    const auto* station = find(station_id);
    if (owner == 0 || !station || station->type != EntityType::Station ||
        ship_capacity <= 0 || ship_capacity > 1000000000LL ||
        hangar_capacity <= 0 || hangar_capacity > 1000000000LL)
        throw std::invalid_argument("invalid station, owner or capacity");
    const auto key = std::make_pair(station_id, owner);
    auto existing = hangars_.find(key);
    if (existing != hangars_.end() && existing->second.capacity != hangar_capacity)
        throw std::invalid_argument("existing station hangar capacity mismatch");
    Entity entity;
    entity.type = EntityType::Ship;
    entity.owner_id = owner;
    entity.position = station->position;
    entity.cargo_capacity = ship_capacity;
    entity.hull=100;entity.weapon_charges=12;
    entity.dock_state = DockState::Docked;
    entity.dock_station = station_id;
    const auto ship = add_entity(entity);
    if (existing == hangars_.end()) hangars_.emplace(key, StationHangar{0, hangar_capacity});
    wallets_.emplace(owner,0);
    snapshot_version_ = std::max(snapshot_version_, 3);
    return ship;
}

void Simulation::configure_market(EntityId station_id, std::int64_t bid, std::int64_t reserve) {
    if (tick_ != 0 || !submitted_ids_.empty() || !find(station_id) ||
        find(station_id)->type != EntityType::Station || bid < 1 || bid > 1000000 ||
        reserve < 0 || reserve > 1000000000000LL || markets_.count(station_id))
        throw std::invalid_argument("invalid initial station market");
    markets_.emplace(station_id, StationMarket{0,reserve,bid});
    snapshot_version_ = std::max(snapshot_version_,4);
}
const StationMarket* Simulation::market(EntityId station_id) const {
    const auto it = markets_.find(station_id);
    return it == markets_.end() ? nullptr : &it->second;
}
std::int64_t Simulation::wallet(PlayerId owner) const {
    const auto it = wallets_.find(owner);
    return it == wallets_.end() ? 0 : it->second;
}

const StationHangar* Simulation::hangar(EntityId station_id, PlayerId owner) const {
    auto it = hangars_.find({station_id, owner});
    return it == hangars_.end() ? nullptr : &it->second;
}

// SplitMix64: fully specified fixed-width integer PRNG, no stdlib distribution differences.
std::uint64_t Simulation::random_u64() {
    std::uint64_t z = (rng_state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::vector<EntityId> Simulation::generate_asteroids(std::size_t count, std::int64_t max_coord) {
    if (max_coord < 1 || max_coord > kWorldBoundMm / 2 || count > 100000)
        throw std::invalid_argument("invalid asteroid generation limits");
    std::vector<EntityId> ids;
    ids.reserve(count);
    const auto diameter = static_cast<std::uint64_t>(2 * max_coord + 1);
    for (std::size_t i = 0; i < count; ++i) {
        const auto x = static_cast<std::int64_t>(random_u64() % diameter) - max_coord;
        const auto y = static_cast<std::int64_t>(random_u64() % diameter) - max_coord;
        const auto ore = 10 + static_cast<std::int64_t>(random_u64() % 1000ULL);
        ids.push_back(add_asteroid({x, y}, ore));
    }
    return ids;
}

NpcHealthStatus Simulation::npc_health_status(std::uint64_t id) const {
    if(casualties_.count(id))return NpcHealthStatus::Deceased;
    if(!npcs_.count(id))return NpcHealthStatus::Unknown;
    const auto p=medical_cases_.find(id);
    if(p==medical_cases_.end())return NpcHealthStatus::Healthy;
    if(p->second.recovered_tick)return NpcHealthStatus::Recovered;
    if(p->second.recovery_due_tick)return NpcHealthStatus::Recovering;
    if(p->second.treatment_command)return NpcHealthStatus::Treated; // legacy v38 treatment
    return p->second.stabilization_command?NpcHealthStatus::Stabilized:NpcHealthStatus::Injured;
}

// FLIGHT-01: one generic pilot reservation spans all NPC ship purposes.
// The native preflight first verifies ordinary duty, health, travel custody;
// this additional derived check prevents other work from stealing a pilot
// while this already-authorized flight owns that individual.
bool Simulation::qualified_npc_flight_pilot(
        EntityId ship,std::uint64_t pilot) const {
    const auto it=npcs_.find(pilot);
    if(!pilot || it==npcs_.end() || it->second.role!=NpcRole::Ship ||
       it->second.home_ship!=ship || it->second.location!=ship ||
       it->second.assigned_site!=ship || it->second.transport_id ||
       it->second.mentor_id ||
       it->second.legal_residence()!=
           (transport::Endpoint{transport::LocationKind::ColonyShip,ship}))
        return false;
    // Ignore only the order's own reservation; all other native jobs
    // consult the full duty check with active-flight reservations.
    return npc_available_for_duty_without_flight(pilot);
}
// Centralized admission: all NPC navigation modes select and reserve from
// the same real named workforce and original native duty checks.
std::uint64_t Simulation::select_npc_flight_pilot(
        EntityId ship,std::uint64_t requested) const {
    if(requested)
        return qualified_npc_flight_pilot(ship,requested) &&
               npc_available_for_duty(requested) ? requested:0;
    for(const auto& [id,person]:npcs_){
        (void)person;
        if(qualified_npc_flight_pilot(ship,id) &&
           npc_available_for_duty(id))return id;
    }
    return 0;
}
bool Simulation::npc_flight_slot_available(
        EntityId ship,std::uint64_t pilot) const {
    if(!ship || !pilot)return false;
    for(const auto& [id,flight]:npc_flight_orders_){
        (void)id;
        if((flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit) &&
           (flight.ship==ship || flight.pilot_npc==pilot))
            return false;
    }
    return true;
}
bool Simulation::npc_available_for_duty_without_flight(std::uint64_t npc_id) const {
    const auto it=npcs_.find(npc_id);
    if(it==npcs_.end())return false;
    const auto& n=it->second;
    if(n.location!=n.assigned_site || n.transport_id)return false;
    // A booked seat reserves the very same NPC for an actual transport leg.
    // Prevent a concurrently issued workforce order from assigning this
    // physical person to two destinations.
    for(const auto& [key,sh]:planetary_shuttles_) {
        (void)key;
        if(sh.pilot_npc==npc_id ||
           std::binary_search(sh.passenger_ids.begin(),
                              sh.passenger_ids.end(),npc_id) ||
           std::binary_search(sh.disembarked_ids.begin(),
                              sh.disembarked_ids.end(),npc_id))return false;
    }
    const auto status=npc_health_status(npc_id);
    return status==NpcHealthStatus::Healthy || status==NpcHealthStatus::Treated ||
           status==NpcHealthStatus::Recovered;
}
bool Simulation::npc_available_for_duty(std::uint64_t npc_id) const {
    if(!npc_available_for_duty_without_flight(npc_id))return false;
    // The existing real worker cannot also pilot a shuttle, perform
    // industrial labor or accept another voyage while on a flight order.
    for(const auto& [id,flight]:npc_flight_orders_){
        (void)id;
        if(flight.pilot_npc==npc_id &&
           (flight.phase==NpcFlightPhase::Planned ||
            flight.phase==NpcFlightPhase::InTransit))return false;
    }
    return true;
}

NpcResidenceAudit Simulation::assess_npc_residence() const {
    NpcResidenceAudit view{};
    for(const auto& [id,n]:npcs_) {
        (void)id;
        const auto address=n.legal_residence();
        if(!address.id || !n.location ||
           view.total_living==std::numeric_limits<std::uint64_t>::max()) {
            view.reconciled=false;
            continue;
        }
        ++view.living_by_residence[address];
        ++view.physically_present[n.location];
        ++view.total_living;
    }
    if(view.total_living!=npcs_.size())
        view.reconciled=false;
    return view;
}

ColonyMedicalAudit Simulation::assess_medical_population() const {
    ColonyMedicalAudit a;std::map<std::pair<EntityId,int>,std::int64_t> counted;
    for(const auto& [id,n]:npcs_){
        auto& c=a.by_home_ship[n.home_ship];++c.living;
        const auto status=npc_health_status(id);
        if(status==NpcHealthStatus::Injured)++c.injured;
        else if(status==NpcHealthStatus::Stabilized)++c.stabilized;
        else if(status==NpcHealthStatus::Recovering)++c.recovering;
        else ++c.fit;
        if(n.transport_id || n.location!=n.assigned_site)++c.in_transit;
        if(npc_available_for_duty(id))++c.on_duty;else ++c.unavailable;
        ++counted[{n.assigned_site,static_cast<int>(n.role)}];
    }
    for(const auto& [id,d]:casualties_){(void)id;++a.by_home_ship[d.identity.home_ship].deceased;}
    a.reconciled=true;
    for(const auto& [id,c]:colony_ships_)
        if(counted[{id,static_cast<int>(NpcRole::Ship)}]!=c.crew)a.reconciled=false;
    for(const auto& [id,c]:farms_)
        if(counted[{id,static_cast<int>(NpcRole::Farm)}]!=c.workers)a.reconciled=false;
    for(const auto& [id,c]:habitats_)
        if(counted[{id,static_cast<int>(NpcRole::Habitat)}]!=c.residents)a.reconciled=false;
    for(const auto& [site_key,facility]:planetary_sites_) {
        (void)site_key;
        if(facility.assigned_workers &&
           counted[{facility.surface_location,
                    static_cast<int>(NpcRole::PlanetarySurface)}]!=
                    facility.assigned_workers)a.reconciled=false;
    }
    for(const auto& [id,c]:a.by_home_ship){(void)id;
        if(c.living!=c.fit+c.injured+c.stabilized+c.recovering ||
           c.living!=c.on_duty+c.unavailable || c.in_transit>c.unavailable)
            a.reconciled=false;
    }
    return a;
}

unsigned Simulation::voyage_status(std::uint64_t id) const {
    const auto it=voyages_.find(id);
    return it==voyages_.end()?0:it->second.status;
}

bool Simulation::completed_voyage_matches(
    std::uint64_t transport_id,EntityId ferry,EntityId origin,
    EntityId destination,const std::vector<std::uint64_t>& passengers) const{
    const auto it=voyages_.find(transport_id);
    return it!=voyages_.end() && it->second.status==2 &&
           it->second.ferry==ferry && it->second.origin==origin &&
           it->second.destination==destination &&
           it->second.passengers==passengers;
}

Simulation::FerryEmergencyReport Simulation::ferry_emergency_report(std::uint64_t voyage_id) const {
    FerryEmergencyReport report;
    const auto v=voyages_.find(voyage_id);
    if(v==voyages_.end() || v->second.status!=6 ||
       !v->second.rescue_transport_id || !v->second.last_incident_id)return report;
    const auto rescue=voyages_.find(v->second.rescue_transport_id);
    if(rescue==voyages_.end())return report;
    report.recorded=true;
    report.incident_command_id=v->second.last_incident_id;
    report.rescue_voyage_id=v->second.rescue_transport_id;
    report.wreck=v->second.ferry;
    report.rescue_ship=v->second.destination;
    report.survivors=rescue->second.passengers;
    for(const auto& [id,death]:casualties_){
        if(death.command_id==report.incident_command_id && death.ferry==report.wreck)
            report.deceased.push_back(id);
    }
    return report;
}

bool Simulation::can_depart_colony(const Command& c) const {
    // v58: a bounded all-adult local colony transfer is ONE world command.
    // No passenger voyage, offsite transport, fabricated guardian authority,
    // remote relocation, or supply deficit can be accepted.
    if(c.action!=Action::HostColonyDeparture ||
       c.issuer!=kServerAuthority || snapshot_version_<57 ||
       !c.actor || !c.target || c.actor==c.target || !c.id ||
       (c.execute_tick!=tick_+1 && c.execute_tick!=tick_) || c.amount || c.transport_id ||
       c.secondary_receiver || c.secondary_transport || c.thrust_x || c.thrust_y ||
       c.passenger_ids.empty() || c.passenger_ids.size()>16 ||
       !std::is_sorted(c.passenger_ids.begin(),c.passenger_ids.end()) ||
       std::adjacent_find(c.passenger_ids.begin(),c.passenger_ids.end())!=c.passenger_ids.end() ||
       !c.passenger_ids.front() || colony_departures_.count(c.actor) ||
       colony_departures_.count(c.target))return false;
    const auto* source=find(c.actor);const auto* receiver=find(c.target);
    const auto from=colony_ships_.find(c.actor),to=colony_ships_.find(c.target);
    const auto* receiver_life=life_support(c.target);
    if(!source||!receiver||from==colony_ships_.end()||to==colony_ships_.end()||
       !receiver_life || source->type!=EntityType::Ship ||
       receiver->type!=EntityType::Ship ||
       source->owner_id!=receiver->owner_id ||
       source->system_id!=receiver->system_id ||
       source->jump_state!=ShipJumpState::Ready ||
       receiver->jump_state!=ShipJumpState::Ready ||
       source->dock_state!=DockState::InSpace ||
       receiver->dock_state!=DockState::InSpace ||
       from->second.crew<1 || to->second.crew<1 ||
       to->second.crew>1000-static_cast<std::int64_t>(c.passenger_ids.size()) ||
       passengers_aboard(c.actor) || passengers_aboard(c.target))return false;
    const auto near=[&](const Entity* origin){
        if(!origin || origin->system_id!=receiver->system_id)return false;
        const auto dx=origin->position.x-receiver->position.x;
        const auto dy=origin->position.y-receiver->position.y;
        return dx>=-kDockingRangeMm && dx<=kDockingRangeMm &&
               dy>=-kDockingRangeMm && dy<=kDockingRangeMm &&
               dx*dx+dy*dy<=kDockingRangeMm*kDockingRangeMm;
    };
    if(!near(source))return false;
    // The source owner cannot suppress urgent life preservation, but a
    // healthy colony cannot be forcibly stripped of its crew by this API.
    bool danger=false;
    if(life_support(c.actor)){
        const auto risk=colony_safety::assess(*this,c.actor,c.actor).risk;
        danger=static_cast<int>(risk)>=static_cast<int>(colony_safety::Risk::Critical);
    }
    std::uint64_t declared=static_cast<std::uint64_t>(from->second.crew);
    for(const auto& [site,h]:habitats_)if(h.source_ship==c.actor){
        if(h.residents<0 || !near(find(site)))return false;
        declared+=static_cast<std::uint64_t>(h.residents);
        if(h.residents &&
           static_cast<int>(colony_safety::assess(*this,c.actor,site).risk)>=
           static_cast<int>(colony_safety::Risk::Critical))danger=true;
    }
    for(const auto& [site,f]:farms_)if(f.source_ship==c.actor){
        if(f.workers<0 || !near(find(site)))return false;
        declared+=static_cast<std::uint64_t>(f.workers);
        if(f.workers && !habitats_.count(site) &&
           static_cast<int>(colony_safety::assess(*this,c.actor,site).risk)>=
           static_cast<int>(colony_safety::Risk::Critical))danger=true;
    }
    if(!danger || declared!=c.passenger_ids.size())return false;
    // Fail closed on staffed services and specialist jobs for which departure
    // needs an independent social/employment/clinical settlement adapter.
    for(const auto& [site,u]:utilities_)
        if(u.source_ship==c.actor && u.technicians)return false;
    for(const auto& [site,cmt]:communities_)
        if(cmt.source_ship==c.actor && cmt.attendants)return false;
    for(const auto& [site,cl]:clinics_)
        if(cl.source_ship==c.actor && (cl.medics||cl.active_cases))return false;
    for(const auto& [site,w]:workshops_)
        if(w.source_ship==c.actor && (w.operator_npc_id||w.finisher_npc_id))return false;
    for(const auto& [site,policy]:policy_sites_)if(policy.source_ship==c.actor)
        for(const auto& d:policy.domains)if(d.inspectors)return false;
    for(const auto& [command,shift]:medic_shifts_){
        (void)command;
        const auto n=npcs_.find(shift.medic_id);
        if(n!=npcs_.end() && n->second.home_ship==c.actor &&
           shift.end_tick>tick_)return false;
    }
    for(const auto& [ship,job]:mining_)if(ship==c.actor)return false;
    for(const auto& [tick,cmds]:commands_)for(const auto& cmd:cmds){
        (void)tick;
        if(cmd.id==c.id)continue; // the executing command is still in the due batch
        if(cmd.actor==c.actor || cmd.actor==c.target ||
           cmd.target==c.actor || cmd.target==c.target)
            return false;
    }
    std::vector<std::uint64_t> exact;
    for(const auto& [id,n]:npcs_)if(n.home_ship==c.actor){
        // No preexisting voyage, detached employment or unverified mentor link.
        if(n.transport_id||n.location!=n.assigned_site||n.mentor_id||
           named_medics_.count(id)||!near(find(n.location)))return false;
        exact.push_back(id);
    }
    if(exact!=c.passenger_ids)return false;
    std::size_t children=0;
    for(const auto& [id,dependent]:physical_dependents_){
        (void)id;
        if(dependent.home_ship!=c.actor && dependent.location!=c.actor)continue;
        if(dependent.home_ship!=c.actor || dependent.location!=c.actor ||
           !std::binary_search(c.passenger_ids.begin(),c.passenger_ids.end(),
                               dependent.guardian_id))return false;
        ++children;
    }
    if(exact.size()+children>16)return false;
    // All persons are rehomed as real ship operating crew, not incorrectly
    // left in an active voyage or simultaneously on the old station payroll.
    const auto present=people_physically_aboard(c.target);
    const auto count=present+children_physically_aboard(c.target)+exact.size()+children;
    if(count>1000 ||
       receiver_life->food_kg<static_cast<std::int64_t>(2*count) ||
       receiver_life->water_kg<static_cast<std::int64_t>(2*count) ||
       receiver_life->oxygen_kg<static_cast<std::int64_t>(2*count))
        return false;
    return true;
}
bool Simulation::submit(const Command& c) {
    if((legacy_planetary_residence_import_ &&
        c.id==legacy_planetary_residence_import_->host_request_id) ||
        planetary_family_booking_ids_.count(c.id))
        return false; // immutable host and family approvals are not ship commands
    if(ship_status_.contains_id(c.id))return false;
    // WO3-E: an accepted original owner colony/site work or founder
    // request ID is immutable and cannot be reused as a ship command.
    // The original host registry does not reassign physical provenance.
    if(snapshot_version_>=109)
        for(const auto& [key,site]:planetary_sites_) {
            (void)key;
            if(c.id && (c.id==site.housing_order_id ||
                        c.id==site.utility_order_id))return false;
            for(const auto request:site.founder_consent_requests)
                if(request && request==c.id)return false;
            for(const auto& [resident,proof]:site.visitor_residency) {
                (void)resident;
                if(c.id && (c.id==proof[1] || c.id==proof[2]))
                    return false;
            }
            for(const auto& relief:site.utility_handovers)
                if(c.id && c.id==relief.request_id)return false;
            for(const auto& project:site.production_projects)
                if(c.id && c.id==project.order_id)return false;
            for(const auto& project:site.service_projects)
                if(c.id && c.id==project.order_id)return false;
            for(const auto& history:site.service_handovers)
                for(const auto& handover:history)
                    if(c.id && c.id==handover.request_id)return false;
            for(const auto& history:site.production_handovers)
                for(const auto& handover:history)
                    if(c.id && c.id==handover.request_id)return false;
        }
    if(colony_departures_.count(c.actor) || colony_departures_.count(c.target))return false;
    // Building a hull never creates a pilot. A manufactured uncommissioned
    // vessel is physically present but not authorized to run ship commands.
    if(snapshot_version_>=91){
        const auto actor=entities_.find(c.actor);
        if(actor!=entities_.end() && actor->second.manufactured_order_id &&
           (!colony_ships_.count(c.actor) ||
            colony_ships_.at(c.actor).crew<1))return false;
    }

    if(c.action==Action::FocusSurveyProbe||c.action==Action::CancelSurveyFocus){
        if(!c.id||!c.actor||!c.target||c.thrust_x||c.thrust_y||
           c.transport_id||!c.passenger_ids.empty()||
           c.secondary_receiver||c.secondary_transport||
           (c.action==Action::FocusSurveyProbe ? c.amount<=0 : c.amount!=0))
            return false;
        snapshot_version_=std::max(snapshot_version_,69);
    }
    if(c.action==Action::LaunchGeologicalDrone){
        if(!c.id || !c.actor || !c.target || c.amount || c.thrust_x || c.thrust_y ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,67);
    }
    if(c.action==Action::DeploySurveyProbe ||
       c.action==Action::ScanSurveyProbe ||
       c.action==Action::RecoverSurveyProbe){
        if(!c.id || !c.actor || c.amount || c.thrust_x || c.thrust_y ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport ||
           (c.action==Action::DeploySurveyProbe ? c.target!=0 : c.target==0))
            return false;
        snapshot_version_=std::max(snapshot_version_,65);
    }
    if(c.action==Action::ConfigureExplorationScanner){
        if(!c.id || !c.actor || c.target || (c.amount!=1 && c.amount!=-1) ||
           c.thrust_x || c.thrust_y || c.transport_id ||
           !c.passenger_ids.empty() || c.secondary_receiver || c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,64);
    }
    // v100: desired state of a fitted hardpoint: amount=slot*2+enabled.
    if(c.action==Action::SetHardpointPower){
        if(!c.id || !c.actor || c.target || c.amount<0 || c.amount>3 ||
           c.thrust_x || c.thrust_y || c.transport_id ||
           !c.passenger_ids.empty() || c.secondary_receiver ||
           c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,100);
    }
    if(c.action==Action::ConfigureCyberModule ||
       c.action==Action::SetCyberIsolation ||
       c.action==Action::HackConfuse){
        if(!c.id || !c.actor || c.thrust_x || c.thrust_y ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport)return false;
        if(c.action==Action::ConfigureCyberModule &&
           (c.target || (c.amount!=3 && c.amount!=-3 &&
                         c.amount!=4 && c.amount!=-4 &&
                         c.amount!=5 && c.amount!=-5 &&
                         c.amount!=6 && c.amount!=-6)))return false;
        if(c.action==Action::SetCyberIsolation &&
           (c.target || c.amount<0 || c.amount>3))return false;
        if(c.action==Action::HackConfuse &&
           (!c.target || c.target==c.actor || c.amount ||
            c.id>=(std::uint64_t{1}<<62)))return false;
        snapshot_version_=std::max(snapshot_version_,
            c.action==Action::ConfigureCyberModule &&
            (c.amount==5 || c.amount==-5 ||
             c.amount==6 || c.amount==-6)?103:101);
    }
    if(c.action==Action::SurveyLocal){
        if(!c.id || !c.actor || c.target || c.amount || c.thrust_x || c.thrust_y ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,63);
    }
    if(c.action==Action::SalvageWreck){
        if(!c.id || !c.actor || !c.target || c.actor==c.target ||
           c.amount<1 || c.amount>10 || c.thrust_x || c.thrust_y ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,62);
    }
    if(c.action==Action::FireTrainingWeapon ||
       c.action==Action::FireShipWeapon){
        if(!c.id||!c.actor||!c.target||c.actor==c.target||
           c.thrust_x||c.thrust_y||c.amount||c.transport_id||
           !c.passenger_ids.empty()||c.secondary_receiver||c.secondary_transport)
            return false;
        snapshot_version_=std::max(snapshot_version_,
            c.action==Action::FireShipWeapon?104:62);
    }
    const bool register_child=c.action==Action::HostRegisterDependent;
    if(register_child){
        if(c.issuer!=kServerAuthority || snapshot_version_<57 ||
           !c.id || !c.actor || c.target!=c.actor || !colony_ships_.count(c.actor) ||
           !c.transport_id || c.transport_id<(1ULL<<62) ||
           c.passenger_ids.size()!=1 || c.amount || c.thrust_x || c.thrust_y ||
           c.secondary_receiver || c.secondary_transport ||
           physical_dependents_.count(c.transport_id) ||
           npcs_.count(c.transport_id) || casualties_.count(c.transport_id) ||
           !npcs_.count(c.passenger_ids.front()))return false;
        const auto& guardian=npcs_.at(c.passenger_ids.front());
        if(guardian.home_ship!=c.actor || guardian.location!=c.actor ||
           guardian.transport_id || colony_departures_.count(c.actor))return false;
        for(const auto& [tick,cmds]:commands_){
            (void)tick;
            for(const auto& pending:cmds)if(
                (pending.action==Action::HostRegisterDependent &&
                 pending.transport_id==c.transport_id) ||
                pending.action==Action::HostColonyDeparture)return false;
        }
        snapshot_version_=std::max(snapshot_version_,59); // never drop persisted yaw v61
    }
    const bool colony_departure=c.action==Action::HostColonyDeparture;
    if(colony_departure){
        if(!can_depart_colony(c)||submitted_ids_.count(c.id))return false;
        snapshot_version_=std::max(snapshot_version_,
            children_physically_aboard(c.actor)?60:58);
    }
    const bool split_action=c.action==Action::HostSplitEvacuatePassengers;
    const bool consigned=c.action==Action::HostTransferConsignedStock;
    if(consigned){
        if(c.issuer!=kServerAuthority||!c.id||c.execute_tick!=tick_+1||
           submitted_ids_.count(c.id)||!c.actor||!c.target||
           c.amount<1||c.amount>1000||(c.thrust_x!=1&&c.thrust_x!=2)||
           (c.thrust_y!=0&&c.thrust_y!=1)||c.transport_id||
           !c.passenger_ids.empty()||c.secondary_receiver||c.secondary_transport)return false;
        snapshot_version_=std::max(snapshot_version_,56);
    }
    const bool return_action=c.action==Action::HostReturnConstructedStock;
    const bool escrow_action=c.action==Action::HostEscrowConstructedStock;
    if(return_action){
        if(c.issuer!=kServerAuthority||!c.id||c.execute_tick!=tick_+1||
           submitted_ids_.count(c.id)||!c.actor||!c.target||c.amount<=0||c.amount>4000||
           (c.thrust_x!=1&&c.thrust_x!=2)||c.thrust_y||c.transport_id||
           !c.passenger_ids.empty()||c.secondary_receiver||c.secondary_transport)return false;
        const auto st=storages_.find(c.target);
        if(st==storages_.end()||st->second.source_ship!=c.actor||
           st->second.network_escrow_kg<c.amount)return false;
        const std::string code=c.thrust_x==1?"aluminum_panels":"aluminum_frames";
        auto bin=storage_inventory_snapshot(c.target);
        if(bin.free_liters()<c.amount||bin.amount("good/"+code)>4000-c.amount)return false;
        snapshot_version_=std::max(snapshot_version_,53);
    }
    if(escrow_action){
        if(c.issuer!=kServerAuthority||!c.id||c.execute_tick!=tick_+1||
           submitted_ids_.count(c.id)||!c.actor||!c.target||
           c.thrust_x!=0||c.thrust_y!=0||c.amount<=0||c.amount>4000||
           c.transport_id||!c.passenger_ids.empty()||
           c.secondary_receiver||c.secondary_transport)return false;
        const auto st=storages_.find(c.target);
        if(st==storages_.end()||st->second.source_ship!=c.actor||
           st->second.goods_kg.count("aluminum")==0||
           st->second.goods_kg.at("aluminum")<c.amount)return false;
        snapshot_version_=std::max(snapshot_version_,52);
    }
    if(!split_action&&(c.secondary_receiver||c.secondary_transport))return false;
    if(split_action){
        if(c.issuer!=kServerAuthority||!c.id||c.execute_tick<=tick_||
           submitted_ids_.count(c.id)||!c.actor||!c.target||!c.secondary_receiver||
           c.actor==c.target||c.actor==c.secondary_receiver||
           c.target==c.secondary_receiver||c.thrust_x<=0||c.thrust_y<0||
           c.amount<=0||!c.secondary_transport||!c.transport_id||
           static_cast<std::size_t>(c.thrust_x)>=c.passenger_ids.size()||
           c.passenger_ids.size()>16||snapshot_version_<33)return false;
        const auto primary=static_cast<std::uint64_t>(c.amount);
        if(primary==c.secondary_transport||primary==c.transport_id||
           c.secondary_transport==c.transport_id||voyages_.count(primary)||
           voyages_.count(c.secondary_transport))return false;
        const auto original=voyages_.find(c.transport_id);
        if(original==voyages_.end()||original->second.ferry!=c.actor||
           original->second.status!=3)return false;
        const auto split=c.passenger_ids.begin()+c.thrust_x;
        if(!std::is_sorted(c.passenger_ids.begin(),split)||
           !std::is_sorted(split,c.passenger_ids.end()))return false;
        auto everybody=c.passenger_ids;std::sort(everybody.begin(),everybody.end());
        if(everybody!=original->second.passengers||
           std::adjacent_find(everybody.begin(),everybody.end())!=everybody.end())return false;
        const auto* source=find(c.actor);const auto* first=find(c.target);
        const auto* second=find(c.secondary_receiver);
        if(!source||!first||!second||source->type!=EntityType::Ship||
           first->type!=EntityType::Ship||second->type!=EntityType::Ship||
           !colony_ships_.count(c.actor)||!colony_ships_.count(c.target)||
           !colony_ships_.count(c.secondary_receiver)||
           source->dock_state!=DockState::InSpace||
           source->jump_state!=ShipJumpState::Ready||
           first->dock_state!=DockState::InSpace||
           second->dock_state!=DockState::InSpace||
           first->jump_state!=ShipJumpState::Ready||
           second->jump_state!=ShipJumpState::Ready||
           source->system_id!=first->system_id||source->system_id!=second->system_id)
            return false;
        for(const auto receiver:{first,second}){
            const auto dx=source->position.x-receiver->position.x;
            const auto dy=source->position.y-receiver->position.y;
            if(dx<-2500||dx>2500||dy<-2500||dy>2500||dx*dx+dy*dy>2500*2500)
                return false;
        }
        for(const auto& [id,n]:npcs_){(void)id;
            if((n.location==c.target||n.location==c.secondary_receiver)&&n.transport_id)
                return false;
        }
        for(const auto adult:c.passenger_ids){
            const auto found=npcs_.find(adult);
            if(found==npcs_.end()||found->second.location!=c.actor||
               found->second.transport_id!=c.transport_id)return false;
        }
        if(c.thrust_y){
            std::size_t crew_count=0;
            for(const auto& [id,n]:npcs_){(void)id;
                if(n.home_ship==c.actor && n.role==NpcRole::Ship &&
                   n.assigned_site==c.actor){
                    // Split rescue cannot strand an absent, dead, or already
                    // transported operating crew member.
                    if(n.location!=c.actor||n.transport_id)return false;
                    ++crew_count;
                }
            }
            if(!crew_count||crew_count!=static_cast<std::size_t>(colony_ships_.at(c.actor).crew)||
               c.thrust_y>=static_cast<int>(crew_count)||
               c.passenger_ids.size()+crew_count>30)return false;
        }
        for(const auto& [when,commands]:commands_){(void)when;
            for(const auto& pending:commands)if(
                pending.transport_id==c.transport_id||
                pending.transport_id==primary||pending.transport_id==c.secondary_transport||
                pending.actor==c.actor||pending.target==c.actor||
                pending.actor==c.target||pending.target==c.target||
                pending.actor==c.secondary_receiver||
                pending.target==c.secondary_receiver)return false;
        }
        snapshot_version_=std::max(snapshot_version_,c.thrust_y?49:48);
        submitted_ids_.insert(c.id);commands_[c.execute_tick].push_back(c);
        return true;
    }
    const bool medical_action=c.action==Action::HostTrainMedic ||
        c.action==Action::HostTriageRescued || c.action==Action::HostTreatInjuries ||
        c.action==Action::HostScheduleMedicShift || c.action==Action::HostLoadMedicalKit ||
        c.action==Action::HostMobileStabilize;
    if(medical_action){
        if(c.issuer!=kServerAuthority || !c.actor || !c.target ||
           c.thrust_x || c.thrust_y || snapshot_version_<32)return false;
        const auto ordered=[](const std::vector<std::uint64_t>& ids){
            return !ids.empty() && ids.size()<=16 && ids.front()!=0 &&
                   std::is_sorted(ids.begin(),ids.end()) &&
                   std::adjacent_find(ids.begin(),ids.end())==ids.end();
        };
        if(c.action==Action::HostTrainMedic){
            if(c.transport_id || !c.passenger_ids.empty() || c.amount<1 || c.amount>3 ||
               !clinics_.count(c.actor) || !npcs_.count(c.target))return false;
            const auto& n=npcs_.at(c.target);
            const bool resident=n.role==NpcRole::Habitat && n.location==c.actor &&
                n.assigned_site==c.actor && habitats_.count(c.actor);
            const bool ship_medic=snapshot_version_>=39 && n.role==NpcRole::Ship && n.location==n.home_ship &&
                n.assigned_site==n.home_ship && entities_.count(n.location) &&
                entities_.count(c.actor) && entities_.at(n.location).type==EntityType::Ship &&
                entities_.at(n.location).owner_id==entities_.at(c.actor).owner_id &&
                entities_.at(n.location).system_id==entities_.at(c.actor).system_id;
            if((!resident && !ship_medic) || clinics_.at(c.actor).medics<1)return false;
        } else if(c.action==Action::HostTriageRescued){
            if(c.actor!=c.target || c.amount<1 || c.amount>3 || !c.transport_id ||
               !ordered(c.passenger_ids) || !voyages_.count(c.transport_id) ||
               voyages_.at(c.transport_id).ferry!=c.actor ||
               voyages_.at(c.transport_id).status!=1)return false;
            // Triage only after a documented evacuation or mixed incident.
            bool rescue=false;
            for(const auto& [id,v]:voyages_){(void)id;
                if((v.status==5 || v.status==6) && v.rescue_transport_id==c.transport_id)
                    rescue=true;
            }
            if(!rescue)return false;
            for(const auto id:c.passenger_ids){
                const auto it=npcs_.find(id);
                if(it==npcs_.end() || it->second.location!=c.actor ||
                   it->second.transport_id!=c.transport_id ||
                   (medical_cases_.count(id) &&
                    medical_cases_.at(id).recovered_tick==0 &&
                    medical_cases_.at(id).treatment_command==0))return false;
            }
        } else if(c.action==Action::HostScheduleMedicShift){
            if(c.transport_id || !c.passenger_ids.empty() || c.amount<1 || c.amount>10000 ||
               !clinics_.count(c.actor) || !npcs_.count(c.target) ||
               !named_medics_.count(c.target) || named_medics_.at(c.target).units<3 ||
               named_medics_.at(c.target).clinic_site!=c.actor)return false;
        } else if(c.action==Action::HostLoadMedicalKit){
            if(c.transport_id || !c.passenger_ids.empty() || c.amount<1 || c.amount>100 ||
               !clinics_.count(c.target) || !colony_ships_.count(c.actor))return false;
        } else if(c.action==Action::HostMobileStabilize){
            if(c.transport_id!=c.target || c.amount || !ordered(c.passenger_ids) ||
               !colony_ships_.count(c.actor) || !npcs_.count(c.target) ||
               !named_medics_.count(c.target) || named_medics_.at(c.target).units<3 ||
               npcs_.at(c.target).location!=c.actor ||
               !mobile_medical_kits_.count(c.actor))return false;
            for(const auto id:c.passenger_ids){
                const auto patient=medical_cases_.find(id);const auto n=npcs_.find(id);
                if(patient==medical_cases_.end() || n==npcs_.end() ||
                   n->second.location!=c.actor || patient->second.treatment_command ||
                   patient->second.stabilization_command)return false;
            }
        } else {
            if(!c.transport_id || c.amount || !ordered(c.passenger_ids) ||
               !clinics_.count(c.target) || !npcs_.count(c.transport_id))return false;
            const auto& n=npcs_.at(c.transport_id);
            const auto qualification=named_medics_.find(c.transport_id);
            if(n.location!=c.target || n.assigned_site!=c.target ||
               n.role!=NpcRole::Habitat || qualification==named_medics_.end() ||
               qualification->second.units<3 || clinics_.at(c.target).medics<1 ||
               c.passenger_ids.size()>static_cast<std::size_t>(clinics_.at(c.target).medics))return false;
            std::size_t qualified=0;
            for(const auto& [id,q]:named_medics_){
                if(q.units>=3 && npcs_.count(id) &&
                   npcs_.at(id).location==c.target &&
                   npcs_.at(id).role==NpcRole::Habitat)++qualified;
            }
            if(c.passenger_ids.size()>qualified)return false;
            for(auto id:c.passenger_ids){
                auto it=medical_cases_.find(id);
                if(it==medical_cases_.end() || it->second.treatment_command ||
                   !npcs_.count(id) || npcs_.at(id).location!=c.actor)return false;
            }
        }
        snapshot_version_=std::max(snapshot_version_,
            c.action==Action::HostScheduleMedicShift || c.action==Action::HostLoadMedicalKit ||
            c.action==Action::HostMobileStabilize ||
            (c.action==Action::HostTriageRescued && !c.passenger_ids.empty() &&
             medical_cases_.count(c.passenger_ids.front())) ? 39 : 38);
    }
    // DEMOG-05q6d: host-only residence settlement is distinct from physical
    // travel. A person stays aboard until disembarkation is committed; only a
    // completed voyage may change their administrative home.
    const bool migration_action=c.action==Action::HostSettleMigrant;
    if(migration_action){
        if(c.issuer!=kServerAuthority || c.actor==c.target || !c.actor || !c.target ||
           !c.transport_id || (c.passenger_ids.empty() || c.passenger_ids.size()>2 ||
            c.passenger_ids[0]==0 ||
            (c.passenger_ids.size()==2 && (c.passenger_ids[1]<=c.passenger_ids[0] ||
             snapshot_version_<40))) ||
           c.amount || c.thrust_x || c.thrust_y || snapshot_version_<40)return false;
        const auto id=c.passenger_ids[0];
        const auto n=npcs_.find(id);
        const auto v=voyages_.find(c.transport_id);
        const auto arrival=npc_transport_arrival(c.transport_id,id);
        if(n==npcs_.end() || v==voyages_.end() || v->second.status!=2 ||
           (!arrival || arrival->mode()!=transport::Mode::Ferry ||
            arrival->source().id!=c.actor ||
            arrival->destination().id!=c.target ||
            !transport::completed_transport(*arrival,id)) ||
           n->second.home_ship!=c.actor || n->second.location!=c.target ||
           n->second.assigned_site!=c.actor || n->second.transport_id ||
           n->second.role!=NpcRole::Ship || n->second.mentor_id ||
           crew_rehomes_.count(id) || named_medics_.count(id) ||
           !colony_ships_.count(c.actor) || !colony_ships_.count(c.target) ||
           colony_ships_.at(c.actor).crew<=1 || colony_ships_.at(c.target).crew>=1000 ||
           !entities_.count(c.actor) || !entities_.count(c.target) ||
           entities_.at(c.actor).type!=EntityType::Ship ||
           entities_.at(c.target).type!=EntityType::Ship ||
           entities_.at(c.target).jump_state!=ShipJumpState::Ready)return false;
        if(c.passenger_ids.size()==2){
            const auto co=npcs_.find(c.passenger_ids[1]);
            if(co==npcs_.end() || co->second.home_ship!=c.actor ||
               co->second.location!=c.target || co->second.assigned_site!=c.actor ||
               co->second.transport_id || co->second.role!=NpcRole::Ship ||
               co->second.mentor_id || crew_rehomes_.count(co->first) ||
               named_medics_.count(co->first) ||
               !std::binary_search(v->second.passengers.begin(),
                   v->second.passengers.end(),co->first) ||
               colony_ships_.at(c.actor).crew<=2 ||
               colony_ships_.at(c.target).crew>998)return false;
        }
        for(const auto& [tick,batch]:commands_){(void)tick;
            for(const auto& queued:batch)
                if(queued.action==Action::HostSettleMigrant &&
                   std::any_of(queued.passenger_ids.begin(),queued.passenger_ids.end(),
                       [&](auto p){return std::binary_search(c.passenger_ids.begin(),
                           c.passenger_ids.end(),p);}))return false;
        }
    }
    const bool passenger_action=c.action==Action::HostEmbarkPassengers ||
        c.action==Action::HostDisembarkPassengers || c.action==Action::HostStrandFerry ||
        c.action==Action::HostResumeFerry || c.action==Action::HostEvacuatePassengers ||
        c.action==Action::HostDestroyPassengerFerry ||
        c.action==Action::HostResolveMixedFerryLoss;
    const bool incident_action=c.action==Action::HostStrandFerry ||
        c.action==Action::HostResumeFerry || c.action==Action::HostEvacuatePassengers ||
        c.action==Action::HostDestroyPassengerFerry ||
        c.action==Action::HostResolveMixedFerryLoss;
    if(passenger_action){
        if(c.issuer!=kServerAuthority || !c.actor || !c.target || !c.transport_id ||
           c.passenger_ids.empty() || c.passenger_ids.size()>10000 ||
           c.thrust_x || c.thrust_y || snapshot_version_<32)return false;
        if(incident_action && snapshot_version_<34)return false;
        if(c.action==Action::HostStrandFerry){
            if(c.actor!=c.target || c.amount<1 || c.amount>5)return false;
        }else if(c.action==Action::HostResumeFerry){
            if(c.actor!=c.target || c.amount)return false;
        }else if(c.action==Action::HostDestroyPassengerFerry){
            if(c.actor!=c.target || c.amount)return false;
        }else if(c.action==Action::HostResolveMixedFerryLoss){
            if(c.actor==c.target || c.amount<=0 ||
               static_cast<std::uint64_t>(c.amount)>=c.passenger_ids.size() ||
               c.id==c.transport_id || voyages_.count(c.id))return false;
            // The explicit survivor and fatality groups have independent sorted
            // order. No inference from a random/untrusted partition is allowed.
            const auto split=c.passenger_ids.begin()+c.amount;
            if(!std::is_sorted(c.passenger_ids.begin(),split) ||
               !std::is_sorted(split,c.passenger_ids.end()))return false;
        }else if(c.action==Action::HostEvacuatePassengers){
            if(c.actor==c.target || c.amount<=0 || voyages_.count(static_cast<std::uint64_t>(c.amount)))return false;
        }else if(c.actor==c.target || c.amount)return false;
        std::set<std::uint64_t> unique;
        for(auto id:c.passenger_ids)if(!id || !unique.insert(id).second)return false;
        if(c.action==Action::HostEmbarkPassengers && voyages_.count(c.transport_id))return false;
        auto voyage=voyages_.find(c.transport_id);
        if(c.action!=Action::HostEmbarkPassengers){
            if(voyage==voyages_.end() || voyage->second.ferry!=c.actor)return false;
            auto ids=c.passenger_ids;std::sort(ids.begin(),ids.end());
            if(c.action!=Action::HostDestroyPassengerFerry && c.action!=Action::HostResolveMixedFerryLoss && ids!=voyage->second.passengers)return false;
            if((c.action==Action::HostDestroyPassengerFerry || c.action==Action::HostResolveMixedFerryLoss) &&
               (voyage->second.status!=1 && voyage->second.status!=3))return false;
            if(c.action==Action::HostStrandFerry && voyage->second.status!=1)return false;
            if(c.action==Action::HostResumeFerry && voyage->second.status!=3)return false;
            if(c.action==Action::HostDisembarkPassengers && voyage->second.status!=1)return false;
            if(c.action==Action::HostEvacuatePassengers &&
               (voyage->second.status!=1 && voyage->second.status!=3))return false;
        }
        // No concurrent mutations of one voyage; reserve custody and rescue berth
        // occupancy against all pending host manifest operations.
        for(const auto& entry:commands_)for(const auto& queued:entry.second){
            if(queued.action!=Action::HostEmbarkPassengers && queued.action!=Action::HostDisembarkPassengers &&
               queued.action!=Action::HostStrandFerry && queued.action!=Action::HostResumeFerry &&
               queued.action!=Action::HostEvacuatePassengers &&
               queued.action!=Action::HostDestroyPassengerFerry &&
               queued.action!=Action::HostResolveMixedFerryLoss)continue;
            // Rescue and destruction may compete on the same tick. Their canonical
            // sequence determines the winner; the loser must revalidate and fail.
            const bool competitive=(c.action==Action::HostDestroyPassengerFerry &&
                queued.action==Action::HostEvacuatePassengers) ||
                (c.action==Action::HostEvacuatePassengers &&
                queued.action==Action::HostDestroyPassengerFerry);
            if(!competitive){
                if(queued.transport_id==c.transport_id ||
                   (c.action==Action::HostEvacuatePassengers &&
                    queued.action==Action::HostEvacuatePassengers && queued.amount==c.amount) ||
                   (c.action==Action::HostResolveMixedFerryLoss &&
                    ((queued.action==Action::HostEvacuatePassengers &&
                      queued.amount>0 && static_cast<std::uint64_t>(queued.amount)==c.id) ||
                     (queued.action==Action::HostEmbarkPassengers &&
                      queued.transport_id==c.id))) ||
                   (queued.action==Action::HostResolveMixedFerryLoss &&
                    ((c.action==Action::HostEvacuatePassengers && c.amount>0 &&
                      static_cast<std::uint64_t>(c.amount)==queued.id) ||
                     (c.action==Action::HostEmbarkPassengers && c.transport_id==queued.id))))return false;
                for(auto id:c.passenger_ids)
                    if(std::find(queued.passenger_ids.begin(),queued.passenger_ids.end(),id)!=queued.passenger_ids.end())return false;
            }
        }
        const auto ship=entities_.find(c.actor),endpoint=entities_.find(c.target);
        if(ship==entities_.end() || endpoint==entities_.end() ||
           ship->second.type!=EntityType::Ship || endpoint->second.type!=EntityType::Ship ||
           !colony_ships_.count(c.actor) || !colony_ships_.count(c.target) ||
           ship->second.system_id!=endpoint->second.system_id)return false;
        if(incident_action && (ship->second.jump_state!=ShipJumpState::Ready ||
           ship->second.dock_state!=DockState::InSpace))return false;
        if(c.action==Action::HostEvacuatePassengers || c.action==Action::HostResolveMixedFerryLoss){
            const auto dx=ship->second.position.x-endpoint->second.position.x;
            const auto dy=ship->second.position.y-endpoint->second.position.y;
            if(dx<-kDockingRangeMm || dx>kDockingRangeMm ||
               dy<-kDockingRangeMm || dy>kDockingRangeMm ||
               dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm)return false;
        }
        if(c.action==Action::HostEmbarkPassengers || c.action==Action::HostEvacuatePassengers ||
           c.action==Action::HostResolveMixedFerryLoss){
            const EntityId receiving=c.action==Action::HostEmbarkPassengers?c.actor:c.target;
            constexpr std::size_t kPassengerBerths=16;
            std::size_t aboard=0;
            for(const auto& [id,n]:npcs_){(void)id;
                if(n.location==receiving && n.transport_id)++aboard;
            }
            for(const auto& [t,batch]:commands_){(void)t;for(const auto& queued:batch){
                if((queued.action==Action::HostEmbarkPassengers && queued.actor==receiving) ||
                   (queued.action==Action::HostEvacuatePassengers && queued.target==receiving))
                    aboard+=queued.passenger_ids.size();
                if(queued.action==Action::HostResolveMixedFerryLoss && queued.target==receiving)
                    aboard+=static_cast<std::size_t>(queued.amount);
            }}
            const auto seats=c.action==Action::HostResolveMixedFerryLoss ?
                static_cast<std::size_t>(c.amount) : c.passenger_ids.size();
            if(aboard>kPassengerBerths || seats>kPassengerBerths-aboard)return false;
        }
        if(c.action==Action::HostDestroyPassengerFerry || c.action==Action::HostResolveMixedFerryLoss){
            // All people physically aboard (including native crew) must be declared.
            // No away-assigned workforce, secondary active voyage or construction
            // dependencies may become orphaned by destroying this ship.
            std::vector<std::uint64_t> aboard;
            for(const auto& [id,n]:npcs_){
                if(n.location==c.actor)aboard.push_back(id);
                if(n.home_ship==c.actor && (n.location!=c.actor ||
                    n.assigned_site!=c.actor || n.role!=NpcRole::Ship))return false;
            }
            std::map<EntityId,std::int64_t> losses;
            const auto fatal_start=c.action==Action::HostResolveMixedFerryLoss ?
                c.passenger_ids.begin()+c.amount : c.passenger_ids.begin();
            for(auto it_id=fatal_start;it_id!=c.passenger_ids.end();++it_id){
                const auto it=npcs_.find(*it_id);
                if(it==npcs_.end() || it->second.role!=NpcRole::Ship ||
                   it->second.assigned_site!=it->second.home_ship)return false;
                ++losses[it->second.home_ship];
            }
            if(c.action==Action::HostResolveMixedFerryLoss){
                for(auto it_id=c.passenger_ids.begin();it_id!=fatal_start;++it_id){
                    const auto it=npcs_.find(*it_id);
                    if(it==npcs_.end() ||
                       (it->second.home_ship==c.actor ? it->second.transport_id!=0 :
                        it->second.transport_id!=c.transport_id) ||
                       it->second.role!=NpcRole::Ship ||
                       it->second.assigned_site!=it->second.home_ship)return false;
                }
            }
            for(const auto& [home,number]:losses){
                auto shipcrew=colony_ships_.find(home);
                if(shipcrew==colony_ships_.end() || shipcrew->second.crew<number ||
                   (home!=c.actor && shipcrew->second.crew<=number))return false;
            }
            std::int64_t rescued_native=0;
            if(c.action==Action::HostResolveMixedFerryLoss)
                for(auto it_id=c.passenger_ids.begin();it_id!=fatal_start;++it_id)
                    if(npcs_.at(*it_id).home_ship==c.actor)++rescued_native;
            if(losses[c.actor]+rescued_native!=colony_ships_.at(c.actor).crew)return false;
            if(rescued_native && (colony_ships_.at(c.target).crew>1000-rescued_native ||
                colony_ships_.at(c.target).crew<1))return false;
            if(rescued_native>0)snapshot_version_=std::max(snapshot_version_,38);
            auto ids=c.passenger_ids;std::sort(ids.begin(),ids.end());
            if(aboard!=ids || !std::includes(ids.begin(),ids.end(),
                voyage->second.passengers.begin(),voyage->second.passengers.end()))return false;
            for(const auto& [id,v]:voyages_)
                if(id!=c.transport_id && v.ferry==c.actor && (v.status==1 || v.status==3))return false;
            for(const auto& [id,site]:built_outposts_){(void)id;
                if(site.source_colony_ship==c.actor)return false;
            }
        }
        for(auto id:c.passenger_ids){
            const auto it=npcs_.find(id);
            if(it==npcs_.end())return false;
            if(c.action==Action::HostDestroyPassengerFerry || c.action==Action::HostResolveMixedFerryLoss){
                if(it->second.location!=c.actor)return false;
            }else if(c.action==Action::HostEmbarkPassengers){
                if(it->second.location!=c.target || it->second.transport_id)return false;
            }else if(it->second.location!=c.actor || it->second.transport_id!=c.transport_id)return false;
        }
    }else if(!medical_action && !migration_action && !colony_departure && !register_child && (c.transport_id || !c.passenger_ids.empty()))return false;
    if (c.id == 0 || c.execute_tick <= tick_ || c.issuer == 0 || submitted_ids_.count(c.id)) return false;
    if (passenger_action || medical_action || migration_action || escrow_action || return_action || consigned || colony_departure || register_child) { /* validated host command above */ }
    else if (c.action == Action::HostDestroyShip || c.action == Action::HostCrewLoss) {
        if(c.issuer != kServerAuthority || !c.actor || !c.target ||
           c.thrust_x || c.thrust_y ||
           (c.action==Action::HostDestroyShip && (c.actor==c.target || c.amount!=0)) ||
           (c.action==Action::HostCrewLoss && (c.actor!=c.target || c.amount<1 || c.amount>1000)))return false;
    } else if (c.action == Action::SetGatePairOnline ||
        c.action == Action::SetGateStaff || c.action == Action::DeliverGateSpares) {
        if (c.issuer != kServerAuthority || c.actor == 0 || c.target == 0 ||
            c.actor == c.target || c.thrust_x != 0 || c.thrust_y != 0 ||
            (c.action==Action::SetGatePairOnline && (c.amount<0 || c.amount>1)) ||
            (c.action==Action::SetGateStaff && (c.amount<0 || c.amount>16)) ||
            (c.action==Action::DeliverGateSpares && (c.amount<1 || c.amount>10000))) return false;
    } else if (c.issuer == kServerAuthority) return false;
    if(c.action==Action::RefineOre || c.action==Action::FabricateModule ||
       c.action==Action::ConstructOutpost || c.action==Action::RefineMineral ||
       c.action==Action::FabricateMaterialModule) {
        if(c.thrust_x!=0 || c.thrust_y!=0 || c.amount<1 ||
           (c.action==Action::RefineMineral ? c.amount>100000 : c.amount>1000) ||
           (c.action==Action::ConstructOutpost && c.amount!=1) ||
           (c.action==Action::RefineMineral ? mineral_name(c.target)==nullptr ||
                c.target==9 : c.action==Action::FabricateMaterialModule ?
                (c.target!=1 && c.target!=2) : c.target!=0)) return false;
    }
    // WO2-H: real NPC training is an explicit owner command on an installed
    // original workshop, never a profession-catalogue license or worker spawn.
    // c.amount = named trainee ID; thrust_x=1 cut or 2 shape.
    if(c.action==Action::TrainWorkshopTrade) {
        if(!c.id || !c.actor || !c.target || c.amount<1 ||
           c.amount>1000000 || (c.thrust_x!=1 && c.thrust_x!=2) ||
           c.thrust_y || c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport ||
           !colony_ships_.count(c.actor) || !workshops_.count(c.target))
            return false;
        const auto* owned_ship=find(c.actor);
        const auto* built_site=find(c.target);
        if(!owned_ship || owned_ship->type!=EntityType::Ship ||
           !built_site || built_site->type!=EntityType::Station ||
           !c.issuer || c.issuer==kServerAuthority ||
           owned_ship->owner_id!=c.issuer ||
           built_site->owner_id!=c.issuer ||
           workshops_.at(c.target).source_ship!=c.actor ||
           !built_outposts_.count(c.target) ||
           built_outposts_.at(c.target).source_colony_ship!=c.actor)
            return false;
    }
    // WO2-I: shift commands are normal owner-authenticated original-world
    // commands. Scheduling uses the original due tick as the shift start and
    // thrust_y as duration; no transport, manifest or material side channel.
    if(c.action==Action::StartWorkshopShift || c.action==Action::CancelWorkshopShift){
        if(!c.actor || !c.target || (c.thrust_x!=1 && c.thrust_x!=2) ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport ||
           (c.action==Action::StartWorkshopShift ?
             (c.amount<1 || c.amount>1000000 || c.thrust_y<1 ||
              c.thrust_y>10000 || c.execute_tick>std::numeric_limits<std::uint64_t>::max()-
                  static_cast<std::uint64_t>(c.thrust_y) ||
              c.execute_tick-tick_>10000) :
             (c.amount<1 || c.thrust_y!=0)) ||
           !colony_ships_.count(c.actor) || !workshops_.count(c.target))
            return false;
        const auto* ship=find(c.actor);
        const auto* site=find(c.target);
        if(!ship || !site || ship->type!=EntityType::Ship ||
           site->type!=EntityType::Station ||
           ship->owner_id!=c.issuer || site->owner_id!=c.issuer ||
           workshops_.at(c.target).source_ship!=c.actor ||
           !built_outposts_.count(c.target) ||
           built_outposts_.at(c.target).source_colony_ship!=c.actor)
            return false;
        if(c.action==Action::StartWorkshopShift){
            const auto end=c.execute_tick+static_cast<std::uint64_t>(c.thrust_y);
            const auto& shop=workshops_.at(c.target);
            const auto& existing=shop.shifts[static_cast<std::size_t>(c.thrust_x-1)];
            if((c.thrust_x==2 && !shop.finisher_installed) ||
               (existing.command_id && c.execute_tick<existing.end_tick &&
                existing.start_tick<end))
                return false;
            for(const auto& [other_site,other]:workshops_){
                (void)other_site;
                for(unsigned slot=0;slot<2;++slot){
                    const auto& sh=other.shifts[slot];
                    if(sh.command_id && sh.worker==static_cast<std::uint64_t>(c.amount) &&
                       c.execute_tick<sh.end_tick && sh.start_tick<end)return false;
                }
            }
            for(const auto& [due,orders]:commands_){
                for(const auto& prior:orders){
                    if(prior.action!=Action::StartWorkshopShift)continue;
                    const auto prior_shop=workshops_.find(prior.target);
                    if(prior_shop!=workshops_.end() &&
                       prior_shop->second.cancelled_pending_shifts.count(prior.id))
                        continue;
                    const auto prior_end=due+static_cast<std::uint64_t>(prior.thrust_y);
                    if(c.execute_tick<prior_end && due<end &&
                       ((prior.target==c.target && prior.thrust_x==c.thrust_x) ||
                        prior.amount==c.amount))return false;
                }
            }
        }
        snapshot_version_=std::max(snapshot_version_,106);
    }
    // WO2-I: atomic relief holds the original finite shift ID and end tick,
    // changing only its named worker in one original, approved command.
    // amount=original shift command ID; thrust_y=replacement NPC; x=slot.
    if(c.action==Action::RelieveWorkshopShift){
        if(!c.actor || !c.target || !c.id ||
           (c.thrust_x!=1 && c.thrust_x!=2) ||
           c.amount<1 || c.thrust_y<1 || c.thrust_y>1000000 ||
           c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport ||
           tick_==std::numeric_limits<std::uint64_t>::max() ||
           c.execute_tick!=tick_+1)
            return false;
        const auto* ship=find(c.actor);
        const auto* site=find(c.target);
        const auto shop=workshops_.find(c.target);
        if(!ship || !site || ship->type!=EntityType::Ship ||
           site->type!=EntityType::Station || ship->owner_id!=c.issuer ||
           site->owner_id!=c.issuer || shop==workshops_.end() ||
           shop->second.source_ship!=c.actor ||
           !colony_ships_.count(c.actor) ||
           !built_outposts_.count(c.target) ||
           built_outposts_.at(c.target).source_colony_ship!=c.actor)
            return false;
        const auto& shift=shop->second.shifts[static_cast<std::size_t>(c.thrust_x-1)];
        if(!shift.in_effect(c.execute_tick) ||
           shift.command_id!=static_cast<std::uint64_t>(c.amount) ||
           shift.worker==static_cast<std::uint64_t>(c.thrust_y))
            return false;
        // A second already-queued handover for this slot at the same tick
        // must not be able to undo/replay an earlier owner approval.
        const auto pending=commands_.find(c.execute_tick);
        if(pending!=commands_.end())
            for(const auto& prior:pending->second)
                if(prior.action==Action::RelieveWorkshopShift &&
                   prior.target==c.target && prior.thrust_x==c.thrust_x)
                    return false;
        snapshot_version_=std::max(snapshot_version_,107);
    }
    // WO2-J: the authenticated colony owner alone admits management policy
    // commands. A certified manager never gains owner-command authority.
    if(c.action==Action::AppointWorkshopSupervisor ||
       c.action==Action::MentorWorkshopTrade ||
       c.action==Action::ApproveWorkshopJob ||
       c.action==Action::CancelWorkshopJob){
        if(!c.id || !c.actor || !c.target || c.thrust_x<1 || c.thrust_x>2 ||
           c.thrust_y || c.transport_id || !c.passenger_ids.empty() ||
           c.secondary_receiver || c.secondary_transport ||
           c.issuer==kServerAuthority || !colony_ships_.count(c.actor) ||
           !workshops_.count(c.target) ||
           (c.action==Action::AppointWorkshopSupervisor ?
               (c.amount<0 || c.amount>1000000) :
            c.action==Action::MentorWorkshopTrade ?
               (c.amount<1 || c.amount>1000000) :
            c.action==Action::ApproveWorkshopJob ?
               (c.amount<1 || c.amount>50 ||
                c.id>static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max())) :
               (c.amount<1)))
            return false;
        const auto* ship=find(c.actor);
        const auto* site=find(c.target);
        if(!ship || !site || ship->type!=EntityType::Ship ||
           site->type!=EntityType::Station || !c.issuer ||
           ship->owner_id!=c.issuer || site->owner_id!=c.issuer ||
           workshops_.at(c.target).source_ship!=c.actor ||
           !built_outposts_.count(c.target) ||
           built_outposts_.at(c.target).source_colony_ship!=c.actor)
            return false;
        if(c.action==Action::AppointWorkshopSupervisor){
            // At most one appointment/revocation for this original workshop
            // per future tick, independent of arbitrary command sequence.
            const auto due=commands_.find(c.execute_tick);
            if(due!=commands_.end())
                for(const auto& prior:due->second)
                    if(prior.action==Action::AppointWorkshopSupervisor &&
                       prior.target==c.target)return false;
        }
        snapshot_version_=std::max(snapshot_version_,108);
    }
    const bool workshop_command=c.action==Action::ConstructWorkshop ||
        c.action==Action::TransferWorkshopFeed ||
        c.action==Action::AssignWorkshopOperator ||
        c.action==Action::TransferWorkshopOutput ||
        c.action==Action::ConstructWorkshopFinisher ||
        c.action==Action::AssignWorkshopFinisher ||
        c.action==Action::TransferWorkshopFinalOutput ||
        c.action==Action::TransferWorkshopStack;
    if(workshop_command){
        if(!c.target || c.thrust_y ||
           ((c.action==Action::ConstructWorkshop || c.action==Action::ConstructWorkshopFinisher) &&
             (c.amount!=1 || c.thrust_x)) ||
           (c.action==Action::TransferWorkshopFeed &&
             (c.thrust_x || !c.amount || c.amount%1000 || c.amount<-10000 || c.amount>10000)) ||
           ((c.action==Action::AssignWorkshopOperator || c.action==Action::AssignWorkshopFinisher) &&
             (c.thrust_x || c.amount<0 || c.amount>1000000)) ||
           ((c.action==Action::TransferWorkshopOutput || c.action==Action::TransferWorkshopFinalOutput) &&
             (c.thrust_x || c.amount<50 || c.amount>500 || c.amount%50)) ||
           (c.action==Action::TransferWorkshopStack &&
             (c.thrust_x<0 || c.thrust_x>3 || !c.amount ||
              c.amount<-1000 || c.amount>1000)))
            return false;
        snapshot_version_=std::max(snapshot_version_,
            c.action==Action::TransferWorkshopStack ? 24 :
            c.action==Action::ConstructWorkshopFinisher ||
            c.action==Action::AssignWorkshopFinisher ||
            c.action==Action::TransferWorkshopFinalOutput ? 23 : 22);
    }
    if(c.action==Action::ConstructStorage || c.action==Action::TransferStorageStack){
        if(!c.target || (c.action==Action::ConstructStorage ?
            (c.amount!=1 || c.thrust_x || c.thrust_y) :
            (c.amount<1 || c.amount>1000 || c.thrust_x<0 || c.thrust_x>3 ||
             c.thrust_y<0 || c.thrust_y>3)))return false;
        snapshot_version_=std::max(snapshot_version_,25);
    }
    if(c.action==Action::PlaceConveyorCell || c.action==Action::CompleteConveyorRoute){
        // amount: real module-built link command ID; x/y: one grid cell,
        // or zero when activating. No free conveyor segments are granted.
        if(!c.target || c.amount<1 ||
           (c.action==Action::CompleteConveyorRoute && (c.thrust_x || c.thrust_y)) ||
           c.thrust_x<-kPlacementBound || c.thrust_x>kPlacementBound ||
           c.thrust_y<-kPlacementBound || c.thrust_y>kPlacementBound)
            return false;
        snapshot_version_=std::max(snapshot_version_,30);
    }
    if(c.action==Action::PlaceFactoryEquipment){
        // amount=-1: installed workshop; 0: original store; positive: installed extra store.
        // thrust_x/y: grid placement, NOT movement. No extra module consumed.
        if(!c.target || c.amount < -1 || c.amount > std::numeric_limits<int>::max()/4 ||
           c.thrust_x < -kPlacementBound || c.thrust_x > kPlacementBound ||
           c.thrust_y < -kPlacementBound || c.thrust_y > kPlacementBound)
            return false;
        snapshot_version_=std::max(snapshot_version_,29);
    }
    if(c.action==Action::ConstructWorkshopLink){
        // thrust_x = existing storage node (0 is base);
        // thrust_y: 0 = storage -> workshop feed, 1 = workshop -> storage.
        // amount = 4 * whole-item transfer rate + item index.
        if(!c.target || c.thrust_x<0 ||
           c.thrust_x>std::numeric_limits<int>::max()/4 ||
           (c.thrust_y!=0 && c.thrust_y!=1) ||
           c.amount<4 || c.amount>4003 || c.amount/4<1 || c.amount/4>1000 ||
           (c.thrust_y==0 && c.amount%4!=0) ||
           (c.thrust_y==1 && c.amount%4==0))return false;
        snapshot_version_=std::max(snapshot_version_,28);
    }
    if(c.action==Action::ConstructStorageLink){
        // source and destination are site-local constructed node IDs (0 is base).
        // amount encodes [rate * 4 + item], rate in 1..1000 whole units/tick.
        if(!c.target || c.thrust_x<0 || c.thrust_y<0 ||
           c.thrust_x>std::numeric_limits<int>::max()/4 ||
           c.thrust_y>std::numeric_limits<int>::max()/4 ||
           c.thrust_x==c.thrust_y || c.amount<4 || c.amount>4003 || c.amount/4<1 || c.amount/4>1000)
            return false;
        snapshot_version_=std::max(snapshot_version_,27);
    }
    if(c.action==Action::ConstructStorageNode || c.action==Action::TransferStorageNodeStack){
        if(!c.target || (c.action==Action::ConstructStorageNode ?
           (c.amount<1 || c.amount>std::numeric_limits<int>::max()/4 || c.thrust_x || c.thrust_y) :
           (c.amount<1 || c.amount>1000 || c.thrust_x<4 ||
            c.thrust_y<0 || c.thrust_y>3)))return false;
        snapshot_version_=std::max(snapshot_version_,26);
    }
    const bool life_action=c.action==Action::PlantCrops || c.action==Action::HarvestCrops ||
      c.action==Action::RecycleWater || c.action==Action::RecycleCompost ||
      c.action==Action::ProduceOxygen || c.action==Action::LoadLifeWater;
    if(life_action){
        if(c.target || c.thrust_x || c.thrust_y || c.amount<1 ||
           c.amount>(c.action==Action::LoadLifeWater?100:10000))return false;
        snapshot_version_=std::max(snapshot_version_,10);
    }
    const bool farm_command=c.action==Action::ConstructFarm ||
        c.action==Action::TransferFarmStock || c.action==Action::AssignFarmWorkers;
    if(farm_command){
        if(c.target==0 || c.thrust_y!=0 ||
           (c.action==Action::ConstructFarm && (c.amount!=1 || c.thrust_x!=0)) ||
           (c.action==Action::AssignFarmWorkers && (c.thrust_x!=0 ||
                c.amount==0 || c.amount < -8 || c.amount > 8)) ||
           (c.action==Action::TransferFarmStock && (c.thrust_x<1 || c.thrust_x>5 ||
                c.amount==0 || c.amount < -10000 || c.amount > 10000)))return false;
        snapshot_version_=std::max(snapshot_version_,11);
    }
    const bool utility_command=c.action==Action::ConstructUtility || c.action==Action::RepairUtility;
    if(utility_command){
        if(!c.target || c.thrust_x || c.thrust_y || c.amount!=1)return false;
        snapshot_version_=std::max(snapshot_version_,13);
    }
    const bool delegated_command=c.action==Action::TransferUtilitySpares ||
        c.action==Action::AssignUtilityTechnicians || c.action==Action::SetUtilityMaintenance;
    if(delegated_command){
        if(!c.target || c.thrust_x || c.thrust_y ||
           (c.action==Action::TransferUtilitySpares && (!c.amount || c.amount<-32 || c.amount>32)) ||
           (c.action==Action::AssignUtilityTechnicians && (c.amount<0 || c.amount>8)) ||
           (c.action==Action::SetUtilityMaintenance && (c.amount<0 || c.amount>1)))return false;
        snapshot_version_=std::max(snapshot_version_,14);
    }
    const bool community_command=c.action==Action::ConstructCommunity ||
       c.action==Action::TransferCommunityStock || c.action==Action::AssignCommunityStaff;
    if(community_command){
        if(!c.target || c.thrust_y ||
           (c.action==Action::ConstructCommunity && (c.amount!=1 || c.thrust_x)) ||
           (c.action==Action::AssignCommunityStaff && (c.amount<0 || c.amount>4 || c.thrust_x)) ||
           (c.action==Action::TransferCommunityStock &&
             (c.thrust_x<1 || c.thrust_x>2 || !c.amount || c.amount<-10000 || c.amount>10000)))
            return false;
        snapshot_version_=std::max(snapshot_version_,15);
    }
    const bool clinic_command=c.action==Action::ConstructClinic ||
        c.action==Action::TransferClinicStock || c.action==Action::AssignClinicStaff;
    if(clinic_command){
        if(!c.target || c.thrust_y ||
           (c.action==Action::ConstructClinic && (c.amount!=1 || c.thrust_x)) ||
           (c.action==Action::AssignClinicStaff && (c.amount<0 || c.amount>4 || c.thrust_x)) ||
           (c.action==Action::TransferClinicStock && (c.thrust_x<1 || c.thrust_x>2 ||
              !c.amount || c.amount<-10000 || c.amount>10000)))return false;
        snapshot_version_=std::max(snapshot_version_,16);
    }
    if(c.action==Action::ConstructReclaimer){
        if(!c.target || c.thrust_x || c.thrust_y || c.amount!=1)return false;
        snapshot_version_=std::max(snapshot_version_,17);
    }
    if(c.action==Action::ConstructWaterworks || c.action==Action::TransferWaterworksFeed){
        if(!c.target || c.thrust_x || c.thrust_y ||
           (c.action==Action::ConstructWaterworks && c.amount!=1) ||
           (c.action==Action::TransferWaterworksFeed &&
              (!c.amount || c.amount%1000 || c.amount<-10000 || c.amount>10000)))return false;
        snapshot_version_=std::max(snapshot_version_,18);
    }
    if(c.action==Action::SetPolicyAssurance || c.action==Action::AssignPolicyInspectors){
        if(!c.target || c.thrust_x<0 || c.thrust_x>3 || c.thrust_y ||
           c.amount<0 || c.amount>3) return false;
        snapshot_version_=std::max(snapshot_version_,19);
    }
    if(c.action==Action::AssignFarmMentor){
        if(npcs_.empty() || !c.target || c.thrust_y || c.amount<=0 || c.amount>1000000 ||
           c.thrust_x<0 || c.thrust_x>1000000)return false;
        snapshot_version_=std::max(snapshot_version_,20);
    }
    const bool habitat_command=c.action==Action::ConstructHabitat ||
        c.action==Action::TransferHabitatStock || c.action==Action::AssignHabitatResidents;
    if(habitat_command){
        if(!c.target || c.thrust_y ||
          (c.action==Action::ConstructHabitat && (c.amount!=1 || c.thrust_x)) ||
          (c.action==Action::AssignHabitatResidents &&
              (c.thrust_x || !c.amount || c.amount<-8 || c.amount>8)) ||
          (c.action==Action::TransferHabitatStock &&
              (c.thrust_x<1 || c.thrust_x>3 || !c.amount ||
               c.amount<-10000 || c.amount>10000)))return false;
        snapshot_version_=std::max(snapshot_version_,12);
    }
    // A queued new industrial command must be restorable even before it executes.
    if(c.action==Action::RefineMineral || c.action==Action::FabricateMaterialModule)
        snapshot_version_=std::max(snapshot_version_,9);
    if(migration_action)snapshot_version_=std::max(snapshot_version_,
        c.passenger_ids.size()==2 ? 44:41);
    if(incident_action)snapshot_version_=std::max(snapshot_version_,
        c.action==Action::HostResolveMixedFerryLoss?37:
        c.action==Action::HostDestroyPassengerFerry?36:35);
    if(c.action==Action::TrainWorkshopTrade)
        snapshot_version_=std::max(snapshot_version_,105);
    submitted_ids_.insert(c.id);
    commands_[c.execute_tick].push_back(c);
    return true;
}

const Entity* Simulation::find(EntityId id) const {
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

std::vector<EntityId> Simulation::entity_ids_in_system(std::uint32_t system) const {
    if (!systems_.count(system)) throw std::invalid_argument("unknown system partition");
    std::vector<EntityId> result;
    for(const auto& [id,entity]:entities_)if(entity.system_id==system)result.push_back(id);
    return result;
}

Outcome Simulation::mine_status(const Entity& actor, EntityId target) const {
    if (actor.dock_state != DockState::InSpace) return Outcome::NotInSpace;
    if (actor.jump_state != ShipJumpState::Ready) return Outcome::InTransition;
    const auto it = entities_.find(target);
    if (it == entities_.end()) return depleted_.count(target) ? Outcome::NoResources : Outcome::InvalidTarget;
    const auto& asteroid = it->second;
    if (asteroid.type != EntityType::Asteroid || asteroid.system_id != actor.system_id)
        return Outcome::InvalidTarget;
    if(!same_local_frame(actor.id,target))return Outcome::OutOfRange;
    const auto dx = actor.position.x - asteroid.position.x;
    const auto dy = actor.position.y - asteroid.position.y;
    const auto* deposit=geology(target);
    // Surface mining: full radius matters for kilometre-scale physical deposits.
    const std::int64_t access=kMiningRangeMm+(deposit ? deposit->radius_m*1000 : 0);
    if(dx < -access || dx > access || dy < -access || dy > access ||
       dx*dx + dy*dy > access*access) return Outcome::OutOfRange;
    if (loaded_cargo_liters(actor) > actor.cargo_capacity*1000-1000) return Outcome::CargoFull;
    if (asteroid.ore <= 0) return Outcome::NoResources;
    return Outcome::Applied;
}

void Simulation::despawn(EntityId asteroid, EntityId actor, std::uint64_t start_command_id) {
    const auto it = entities_.find(asteroid);
    if (it == entities_.end() || it->second.type != EntityType::Asteroid || it->second.ore != 0)
        throw std::logic_error("despawn requires a fully depleted asteroid");
    entities_.erase(it);
    local_entity_anchors_.erase(asteroid);
    geology_.erase(asteroid);
    depleted_.insert(asteroid); // stable tombstone for NO_RESOURCES responses
    snapshot_version_ = std::max(snapshot_version_, 2);
    activity_events_.push_back({tick_, ActivityKind::AsteroidDespawned, start_command_id,
                                actor, asteroid, Outcome::NoResources, 0});
    // Stop EVERY miner targeting the vanished rock in the same tick, including
    // other ships not yet processed in the deterministic actor order.
    std::vector<EntityId> affected;
    for (const auto& [ship_id, job] : mining_) {
        if (job.target == asteroid) affected.push_back(ship_id);
    }
    for (const auto ship_id : affected) {
        const auto ship_it = entities_.find(ship_id);
        const auto full = ship_it != entities_.end() &&
            loaded_cargo_liters(ship_it->second) > ship_it->second.cargo_capacity*1000-1000;
        stop_mining(ship_id, full ? Outcome::CargoFull : Outcome::NoResources);
    }
}

void Simulation::stop_mining(EntityId actor, Outcome reason) {
    auto it = mining_.find(actor);
    if (it == mining_.end()) return;
    const auto job = it->second;
    mining_.erase(it);
    snapshot_version_ = std::max(snapshot_version_, 2);
    activity_events_.push_back({tick_, ActivityKind::MiningStopped, job.start_command_id,
                                actor, job.target, reason, 0});
}

Event Simulation::execute(const Command& c) {
    Event result{tick_, c.id, Outcome::InvalidActor, 0};
    if(snapshot_version_>=91){
        const auto actor=entities_.find(c.actor);
        if(actor!=entities_.end() && actor->second.manufactured_order_id &&
           (!colony_ships_.count(c.actor) ||
            colony_ships_.at(c.actor).crew<1)){
            result.result=Outcome::InsufficientCrew;return result;
        }
    }
    if(snapshot_version_>=72) {
        // Note that native survey commands and combat must not escape their
        // ship's local frame even when both regions share one star system.
        // Existing anchored-ship restrictions below remain conservative.
        // No legacy 2-D interaction can silently cross a celestial local
        // frame. Transit ships are unavailable until atomic arrival.
        const auto active=[](const auto& rows,EntityId id){
            for(const auto& [key,p]:rows){(void)key;if(p.ship==id)return true;}
            return false;
        };
        if(active(pending_celestial_transits_,c.actor) ||
           active(pending_celestial_transits_,c.target)){
            result.result=Outcome::InTransition;return result;
        }
        const auto source=entities_.find(c.actor);
        const auto target=entities_.find(c.target);
        if(source!=entities_.end() && source->second.type==EntityType::Ship){
            const auto frame=ship_frames_.find(c.actor);
            if(frame!=ship_frames_.end() && frame->second.anchor_body &&
               c.action!=Action::Thrust &&
               c.action!=Action::SurveyLocal &&
               c.action!=Action::SetHardpointPower &&
               c.action!=Action::SetCyberIsolation &&
               c.action!=Action::HackConfuse &&
               c.action!=Action::Mine &&
               c.action!=Action::StartMining &&
               c.action!=Action::StopMining){
                result.result=Outcome::OutOfRange;return result;
            }
            if(target!=entities_.end() && !same_local_frame(c.actor,c.target)){
                result.result=Outcome::OutOfRange;return result;
            }
        }
    }
    // SHIP-STATUS-04: an NPC-operator receipt is checked AGAIN at native
    // execution, not just at the host's earlier scheduling tick. A crew
    // transfer, casualty or hijack cannot remotely animate its old vessel.
    if(c.action==Action::HackConfuse){
        const auto attribution=npc_cyber_operators_.find(c.id);
        if(attribution!=npc_cyber_operators_.end()){
            const auto& o=attribution->second;
            const auto pilot=npcs_.find(o.pilot_npc);
            if(o.owner!=c.issuer || o.attacking_ship!=c.actor ||
               o.target_ship!=c.target || pilot==npcs_.end() ||
               pilot->second.home_ship!=c.actor ||
               pilot->second.location!=c.actor ||
               pilot->second.role!=NpcRole::Ship ||
               pilot->second.transport_id ||
               !npc_available_for_duty(o.pilot_npc)){
                result.result=Outcome::Unauthorized;return result;
            }
        }
    }
    if(c.action==Action::HostRegisterDependent){
        if(c.issuer!=kServerAuthority || snapshot_version_<59 || !c.actor ||
           c.actor!=c.target || !c.transport_id || c.transport_id<(1ULL<<62) ||
           c.passenger_ids.size()!=1 || physical_dependents_.count(c.transport_id) ||
           !colony_ships_.count(c.actor) || !npcs_.count(c.passenger_ids.front()) ||
           npcs_.at(c.passenger_ids.front()).home_ship!=c.actor ||
           npcs_.at(c.passenger_ids.front()).location!=c.actor){
            result.result=Outcome::InvalidArgument;return result;
        }
        physical_dependents_.emplace(c.transport_id,PhysicalDependent{
            c.transport_id,c.passenger_ids.front(),c.id,tick_,c.actor,c.actor});
        result.result=Outcome::Applied;
        result.transferred=0; // Physical people must not masquerade as material.
        return result;
    }
    if(c.action==Action::HostColonyDeparture){
        if(!can_depart_colony(c)){
            result.result=Outcome::InvalidArgument;return result;
        }
        // Every identity and receiver supply has passed preflight at the
        // actual execution tick. No mutation occurs until ALL are verified.
        auto& source=colony_ships_.at(c.actor);
        auto& receiver=colony_ships_.at(c.target);
        for(const auto id:c.passenger_ids){
            auto& n=npcs_.at(id);
            n.home_ship=c.target;
            if(n.residence_kind==transport::LocationKind::ColonyShip)
                n.residence_id=c.target;
            n.location=c.target;
            n.assigned_site=c.target;
            n.role=NpcRole::Ship;
            n.transport_id=0;
        }
        for(auto& [site,f]:farms_)if(f.source_ship==c.actor)f.workers=0;
        for(auto& [site,h]:habitats_)if(h.source_ship==c.actor)h.residents=0;
        source.crew=0;
        receiver.crew+=static_cast<std::int64_t>(c.passenger_ids.size());
        std::vector<std::uint64_t> relocated_dependents;
        for(auto& [id,dependent]:physical_dependents_)if(dependent.home_ship==c.actor){
            dependent.home_ship=c.target;
            dependent.location=c.target;
            relocated_dependents.push_back(id);
        }
        // An intact source hull stays physically present and retains all
        // untouched inventory, but can never be operated with zero crew.
        colony_departures_.emplace(c.actor,ColonyDeparture{
            c.actor,c.target,c.id,tick_,c.passenger_ids,relocated_dependents});
        result.result=Outcome::Applied;
        result.transferred=0; // Reserved for MATERIAL ledger transfers, never people.
        return result;
    }
    // A separate host-authorized action: never relax ordinary owner-bound
    // TransferStorageStack. Permission and immutable order are checked by the
    // SQLite world writer before a command can reach this deterministic replay.
    if(c.action==Action::HostTransferConsignedStock){
        if(c.issuer!=kServerAuthority||!colony_ships_.count(c.actor)||
           !life_support_.count(c.actor)||c.amount<1||c.amount>1000||
           (c.thrust_x!=1&&c.thrust_x!=2)||(c.thrust_y!=0&&c.thrust_y!=1)){
            result.result=Outcome::Unauthorized;return result;
        }
        auto ship=entities_.find(c.actor),site=entities_.find(c.target);
        auto storage=storages_.find(c.target);auto built=built_outposts_.find(c.target);
        if(ship==entities_.end()||site==entities_.end()||storage==storages_.end()||
           built==built_outposts_.end()||ship->second.type!=EntityType::Ship||
           site->second.type!=EntityType::Station||
           site->second.owner_id==ship->second.owner_id||
           built->second.source_colony_ship!=storage->second.source_ship||
           built->second.source_colony_ship==c.actor||
           entities_.at(built->second.source_colony_ship).owner_id!=site->second.owner_id||
           ship->second.system_id!=site->second.system_id){
            result.result=Outcome::InvalidTarget;return result;
        }
        const auto& vehicle=ship->second;
        if(vehicle.jump_state!=ShipJumpState::Ready ||
           (vehicle.dock_state!=DockState::InSpace&&vehicle.dock_station!=c.target)){
            result.result=Outcome::InTransition;return result;
        }
        const auto dx=vehicle.position.x-site->second.position.x;
        const auto dy=vehicle.position.y-site->second.position.y;
        if(dx<-kDockingRangeMm||dx>kDockingRangeMm||
           dy<-kDockingRangeMm||dy>kDockingRangeMm||
           dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){
            result.result=Outcome::OutOfRange;return result;
        }
        const std::string item_name=c.thrust_x==1?"aluminum_panels":"aluminum_frames";
        const auto code="good/"+item_name;
        auto bin=storage_inventory_snapshot(c.target);
        auto cargo=ship_inventory_snapshot(c.actor);
        const auto status=c.thrust_y==1?bin.transfer_to(cargo,code,c.amount):
                                         cargo.transfer_to(bin,code,c.amount);
        if(status!=inventory::TransferStatus::Applied){
            result.result=status==inventory::TransferStatus::DestinationFull?
                Outcome::CargoFull:Outcome::NoResources;return result;
        }
        const auto bqty=bin.amount(code),cqty=cargo.amount(code);
        if(bqty)storage->second.goods_kg[item_name]=bqty;
        else storage->second.goods_kg.erase(item_name);
        if(cqty)colony_ships_.at(c.actor).goods_kg[item_name]=cqty;
        else colony_ships_.at(c.actor).goods_kg.erase(item_name);
        snapshot_version_=std::max(snapshot_version_,56);
        result.transferred=c.amount;result.result=Outcome::Applied;return result;
    }
    if(c.action==Action::HostEscrowConstructedStock){
        const auto st=storages_.find(c.target);
        const auto site=entities_.find(c.target),ship=entities_.find(c.actor);
        if(c.issuer!=kServerAuthority||st==storages_.end()||
           site==entities_.end()||ship==entities_.end()||
           site->second.type!=EntityType::Station||
           ship->second.type!=EntityType::Ship||
           st->second.source_ship!=c.actor||
           site->second.owner_id!=ship->second.owner_id||
           site->second.system_id!=ship->second.system_id||
           !built_outposts_.count(c.target)||
           built_outposts_.at(c.target).source_colony_ship!=c.actor||
           c.thrust_x||c.thrust_y||c.amount<=0||c.amount>4000||
           st->second.goods_kg.count("aluminum")==0||
           st->second.goods_kg.at("aluminum")<c.amount||
           st->second.network_escrow_kg>1000000000-c.amount){
            result.result=Outcome::InvalidArgument;return result;
        }
        auto& storage=st->second;
        storage.goods_kg.at("aluminum")-=c.amount;
        if(storage.goods_kg.at("aluminum")==0)storage.goods_kg.erase("aluminum");
        storage.network_escrow_kg+=c.amount;
        snapshot_version_=std::max(snapshot_version_,52);
        result.transferred=c.amount;result.result=Outcome::Applied;return result;
    }
    if(c.action==Action::HostReturnConstructedStock){
        const auto st=storages_.find(c.target);
        const auto site=entities_.find(c.target),ship=entities_.find(c.actor);
        if(c.issuer!=kServerAuthority||st==storages_.end()||
           site==entities_.end()||ship==entities_.end()||
           site->second.type!=EntityType::Station||ship->second.type!=EntityType::Ship||
           st->second.source_ship!=c.actor||
           site->second.owner_id!=ship->second.owner_id||
           site->second.system_id!=ship->second.system_id||
           !built_outposts_.count(c.target)||
           built_outposts_.at(c.target).source_colony_ship!=c.actor||
           (c.thrust_x!=1&&c.thrust_x!=2)||c.thrust_y||c.amount<=0||c.amount>4000||
           st->second.network_escrow_kg<c.amount){
            result.result=Outcome::InvalidArgument;return result;
        }
        const std::string code=c.thrust_x==1?"aluminum_panels":"aluminum_frames";
        auto available=storage_inventory_snapshot(c.target);
        if(available.free_liters()<c.amount||available.amount("good/"+code)>4000-c.amount){
            result.result=Outcome::CargoFull;return result;
        }
        auto& storage=st->second;
        storage.network_escrow_kg-=c.amount;
        storage.goods_kg[code]+=c.amount;
        snapshot_version_=std::max(snapshot_version_,53);
        result.transferred=c.amount;result.result=Outcome::Applied;return result;
    }
    // GRSM-01b: host-authorized deterministic physical incidents. These are
    // simulation state transitions, NOT user-invokable reputation events.
    // Combat/damage resolution will eventually authorize these outcomes.
    if(c.action==Action::FireTrainingWeapon || c.action==Action::FireShipWeapon ||
       c.action==Action::Thrust || c.action==Action::RequestJump ||
       c.action==Action::RequestDock || c.action==Action::RequestUndock){
        for(const auto& [id,v]:voyages_){(void)id;
            if(v.ferry==c.actor && v.status==3){result.result=Outcome::InTransition;return result;}
        }
    }
    if(c.action==Action::HostTrainMedic || c.action==Action::HostTriageRescued ||
       c.action==Action::HostTreatInjuries || c.action==Action::HostScheduleMedicShift ||
       c.action==Action::HostLoadMedicalKit || c.action==Action::HostMobileStabilize){
        if(c.issuer!=kServerAuthority){result.result=Outcome::Unauthorized;return result;}
        if(c.action==Action::HostScheduleMedicShift){
            const auto cl=clinics_.find(c.actor);const auto n=npcs_.find(c.target);
            const auto q=named_medics_.find(c.target);
            if(cl==clinics_.end() || n==npcs_.end() || q==named_medics_.end() ||
               q->second.units<3 || q->second.clinic_site!=c.actor ||
               n->second.role!=NpcRole::Habitat || n->second.location!=c.actor ||
               n->second.assigned_site!=c.actor || c.amount<1 || c.amount>10000 ||
               tick_>std::numeric_limits<std::uint64_t>::max()-static_cast<std::uint64_t>(c.amount)){
                result.result=Outcome::InvalidArgument;return result;
            }
            // WO2: named medical and industrial jobs share ONE finite NPC.
            // A hospital shift may not reuse a presently installed hands-on
            // workshop operator/finisher; deny before any medical receipt.
            for(const auto& [site,shop]:workshops_){(void)site;
                if(shop.operator_npc_id==c.target ||
                   shop.finisher_npc_id==c.target ||
                   shop.supervisor_npc_id==c.target){
                    result.result=Outcome::InvalidArgument;return result;
                }
            }
            unsigned overlapping=0;
            for(const auto& [id,shift]:medic_shifts_){(void)id;
                if(tick_>=shift.end_tick || tick_+static_cast<std::uint64_t>(c.amount)<=shift.start_tick)continue;
                if(shift.medic_id==c.target){result.result=Outcome::InvalidArgument;return result;}
                if(shift.clinic_site==c.actor)++overlapping;
            }
            if(overlapping>=static_cast<unsigned>(cl->second.medics)){
                result.result=Outcome::NoResources;return result;
            }
            medic_shifts_.emplace(c.id,MedicShift{c.actor,c.target,tick_,tick_+static_cast<std::uint64_t>(c.amount)});
            result.result=Outcome::Applied;return result;
        }
        if(c.action==Action::HostLoadMedicalKit){
            const auto ship=entities_.find(c.actor),site=entities_.find(c.target);
            auto cl=clinics_.find(c.target);
            if(ship==entities_.end() || site==entities_.end() ||
               ship->second.type!=EntityType::Ship || site->second.type!=EntityType::Station ||
               !colony_ships_.count(c.actor) || cl==clinics_.end() ||
               ship->second.owner_id!=site->second.owner_id ||
               ship->second.system_id!=site->second.system_id || c.amount<1 || c.amount>100){
                result.result=Outcome::InvalidTarget;return result;
            }
            const auto dx=ship->second.position.x-site->second.position.x;
            const auto dy=ship->second.position.y-site->second.position.y;
            if(dx<-kDockingRangeMm || dx>kDockingRangeMm ||
               dy<-kDockingRangeMm || dy>kDockingRangeMm ||
               dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){result.result=Outcome::OutOfRange;return result;}
            auto& kit=mobile_medical_kits_[c.actor];
            if(cl->second.water_kg<c.amount || cl->second.substrate_kg<c.amount ||
               kit.water_kg>1000-c.amount || kit.substrate_kg>1000-c.amount){
                result.result=Outcome::NoResources;return result;
            }
            cl->second.water_kg-=c.amount;cl->second.substrate_kg-=c.amount;
            kit.water_kg+=c.amount;kit.substrate_kg+=c.amount;
            result.result=Outcome::Applied;return result;
        }
        if(c.action==Action::HostMobileStabilize){
            const auto ship=entities_.find(c.actor);const auto medic=npcs_.find(c.target);
            const auto q=named_medics_.find(c.target);
            auto kit=mobile_medical_kits_.find(c.actor);
            if(ship==entities_.end() || ship->second.type!=EntityType::Ship ||
               medic==npcs_.end() || q==named_medics_.end() || q->second.units<3 ||
               medic->second.location!=c.actor || kit==mobile_medical_kits_.end()){
                result.result=Outcome::InvalidTarget;return result;
            }
            std::int64_t need=0;
            for(auto id:c.passenger_ids){
                const auto n=npcs_.find(id);const auto care=medical_cases_.find(id);
                if(n==npcs_.end() || care==medical_cases_.end() ||
                   n->second.location!=c.actor || care->second.treatment_command ||
                   care->second.stabilization_command || need>1000-care->second.severity){
                    result.result=Outcome::InvalidArgument;return result;
                }
                need+=care->second.severity;
            }
            if(kit->second.water_kg<need || kit->second.substrate_kg<need){
                result.result=Outcome::NoResources;return result;
            }
            for(auto id:c.passenger_ids){auto& m=medical_cases_.at(id);
                m.stabilization_command=c.id;m.stabilized_tick=tick_;
            }
            kit->second.water_kg-=need;kit->second.substrate_kg-=need;
            kit->second.wastewater_kg+=need;kit->second.medical_waste_kg+=need;
            result.result=Outcome::Applied;return result;
        }
        if(c.action==Action::HostTrainMedic){
            auto clinic=clinics_.find(c.actor);auto medic=npcs_.find(c.target);
            const bool resident=medic!=npcs_.end() && medic->second.role==NpcRole::Habitat &&
                medic->second.location==c.actor && medic->second.assigned_site==c.actor;
            bool crew=false;
            if(snapshot_version_>=39 && medic!=npcs_.end() && medic->second.role==NpcRole::Ship &&
               medic->second.location==medic->second.home_ship &&
               entities_.count(medic->second.location) && entities_.count(c.actor)){
                const auto& ship=entities_.at(medic->second.location),&site=entities_.at(c.actor);
                const auto dx=ship.position.x-site.position.x,dy=ship.position.y-site.position.y;
                crew=ship.type==EntityType::Ship && ship.owner_id==site.owner_id &&
                     ship.system_id==site.system_id && dx>=-kDockingRangeMm && dx<=kDockingRangeMm &&
                     dy>=-kDockingRangeMm && dy<=kDockingRangeMm &&
                     dx*dx+dy*dy<=kDockingRangeMm*kDockingRangeMm;
            }
            if(clinic==clinics_.end() || medic==npcs_.end() || clinic->second.medics<1 ||
               (!resident && !crew) || c.amount<1 || c.amount>3 ||
               clinic->second.water_kg<c.amount || clinic->second.substrate_kg<c.amount){
                result.result=Outcome::NoResources;return result;
            }
            auto& q=named_medics_[c.target];
            if((q.clinic_site && q.clinic_site!=c.actor) ||
               q.units>1000-static_cast<std::uint64_t>(c.amount)){
                result.result=Outcome::InvalidArgument;return result;
            }
            q.clinic_site=c.actor;q.units+=static_cast<std::uint64_t>(c.amount);
            auto& cl=clinic->second;cl.water_kg-=c.amount;cl.substrate_kg-=c.amount;
            cl.medical_waste_kg+=c.amount;cl.wastewater_kg+=c.amount;
            cl.training_units+=static_cast<std::uint64_t>(c.amount);
            snapshot_version_=std::max(snapshot_version_,38);
            result.result=Outcome::Applied;return result;
        }
        if(c.action==Action::HostTriageRescued){
            auto voyage=voyages_.find(c.transport_id);
            bool rescue=false;
            for(const auto& [id,v]:voyages_){(void)id;
                if((v.status==5 || v.status==6) && v.rescue_transport_id==c.transport_id)rescue=true;
            }
            if(!rescue || voyage==voyages_.end() || voyage->second.status!=1 ||
               voyage->second.ferry!=c.actor || c.actor!=c.target ||
               c.amount<1 || c.amount>3){result.result=Outcome::InvalidArgument;return result;}
            for(auto id:c.passenger_ids){
                auto it=npcs_.find(id);
                if(it==npcs_.end() || it->second.location!=c.actor ||
                   it->second.transport_id!=c.transport_id ||
                   (medical_cases_.count(id) &&
                    !medical_cases_.at(id).recovered_tick &&
                    !(!medical_cases_.at(id).recovery_due_tick && medical_cases_.at(id).treatment_command))){
                    result.result=Outcome::InvalidArgument;return result;
                }
            }
            for(auto id:c.passenger_ids){
                auto older=medical_cases_.find(id);
                if(older!=medical_cases_.end()){
                    medical_history_.emplace(std::make_pair(id,older->second.triage_command),older->second);
                    medical_cases_.erase(older);
                }
                medical_cases_.emplace(id,MedicalCase{
                c.transport_id,c.id,tick_,static_cast<unsigned>(c.amount),0,0,0,0});
            }
            snapshot_version_=std::max(snapshot_version_,medical_history_.empty()?38:39);
            result.result=Outcome::Applied;return result;
        }
        // Treatment occurs at an existing player-built, supplied clinic. The
        // patients remain in the adjacent EMS vessel; no teleport/copy of people.
        const auto ship=entities_.find(c.actor),station=entities_.find(c.target);
        auto clinic=clinics_.find(c.target);auto medic=npcs_.find(c.transport_id);
        auto training=named_medics_.find(c.transport_id);
        if(ship==entities_.end() || station==entities_.end() ||
           ship->second.type!=EntityType::Ship || station->second.type!=EntityType::Station ||
           !built_outposts_.count(c.target) || clinic==clinics_.end() ||
           medic==npcs_.end() || training==named_medics_.end() || training->second.units<3 ||
           medic->second.role!=NpcRole::Habitat || medic->second.location!=c.target ||
           medic->second.assigned_site!=c.target || clinic->second.medics<1 ||
           ship->second.system_id!=station->second.system_id ||
           ship->second.owner_id!=station->second.owner_id){
            result.result=Outcome::InvalidTarget;return result;
        }
        const auto dx=ship->second.position.x-station->second.position.x;
        const auto dy=ship->second.position.y-station->second.position.y;
        if(dx<-kDockingRangeMm || dx>kDockingRangeMm ||
           dy<-kDockingRangeMm || dy>kDockingRangeMm ||
           dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){
            result.result=Outcome::OutOfRange;return result;
        }
        auto& cl=clinic->second;
        // Never mutate durable clock/slot state for a refused action.
        const auto used=cl.last_ems_tick==tick_?cl.ems_cases_this_tick:0;
        std::uint64_t slots=static_cast<std::uint64_t>(cl.medics);
        if(snapshot_version_>=39){
            std::set<std::uint64_t> on_shift;
            for(const auto& [id,shift]:medic_shifts_){(void)id;
                if(shift.clinic_site!=c.target || tick_<shift.start_tick || tick_>=shift.end_tick)continue;
                const auto n=npcs_.find(shift.medic_id);
                if(n==npcs_.end() || n->second.location!=c.target ||
                   npc_health_status(shift.medic_id)==NpcHealthStatus::Injured ||
                   npc_health_status(shift.medic_id)==NpcHealthStatus::Stabilized ||
                   npc_health_status(shift.medic_id)==NpcHealthStatus::Recovering)continue;
                on_shift.insert(shift.medic_id);
            }
            if(!on_shift.count(c.transport_id)){
                result.result=Outcome::NoResources;return result;
            }
            slots=std::min(slots,static_cast<std::uint64_t>(on_shift.size()));
        }
        if(c.passenger_ids.size()>slots || used>slots-c.passenger_ids.size()){
            result.result=Outcome::NoResources;return result;
        }
        std::int64_t needed=0;
        for(auto id:c.passenger_ids){
            auto case_it=medical_cases_.find(id);auto person=npcs_.find(id);
            if(case_it==medical_cases_.end() || person==npcs_.end() ||
               case_it->second.treatment_command || person->second.location!=c.actor){
                result.result=Outcome::InvalidArgument;return result;
            }
            needed+=case_it->second.severity;
        }
        if(cl.water_kg<needed || cl.substrate_kg<needed){
            result.result=Outcome::NoResources;return result;
        }
        for(auto id:c.passenger_ids){
            auto& care=medical_cases_.at(id);
            care.treatment_command=c.id;care.treatment_tick=tick_;
            care.clinic_site=c.target;care.medic_id=c.transport_id;
            if(snapshot_version_>=39){
                const auto period=3ULL*care.severity;
                if(tick_>std::numeric_limits<std::uint64_t>::max()-period)
                    throw std::overflow_error("medical recovery tick overflow");
                care.recovery_due_tick=tick_+period;
            }
        }
        cl.water_kg-=needed;cl.substrate_kg-=needed;
        cl.wastewater_kg+=needed;cl.medical_waste_kg+=needed;
        cl.ems_treatment_units+=static_cast<std::uint64_t>(needed);
        cl.ems_treated_cases+=c.passenger_ids.size();
        cl.last_ems_tick=tick_;
        cl.ems_cases_this_tick=used+c.passenger_ids.size();
        snapshot_version_=std::max(snapshot_version_,38);
        result.result=Outcome::Applied;return result;
    }
    if(c.action==Action::HostSettleMigrant){
        const auto id=c.passenger_ids.empty()?0:c.passenger_ids.front();
        auto n=npcs_.find(id);
        const auto v=voyages_.find(c.transport_id);
        const auto arrival=npc_transport_arrival(c.transport_id,id);
        if(c.issuer!=kServerAuthority || c.passenger_ids.empty() ||
           c.passenger_ids.size()>2 || !id ||
           (c.passenger_ids.size()==2 && (c.passenger_ids[1]<=id ||
             snapshot_version_<41)) ||
           n==npcs_.end() || v==voyages_.end() || v->second.status!=2 ||
           (!arrival || arrival->mode()!=transport::Mode::Ferry ||
            arrival->source().id!=c.actor ||
            arrival->destination().id!=c.target ||
            !transport::completed_transport(*arrival,id)) ||
           n->second.home_ship!=c.actor || n->second.assigned_site!=c.actor ||
           n->second.location!=c.target || n->second.transport_id ||
           n->second.role!=NpcRole::Ship || n->second.mentor_id ||
           crew_rehomes_.count(id) || named_medics_.count(id) ||
           !colony_ships_.count(c.actor) || !colony_ships_.count(c.target) ||
           colony_ships_.at(c.actor).crew<=1 || colony_ships_.at(c.target).crew>=1000){
            result.result=Outcome::InvalidArgument;return result;
        }
        if(c.passenger_ids.size()==2){
            const auto co=npcs_.find(c.passenger_ids[1]);
            if(co==npcs_.end() || co->second.home_ship!=c.actor ||
               co->second.assigned_site!=c.actor ||
               co->second.location!=c.target || co->second.transport_id ||
               co->second.role!=NpcRole::Ship || co->second.mentor_id ||
               crew_rehomes_.count(co->first) || named_medics_.count(co->first) ||
               !std::binary_search(v->second.passengers.begin(),
                    v->second.passengers.end(),co->first) ||
               colony_ships_.at(c.actor).crew<=2 ||
               colony_ships_.at(c.target).crew>998){
                result.result=Outcome::InvalidArgument;return result;
            }
            --colony_ships_.at(c.actor).crew;
            ++colony_ships_.at(c.target).crew;
            co->second.home_ship=c.target;
            if(co->second.residence_kind==transport::LocationKind::ColonyShip)
                co->second.residence_id=c.target;
            co->second.assigned_site=c.target;
        }
        --colony_ships_.at(c.actor).crew;
        ++colony_ships_.at(c.target).crew;
        n->second.home_ship=c.target;
        if(n->second.residence_kind==transport::LocationKind::ColonyShip)
            n->second.residence_id=c.target;
        n->second.assigned_site=c.target;
        snapshot_version_=std::max(snapshot_version_,
            c.passenger_ids.size()==2 ? 44:41);
        result.result=Outcome::Applied;
        return result;
    }
    if(c.action==Action::HostSplitEvacuatePassengers){
        const auto old=voyages_.find(c.transport_id);
        if(old==voyages_.end()||old->second.status!=3||old->second.ferry!=c.actor||
           c.amount<=0||!c.secondary_transport||
           voyages_.count(static_cast<std::uint64_t>(c.amount))||
           voyages_.count(c.secondary_transport)||
           c.thrust_x<=0||static_cast<std::size_t>(c.thrust_x)>=c.passenger_ids.size()){
            result.result=Outcome::InvalidArgument;return result;
        }
        const auto divider=c.passenger_ids.begin()+c.thrust_x;
        auto ids=c.passenger_ids;std::sort(ids.begin(),ids.end());
        if(ids!=old->second.passengers||
           std::adjacent_find(ids.begin(),ids.end())!=ids.end()){
            result.result=Outcome::InvalidArgument;return result;
        }
        const auto* source=find(c.actor);const auto* first=find(c.target);
        const auto* second=find(c.secondary_receiver);
        if(!source||!first||!second||source->type!=EntityType::Ship||
           first->type!=EntityType::Ship||second->type!=EntityType::Ship||
           c.actor==c.target||c.actor==c.secondary_receiver||
           c.target==c.secondary_receiver||
           source->system_id!=first->system_id||
           source->system_id!=second->system_id||source->jump_state!=ShipJumpState::Ready||
           first->jump_state!=ShipJumpState::Ready||
           second->jump_state!=ShipJumpState::Ready||
           source->dock_state!=DockState::InSpace||
           first->dock_state!=DockState::InSpace||
           second->dock_state!=DockState::InSpace||
           c.secondary_transport==static_cast<std::uint64_t>(c.amount)||
           c.secondary_transport==c.transport_id||
           static_cast<std::uint64_t>(c.amount)==c.transport_id){
            result.result=Outcome::InvalidTarget;return result;
        }
        for(const auto receiver:{first,second}){
            const auto dx=source->position.x-receiver->position.x;
            const auto dy=source->position.y-receiver->position.y;
            if(dx<-2500||dx>2500||dy<-2500||dy>2500||dx*dx+dy*dy>2500*2500){
                result.result=Outcome::OutOfRange;return result;
            }
        }
        for(const auto receiver:{c.target,c.secondary_receiver}){
            for(const auto& [id,n]:npcs_){(void)id;
                if(n.location==receiver&&n.transport_id){
                    result.result=Outcome::InvalidArgument;return result;
                }
            }
        }
        for(const auto passenger:c.passenger_ids){
            const auto it=npcs_.find(passenger);
            if(it==npcs_.end()||it->second.location!=c.actor||
               it->second.transport_id!=c.transport_id){
                result.result=Outcome::InvalidArgument;return result;
            }
        }
        const auto primary=static_cast<std::uint64_t>(c.amount);
        const auto secondary=c.secondary_transport;
        std::vector<std::uint64_t> p1(c.passenger_ids.begin(),divider);
        std::vector<std::uint64_t> p2(divider,c.passenger_ids.end());
        std::vector<std::uint64_t> operating_crew;
        if(c.thrust_y>0){
            for(const auto& [id,n]:npcs_)
                if(n.home_ship==c.actor&&n.assigned_site==c.actor&&
                   n.role==NpcRole::Ship){
                    if(n.location!=c.actor||n.transport_id){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    operating_crew.push_back(id);
                }
            if(operating_crew.size()!=static_cast<std::size_t>(colony_ships_.at(c.actor).crew)||
               operating_crew.size()<2||
               static_cast<std::size_t>(c.thrust_y)>=operating_crew.size()){
                result.result=Outcome::InvalidArgument;return result;
            }
            p1.insert(p1.end(),operating_crew.begin(),operating_crew.begin()+c.thrust_y);
            p2.insert(p2.end(),operating_crew.begin()+c.thrust_y,operating_crew.end());
            std::sort(p1.begin(),p1.end());std::sort(p2.begin(),p2.end());
        }
        if(p1.size()>16||p2.size()>16){
            result.result=Outcome::InvalidArgument;return result;
        }
        voyages_.emplace(primary,VoyageRecord{c.target,c.actor,p1,1,0});
        voyages_.emplace(secondary,VoyageRecord{c.secondary_receiver,c.actor,p2,1,0});
        for(const auto passenger:p1){auto& n=npcs_.at(passenger);
            n.location=c.target;n.transport_id=primary;
        }
        for(const auto passenger:p2){auto& n=npcs_.at(passenger);
            n.location=c.secondary_receiver;n.transport_id=secondary;
        }
        // Native crew are now physically aboard their assigned rescue vessel,
        // but retain their original employer and home. No free replacement crew.
        old->second.status=5;old->second.destination=c.target;
        old->second.rescue_transport_id=primary;
        old->second.secondary_rescue_transport_id=secondary;
        old->second.last_incident_id=c.id;old->second.stranded_tick=0;
        old->second.stranded_reason=0;
        snapshot_version_=std::max(snapshot_version_,c.thrust_y?49:48);
        result.result=Outcome::Applied;return result;
    }
    if(c.action==Action::HostEmbarkPassengers || c.action==Action::HostDisembarkPassengers ||
       c.action==Action::HostStrandFerry || c.action==Action::HostResumeFerry ||
       c.action==Action::HostEvacuatePassengers || c.action==Action::HostDestroyPassengerFerry ||
       c.action==Action::HostResolveMixedFerryLoss){
        const bool incident=c.action==Action::HostStrandFerry ||
            c.action==Action::HostResumeFerry || c.action==Action::HostEvacuatePassengers ||
        c.action==Action::HostDestroyPassengerFerry || c.action==Action::HostResolveMixedFerryLoss;
        if(c.issuer!=kServerAuthority || !c.transport_id || c.passenger_ids.empty() ||
           c.thrust_x || c.thrust_y){result.result=Outcome::Unauthorized;return result;}
        auto ship=entities_.find(c.actor),endpoint=entities_.find(c.target);
        if(ship==entities_.end() || endpoint==entities_.end() ||
           ship->second.type!=EntityType::Ship || endpoint->second.type!=EntityType::Ship ||
           !colony_ships_.count(c.actor) || !colony_ships_.count(c.target) ||
           ship->second.system_id!=endpoint->second.system_id){
            result.result=Outcome::InvalidTarget;return result;
        }
        auto ids=c.passenger_ids;std::sort(ids.begin(),ids.end());
        if(ids.front()==0 || std::adjacent_find(ids.begin(),ids.end())!=ids.end()){
            result.result=Outcome::InvalidArgument;return result;
        }
        if(incident){
            auto voyage=voyages_.find(c.transport_id);
            if(voyage==voyages_.end() || voyage->second.ferry!=c.actor ||
               (c.action!=Action::HostDestroyPassengerFerry &&
                c.action!=Action::HostResolveMixedFerryLoss && voyage->second.passengers!=ids) ||
               ship->second.jump_state!=ShipJumpState::Ready ||
               ship->second.dock_state!=DockState::InSpace ||
               (c.action!=Action::HostEvacuatePassengers &&
                c.action!=Action::HostResolveMixedFerryLoss && c.actor!=c.target)){
                result.result=Outcome::InvalidArgument;return result;
            }
            const auto status=voyage->second.status;
            if((c.action==Action::HostStrandFerry && (status!=1 || c.amount<1 || c.amount>5)) ||
               (c.action==Action::HostResumeFerry && (status!=3 || c.amount)) ||
               (c.action==Action::HostEvacuatePassengers &&
                ((status!=1 && status!=3) || c.actor==c.target || c.amount<=0 ||
                 voyages_.count(static_cast<std::uint64_t>(c.amount)))) ||
               (c.action==Action::HostDestroyPassengerFerry &&
                 ((status!=1 && status!=3) || c.amount!=0)) ||
               (c.action==Action::HostResolveMixedFerryLoss &&
                 ((status!=1 && status!=3) || c.actor==c.target ||
                  c.amount<=0 || static_cast<std::uint64_t>(c.amount)>=ids.size() ||
                  c.id==c.transport_id || voyages_.count(c.id)))){
                result.result=Outcome::InvalidArgument;return result;
            }
            for(auto npc:ids){const auto n=npcs_.find(npc);
                if(n==npcs_.end() || n->second.location!=c.actor ||
                   (c.action!=Action::HostDestroyPassengerFerry &&
                    c.action!=Action::HostResolveMixedFerryLoss && n->second.transport_id!=c.transport_id)){
                    result.result=Outcome::InvalidArgument;return result;
                }
            }
            if(c.action==Action::HostDestroyPassengerFerry || c.action==Action::HostResolveMixedFerryLoss){
                const bool mixed=c.action==Action::HostResolveMixedFerryLoss;
                const auto rescue_count=mixed?static_cast<std::size_t>(c.amount):0U;
                std::vector<std::uint64_t> aboard;
                for(const auto& [id,n]:npcs_){
                    if(n.location==c.actor)aboard.push_back(id);
                    if(n.home_ship==c.actor && (n.location!=c.actor ||
                       n.assigned_site!=c.actor || n.role!=NpcRole::Ship)){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                }
                if(aboard!=ids || !std::includes(ids.begin(),ids.end(),
                     voyage->second.passengers.begin(),voyage->second.passengers.end())){
                    result.result=Outcome::InvalidArgument;return result;
                }
                for(const auto& [id,v]:voyages_){
                    if(id!=c.transport_id && v.ferry==c.actor && (v.status==1 || v.status==3)){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                }
                for(const auto& [id,site]:built_outposts_){(void)id;
                    if(site.source_colony_ship==c.actor){result.result=Outcome::InvalidArgument;return result;}
                }
                std::map<EntityId,std::int64_t> losses;
                // The command carries a COMPLETE onboard partition. In the mixed
                // variant, the first amount IDs are survivors, the rest fatalities.
                // Native ferry crew cannot yet be rehomed without a dedicated
                // employment/kinship transfer, so fail closed if they survive.
                const auto survivors_end=c.passenger_ids.begin()+rescue_count;
                if(mixed){
                    if(!std::is_sorted(c.passenger_ids.begin(),survivors_end) ||
                       !std::is_sorted(survivors_end,c.passenger_ids.end())){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    if(snapshot_version_<38 &&
                       !std::includes(voyage->second.passengers.begin(),voyage->second.passengers.end(),
                       c.passenger_ids.begin(),survivors_end)){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                }
                for(auto i=c.passenger_ids.begin();i!=c.passenger_ids.end();++i){
                    const auto& n=npcs_.at(*i);
                    if(n.role!=NpcRole::Ship || n.assigned_site!=n.home_ship ||
                       (mixed && i<survivors_end &&
                        (n.home_ship==c.actor ? n.transport_id!=0 : n.transport_id!=c.transport_id))){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    if(!mixed || i>=survivors_end)++losses[n.home_ship];
                }
                for(const auto& [home,number]:losses){
                    auto it=colony_ships_.find(home);
                    if(it==colony_ships_.end() || it->second.crew<number ||
                       (home!=c.actor && it->second.crew<=number)){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                }
                std::int64_t rescued_native=0;
                if(mixed)for(auto i=c.passenger_ids.begin();i!=survivors_end;++i)
                    if(npcs_.at(*i).home_ship==c.actor)++rescued_native;
                if(losses[c.actor]+rescued_native!=colony_ships_.at(c.actor).crew ||
                   (mixed && rescued_native &&
                    colony_ships_.at(c.target).crew>1000-rescued_native)){
                    result.result=Outcome::InvalidArgument;return result;
                }
                if(mixed){
                    const auto dx=ship->second.position.x-endpoint->second.position.x;
                    const auto dy=ship->second.position.y-endpoint->second.position.y;
                    if(dx<-kDockingRangeMm || dx>kDockingRangeMm ||
                       dy<-kDockingRangeMm || dy>kDockingRangeMm ||
                       dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){
                        result.result=Outcome::OutOfRange;return result;
                    }
                    std::size_t occupied=0;
                    for(const auto& [id,n]:npcs_){(void)id;
                        if(n.location==c.target && n.transport_id)++occupied;
                    }
                    if(occupied>16 || rescue_count>16-occupied ||
                       endpoint->second.type!=EntityType::Ship ||
                       colony_ships_.at(c.target).crew<1){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                }
                for(auto i=survivors_end;i!=c.passenger_ids.end();++i){
                    const auto npc=*i;
                    const auto n=npcs_.at(npc);
                    casualties_.emplace(npc,NpcCasualty{n,tick_,c.id,c.actor});
                    npcs_.erase(npc);
                    activity_events_.push_back({tick_,ActivityKind::NpcLost,c.id,c.actor,npc,Outcome::Applied,0});
                }
                for(const auto& [home,number]:losses)colony_ships_.at(home).crew-=number;
                if(mixed){
                    std::vector<std::uint64_t> saved(c.passenger_ids.begin(),survivors_end);
                    voyages_.emplace(c.id,VoyageRecord{c.target,c.actor,saved,1,0});
                    for(auto npc:saved){
                        auto& n=npcs_.at(npc);
                        if(n.home_ship==c.actor){
                            crew_rehomes_.emplace(npc,CrewRehome{c.actor,c.target,c.id,tick_});
                            n.home_ship=c.target;n.assigned_site=c.target;
                            if(n.residence_kind==transport::LocationKind::ColonyShip)
                                n.residence_id=c.target;
                            --colony_ships_.at(c.actor).crew;
                            ++colony_ships_.at(c.target).crew;
                        }
                        n.location=c.target;n.transport_id=c.id;
                    }
                    voyage->second.destination=c.target;
                    voyage->second.rescue_transport_id=c.id;
                }
                voyage->second.status=6;voyage->second.last_incident_id=c.id;
                voyage->second.stranded_tick=0;voyage->second.stranded_reason=0;
                stop_mining(c.actor,Outcome::NotInSpace);
                ship->second.type=EntityType::Wreck;
            ship->second.hull=0;ship->second.weapon_charges=0;ship->second.last_weapon_tick=0;ship->second.training_drone=false;
                ship->second.velocity={};
                activity_events_.push_back({tick_,ActivityKind::ShipDestroyed,c.id,c.actor,c.actor,Outcome::Applied,0});
            }else if(c.action==Action::HostStrandFerry){
                voyage->second.status=3;voyage->second.stranded_reason=static_cast<unsigned>(c.amount);
                voyage->second.stranded_tick=tick_;voyage->second.last_incident_id=c.id;
                ship->second.velocity={};stop_mining(c.actor,Outcome::InTransition);
            }else if(c.action==Action::HostResumeFerry){
                voyage->second.status=1;voyage->second.stranded_reason=0;
                voyage->second.stranded_tick=0;voyage->second.last_incident_id=c.id;
            }else{
                const auto dx=ship->second.position.x-endpoint->second.position.x;
                const auto dy=ship->second.position.y-endpoint->second.position.y;
                if(dx<-kDockingRangeMm || dx>kDockingRangeMm ||
                   dy<-kDockingRangeMm || dy>kDockingRangeMm ||
                   dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){
                    result.result=Outcome::OutOfRange;return result;
                }
                std::size_t aboard=0;
                for(const auto& [id,n]:npcs_){(void)id;
                    if(n.location==c.target && n.transport_id)++aboard;
                }
                if(aboard>16 || ids.size()>16-aboard){
                    result.result=Outcome::InvalidArgument;return result;
                }
                voyage->second.status=5;voyage->second.destination=c.target;
                voyage->second.rescue_transport_id=static_cast<std::uint64_t>(c.amount);
                voyage->second.last_incident_id=c.id;voyage->second.stranded_tick=0;
                voyage->second.stranded_reason=0;
                voyages_.emplace(static_cast<std::uint64_t>(c.amount),
                    VoyageRecord{c.target,c.actor,ids,1,0});
                for(auto npc:ids){auto& n=npcs_.at(npc);n.location=c.target;
                    n.transport_id=static_cast<std::uint64_t>(c.amount);
                }
            }
            snapshot_version_=std::max(snapshot_version_,
                c.action==Action::HostResolveMixedFerryLoss?37:
                c.action==Action::HostDestroyPassengerFerry?36:35);
            result.result=Outcome::Applied;return result;
        }
        if(c.actor==c.target || c.amount){result.result=Outcome::InvalidArgument;return result;}
        for(auto id:ids){const auto it=npcs_.find(id);
            if(it==npcs_.end() || (c.action==Action::HostEmbarkPassengers ?
                (it->second.location!=c.target || it->second.transport_id!=0) :
                (it->second.location!=c.actor || it->second.transport_id!=c.transport_id))){
                result.result=Outcome::InvalidArgument;return result;
            }
        }
        if(c.action==Action::HostEmbarkPassengers){
            std::size_t aboard=0;
            for(const auto& [id,n]:npcs_){(void)id;
                if(n.location==c.actor && n.transport_id)++aboard;
            }
            if(aboard>16 || ids.size()>16-aboard || voyages_.count(c.transport_id)){
                result.result=Outcome::InvalidArgument;return result;
            }
            voyages_.emplace(c.transport_id,VoyageRecord{c.actor,c.target,ids,1,0});
        }else{
            const auto v=voyages_.find(c.transport_id);
            if(v==voyages_.end() || v->second.status!=1 || v->second.ferry!=c.actor ||
               v->second.origin==0 || v->second.passengers!=ids){
                result.result=Outcome::InvalidArgument;return result;
            }
            v->second.status=2;v->second.destination=c.target;
        }
        for(auto id:ids){auto& n=npcs_.at(id);
            n.location=c.action==Action::HostEmbarkPassengers ? c.actor : c.target;
            n.transport_id=c.action==Action::HostEmbarkPassengers ? c.transport_id : 0;
        }
        snapshot_version_=std::max(snapshot_version_,34);
        result.result=Outcome::Applied;return result;
    }
    if (c.action==Action::HostDestroyShip || c.action==Action::HostCrewLoss) {
        if(c.issuer!=kServerAuthority || !c.actor || !c.target || c.thrust_x || c.thrust_y)
            {result.result=Outcome::Unauthorized;return result;}
        if(c.action==Action::HostDestroyShip){
            const auto attacker=entities_.find(c.actor), victim=entities_.find(c.target);
            if(attacker==entities_.end() || victim==entities_.end() || c.actor==c.target ||
               attacker->second.type!=EntityType::Ship || victim->second.type!=EntityType::Ship ||
               attacker->second.system_id!=victim->second.system_id || colony_ships_.count(c.target))
                {result.result=Outcome::InvalidTarget;return result;}
            auto& ship=victim->second;
            if(ship.dock_state!=DockState::InSpace || ship.jump_state!=ShipJumpState::Ready)
                {result.result=Outcome::InTransition;return result;}
            if(c.amount!=0){result.result=Outcome::InvalidArgument;return result;}
            stop_mining(ship.id,Outcome::NotInSpace);
            // Cargo remains on the persistent wreck; there is no magical
            // deletion, addition, or instant looting of physical ore.
            ship.type=EntityType::Wreck;
            // A manufactured vessel keeps its immutable hull provenance as
            // physical wreckage; it is no longer a navigable ship frame.
            if(snapshot_version_>=91 && ship.manufactured_order_id)
                ship_frames_.erase(ship.id);
            ship.hull=0;ship.weapon_charges=0;ship.last_weapon_tick=0;ship.training_drone=false;
            // Preserve former ship owner for salvage claims and victim attribution.
            // The Wreck entity is never an executable player ship.
            ship.velocity={};
            activity_events_.push_back({tick_,ActivityKind::ShipDestroyed,c.id,c.actor,c.target,Outcome::Applied,0});
            snapshot_version_=std::max(snapshot_version_,21);
            result.result=Outcome::Applied;return result;
        }
        const auto ship=entities_.find(c.actor);
        const auto crew=colony_ships_.find(c.actor);
        if(c.actor!=c.target || ship==entities_.end() || ship->second.type!=EntityType::Ship ||
           crew==colony_ships_.end() || npcs_.empty())
            {result.result=Outcome::InvalidTarget;return result;}
        if(c.amount<=0 || c.amount>=crew->second.crew){result.result=Outcome::InvalidArgument;return result;}
        std::vector<std::uint64_t> casualties;
        for(const auto& [id,n]:npcs_)if(n.home_ship==c.actor && n.location==c.actor &&
             n.role==NpcRole::Ship && casualties.size()<static_cast<std::size_t>(c.amount))casualties.push_back(id);
        if(casualties.size()!=static_cast<std::size_t>(c.amount))
            throw std::logic_error("NPC manifest inconsistent with crew loss");
        for(const auto id:casualties){
            npcs_.erase(id);
            activity_events_.push_back({tick_,ActivityKind::NpcLost,c.id,c.actor,id,Outcome::Applied,0});
        }
        crew->second.crew-=c.amount;
        snapshot_version_=std::max(snapshot_version_,21);
        result.result=Outcome::Applied;return result;
    }
    if (c.action == Action::SetGatePairOnline ||
        c.action == Action::SetGateStaff || c.action == Action::DeliverGateSpares) {
        if (c.issuer != kServerAuthority || c.actor == c.target ||
            (c.action==Action::SetGatePairOnline && (c.amount != 0 && c.amount != 1)) ||
            (c.action==Action::SetGateStaff && (c.amount < 0 || c.amount > 16)) ||
            (c.action==Action::DeliverGateSpares && (c.amount < 1 || c.amount > 10000)))
            {result.result=Outcome::Unauthorized;return result;}
        auto a=entities_.find(c.target), b=entities_.find(c.actor);
        if (a==entities_.end() || b==entities_.end() ||
            a->second.type!=EntityType::Stargate || b->second.type!=EntityType::Stargate ||
            a->second.system_id!=b->second.gate_destination ||
            b->second.system_id!=a->second.gate_destination) {
            result.result=Outcome::InvalidTarget;return result;
        }
        auto it=gate_ops_.find(std::minmax(c.actor,c.target));
        if(c.action==Action::SetGateStaff || c.action==Action::DeliverGateSpares){
            if(it==gate_ops_.end()){result.result=Outcome::InvalidTarget;return result;}
            if(c.action==Action::SetGateStaff)it->second.technicians=c.amount;
            else {
                if(it->second.spare_parts>1000000-c.amount){result.result=Outcome::InvalidArgument;return result;}
                it->second.spare_parts+=c.amount;
            }
            result.result=Outcome::Applied;return result;
        }
        if(c.amount==1 && it!=gate_ops_.end() && it->second.condition==0){
            result.result=Outcome::GateOffline;return result;
        }
        // Both directional endpoints change together; in-flight jumps complete.
        a->second.gate_online=(c.amount==1);
        b->second.gate_online=(c.amount==1);
        snapshot_version_=std::max(snapshot_version_,5);
        result.result=Outcome::Applied;return result;
    }
    const auto actor_it = entities_.find(c.actor);
    if (actor_it == entities_.end() || actor_it->second.type != EntityType::Ship) return result;
    Entity& actor = actor_it->second;
    if (actor.owner_id != c.issuer) { result.result = Outcome::Unauthorized; return result; }
    switch (c.action) {
        case Action::FocusSurveyProbe:
        case Action::CancelSurveyFocus:
        case Action::LaunchGeologicalDrone:
        case Action::DeploySurveyProbe:
        case Action::ScanSurveyProbe:
        case Action::RecoverSurveyProbe: {
            if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            if(!actor.fitting.probe_launcher()){result.result=Outcome::NoResources;return result;}
            const auto probe_slot=actor.fitting.hardpoints[0]==2?0u:1u;
            if(!hardpoint_effective_on(c.actor,static_cast<std::uint8_t>(probe_slot))){
                result.result=Outcome::ModuleOffline;return result;
            }
            if(c.action==Action::LaunchGeologicalDrone){
                if(!actor.survey_probes_carried){result.result=Outcome::NoResources;return result;}
                if(survey_probes_.size()>=1000){result.result=Outcome::InvalidArgument;return result;}
                const auto target=entities_.find(c.target);
                if(target==entities_.end() || target->second.type!=EntityType::Asteroid ||
                   !geology_.count(c.target) || target->second.ore<=0){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto& asteroid=target->second;
                if(asteroid.system_id!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
                // Geological drones require a 10 km ship-to-target operating
                // envelope. The device finishes its assigned mission autonomously
                // if the carrier subsequently flies away.
                constexpr auto range=kDroneControlRangeMm;
                const auto dx=asteroid.position.x-actor.position.x;
                const auto dy=asteroid.position.y-actor.position.y;
                if(dx<-range||dx>range||dy<-range||dy>range||dx*dx+dy*dy>range*range){
                    result.result=Outcome::OutOfRange;return result;
                }
                DeployedSurveyProbe drone{c.id,c.actor,c.issuer,actor.system_id,actor.position,tick_};
                drone.drone_phase=1;drone.drone_target=c.target;drone.drone_home=actor.position;
                survey_probes_.emplace(c.id,drone);
                --actor.survey_probes_carried;
                result.transferred=1;result.result=Outcome::Applied;
            }else if(c.action==Action::FocusSurveyProbe ||
                     c.action==Action::CancelSurveyFocus){
                const auto probe_it=survey_probes_.find(c.target);
                if(probe_it==survey_probes_.end()||
                   probe_it->second.owner!=c.issuer||
                   probe_it->second.carrier!=c.actor||
                   probe_it->second.system!=actor.system_id||
                   probe_it->second.drone_phase!=0){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& survey=probe_it->second;
                if(!within_deep_probe_link(survey.position.x-actor.position.x,
                                           survey.position.y-actor.position.y)){
                    result.result=Outcome::OutOfRange;return result;
                }
                if(c.action==Action::CancelSurveyFocus){
                    survey.focus_site=0;survey.focus_waypoint={};
                    result.result=Outcome::Applied;return result;
                }
                if(survey.focus_site){
                    result.result=Outcome::InTransition;return result;
                }
                // Only a current coarse sensor contact is lockable.
                // Clients may submit an ID but never an oracle waypoint.
                const auto candidates=coarse_probe_signatures(c.issuer,c.actor);
                const auto found=std::find_if(candidates.begin(),candidates.end(),
                    [&](const SignatureHint& h){return h.id==static_cast<EntityId>(c.amount);});
                if(found==candidates.end()){
                    result.result=Outcome::InvalidTarget;return result;
                }
                survey.focus_site=found->id;
                survey.focus_waypoint=found->coarse;
                snapshot_version_=std::max(snapshot_version_,69);
                result.result=Outcome::Applied;
            }else if(c.action==Action::DeploySurveyProbe){
                if(!actor.survey_probes_carried){result.result=Outcome::NoResources;return result;}
                if(survey_probes_.size()>=1000){result.result=Outcome::InvalidArgument;return result;}
                survey_probes_.emplace(c.id,DeployedSurveyProbe{
                    c.id,c.actor,c.issuer,actor.system_id,actor.position,tick_});
                --actor.survey_probes_carried;
                result.transferred=1;result.result=Outcome::Applied;
            }else{
                auto it=survey_probes_.find(c.target);
                if(it==survey_probes_.end() || it->second.owner!=c.issuer ||
                   it->second.carrier!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto& probe=it->second;
                if(probe.system!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
                if(c.action==Action::ScanSurveyProbe){
                    if(probe.drone_phase||probe.focus_site){result.result=Outcome::InvalidTarget;return result;}
                    // 0.5 AU is the planned deep-space probe command budget.
                    // Native positions currently span only ~2,000 km per axis,
                    // so this limit will become meaningful with cosmic scaling.
                    const auto dx=probe.position.x-actor.position.x;
                    const auto dy=probe.position.y-actor.position.y;
                    if(!within_deep_probe_link(dx,dy)){
                        result.result=Outcome::OutOfRange;return result;
                    }
                    std::vector<DiscoveryRecord> candidates;
                    constexpr auto r=kProbeScanRangeMm;
                    for(const auto& [id,e]:entities_){
                        if(e.type!=EntityType::Asteroid || e.system_id!=probe.system)continue;
                        const auto dx=e.position.x-probe.position.x,dy=e.position.y-probe.position.y;
                        if(dx< -r || dx>r || dy< -r || dy>r || dx*dx+dy*dy>r*r)continue;
                        const auto key=std::make_pair(c.issuer,id);
                        if(!discoveries_.count(key)&&!pending_probe_discoveries_.count(key))
                            candidates.push_back(DiscoveryRecord{c.issuer,id,e.system_id,e.position,
                                tick_,(geology_.count(id)!=0||local_deposit_taxonomy(id).has_value())});
                    }
                    if(pending_probe_discoveries_.size()>10000-discoveries_.size()||
                       candidates.size()>10000-discoveries_.size()-pending_probe_discoveries_.size()||
                       probe_report_delay_ticks_>std::numeric_limits<std::uint64_t>::max()-tick_){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    // Native authority: a probe scan schedules observations,
                    // not instant player knowledge. A 10-second gameplay
                    // response is configured in committed simulation ticks.
                    for(const auto& d:candidates) {
                        const auto key=std::make_pair(d.owner,d.site);
                        pending_probe_discoveries_.emplace(key,
                            PendingProbeDiscovery{d,c.actor,probe.id,
                                tick_+probe_report_delay_ticks_});
                    }
                    snapshot_version_=std::max(snapshot_version_,68);
                    result.transferred=static_cast<std::int64_t>(candidates.size());
                    result.result=Outcome::Applied;
                }else{
                    constexpr auto r=kProbeRecoveryRangeMm;
                    const auto dx=probe.position.x-actor.position.x;
                    const auto dy=probe.position.y-actor.position.y;
                    if(dx< -r || dx>r || dy< -r || dy>r || dx*dx+dy*dy>r*r){
                        result.result=Outcome::OutOfRange;return result;
                    }
                    if(probe.focus_site || (probe.drone_phase && probe.drone_phase!=3)){
                        result.result=Outcome::InTransition;return result;
                    }
                    if(actor.survey_probes_carried>=8){
                        result.result=Outcome::CargoFull;return result;
                    }
                    // The geological report is durable ship data, not cargo.
                    // Only transferring the recovered device to the ship writes
                    // its first-observation/taxonomy record; a remote, offline
                    // drone cannot silently reveal its findings to the player.
                    if(probe.drone_phase==3 && probe.taxonomy){
                        const auto site=entities_.find(probe.drone_target);
                        const auto key=std::make_pair(probe.owner,probe.drone_target);
                        if(!discoveries_.count(key)){
                            if(discoveries_.size()>=10000){
                                result.result=Outcome::InvalidArgument;return result;
                            }
                            const auto where=site!=entities_.end()?site->second.position:probe.position;
                            discoveries_.emplace(key,DiscoveryRecord{probe.owner,
                                probe.drone_target,probe.system,where,tick_,true,
                                probe.taxonomy});
                        }else discoveries_.at(key).survey_taxonomy=probe.taxonomy;
                    }
                    ++actor.survey_probes_carried;
                    survey_probes_.erase(it);
                    result.transferred=1;result.result=Outcome::Applied;
                }
            }
            snapshot_version_=std::max(snapshot_version_,66);return result;
        }
        case Action::SetHardpointPower: {
            const auto slot=static_cast<std::uint8_t>(c.amount/2);
            if(slot>=2 || !actor.fitting.hardpoints[slot]) {
                result.result=Outcome::InvalidTarget;return result;
            }
            actor.hardpoint_requested_on[slot]=(c.amount%2)==1;
            snapshot_version_=std::max(snapshot_version_,100);
            result.result=Outcome::Applied;return result;
        }
        case Action::SetCyberIsolation: {
            const auto slot=static_cast<std::uint8_t>(c.amount/2);
            if(slot>=2 || !actor.fitting.hardpoints[slot]){
                result.result=Outcome::InvalidTarget;return result;
            }
            // A manual/isolated control line cannot be spoofed by the
            // remote emitter; the physically installed module remains.
            actor.hardpoint_isolated[slot]=(c.amount%2)==1;
            snapshot_version_=std::max(snapshot_version_,101);
            result.result=Outcome::Applied;return result;
        }
        case Action::ConfigureCyberModule: {
            const auto colony=colony_ships_.find(c.actor);
            if(colony==colony_ships_.end()){
                result.result=Outcome::NotColonyShip;return result;
            }
            if(actor.dock_state!=DockState::InSpace ||
               actor.velocity.x || actor.velocity.y ||
               actor.jump_state!=ShipJumpState::Ready) {
                result.result=actor.dock_state!=DockState::InSpace?
                    Outcome::NotInSpace:Outcome::ShipMoving;
                return result;
            }
            const bool installing=c.amount>0;
            const auto type=std::uint8_t(c.amount>0?c.amount:-c.amount);
            const bool emitter=type==3;
            // 3 intrusion emitter, 4 firewall, 5 stealth baffle, 6
            // high-resolution counter-ECM sensor array. No free hardware.
            const std::uint8_t hardware=emitter?3:
                type==4?1:type==5?2:3;
            auto& rack=emitter?actor.fitting.hardpoints:actor.fitting.support;
            if(installing) {
                if(colony->second.modules<=0){
                    result.result=Outcome::NoResources;return result;
                }
                if(std::find(rack.begin(),rack.end(),hardware)!=rack.end()){
                    result.result=Outcome::InvalidArgument;return result;
                }
                const auto empty=std::find(rack.begin(),rack.end(),0);
                if(empty==rack.end()){
                    result.result=Outcome::CargoFull;return result;
                }
                const auto slot=static_cast<std::size_t>(empty-rack.begin());
                *empty=hardware;
                if(emitter) {
                    actor.hardpoint_requested_on[slot]=true;
                    actor.hardpoint_isolated[slot]=false;
                }
                --colony->second.modules; // one real fabricated 2-tonne module
            } else {
                const auto installed=std::find(rack.begin(),rack.end(),hardware);
                if(installed==rack.end()){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto slot=static_cast<std::size_t>(installed-rack.begin());
                if(emitter)for(const auto& effect:ship_status_.records()) {
                    if(effect.target_ship!=c.actor ||
                       tick_<effect.started_tick || tick_>=effect.expires_tick)
                        continue;
                    for(const auto& key:effect.targets)
                        if(key.rack==0 && key.slot==slot && key.hardware==3){
                            result.result=Outcome::InTransition;return result;
                        }
                }
                *installed=0;
                if(emitter){
                    actor.hardpoint_requested_on[slot]=true;
                    actor.hardpoint_isolated[slot]=false;
                }
                ++colony->second.modules; // material returns to the original cargo
            }
            snapshot_version_=std::max(snapshot_version_,
                (type==5 || type==6)?103:101);
            result.result=Outcome::Applied;return result;
        }
        case Action::HackConfuse: {
            if(!actor.fitting.intrusion_emitter()){
                result.result=Outcome::NoResources;return result;
            }
            const auto emitter_slot=actor.fitting.hardpoints[0]==3?0u:1u;
            if(!hardpoint_effective_on(c.actor,
                    static_cast<std::uint8_t>(emitter_slot))) {
                result.result=Outcome::ModuleOffline;return result;
            }
            if(actor.dock_state!=DockState::InSpace ||
               actor.jump_state!=ShipJumpState::Ready) {
                result.result=Outcome::InTransition;return result;
            }
            if(actor.last_hack_tick &&
               tick_>=actor.last_hack_tick &&
               tick_-actor.last_hack_tick<kHackCooldownTicks){
                result.result=Outcome::CooldownActive;return result;
            }
            const auto target_it=entities_.find(c.target);
            if(target_it==entities_.end() ||
               target_it->second.type!=EntityType::Ship) {
                result.result=Outcome::InvalidTarget;return result;
            }
            const auto& defender=target_it->second;
            if(!defender.owner_id ||
               defender.owner_id==actor.owner_id) {
                result.result=Outcome::Unauthorized;return result;
            }
            if(protected_from_attack(c.target) ||
               defender.dock_state!=DockState::InSpace) {
                result.result=Outcome::InTransition;return result;
            }
            if(defender.system_id!=actor.system_id ||
               !same_local_frame(c.actor,c.target)) {
                result.result=Outcome::OutOfRange;return result;
            }
            // Never trust a guessed target entity ID from the browser or
            // an autonomous NPC. Native sensor acquisition is mandatory at
            // the actual execution tick, not merely at target-list render.
            // A baffle can therefore make a vessel invisible to a nearby
            // emitter until its carrier approaches or installs stronger
            // original material-backed detection hardware.
            const auto acquisition=ship_sensors::detection_range_mm(
                *this,actor,defender);
            const auto physical_distance=std::hypot(
                double(defender.position.x)-actor.position.x,
                double(defender.position.y)-actor.position.y);
            if(physical_distance>double(kHackRangeMm) ||
               physical_distance>double(acquisition)) {
                result.result=Outcome::OutOfRange;return result;
            }
            const std::array<bool,2> exposed{{
                bool(defender.fitting.hardpoints[0]),
                bool(defender.fitting.hardpoints[1])}};
            const auto roster=ship_status::fitted_controls(defender,
                defender.hardpoint_requested_on,exposed,
                defender.hardpoint_isolated);
            // One real fitted passive firewall subtracts 60 attack points.
            // Unplugged manually isolated modules cannot be attacked at all.
            ship_status::Effect effect{};
            effect.id=c.id | (std::uint64_t{1}<<63);
            effect.source_ship=c.actor;
            effect.target_ship=c.target;
            effect.started_tick=tick_;
            effect.expires_tick=tick_+
                (defender.fitting.cyber_firewall()?kHackDurationTicks/2:
                 kHackDurationTicks);
            effect.pulse_ticks=2;
            effect.intensity=100;
            effect.resistance=defender.fitting.cyber_firewall()?60:0;
            std::uint64_t seed=rng_state_ ^ (c.id*0x9e3779b97f4a7c15ULL)
                                    ^ c.target;
            seed=(seed^(seed>>30))*0xbf58476d1ce4e5b9ULL;
            seed=(seed^(seed>>27))*0x94d049bb133111ebULL;
            effect.seed=(seed^(seed>>31))|1ULL;
            // The original world owns both receipt and target status.
            // A resisted attack still expends this emission's cooldown.
            actor.last_hack_tick=tick_;
            snapshot_version_=std::max(snapshot_version_,101);
            if(!ship_status_.inflict_confusion(effect,roster,tick_)) {
                result.result=Outcome::AttackResisted;return result;
            }
            result.result=Outcome::Applied;return result;
        }
        case Action::ConfigureExplorationScanner: {
            const auto cit=colony_ships_.find(c.actor);
            if(cit==colony_ships_.end()){result.result=Outcome::NotColonyShip;return result;}
            if(actor.dock_state!=DockState::InSpace || actor.velocity.x || actor.velocity.y){
                result.result=actor.dock_state!=DockState::InSpace?Outcome::NotInSpace:Outcome::ShipMoving;
                return result;
            }
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            const auto installed=actor.fitting.exploration_scanner();
            // A compromised fitted module must not be removed and then
            // reinstalled into the same slot/type to evade the immutable
            // effect ancestry or silently transfer an old effect to new gear.
            for(const auto& effect:ship_status_.records())
                if(effect.target_ship==c.actor &&
                   tick_>=effect.started_tick &&
                   tick_<effect.expires_tick)
                    for(const auto& key:effect.targets)
                        if(key.rack==0 && key.hardware==1){
                            result.result=Outcome::InTransition;return result;
                        }
            if(c.amount==1){
                if(installed){result.result=Outcome::InvalidArgument;return result;}
                if(cit->second.modules<=0){result.result=Outcome::NoResources;return result;}
                auto& rack=actor.fitting.hardpoints;
                const auto slot=std::find(rack.begin(),rack.end(),0);
                if(slot==rack.end()){result.result=Outcome::CargoFull;return result;}
                *slot=1;
                actor.hardpoint_requested_on[static_cast<std::size_t>(slot-rack.begin())]=true;
                --cit->second.modules;
            }else{
                if(!installed){result.result=Outcome::InvalidArgument;return result;}
                for(std::size_t slot=0;slot<2;++slot)
                    if(actor.fitting.hardpoints[slot]==1){
                        actor.fitting.hardpoints[slot]=0;
                        actor.hardpoint_requested_on[slot]=true;
                        break;
                    }
                ++cit->second.modules;
            }
            snapshot_version_=std::max(snapshot_version_,64);
            result.result=Outcome::Applied;return result;
        }
        case Action::SurveyLocal: {
            if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            if(!actor.fitting.exploration_scanner()){result.result=Outcome::NoResources;return result;}
            const auto scanner_slot=actor.fitting.hardpoints[0]==1?0u:1u;
            if(!hardpoint_effective_on(c.actor,static_cast<std::uint8_t>(scanner_slot))){
                result.result=Outcome::ModuleOffline;return result;
            }
            constexpr auto range=kLocalSurveyRangeMm;
            std::vector<DiscoveryRecord> candidates;
            for(const auto& [id,e]:entities_){
                if(e.type!=EntityType::Asteroid || e.system_id!=actor.system_id ||
                   !same_local_frame(c.actor,id))continue;
                const auto dx=e.position.x-actor.position.x,dy=e.position.y-actor.position.y;
                if(dx<-range || dx>range || dy<-range || dy>range ||
                   dx*dx+dy*dy>range*range)continue;
                auto key=std::make_pair(c.issuer,id);
                if(!discoveries_.count(key)){
                    candidates.push_back(DiscoveryRecord{c.issuer,id,e.system_id,e.position,
                        tick_,geology_.count(id)!=0});
                }
            }
            if(candidates.size()>10000-discoveries_.size()){
                result.result=Outcome::InvalidArgument;return result;
            }
            for(const auto& d:candidates)
                discoveries_.emplace(std::make_pair(d.owner,d.site),d);
            snapshot_version_=std::max(snapshot_version_,63);
            result.transferred=static_cast<std::int64_t>(candidates.size());result.result=Outcome::Applied;
            return result;
        }
        case Action::SalvageWreck: {
            if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            const auto it=entities_.find(c.target);
            // Limited first pass: only owner-8 uncrewed training-drone wreck stock.
            // Player or civilian colony/ferry wrecks are not lootable here.
            if(it==entities_.end() || it->second.type!=EntityType::Wreck ||
               it->second.owner_id!=8 || colony_ships_.count(c.target)){
                result.result=Outcome::InvalidTarget;return result;
            }
            auto& wreck=it->second;
            if(wreck.system_id!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
            constexpr auto range=kSalvageRangeMm;
            const auto dx=wreck.position.x-actor.position.x;
            const auto dy=wreck.position.y-actor.position.y;
            if(dx< -range || dx>range || dy< -range || dy>range ||
               dx*dx+dy*dy>range*range){result.result=Outcome::OutOfRange;return result;}
            if(wreck.cargo==0){result.result=Outcome::NoResources;return result;}
            const auto free_liters=actor.cargo_capacity*1000-loaded_cargo_liters(actor);
            if(free_liters<1000){result.result=Outcome::CargoFull;return result;}
            const auto moved=std::min({c.amount,wreck.cargo,free_liters/1000});
            if(moved<=0){result.result=Outcome::CargoFull;return result;}
            actor.cargo+=moved;wreck.cargo-=moved;
            result.transferred=moved;result.result=Outcome::Applied;
            snapshot_version_=std::max(snapshot_version_,62);
            return result;
        }
        case Action::FireShipWeapon: {
            // COMBAT-PHYSICAL-01A: native ship-to-ship projectile impact,
            // consuming the SAME finite original fitted starter weapon
            // charge used by training combat. No generated ammo or second
            // combat writer. The sensor and yaw are checked at EXECUTION,
            // not trusted from the browser's target list.
            if(actor.type!=EntityType::Ship ||
               actor.dock_state!=DockState::InSpace){
                result.result=Outcome::NotInSpace;return result;
            }
            if(actor.jump_state!=ShipJumpState::Ready){
                result.result=Outcome::InTransition;return result;
            }
            const auto it=entities_.find(c.target);
            if(it==entities_.end() || it->second.type!=EntityType::Ship ||
               it->second.training_drone ||
               !it->second.owner_id ||
               it->second.owner_id==actor.owner_id ||
               it->second.hull<=0){
                result.result=Outcome::InvalidTarget;return result;
            }
            auto& target=it->second;
            if(target.dock_state!=DockState::InSpace ||
               target.jump_state!=ShipJumpState::Ready){
                result.result=Outcome::InTransition;return result;
            }
            if(!same_local_frame(actor.id,target.id)){
                result.result=Outcome::OutOfRange;return result;
            }
            const auto dx=target.position.x-actor.position.x;
            const auto dy=target.position.y-actor.position.y;
            const auto distance=std::hypot(double(dx),double(dy));
            const auto sensed=ship_sensors::detection_range_mm(
                *this,actor,target);
            if(distance>double(kTrainingWeaponRangeMm) ||
               distance>double(sensed)){
                result.result=Outcome::OutOfRange;return result;
            }
            if(actor.weapon_charges<=0){
                result.result=Outcome::NoResources;return result;
            }
            if(actor.last_weapon_tick &&
               tick_-actor.last_weapon_tick<kTrainingWeaponCooldownTicks){
                result.result=Outcome::CooldownActive;return result;
            }
            const double radians=double(actor.heading_deg)*
                3.14159265358979323846/180.0;
            const double along=double(dx)*std::cos(radians)+
                double(dy)*std::sin(radians);
            const double lateral=double(dy)*std::cos(radians)-
                double(dx)*std::sin(radians);
            if(along<=0 || std::abs(lateral)>along){
                result.result=Outcome::OutOfRange;return result;
            }
            // Crew and dependents have real, permanent identities. Until
            // their physical injury/rescue/custody rules are connected,
            // only unoccupied non-colony hulls may reach zero and wreck.
            const bool lives_aboard=
                people_physically_aboard(target.id)!=0 ||
                children_physically_aboard(target.id)!=0 ||
                colony_ships_.count(target.id)!=0;
            const auto floor=lives_aboard?std::int64_t{1}:std::int64_t{0};
            const auto possible=std::max<std::int64_t>(
                0,target.hull-floor);
            if(!possible){
                result.result=Outcome::AttackResisted;return result;
            }
            const auto damage=std::min(possible,kShipWeaponDamage);
            --actor.weapon_charges;
            actor.last_weapon_tick=tick_;
            target.hull-=damage;
            result.transferred=damage; // exact damage in original event
            if(target.hull==0){
                // Same physical wreck semantics as original host-authorized
                // ship loss; cargo and ownership survive as salvage claims.
                stop_mining(target.id,Outcome::NotInSpace);
                target.type=EntityType::Wreck;
                target.weapon_charges=0;
                target.last_weapon_tick=0;
                target.training_drone=false;
                target.velocity={};
                if(target.manufactured_order_id)
                    ship_frames_.erase(target.id);
                activity_events_.push_back({tick_,ActivityKind::ShipDestroyed,
                    c.id,c.actor,c.target,Outcome::Applied,0});
            }
            snapshot_version_=std::max(snapshot_version_,104);
            result.result=Outcome::Applied;
            return result;
        }
        case Action::FireTrainingWeapon: {
            if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            const auto it=entities_.find(c.target);
            if(it==entities_.end() || !it->second.training_drone ||
               it->second.type!=EntityType::Ship || colony_ships_.count(c.target) ||
               it->second.owner_id==c.issuer || it->second.hull<=0){
                result.result=Outcome::InvalidTarget;return result;
            }
            auto& drone=it->second;
            if(drone.system_id!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
            const auto dx=drone.position.x-actor.position.x;
            const auto dy=drone.position.y-actor.position.y;
            constexpr auto range=kTrainingWeaponRangeMm;
            if(dx < -range || dx > range || dy < -range || dy > range ||
               dx*dx+dy*dy>range*range){result.result=Outcome::OutOfRange;return result;}
            if(!actor.weapon_charges){result.result=Outcome::NoResources;return result;}
            if(actor.last_weapon_tick &&
               tick_-actor.last_weapon_tick<kTrainingWeaponCooldownTicks){
                result.result=Outcome::InTransition;return result;
            }
            // Integer aim cone: native yaw matters. No browser-side hit authority.
            const double radians=static_cast<double>(actor.heading_deg)*3.14159265358979323846/180.0;
            const double dot=static_cast<double>(dx)*std::cos(radians)+
                static_cast<double>(dy)*std::sin(radians);
            const double cross=static_cast<double>(dy)*std::cos(radians)-
                static_cast<double>(dx)*std::sin(radians);
            if(dot<=0 || std::abs(cross)>dot){result.result=Outcome::OutOfRange;return result;}
            --actor.weapon_charges;
            actor.last_weapon_tick=tick_;
            result.transferred=std::min(drone.hull,kTrainingWeaponDamage);
            drone.hull-=result.transferred;
            if(!drone.hull){
                drone.type=EntityType::Wreck;drone.training_drone=false;
                drone.weapon_charges=0;drone.last_weapon_tick=0;drone.velocity={};
            }
            snapshot_version_=std::max(snapshot_version_,62);
            result.result=Outcome::Applied;return result;
        }
        case Action::Thrust:
            if (actor.dock_state != DockState::InSpace) {
                result.result = Outcome::NotInSpace; return result;
            }
            if (actor.jump_state==ShipJumpState::Jumping) {
                result.result=Outcome::InTransition;return result;
            }
            if (actor.jump_state==ShipJumpState::ArrivalCloak && (c.thrust_x || c.thrust_y ||
                (snapshot_version_>=119 && (c.amount==1 || c.amount==-1)))) {
                actor.jump_state=ShipJumpState::Ready;
                actor.jump_ticks=0;
            }
            if (c.thrust_x < -1 || c.thrust_x > 1 || c.thrust_y < -1 || c.thrust_y > 1) {
                result.result = Outcome::InvalidArgument;
                return result;
            }
            if(c.amount){
                if(snapshot_version_>=119 && (c.amount==1 || c.amount==-1) &&
                   !c.thrust_x && !c.thrust_y){
                    actor.velocity_z_mm_per_tick=clamp_velocity(
                        actor.velocity_z_mm_per_tick+
                        c.amount*kAccelerationMmPerTick2);
                    result.result=Outcome::Applied;return result;
                }
                if((c.amount!=15 && c.amount!=-15) || c.thrust_x || c.thrust_y){result.result=Outcome::InvalidArgument;return result;}
                actor.heading_deg=static_cast<std::uint16_t>((static_cast<int>(actor.heading_deg)+static_cast<int>(c.amount)+360)%360);
                snapshot_version_=std::max(snapshot_version_,61);
                result.result=Outcome::Applied;return result;
            }
            actor.velocity.x = clamp_velocity(actor.velocity.x + c.thrust_x * kAccelerationMmPerTick2);
            actor.velocity.y = clamp_velocity(actor.velocity.y + c.thrust_y * kAccelerationMmPerTick2);
            result.result = Outcome::Applied;
            return result;
        case Action::Mine: {
            if (c.amount <= 0) { result.result = Outcome::InvalidArgument; return result; }
            result.result = mine_status(actor, c.target);
            if (result.result != Outcome::Applied) return result;
            auto& rock = entities_.at(c.target);
            const auto n = std::min({c.amount, rock.ore, (actor.cargo_capacity*1000 - loaded_cargo_liters(actor))/1000});
            if(n>0 && colony_ships_.count(c.actor) &&
               (geology_.count(c.target)||local_deposit_taxonomy(c.target)))
                transfer_geological_minerals(c.actor,c.target,n);
            else actor.cargo += n;
            rock.ore -= n;
            result.transferred = n;
            if (rock.ore == 0) despawn(c.target, c.actor, 0);
            if (loaded_cargo_liters(actor) > actor.cargo_capacity*1000-1000) {
                // A single-shot mine may also fill the hold: notify UI immediately.
                snapshot_version_ = std::max(snapshot_version_, 2);
                activity_events_.push_back({tick_, ActivityKind::MiningStopped, c.id,
                                            c.actor, c.target, Outcome::CargoFull, 0});
            }
            return result;
        }
        case Action::StartMining: {
            if (c.amount <= 0 || c.amount > 1000000000LL) { result.result = Outcome::InvalidArgument; return result; }
            result.result = mine_status(actor, c.target);
            if (result.result != Outcome::Applied) return result;
            if (mining_.count(c.actor)) stop_mining(c.actor, Outcome::Applied); // switch targets
            mining_[c.actor] = {c.target, c.amount, c.id};
            snapshot_version_ = std::max(snapshot_version_, 2);
            return result;
        }
        case Action::StopMining:
            if (!mining_.count(c.actor)) { result.result = Outcome::NotMining; return result; }
            stop_mining(c.actor, Outcome::Applied);
            result.result = Outcome::Applied;
            return result;
        case Action::RequestUndock: {
            if (actor.dock_state == DockState::Undocking || actor.dock_state == DockState::Docking) {
                result.result = Outcome::InTransition; return result;
            }
            if (actor.dock_state != DockState::Docked) {
                result.result = Outcome::NotDocked; return result;
            }
            const auto* station = find(actor.dock_station);
            if (!station || station->type != EntityType::Station) {
                result.result = Outcome::InvalidTarget; return result;
            }
            actor.dock_state = DockState::Undocking;
            actor.transition_ticks = kDockTransitionTicks;
            snapshot_version_ = std::max(snapshot_version_, 3);
            result.result = Outcome::Applied;
            return result;
        }
        case Action::RequestDock: {
            if (actor.dock_state == DockState::Undocking || actor.dock_state == DockState::Docking) {
                result.result = Outcome::InTransition; return result;
            }
            if (actor.dock_state != DockState::InSpace) {
                result.result = Outcome::NotInSpace; return result;
            }
            if (actor.jump_state!=ShipJumpState::Ready) {
                result.result=Outcome::InTransition;return result;
            }
            const auto* station = find(c.target);
            if (!station || station->type != EntityType::Station) {
                result.result = Outcome::InvalidTarget; return result;
            }
            if(station->system_id!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
            const auto dx = actor.position.x - station->position.x;
            const auto dy = actor.position.y - station->position.y;
            if (dx < -kDockingRangeMm || dx > kDockingRangeMm ||
                dy < -kDockingRangeMm || dy > kDockingRangeMm ||
                dx * dx + dy * dy > kDockingRangeMm * kDockingRangeMm) {
                result.result = Outcome::OutOfRange; return result;
            }
            if (actor.velocity.x != 0 || actor.velocity.y != 0) {
                result.result = Outcome::ShipMoving; return result;
            }
            stop_mining(c.actor, Outcome::NotInSpace);
            // A player's hangar is private to this station. The first docking
            // allocates an EMPTY bay here; it never imports remote inventory.
            if (!hangar(c.target, c.issuer))
                hangars_.emplace(std::make_pair(c.target, c.issuer),
                                 StationHangar{0, kDefaultHangarCapacity});
            actor.dock_state = DockState::Docking;
            actor.dock_station = c.target;
            actor.transition_ticks = kDockTransitionTicks;
            snapshot_version_ = std::max(snapshot_version_, 3);
            result.result = Outcome::Applied;
            return result;
        }
        case Action::UnloadOre: {
            if (actor.dock_state == DockState::Docking || actor.dock_state == DockState::Undocking) {
                result.result = Outcome::InTransition; return result;
            }
            if (actor.dock_state != DockState::Docked) {
                result.result = Outcome::NotDocked; return result;
            }
            if (c.target != actor.dock_station) {
                result.result = Outcome::InvalidTarget; return result;
            }
            if (c.amount <= 0) { result.result = Outcome::InvalidArgument; return result; }
            const auto h = hangars_.find({actor.dock_station, c.issuer});
            if (h == hangars_.end()) { result.result = Outcome::Unauthorized; return result; }
            if (actor.cargo == 0) { result.result = Outcome::NoResources; return result; }
            if (h->second.ore >= h->second.capacity) {
                result.result = Outcome::HangarFull; return result;
            }
            const auto moved = std::min({c.amount, actor.cargo, h->second.capacity - h->second.ore});
            h->second.ore += moved;
            actor.cargo -= moved;
            result.transferred = moved;
            snapshot_version_ = std::max(snapshot_version_, 3);
            result.result = Outcome::Applied;
            return result;
        }
        case Action::LoadOre:
        case Action::SellOre: {
            if (actor.dock_state == DockState::Docking || actor.dock_state == DockState::Undocking) {
                result.result = Outcome::InTransition; return result;
            }
            if (actor.dock_state != DockState::Docked) {
                result.result = Outcome::NotDocked; return result;
            }
            if (c.target != actor.dock_station) {
                result.result = Outcome::InvalidTarget; return result;
            }
            if (c.amount <= 0) {result.result = Outcome::InvalidArgument; return result;}
            const auto h = hangars_.find({actor.dock_station,c.issuer});
            if (h == hangars_.end()) {result.result = Outcome::Unauthorized; return result;}
            if (h->second.ore == 0) {result.result = Outcome::NoResources; return result;}
            if (c.action == Action::LoadOre) {
                if (loaded_cargo_liters(actor) > actor.cargo_capacity*1000-1000) {
                    result.result = Outcome::CargoFull; return result;
                }
                const auto moved = std::min({c.amount, h->second.ore, (actor.cargo_capacity*1000-loaded_cargo_liters(actor))/1000});
                actor.cargo += moved; h->second.ore -= moved;
                result.transferred = moved;
            } else {
                const auto m = markets_.find(c.target);
                if (m == markets_.end()) {result.result = Outcome::InvalidTarget; return result;}
                const auto price = m->second.price_per_ore;
                if (price <= 0 || m->second.credits < price) {
                    result.result = Outcome::InsufficientFunds; return result;
                }
                auto& balance = wallets_[c.issuer];
                if (balance < 0) throw std::logic_error("negative wallet");
                const auto wallet_room = (std::numeric_limits<std::int64_t>::max()-balance)/price;
                const auto market_affordable = m->second.credits/price;
                const auto moved = std::min({c.amount, h->second.ore, wallet_room, market_affordable});
                if (moved == 0) {result.result = Outcome::InsufficientFunds; return result;}
                const auto payout = moved*price;
                m->second.credits -= payout;
                balance += payout;
                m->second.ore += moved;
                h->second.ore -= moved;
                result.transferred = moved;
            }
            snapshot_version_ = std::max(snapshot_version_,4);
            result.result = Outcome::Applied;
            return result;
        }
        case Action::RequestJump: {
            if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            const auto* gate=find(c.target);
            if(!gate || gate->type!=EntityType::Stargate){result.result=Outcome::InvalidTarget;return result;}
            if(gate->system_id!=actor.system_id){result.result=Outcome::WrongSystem;return result;}
            if(!gate->gate_online){result.result=Outcome::GateOffline;return result;}
            const auto dx=actor.position.x-gate->position.x;
            const auto dy=actor.position.y-gate->position.y;
            if(dx<-kJumpActivationMm || dx>kJumpActivationMm ||
               dy<-kJumpActivationMm || dy>kJumpActivationMm ||
               dx*dx+dy*dy>kJumpActivationMm*kJumpActivationMm){
                result.result=Outcome::OutOfRange;return result;
            }
            stop_mining(c.actor,Outcome::NotInSpace);
            actor.velocity={};actor.jump_state=ShipJumpState::Jumping;
            actor.jump_ticks=kJumpTransitTicks;actor.pending_gate=c.target;
            snapshot_version_=std::max(snapshot_version_,5);result.result=Outcome::Applied;return result;
        }
        case Action::TrainWorkshopTrade:
        case Action::StartWorkshopShift:
        case Action::CancelWorkshopShift:
        case Action::RelieveWorkshopShift:
        case Action::AppointWorkshopSupervisor:
        case Action::MentorWorkshopTrade:
        case Action::ApproveWorkshopJob:
        case Action::CancelWorkshopJob:
        case Action::ConstructWorkshop:
        case Action::TransferWorkshopFeed:
        case Action::AssignWorkshopOperator:
        case Action::TransferWorkshopOutput:
        case Action::ConstructWorkshopFinisher:
        case Action::AssignWorkshopFinisher:
        case Action::TransferWorkshopFinalOutput:
        case Action::TransferWorkshopStack:
        case Action::ConstructStorage:
        case Action::TransferStorageStack:
        case Action::ConstructStorageNode:
        case Action::TransferStorageNodeStack:
        case Action::ConstructStorageLink:
        case Action::ConstructWorkshopLink:
        case Action::PlaceFactoryEquipment:
        case Action::PlaceConveyorCell:
        case Action::CompleteConveyorRoute:
        case Action::AssignFarmMentor:
        case Action::SetPolicyAssurance:
        case Action::AssignPolicyInspectors:
        case Action::ConstructWaterworks:
        case Action::TransferWaterworksFeed:
        case Action::ConstructReclaimer:
        case Action::ConstructClinic:
        case Action::TransferClinicStock:
        case Action::AssignClinicStaff:
        case Action::ConstructCommunity:
        case Action::TransferCommunityStock:
        case Action::AssignCommunityStaff:
        case Action::ConstructUtility:
        case Action::RepairUtility:
        case Action::TransferUtilitySpares:
        case Action::AssignUtilityTechnicians:
        case Action::SetUtilityMaintenance:
        case Action::ConstructHabitat:
        case Action::TransferHabitatStock:
        case Action::AssignHabitatResidents:
        case Action::ConstructFarm:
        case Action::TransferFarmStock:
        case Action::AssignFarmWorkers: {
            if(!colony_ships_.count(c.actor) || !life_support_.count(c.actor)){
                result.result=Outcome::NotColonyShip;return result;
            }
            const auto* site=find(c.target);
            if(!site || !built_outposts_.count(c.target) ||
               site->type!=EntityType::Station || site->owner_id!=actor.owner_id ||
               built_outposts_.at(c.target).source_colony_ship!=c.actor ||
               site->system_id!=actor.system_id){result.result=Outcome::InvalidTarget;return result;}
            if(actor.jump_state!=ShipJumpState::Ready ||
               (actor.dock_state!=DockState::InSpace && actor.dock_station!=c.target)){
                result.result=Outcome::InTransition;return result;
            }
            auto dx=actor.position.x-site->position.x,dy=actor.position.y-site->position.y;
            if(dx < -kDockingRangeMm || dx>kDockingRangeMm ||
               dy < -kDockingRangeMm || dy>kDockingRangeMm ||
               dx*dx+dy*dy>kDockingRangeMm*kDockingRangeMm){
                result.result=Outcome::OutOfRange;return result;
            }
            auto& cship=colony_ships_.at(c.actor);
            auto& onboard=life_support_.at(c.actor);
            if(c.action==Action::PlaceConveyorCell || c.action==Action::CompleteConveyorRoute){
                auto it=storages_.find(c.target);
                if(it==storages_.end() || it->second.source_ship!=c.actor ||
                   !it->second.links.count(static_cast<std::uint64_t>(c.amount))){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto id=static_cast<std::uint64_t>(c.amount);
                auto proposed=it->second;
                auto& link=proposed.links.at(id);
                if(link.route_ready || !proposed.equipment_positions.count(link.source) ||
                   !proposed.equipment_positions.count(link.destination)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                if(c.action==Action::PlaceConveyorCell){
                    if(link.route_cells.size()>=4096){result.result=Outcome::InvalidArgument;return result;}
                    if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                    link.route_required=true;
                    link.route_cells.push_back({c.thrust_x,c.thrust_y});
                }else{
                    if(link.route_cells.empty()){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    link.route_required=true;
                    link.route_ready=true;
                }
                try{validate_site_conveyors(proposed);}
                catch(const std::exception&){result.result=Outcome::InvalidArgument;return result;}
                auto& original=it->second.links.at(id);
                original.route_required=link.route_required;
                original.route_ready=link.route_ready;
                original.route_cells=std::move(link.route_cells);
                if(c.action==Action::PlaceConveyorCell){
                    --cship.modules;result.transferred=2;
                }
                snapshot_version_=std::max(snapshot_version_,30);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::PlaceFactoryEquipment){
                auto st=storages_.find(c.target);
                const auto node=placement_node(c.amount);
                if(st==storages_.end() || st->second.source_ship!=c.actor ||
                   (node==kWorkshopConveyorNode ?
                     (!workshops_.count(c.target) || workshops_.at(c.target).source_ship!=c.actor) :
                     (node!=0 && !st->second.extra_bins.count(node))) ||
                   st->second.equipment_positions.count(node)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto proposed=st->second;
                proposed.equipment_positions.emplace(node,Vector2{c.thrust_x,c.thrust_y});
                try{validate_site_conveyors(proposed);}
                catch(const std::exception&){result.result=Outcome::InvalidArgument;return result;}
                st->second.equipment_positions.emplace(node,Vector2{c.thrust_x,c.thrust_y});
                snapshot_version_=std::max(snapshot_version_,29);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructStorage){
                if(storages_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                OutpostStorage bin{};bin.source_ship=c.actor;
                storages_.emplace(c.target,std::move(bin));
                --cship.modules;
                snapshot_version_=std::max(snapshot_version_,25);
                result.transferred=2;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructWorkshopLink){
                const auto si=storages_.find(c.target);
                const auto wi=workshops_.find(c.target);
                const auto node=static_cast<std::uint32_t>(c.thrust_x);
                const bool input=c.thrust_y==0;
                const auto item=static_cast<std::uint8_t>(c.amount%4);
                if(si==storages_.end() || wi==workshops_.end() ||
                   si->second.source_ship!=c.actor || wi->second.source_ship!=c.actor ||
                   (node && !si->second.extra_bins.count(node)) ||
                   (!input && item==2 && !wi->second.finisher_installed)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto from=input?node:kWorkshopConveyorNode;
                const auto to=input?kWorkshopConveyorNode:node;
                for(const auto& [id,link]:si->second.links){(void)id;
                    if(link.source==from && link.destination==to && link.item_index==item){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                }
                if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                const bool needs_route=si->second.equipment_positions.count(from) &&
                                       si->second.equipment_positions.count(to);
                OutpostStorageLink route{c.id,from,to,item,c.amount/4,0,false,false,{}};
                route.route_required=needs_route;
                si->second.links.emplace(c.id,std::move(route));
                --cship.modules;
                snapshot_version_=std::max(snapshot_version_,needs_route?30:28);
                result.transferred=2;result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructStorageLink){
                auto it=storages_.find(c.target);
                const auto src=static_cast<std::uint32_t>(c.thrust_x);
                const auto dst=static_cast<std::uint32_t>(c.thrust_y);
                if(it==storages_.end() || it->second.source_ship!=c.actor ||
                   (src && !it->second.extra_bins.count(src)) ||
                   (dst && !it->second.extra_bins.count(dst)) ||
                   it->second.links.count(c.id)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                // Acyclic transport graph: prevents free infinite recirculation.
                // Include all filters in cycle detection because future rate policies
                // may change the item allowed by a transport link.
                std::set<std::uint32_t> seen;
                std::vector<std::uint32_t> todo{dst};
                while(!todo.empty()){
                    auto node=todo.back();todo.pop_back();
                    if(!seen.insert(node).second)continue;
                    for(const auto& [id,link]:it->second.links){(void)id;
                        if(link.source==node)todo.push_back(link.destination);
                    }
                }
                for(const auto& [id,link]:it->second.links){(void)id;
                    if(link.source==src && link.destination==dst &&
                       link.item_index==c.amount%4){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                }
                if(seen.count(src)){
                    result.result=Outcome::InvalidArgument;return result;
                }
                if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                const bool needs_route=it->second.equipment_positions.count(src) &&
                                       it->second.equipment_positions.count(dst);
                OutpostStorageLink route{c.id,src,dst,static_cast<std::uint8_t>(c.amount%4),c.amount/4,0,false,false,{}};
                route.route_required=needs_route;
                it->second.links.emplace(c.id,std::move(route));
                --cship.modules;
                snapshot_version_=std::max(snapshot_version_,needs_route?30:27);
                result.transferred=2;result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructStorageNode){
                auto it=storages_.find(c.target);
                if(it==storages_.end() || it->second.source_ship!=c.actor ||
                   it->second.extra_bins.count(static_cast<std::uint32_t>(c.amount))){
                    result.result=Outcome::InvalidTarget;return result;
                }
                if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                it->second.extra_bins.emplace(static_cast<std::uint32_t>(c.amount),
                                               std::map<std::string,std::int64_t>{});
                --cship.modules;
                snapshot_version_=std::max(snapshot_version_,26);
                result.transferred=2;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferStorageNodeStack){
                const std::uint32_t node=static_cast<std::uint32_t>(c.thrust_x/4);
                const int item=c.thrust_x%4;
                auto it=storages_.find(c.target);
                if(it==storages_.end() || it->second.source_ship!=c.actor ||
                   !it->second.extra_bins.count(node)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                const std::string name=names[item],code="good/"+name;
                auto bin=storage_node_inventory_snapshot(c.target,node);
                const bool use_workshop=c.thrust_y>=2;
                auto wi=workshops_.find(c.target);
                if(use_workshop && (wi==workshops_.end() || wi->second.source_ship!=c.actor ||
                                    (item==2 && !wi->second.finisher_installed))){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto other=use_workshop ? workshop_bin_inventory_snapshot(c.target,item) :
                    ship_inventory_snapshot(c.actor);
                const bool inbound=(c.thrust_y==0 || c.thrust_y==2);
                const auto status=inbound ? other.transfer_to(bin,code,c.amount) :
                                            bin.transfer_to(other,code,c.amount);
                if(status!=inventory::TransferStatus::Applied){
                    result.result=status==inventory::TransferStatus::DestinationFull ?
                        Outcome::CargoFull:Outcome::NoResources;return result;
                }
                auto& stacks=it->second.extra_bins.at(node);
                const auto quantity=bin.amount(code);
                if(quantity)stacks[name]=quantity;else stacks.erase(name);
                if(use_workshop){
                    auto& w=wi->second;
                    std::int64_t* slots[]={&w.aluminum_kg,&w.panels_kg,&w.frames_kg,&w.scrap_kg};
                    *slots[item]=other.amount(code);
                }else{
                    const auto qty=other.amount(code);
                    if(qty)cship.goods_kg[name]=qty;else cship.goods_kg.erase(name);
                }
                snapshot_version_=std::max(snapshot_version_,26);
                result.transferred=c.amount;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferStorageStack){
                auto it=storages_.find(c.target);
                if(it==storages_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                const std::string name=names[c.thrust_x],code="good/"+name;
                auto bin=storage_inventory_snapshot(c.target);
                const bool use_workshop=c.thrust_y>=2;
                auto wi=workshops_.find(c.target);
                if(use_workshop && (wi==workshops_.end() || wi->second.source_ship!=c.actor ||
                                    (c.thrust_x==2 && !wi->second.finisher_installed))){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto other=use_workshop ? workshop_bin_inventory_snapshot(c.target,c.thrust_x) :
                    ship_inventory_snapshot(c.actor);
                const bool inbound=(c.thrust_y==0 || c.thrust_y==2);
                auto status=inbound ? other.transfer_to(bin,code,c.amount) :
                                      bin.transfer_to(other,code,c.amount);
                if(status!=inventory::TransferStatus::Applied){
                    result.result=status==inventory::TransferStatus::DestinationFull ? Outcome::CargoFull : Outcome::NoResources;
                    return result;
                }
                // Commit the validated value-copy amounts. Preserve every other item stack.
                const auto binQty=bin.amount(code);
                if(binQty)it->second.goods_kg[name]=binQty;
                else it->second.goods_kg.erase(name);
                if(use_workshop){
                    auto& w=wi->second;
                    std::int64_t* slots[]={&w.aluminum_kg,&w.panels_kg,&w.frames_kg,&w.scrap_kg};
                    *slots[c.thrust_x]=other.amount(code);
                }else{
                    const auto qty=other.amount(code);
                    if(qty)cship.goods_kg[name]=qty;
                    else cship.goods_kg.erase(name);
                }
                snapshot_version_=std::max(snapshot_version_,25);
                result.transferred=c.amount;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructWorkshop){
                if(workshops_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                --cship.modules;
                OutpostWorkshop w{};w.source_ship=c.actor;
                workshops_.emplace(c.target,w);
                snapshot_version_=std::max(snapshot_version_,22);
                result.transferred=2;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::StartWorkshopShift ||
               c.action==Action::CancelWorkshopShift ||
               c.action==Action::RelieveWorkshopShift ||
               c.action==Action::AppointWorkshopSupervisor ||
               c.action==Action::MentorWorkshopTrade ||
               c.action==Action::ApproveWorkshopJob ||
               c.action==Action::CancelWorkshopJob ||
               c.action==Action::TrainWorkshopTrade ||
               c.action==Action::TransferWorkshopFeed ||
               c.action==Action::AssignWorkshopOperator ||
               c.action==Action::TransferWorkshopOutput ||
               c.action==Action::ConstructWorkshopFinisher ||
               c.action==Action::AssignWorkshopFinisher ||
               c.action==Action::TransferWorkshopFinalOutput ||
               c.action==Action::TransferWorkshopStack){
                auto wi=workshops_.find(c.target);
                if(wi==workshops_.end() || wi->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& w=wi->second;
                const auto occupied_native_shipyard=[&](std::uint64_t who){
                    for(const auto& [id,yard]:shipyard_constructions_){
                        (void)id;
                        if(!yard.operational() &&
                           (yard.workers[0]==who || yard.workers[1]==who))
                            return true;
                    }
                    for(const auto& [id,build]:freighter_build_orders_){
                        (void)id;
                        if(build.finished())continue;
                        const auto yard=shipyard_constructions_.find(build.yard_order);
                        if(yard!=shipyard_constructions_.end() &&
                           (yard->second.workers[0]==who ||
                            yard->second.workers[1]==who))
                            return true;
                    }
                    return false;
                };
                // WO2-J appointment, mentorship and job policy are original
                // site-owned actions; they cannot bypass workshop execution.
                if(c.action==Action::AppointWorkshopSupervisor){
                    if(w.last_mentored_tick==tick_){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const auto trade=static_cast<WorkshopTrade>(c.thrust_x);
                    const auto id=static_cast<std::uint64_t>(c.amount);
                    if(!id){
                        if(!w.supervisor_npc_id){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        w.supervisor_npc_id=0;w.supervisor_appointment_id=0;
                        w.approved_jobs={};
                        result.result=Outcome::Applied;return result;
                    }
                    const auto n=npcs_.find(id);
                    const auto h=habitats_.find(c.target);
                    if((trade==WorkshopTrade::Shape && !w.finisher_installed) ||
                       n==npcs_.end() || h==habitats_.end() ||
                       h->second.source_ship!=c.actor || h->second.residents<=0 ||
                       n->second.home_ship!=c.actor ||
                       n->second.location!=c.target ||
                       n->second.assigned_site!=c.target ||
                       n->second.role!=NpcRole::Habitat ||
                       n->second.mentor_id || n->second.transport_id ||
                       !npc_available_for_duty(id) ||
                       occupied_native_shipyard(id) ||
                       pending_workshop_worker_duty(id,tick_) ||
                       !workshop_trade_certified(id,trade)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    for(const auto& [site,shop]:workshops_){
                        if(shop.supervisor_npc_id==id && site!=c.target){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        if(shop.operator_npc_id==id || shop.finisher_npc_id==id){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    for(const auto& [rid,shift]:medic_shifts_){
                        (void)rid;
                        if(shift.medic_id==id && shift.end_tick>tick_){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    if(w.supervisor_npc_id==id && w.supervisor_trade==trade){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    // Manager replacement invalidates prior owner-approved
                    // board entries. Historical applied receipts remain.
                    w.supervisor_npc_id=id;w.supervisor_trade=trade;
                    w.supervisor_appointment_id=c.id;
                    w.approved_jobs={};
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::ApproveWorkshopJob ||
                   c.action==Action::CancelWorkshopJob){
                    const auto slot=static_cast<std::size_t>(c.thrust_x-1);
                    auto& job=w.approved_jobs[slot];
                    if(c.action==Action::CancelWorkshopJob){
                        if(!job.command_id ||
                           job.command_id!=static_cast<std::uint64_t>(c.amount)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        job={};
                        result.result=Outcome::Applied;return result;
                    }
                    const auto supervisor=npcs_.find(w.supervisor_npc_id);
                    // An appointment does not reserve a separate person:
                    // other native duties may have changed since it was
                    // issued. Recheck the same finite worker at job approval.
                    bool supervisor_double_booked=false;
                    for(const auto& [other_site,other]:workshops_){
                        (void)other_site;
                        if(other.operator_npc_id==w.supervisor_npc_id ||
                           other.finisher_npc_id==w.supervisor_npc_id)
                            supervisor_double_booked=true;
                    }
                    for(const auto& [shift_id,shift]:medic_shifts_){
                        (void)shift_id;
                        if(shift.medic_id==w.supervisor_npc_id &&
                           shift.start_tick<=tick_ && tick_<shift.end_tick)
                            supervisor_double_booked=true;
                    }
                    if(job.command_id || !w.supervisor_npc_id ||
                       supervisor_double_booked ||
                       !w.supervisor_appointment_id ||
                       std::any_of(events_.begin(),events_.end(),
                         [&](const Event& e){return e.command_id==w.supervisor_appointment_id &&
                           e.tick==tick_ && e.result==Outcome::Applied;}) ||
                       supervisor==npcs_.end() ||
                       supervisor->second.home_ship!=c.actor ||
                       supervisor->second.location!=c.target ||
                       supervisor->second.assigned_site!=c.target ||
                       supervisor->second.role!=NpcRole::Habitat ||
                       !npc_available_for_duty(w.supervisor_npc_id) ||
                       occupied_native_shipyard(w.supervisor_npc_id) ||
                       w.supervisor_trade!=static_cast<WorkshopTrade>(c.thrust_x) ||
                       (slot==1 && !w.finisher_installed)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const auto current=slot==0?w.completed_batches:w.finisher_batches;
                    job.command_id=c.id;
                    job.supervisor_appointment_id=w.supervisor_appointment_id;
                    job.supervisor_npc_id=w.supervisor_npc_id;
                    job.approved_tick=tick_;
                    job.target_batches=static_cast<std::uint32_t>(c.amount);
                    job.baseline_batches=current;
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::StartWorkshopShift ||
                   c.action==Action::CancelWorkshopShift ||
                   c.action==Action::RelieveWorkshopShift){
                    const auto slot=static_cast<std::size_t>(c.thrust_x-1);
                    auto& sh=w.shifts[slot];
                    auto& assigned=slot==0?w.operator_npc_id:w.finisher_npc_id;
                    if(c.action==Action::CancelWorkshopShift){
                        if(sh.command_id==static_cast<std::uint64_t>(c.amount)){
                            if(assigned==sh.worker)assigned=0;
                            sh={};
                            result.result=Outcome::Applied;return result;
                        }
                        // A future accepted start still exists in the original
                        // pending queue. Cancel by original order ID, leaving
                        // that order's eventual rejected receipt intact.
                        if(w.cancelled_pending_shifts.size()>=256){
                            result.result=Outcome::InvalidArgument;return result;
                        }
                        bool found=false;
                        for(const auto& [due,orders]:commands_){
                            if(due<=tick_)continue;
                            for(const auto& pending:orders){
                                if(pending.id==static_cast<std::uint64_t>(c.amount) &&
                                   pending.action==Action::StartWorkshopShift &&
                                   pending.target==c.target &&
                                   pending.actor==c.actor &&
                                   pending.thrust_x==c.thrust_x){
                                    found=true;break;
                                }
                            }
                            if(found)break;
                        }
                        if(!found ||
                           !w.cancelled_pending_shifts.insert(
                                static_cast<std::uint64_t>(c.amount)).second){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        result.result=Outcome::Applied;return result;
                    }
                    if(c.action==Action::RelieveWorkshopShift){
                        const auto replacement=static_cast<std::uint64_t>(c.thrust_y);
                        const auto person=npcs_.find(replacement);
                        const auto h=habitats_.find(c.target);
                        if(sh.command_id!=static_cast<std::uint64_t>(c.amount) ||
                           !sh.in_effect(tick_) || assigned!=sh.worker ||
                           sh.worker==replacement || person==npcs_.end() ||
                           h==habitats_.end() || h->second.source_ship!=c.actor ||
                           h->second.residents<=0 ||
                           person->second.home_ship!=c.actor ||
                           person->second.location!=c.target ||
                           person->second.assigned_site!=c.target ||
                           person->second.role!=NpcRole::Habitat ||
                           person->second.mentor_id || person->second.transport_id ||
                           !npc_available_for_duty(replacement)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        // No two handovers in one committed tick, even when
                        // the command IDs and both replacement workers differ.
                        if(sh.relief_command_id &&
                           std::any_of(events_.begin(),events_.end(),
                              [&](const Event& e){return e.command_id==sh.relief_command_id &&
                                  e.tick==tick_ && e.result==Outcome::Applied;})){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        for(const auto& [id,shop]:workshops_){
                            (void)id;
                            if(shop.operator_npc_id==replacement ||
                               shop.finisher_npc_id==replacement ||
                               shop.supervisor_npc_id==replacement){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                            for(const auto& duty:shop.shifts)
                                if(duty.command_id && duty.worker==replacement &&
                                   tick_<duty.end_tick && duty.start_tick<sh.end_tick){
                                    result.result=Outcome::InvalidTarget;return result;
                                }
                        }
                        for(const auto& [id,shift]:medic_shifts_){
                            (void)id;
                            if(shift.medic_id==replacement &&
                               tick_<shift.end_tick && shift.start_tick<sh.end_tick){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                        for(const auto& [id,yard]:shipyard_constructions_){
                            (void)id;
                            if(!yard.operational() &&
                               (yard.workers[0]==replacement ||
                                yard.workers[1]==replacement)){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                        for(const auto& [id,build]:freighter_build_orders_){
                            (void)id;
                            if(build.finished())continue;
                            const auto yard=shipyard_constructions_.find(build.yard_order);
                            if(yard!=shipyard_constructions_.end() &&
                               (yard->second.workers[0]==replacement ||
                                yard->second.workers[1]==replacement)){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                        if(std::any_of(person->second.industrial_training.begin(),
                                       person->second.industrial_training.end(),
                           [&](const NpcTradeTraining& t){return t.tick==tick_;})){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        for(const auto& [due,orders]:commands_){
                            if(due<tick_)continue;
                            for(const auto& pending:orders){
                                if(pending.action!=Action::StartWorkshopShift ||
                                   pending.amount!=static_cast<std::int64_t>(replacement))
                                    continue;
                                const auto ps=workshops_.find(pending.target);
                                if(ps!=workshops_.end() &&
                                   ps->second.cancelled_pending_shifts.count(pending.id))
                                    continue;
                                if(due<sh.end_tick &&
                                   due+static_cast<std::uint64_t>(pending.thrust_y)>tick_){
                                    result.result=Outcome::InvalidTarget;return result;
                                }
                            }
                        }
                        // One atomic change: leave physical WIP, original
                        // duration, input and energy untouched.
                        assigned=replacement;
                        sh.worker=replacement;
                        sh.relief_command_id=c.id;
                        result.result=Outcome::Applied;return result;
                    }
                    if(w.cancelled_pending_shifts.erase(c.id)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const auto worker=static_cast<std::uint64_t>(c.amount);
                    const auto npc=npcs_.find(worker);
                    const auto hab=habitats_.find(c.target);
                    if(sh.command_id || assigned || (slot==1 && !w.finisher_installed) ||
                       w.supervisor_npc_id==worker ||
                       npc==npcs_.end() || hab==habitats_.end() ||
                       hab->second.source_ship!=c.actor || hab->second.residents<=0 ||
                       npc->second.home_ship!=c.actor ||
                       npc->second.location!=c.target ||
                       npc->second.assigned_site!=c.target ||
                       npc->second.role!=NpcRole::Habitat ||
                       npc->second.mentor_id || npc->second.transport_id ||
                       !npc_available_for_duty(worker)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    for(const auto& [other_site,shop]:workshops_){
                        (void)other_site;
                        if(shop.operator_npc_id==worker || shop.finisher_npc_id==worker ||
                           shop.supervisor_npc_id==worker){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        for(const auto& other:shop.shifts)
                            if(other.command_id && other.worker==worker){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                    }
                    for(const auto& [id,shift]:medic_shifts_){
                        (void)id;
                        if(shift.medic_id==worker && tick_<shift.end_tick &&
                           shift.start_tick<tick_+static_cast<std::uint64_t>(c.thrust_y)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    // Native shipyard duties are physical work, not an
                    // independent person pool. An active construction crew
                    // cannot be scheduled onto the installed workshop.
                    for(const auto& [id,yard]:shipyard_constructions_){
                        (void)id;
                        if(!yard.operational() &&
                           (yard.workers[0]==worker || yard.workers[1]==worker)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    for(const auto& [id,build]:freighter_build_orders_){
                        (void)id;
                        if(build.finished())continue;
                        const auto yard=shipyard_constructions_.find(build.yard_order);
                        if(yard!=shipyard_constructions_.end() &&
                           (yard->second.workers[0]==worker ||
                            yard->second.workers[1]==worker)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    if(snapshot_version_>=105 &&
                       std::any_of(npc->second.industrial_training.begin(),
                                   npc->second.industrial_training.end(),
                          [&](const NpcTradeTraining& t){return t.tick==tick_;})){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    sh={c.id,worker,tick_,
                        tick_+static_cast<std::uint64_t>(c.thrust_y)};
                    assigned=worker;
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::TrainWorkshopTrade ||
                   c.action==Action::MentorWorkshopTrade){
                    // Authoritative execution rechecks the owner, custody,
                    // installed station, real supply and duty. A queued order
                    // may become invalid between approval and this tick.
                    const auto trade=static_cast<WorkshopTrade>(c.thrust_x);
                    const auto npc_id=static_cast<std::uint64_t>(c.amount);
                    auto ni=npcs_.find(npc_id);
                    const auto h=habitats_.find(c.target);
                    if(ni==npcs_.end() || h==habitats_.end() ||
                       h->second.source_ship!=c.actor || h->second.residents<=0 ||
                       ni->second.home_ship!=c.actor ||
                       ni->second.location!=c.target ||
                       ni->second.assigned_site!=c.target ||
                       ni->second.role!=NpcRole::Habitat ||
                       ni->second.mentor_id || ni->second.transport_id ||
                       (trade==WorkshopTrade::Shape && !w.finisher_installed) ||
                       !npc_available_for_duty(npc_id)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const bool mentoring=c.action==Action::MentorWorkshopTrade;
                    std::uint64_t mentor_id=0,mentor_appointment=0;
                    if(mentoring){
                        mentor_id=w.supervisor_npc_id;
                        mentor_appointment=w.supervisor_appointment_id;
                        const auto mentor=npcs_.find(mentor_id);
                        if(!mentor_id || !mentor_appointment || mentor_id==npc_id ||
                           w.supervisor_trade!=trade || mentor==npcs_.end() ||
                           w.last_mentored_tick==tick_ ||
                           std::any_of(events_.begin(),events_.end(),
                             [&](const Event& e){
                               return e.command_id==mentor_appointment &&
                                      e.tick==tick_ && e.result==Outcome::Applied;}) ||
                           !workshop_trade_certified(mentor_id,trade) ||
                           mentor->second.home_ship!=c.actor ||
                           mentor->second.location!=c.target ||
                           mentor->second.assigned_site!=c.target ||
                           mentor->second.role!=NpcRole::Habitat ||
                           mentor->second.transport_id || mentor->second.mentor_id ||
                           !npc_available_for_duty(mentor_id) ||
                           occupied_native_shipyard(mentor_id)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        for(const auto& [site,shop]:workshops_){
                            (void)site;
                            if(shop.operator_npc_id==mentor_id ||
                               shop.finisher_npc_id==mentor_id){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                        for(const auto& [id,shift]:medic_shifts_){
                            (void)id;
                            if(shift.medic_id==mentor_id && shift.end_tick>tick_){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                    }
                    unsigned trained=0;
                    for(const auto& credit:ni->second.industrial_training){
                        // Original NPC cannot perform two separate training
                        // sessions in the same logical tick, even for
                        // different trades or distinct command IDs.
                        if(credit.tick==tick_){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        if(credit.trade==trade)++trained;
                    }
                    if(trained>=3 || ni->second.industrial_training.size()>=6){
                        result.result=Outcome::InvalidArgument;return result;
                    }
                    for(const auto& [other_site,shop]:workshops_){
                        (void)other_site;
                        if(shop.operator_npc_id==npc_id ||
                           shop.finisher_npc_id==npc_id ||
                           shop.supervisor_npc_id==npc_id){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    for(const auto& [id,shift]:medic_shifts_){
                        (void)id;
                        if(shift.medic_id==npc_id && shift.end_tick>tick_){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    for(const auto& [id,yard]:shipyard_constructions_){
                        (void)id;
                        if(!yard.operational() &&
                           (yard.workers[0]==npc_id || yard.workers[1]==npc_id)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    for(const auto& [id,build]:freighter_build_orders_){
                        (void)id;
                        if(build.finished())continue;
                        const auto yard=shipyard_constructions_.find(build.yard_order);
                        if(yard!=shipyard_constructions_.end() &&
                           (yard->second.workers[0]==npc_id ||
                            yard->second.workers[1]==npc_id)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                    }
                    auto utility=utilities_.find(c.target);
                    if(utility==utilities_.end() || !utility->second.powered ||
                       utility->second.condition<=0 ||
                       utility->second.stored_energy<1 ||
                       utility->second.consumed_energy==std::numeric_limits<std::int64_t>::max()){
                        result.result=Outcome::NoResources;return result;
                    }
                    auto& feed=trade==WorkshopTrade::Cut?w.aluminum_kg:w.panels_kg;
                    // One kg of physical stock becomes one kg of recoverable
                    // machining practice scrap. No matter disappears, and
                    // training can't bypass native workshop inventory limits.
                    if(feed<1 || w.scrap_kg>=1000){
                        result.result=Outcome::NoResources;return result;
                    }
                    --feed;++w.scrap_kg;
                    --utility->second.stored_energy;
                    ++utility->second.consumed_energy;
                    ni->second.industrial_training.push_back(
                        NpcTradeTraining{c.id,tick_,c.target,trade,
                                         mentor_id,mentor_appointment});
                    if(mentoring)w.last_mentored_tick=tick_;
                    result.transferred=1;
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::ConstructWorkshopFinisher){
                    if(w.finisher_installed){result.result=Outcome::InvalidTarget;return result;}
                    if(cship.modules<1){result.result=Outcome::NoResources;return result;}
                    --cship.modules;
                    w.finisher_installed=true;
                    w.embodied_ore+=2;
                    snapshot_version_=std::max(snapshot_version_,23);
                    result.transferred=2;
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::AssignWorkshopOperator ||
                   c.action==Action::AssignWorkshopFinisher){
                    const bool second=c.action==Action::AssignWorkshopFinisher;
                    if(second && !w.finisher_installed){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    if(w.shifts[second?1:0].command_id){
                        // A finite approved shift, not a legacy permanent
                        // worker slot. Relief requires explicit cancellation.
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    if(c.amount){
                        auto npc=npcs_.find(static_cast<std::uint64_t>(c.amount));
                        if(npc==npcs_.end() || npc->second.home_ship!=c.actor ||
                           npc->second.location!=c.target || npc->second.role!=NpcRole::Habitat ||
                           !habitats_.count(c.target)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        // WO2 enforce this at the ORIGINAL execution boundary,
                        // not only in a host planning screen. A stale scheduled
                        // order must not duplicate a traveling, injured, or
                        // medically committed physical worker.
                        const auto worker=static_cast<std::uint64_t>(c.amount);
                        if(snapshot_version_>=40 && !npc_available_for_duty(worker)){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        // WO2-H: practice consumes this named NPC's hands for
                        // the entire current tick. A later command on the same
                        // committed tick cannot double-book them as an operator.
                        if(snapshot_version_>=105 &&
                           std::any_of(npc->second.industrial_training.begin(),
                                       npc->second.industrial_training.end(),
                              [&](const NpcTradeTraining& t){return t.tick==tick_;})){
                            result.result=Outcome::InvalidTarget;return result;
                        }
                        for(const auto& [id,existing]:workshops_){
                            // Preserve the original harmless same-slot
                            // idempotent assignment; reject ONLY a second
                            // distinct simultaneous physical duty.
                            const bool other_operator=existing.operator_npc_id==worker &&
                                !(id==c.target && !second);
                            const bool other_finisher=existing.finisher_npc_id==worker &&
                                !(id==c.target && second);
                            if(other_operator || other_finisher ||
                               existing.supervisor_npc_id==worker){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                        for(const auto& [id,shift]:medic_shifts_){(void)id;
                            if(shift.medic_id==worker && shift.end_tick>tick_){
                                result.result=Outcome::InvalidTarget;return result;
                            }
                        }
                    }
                    // Tier 1 devices require exclusive hands-on NPC labor.
                    if(c.amount && static_cast<std::uint64_t>(c.amount)==
                       (second?w.operator_npc_id:w.finisher_npc_id)){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    if(second)w.finisher_npc_id=static_cast<std::uint64_t>(c.amount);
                    else w.operator_npc_id=static_cast<std::uint64_t>(c.amount);
                    result.result=Outcome::Applied;return result;
                }
                // ECON-02B2a: catalogue-validated, volume-bounded transfers.
                // First operate on value-copy inventory views. Only commit to
                // PersistentWorld's authoritative fields if both sides accept.
                if(c.action==Action::TransferWorkshopStack){
                    if(c.thrust_x==2 && !w.finisher_installed){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const auto qty=std::abs(c.amount);
                    const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                    std::int64_t* local[]={&w.aluminum_kg,&w.panels_kg,&w.frames_kg,&w.scrap_kg};
                    const std::string name=names[c.thrust_x];
                    const std::string code="good/"+name;
                    auto shipstore=ship_inventory_snapshot(actor.id);
                    auto bin=workshop_bin_inventory_snapshot(c.target,c.thrust_x);
                    const bool inbound=c.amount>0;
                    const auto status=inbound ? shipstore.transfer_to(bin,code,qty) :
                                               bin.transfer_to(shipstore,code,qty);
                    if(status!=inventory::TransferStatus::Applied){
                        result.result=(!inbound && status==inventory::TransferStatus::DestinationFull) ?
                                      Outcome::CargoFull : Outcome::NoResources;
                        return result;
                    }
                    const auto remaining=shipstore.amount(code);
                    if(remaining)cship.goods_kg[name]=remaining;
                    else cship.goods_kg.erase(name);
                    *local[c.thrust_x]=bin.amount(code);
                    result.transferred=qty;
                    snapshot_version_=std::max(snapshot_version_,24);
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::TransferWorkshopFeed){
                    const auto qty=std::abs(c.amount);
                    if(c.amount>0){
                        auto it=cship.goods_kg.find("aluminum");
                        if(it==cship.goods_kg.end() || it->second<qty || w.aluminum_kg>1000-qty){
                            result.result=Outcome::NoResources;return result;
                        }
                        it->second-=qty;if(!it->second)cship.goods_kg.erase(it);
                        w.aluminum_kg+=qty;
                    }else{
                        if(w.aluminum_kg<qty || loaded_cargo_liters(actor)>actor.cargo_capacity*1000-qty){
                            result.result=Outcome::NoResources;return result;
                        }
                        w.aluminum_kg-=qty;cship.goods_kg["aluminum"]+=qty;
                    }
                    result.transferred=qty;
                    result.result=Outcome::Applied;return result;
                }
                if(c.action==Action::TransferWorkshopFinalOutput){
                    if(!w.finisher_installed){result.result=Outcome::InvalidTarget;return result;}
                    // 50 finisher batches = 950kg frames + 50kg scrap.
                    // The shared scrap bin must contain all withdrawn scrap;
                    // residual first-stage scrap remains at the outpost.
                    const auto batches=c.amount;
                    const auto frames=19*batches,scrap=batches;
                    if(w.frames_kg<frames || w.scrap_kg<scrap ||
                       loaded_cargo_liters(actor)>actor.cargo_capacity*1000-20*batches){
                        result.result=Outcome::NoResources;return result;
                    }
                    w.frames_kg-=frames;w.scrap_kg-=scrap;
                    cship.goods_kg["aluminum_frames"]+=frames;
                    cship.goods_kg["aluminum_scrap"]+=scrap;
                    result.transferred=20*batches;
                    result.result=Outcome::Applied;return result;
                }
                // Palletized production lot: 50 * (19kg panels + 1kg scrap)
                // moves exactly 1t, preserving existing whole-tonne ship cargo.
                // Later item-level logistics will support smaller split loads.
                const auto batches=c.amount;
                const auto panels=19*batches, scrap=batches;
                if(w.panels_kg<panels || w.scrap_kg<scrap ||
                   loaded_cargo_liters(actor)>actor.cargo_capacity*1000-20*batches){
                    result.result=Outcome::NoResources;return result;
                }
                w.panels_kg-=panels;w.scrap_kg-=scrap;
                cship.goods_kg["aluminum_panels"]+=panels;
                cship.goods_kg["aluminum_scrap"]+=scrap;
                result.transferred=20*batches;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::AssignFarmMentor){
                if(snapshot_version_<20 || !farms_.count(c.target)){
                    result.result=Outcome::InvalidTarget;return result;
                }
                const auto mentor=npcs_.find(static_cast<std::uint64_t>(c.amount));
                if(mentor==npcs_.end() || mentor->second.home_ship!=c.actor ||
                   mentor->second.location!=c.target || mentor->second.role!=NpcRole::Farm){
                    result.result=Outcome::InvalidTarget;return result;
                }
                if(c.thrust_x==0){
                    for(auto& [id,n]:npcs_){(void)id;
                        if(n.mentor_id==mentor->first)n.mentor_id=0;
                    }
                    result.result=Outcome::Applied;return result;
                }
                const auto student=npcs_.find(static_cast<std::uint64_t>(c.thrust_x));
                if(student==npcs_.end() || student==mentor ||
                   student->second.home_ship!=c.actor || student->second.location!=c.target ||
                   student->second.role!=NpcRole::Farm ||
                   mentor->second.farm_xp<=student->second.farm_xp){
                    result.result=Outcome::InvalidTarget;return result;
                }
                for(const auto& [id,n]:npcs_){(void)id;
                    if(n.mentor_id==mentor->first && n.id!=student->first){
                        result.result=Outcome::NoResources;return result;
                    }
                }
                student->second.mentor_id=mentor->first;
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::SetPolicyAssurance || c.action==Action::AssignPolicyInspectors){
                const auto h=habitats_.find(c.target);
                if(h==habitats_.end() || h->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto existing=policy_sites_.find(c.target);
                PolicySite temp=existing!=policy_sites_.end()?existing->second:PolicySite{};
                temp.source_ship=c.actor;
                auto& p=temp.domains[static_cast<std::size_t>(c.thrust_x)];
                if(c.action==Action::SetPolicyAssurance){
                    p.declared=static_cast<AssuranceLevel>(c.amount);
                }else{
                    const auto current=assigned_policy_inspectors(c.target);
                    const auto techs=utilities_.count(c.target)?utilities_.at(c.target).technicians:0;
                    const auto medics=clinics_.count(c.target)?clinics_.at(c.target).medics:0;
                    const auto attendants=communities_.count(c.target)?communities_.at(c.target).attendants:0;
                    if(c.amount+current-p.inspectors+techs+medics+attendants>h->second.residents){
                        result.result=Outcome::NoResources;return result;
                    }
                    p.inspectors=c.amount;
                }
                policy_sites_[c.target]=temp;
                snapshot_version_=std::max(snapshot_version_,19);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructWaterworks){
                if(waterworks_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(!habitats_.count(c.target) || !utilities_.count(c.target) ||
                   cship.modules<2 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                cship.modules-=2;
                WaterworksFacility plant{};plant.source_ship=c.actor;
                waterworks_.emplace(c.target,plant);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,18);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferWaterworksFeed){
                auto it=waterworks_.find(c.target);
                if(it==waterworks_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& raw=cship.goods_kg["water"];
                auto& local=it->second.raw_feed_kg;
                const auto qty=std::abs(c.amount);
                auto& src=c.amount>0?raw:local;
                auto& dst=c.amount>0?local:raw;
                if(src<qty || dst>100000000-qty ||
                   (c.amount<0 && loaded_cargo_liters(actor)>actor.cargo_capacity*1000-qty)){
                    result.result=Outcome::NoResources;return result;
                }
                src-=qty;dst+=qty;
                if(!raw)cship.goods_kg.erase("water");
                result.transferred=qty;
                snapshot_version_=std::max(snapshot_version_,18);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructReclaimer){
                if(reclaimers_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(!habitats_.count(c.target) || !utilities_.count(c.target) ||
                   cship.modules<2 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                cship.modules-=2;
                ReclaimerFacility plant{};plant.source_ship=c.actor;
                reclaimers_.emplace(c.target,plant);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,17);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructClinic){
                if(clinics_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(!habitats_.count(c.target) || cship.modules<2 || cship.crew<2){
                    result.result=Outcome::NoResources;return result;
                }
                cship.modules-=2;
                ClinicFacility facility{};facility.source_ship=c.actor;
                clinics_.emplace(c.target,facility);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,16);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferClinicStock || c.action==Action::AssignClinicStaff){
                auto it=clinics_.find(c.target),end=clinics_.end();
                const auto hi=habitats_.find(c.target);
                if(it==end || it->second.source_ship!=c.actor || hi==habitats_.end()){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& clinic=it->second;
                if(c.action==Action::AssignClinicStaff){
                    const auto ui=utilities_.find(c.target);const auto ci=communities_.find(c.target);
                    const auto techs=ui==utilities_.end()?0:ui->second.technicians;
                    const auto hall=ci==communities_.end()?0:ci->second.attendants;
                    if(c.amount+techs+hall+assigned_policy_inspectors(c.target)>hi->second.residents){
                        result.result=Outcome::NoResources;return result;
                    }
                    clinic.medics=c.amount;
                }else{
                    auto& aboard=c.thrust_x==1?onboard.water_kg:onboard.nutrients_kg;
                    auto& local=c.thrust_x==1?clinic.water_kg:clinic.substrate_kg;
                    const auto qty=std::abs(c.amount);
                    auto& src=c.amount>0?aboard:local;
                    auto& dst=c.amount>0?local:aboard;
                    if(src<qty || dst>100000000-qty){result.result=Outcome::NoResources;return result;}
                    src-=qty;dst+=qty;result.transferred=qty;
                }
                snapshot_version_=std::max(snapshot_version_,16);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructCommunity){
                if(communities_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<2 || cship.crew<2 || !habitats_.count(c.target)){
                    result.result=Outcome::NoResources;return result;
                }
                cship.modules-=2;
                CommunityFacility hall{};hall.source_ship=c.actor;
                communities_.emplace(c.target,hall);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,15);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferCommunityStock || c.action==Action::AssignCommunityStaff){
                auto it=communities_.find(c.target);
                if(it==communities_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& hall=it->second;
                const auto hi=habitats_.find(c.target);
                if(hi==habitats_.end()) {result.result=Outcome::InvalidTarget;return result;}
                if(c.action==Action::AssignCommunityStaff){
                    const auto ui=utilities_.find(c.target);
                    const auto techs=ui==utilities_.end()?0:ui->second.technicians;
                    const auto cli=clinics_.find(c.target);
                    const auto medic=cli==clinics_.end()?0:cli->second.medics;
                    if(c.amount+techs+medic+assigned_policy_inspectors(c.target)>hi->second.residents){
                        result.result=Outcome::NoResources;return result;
                    }
                    hall.attendants=c.amount;
                }else{
                    auto& aboard=c.thrust_x==1?onboard.food_kg:onboard.water_kg;
                    auto& at_site=c.thrust_x==1?hall.food_kg:hall.water_kg;
                    const auto qty=std::abs(c.amount);
                    auto& src=c.amount>0?aboard:at_site;
                    auto& dst=c.amount>0?at_site:aboard;
                    if(src<qty || dst>100000000-qty){result.result=Outcome::NoResources;return result;}
                    src-=qty;dst+=qty;result.transferred=qty;
                }
                snapshot_version_=std::max(snapshot_version_,15);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructUtility){
                if(utilities_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<2 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                cship.modules-=2;
                UtilityFacility u{};u.source_ship=c.actor;
                utilities_.emplace(c.target,u);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,13);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::RepairUtility){
                auto it=utilities_.find(c.target);
                if(it==utilities_.end()||it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                if(cship.modules<1 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                if(it->second.condition>=100){result.result=Outcome::InvalidArgument;return result;}
                cship.modules-=1;it->second.embodied_ore+=2;
                it->second.condition=std::min<std::int64_t>(100,it->second.condition+70);
                ++it->second.repairs;
                result.transferred=2;
                snapshot_version_=std::max(snapshot_version_,13);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferUtilitySpares ||
               c.action==Action::AssignUtilityTechnicians ||
               c.action==Action::SetUtilityMaintenance){
                auto it=utilities_.find(c.target);
                if(it==utilities_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& u=it->second;
                if(c.action==Action::TransferUtilitySpares){
                    const auto n=std::abs(c.amount);
                    if(c.amount>0){
                        if(cship.modules<n || u.spare_modules>1000-n){
                            result.result=Outcome::NoResources;return result;
                        }
                        cship.modules-=n;u.spare_modules+=n;
                    }else{
                        if(u.spare_modules<n || cship.modules>1000000-n){
                            result.result=Outcome::NoResources;return result;
                        }
                        u.spare_modules-=n;cship.modules+=n;
                    }
                    result.transferred=2*n; // mass in ore-equivalent tonnes
                }else if(c.action==Action::AssignUtilityTechnicians){
                    const auto hi=habitats_.find(c.target);
                    if((hi==habitats_.end() && c.amount) || (hi!=habitats_.end() &&
                       c.amount>hi->second.residents)){
                        result.result=Outcome::NoResources;return result;
                    }
                    const auto hall=communities_.count(c.target)?communities_.at(c.target).attendants:0;
                    const auto medics=clinics_.count(c.target)?clinics_.at(c.target).medics:0;
                    if(hi!=habitats_.end() &&
                       hall+medics+c.amount+assigned_policy_inspectors(c.target)>hi->second.residents){
                        result.result=Outcome::NoResources;return result;
                    }
                    u.technicians=c.amount;
                }else u.delegated_maintenance=c.amount==1;
                snapshot_version_=std::max(snapshot_version_,14);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructHabitat){
                if(habitats_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<2 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                const auto fit=farms_.find(c.target);
                if(fit!=farms_.end() && fit->second.workers>8){result.result=Outcome::NoResources;return result;}
                cship.modules-=2;
                HabitatFacility h{};h.source_ship=c.actor;
                habitats_.emplace(c.target,h);
                result.transferred=4;
                snapshot_version_=std::max(snapshot_version_,12);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::TransferHabitatStock || c.action==Action::AssignHabitatResidents){
                auto it=habitats_.find(c.target);
                if(it==habitats_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& h=it->second;
                if(c.action==Action::AssignHabitatResidents){
                    const auto fit=farms_.find(c.target);
                    const auto farm_crew=fit==farms_.end()?0:fit->second.workers;
                    if(c.amount>0){
                        if(cship.crew<=c.amount || h.residents+c.amount+farm_crew>h.capacity ||
                           h.life.food_kg<(h.residents+c.amount)*3 ||
                           h.life.water_kg<(h.residents+c.amount)*3 ||
                           h.life.oxygen_kg<(h.residents+c.amount)*3){
                            result.result=Outcome::NoResources;return result;
                        }
                    }else if(h.residents<-c.amount){result.result=Outcome::NoResources;return result;}
                    const auto ui=utilities_.find(c.target);
                    const auto ci=communities_.find(c.target);
                    const auto community_staff=ci==communities_.end()?0:ci->second.attendants;
                    const auto clinic_staff=clinics_.count(c.target)?clinics_.at(c.target).medics:0;
                    const auto patients=clinics_.count(c.target)?clinics_.at(c.target).active_cases:0;
                    if(h.residents+c.amount+farm_crew<patients ||
                       h.residents+c.amount<(ui==utilities_.end()?0:ui->second.technicians)+community_staff+clinic_staff+assigned_policy_inspectors(c.target)){
                        result.result=Outcome::NoResources;return result;
                    }
                    if(snapshot_version_>=40 && c.amount>0){
                        std::int64_t ready=0;
                        for(const auto& [id,n]:npcs_)
                            if(n.home_ship==c.actor && n.role==NpcRole::Ship &&
                               n.assigned_site==c.actor && npc_available_for_duty(id))++ready;
                        if(ready<c.amount){result.result=Outcome::NoResources;return result;}
                    }
                    move_npc_records(c.actor,c.target,NpcRole::Habitat,c.amount);
                    cship.crew-=c.amount;h.residents+=c.amount;
                    h.consecutive_comfort_cycles=0;
                    h.life.consecutive_supplied_cycles=0;
                }else{
                    auto select=[](ColonyLifeSupport& l,int sel)->std::int64_t&{
                        return sel==1?l.food_kg:sel==2?l.water_kg:l.oxygen_kg;
                    };
                    auto& src=select(c.amount>0?onboard:h.life,c.thrust_x);
                    auto& dst=select(c.amount>0?h.life:onboard,c.thrust_x);
                    const auto qty=std::abs(c.amount);
                    if(src<qty || dst>100000000-qty){result.result=Outcome::NoResources;return result;}
                    src-=qty;dst+=qty;result.transferred=qty;
                }
                snapshot_version_=std::max(snapshot_version_,12);
                result.result=Outcome::Applied;return result;
            }
            if(c.action==Action::ConstructFarm){
                if(farms_.count(c.target)){result.result=Outcome::InvalidTarget;return result;}
                if(cship.modules<2 || cship.crew<2){result.result=Outcome::NoResources;return result;}
                cship.modules-=2;
                FarmFacility farm{};farm.source_ship=c.actor;
                farms_.emplace(c.target,farm);
                result.transferred=4; // tonnes embodied (same as 2 modules)
            }else{
                auto it=farms_.find(c.target);
                if(it==farms_.end() || it->second.source_ship!=c.actor){
                    result.result=Outcome::InvalidTarget;return result;
                }
                auto& farm=it->second;
                if(c.action==Action::AssignFarmWorkers){
                    if(c.amount>0){
                        // Reserve one ship pilot and three crew support cycles at the destination.
                        if(cship.crew<=c.amount || farm.workers+c.amount>8 ||
                           farm.life.food_kg<(farm.workers+c.amount)*3 ||
                           farm.life.water_kg<(farm.workers+c.amount)*3 ||
                           farm.life.oxygen_kg<(farm.workers+c.amount)*3){
                            result.result=Outcome::NoResources;return result;
                        }
                    }else if(farm.workers<-c.amount){
                        result.result=Outcome::NoResources;return result;
                    }
                    if(c.amount>0 && habitats_.count(c.target) &&
                       habitats_.at(c.target).residents+farm.workers+c.amount>
                          habitats_.at(c.target).capacity){
                        result.result=Outcome::NoResources;return result;
                    }
                    const auto cli=clinics_.find(c.target);
                    if(cli!=clinics_.end() && c.amount<0 &&
                       cli->second.active_cases>farm.workers+c.amount+
                           (habitats_.count(c.target)?habitats_.at(c.target).residents:0)){
                        result.result=Outcome::NoResources;return result;
                    }
                    if(snapshot_version_>=40 && c.amount>0){
                        std::int64_t ready=0;
                        for(const auto& [id,n]:npcs_)
                            if(n.home_ship==c.actor && n.role==NpcRole::Ship &&
                               n.assigned_site==c.actor && npc_available_for_duty(id))++ready;
                        if(ready<c.amount){result.result=Outcome::NoResources;return result;}
                    }
                    move_npc_records(c.actor,c.target,NpcRole::Farm,c.amount);
                    cship.crew-=c.amount;farm.workers+=c.amount;
                    farm.life.consecutive_supplied_cycles=0;
                    // Crew movement is recorded in world state, not physical material ledger.
                }else{
                    auto select=[](ColonyLifeSupport& l,int selector)->std::int64_t&{
                        switch(selector){
                            case 1:return l.food_kg;
                            case 2:return l.water_kg;
                            case 3:return l.oxygen_kg;
                            case 4:return l.nutrients_kg;
                            default:return l.co2_kg;
                        }
                    };
                    auto& src=select(c.amount>0?onboard:farm.life,c.thrust_x);
                    auto& dst=select(c.amount>0?farm.life:onboard,c.thrust_x);
                    const auto qty=std::abs(c.amount);
                    if(src<qty || dst>100000000-qty){result.result=Outcome::NoResources;return result;}
                    src-=qty;dst+=qty;result.transferred=qty; // kilograms
                }
            }
            snapshot_version_=std::max(snapshot_version_,11);
            result.result=Outcome::Applied;return result;
        }
        case Action::PlantCrops:
        case Action::HarvestCrops:
        case Action::RecycleWater:
        case Action::RecycleCompost:
        case Action::ProduceOxygen:
        case Action::LoadLifeWater: {
            auto it=life_support_.find(c.actor);
            if(it==life_support_.end() || !colony_ships_.count(c.actor)){
                result.result=Outcome::NotColonyShip;return result;
            }
            if(actor.jump_state!=ShipJumpState::Ready){result.result=Outcome::InTransition;return result;}
            auto& l=it->second;
            if(c.action==Action::PlantCrops){
                // Physical prototype: per batch, 2 kg water + 1 kg nutrient +
                // 1 kg captured CO2 becomes 3 kg developing biomass + 1 kg O2.
                // 5-tick maturation, max 300 kg of crops in shipboard beds.
                if(l.crops_kg>300-3*c.amount){result.result=Outcome::CargoFull;return result;}
                if(l.water_kg<2*c.amount || l.nutrients_kg<c.amount || l.co2_kg<c.amount){
                    result.result=Outcome::NoResources;return result;
                }
                l.water_kg-=2*c.amount;l.nutrients_kg-=c.amount;l.co2_kg-=c.amount;
                l.crops_kg+=3*c.amount;l.oxygen_kg+=c.amount;
                l.crops_ready_tick=tick_+5;
                result.transferred=4*c.amount;
            }else if(c.action==Action::HarvestCrops){
                if(tick_<l.crops_ready_tick || l.crops_kg<3*c.amount){
                    result.result=Outcome::NoResources;return result;
                }
                l.crops_kg-=3*c.amount;l.food_kg+=2*c.amount;
                l.compost_kg+=c.amount;
                l.harvested_food_kg+=2*c.amount;
                result.transferred=2*c.amount;
            }else if(c.action==Action::RecycleWater){
                if(l.wastewater_kg<c.amount){result.result=Outcome::NoResources;return result;}
                const auto restored=c.amount*9/10;
                l.wastewater_kg-=c.amount;l.water_kg+=restored;
                l.sludge_kg+=c.amount-restored;l.recovered_water_kg+=restored;
                result.transferred=restored;
            }else if(c.action==Action::RecycleCompost){
                // Simplified onboard bio-reactor. No new nutrients are created.
                if(l.compost_kg<c.amount){result.result=Outcome::NoResources;return result;}
                l.compost_kg-=c.amount;l.nutrients_kg+=c.amount;
                result.transferred=c.amount;
            }else if(c.action==Action::ProduceOxygen){
                // Game-balanced electrolysis: water mass -> O2 + stored hydrogen.
                if(l.water_kg<10*c.amount){result.result=Outcome::NoResources;return result;}
                l.water_kg-=10*c.amount;l.oxygen_kg+=8*c.amount;
                l.hydrogen_kg+=2*c.amount;result.transferred=8*c.amount;
            }else{ // deliver already-mined and refined water into life-support tanks
                auto& goods=colony_ships_.at(c.actor).goods_kg;
                auto g=goods.find("water");
                const auto kg=c.amount*1000;
                if(g==goods.end() || g->second<kg){result.result=Outcome::NoResources;return result;}
                g->second-=kg;if(!g->second)goods.erase(g);
                l.water_kg+=kg;result.transferred=kg;
            }
            result.result=Outcome::Applied;
            snapshot_version_=std::max(snapshot_version_,10);
            return result;
        }
        case Action::RefineMineral:
        case Action::FabricateMaterialModule:
        case Action::RefineOre:
        case Action::FabricateModule:
        case Action::ConstructOutpost: {
            auto it=colony_ships_.find(c.actor);
            if(it==colony_ships_.end()){result.result=Outcome::NotColonyShip;return result;}
            ColonyShip& industry=it->second;
            if(actor.jump_state!=ShipJumpState::Ready || actor.dock_state==DockState::Docking ||
               actor.dock_state==DockState::Undocking){result.result=Outcome::InTransition;return result;}
            if(industry.crew < (c.action==Action::ConstructOutpost ? 3 : 2)){
                result.result=Outcome::InsufficientCrew;return result;
            }
            if(c.action==Action::RefineMineral) {
                const char* phase=mineral_name(c.target);
                if(!phase || c.target==9 || c.amount<1 || c.amount>100000){result.result=Outcome::InvalidArgument;return result;}
                const auto recipe=separation(phase);
                if(!recipe.a){result.result=Outcome::InvalidArgument;return result;}
                auto& source=industry.minerals_kg[phase];
                if(source<c.amount){result.result=Outcome::NoResources;return result;}
                const auto first=c.amount*recipe.a_permille/1000;
                const auto second=c.amount*recipe.b_permille/1000;
                source-=c.amount;
                if(!source)industry.minerals_kg.erase(phase);
                industry.goods_kg[recipe.a]+=first;
                if(recipe.b)industry.goods_kg[recipe.b]+=second;
                industry.tailings_kg+=c.amount-first-second;
                result.result=Outcome::Applied;
                result.transferred=c.amount; // kilograms from mineral stock
                snapshot_version_=std::max(snapshot_version_,9);
                return result;
            }
            if(c.action==Action::FabricateMaterialModule){
                if(c.amount<1 || c.amount>1000 || (c.target!=1 && c.target!=2)){
                    result.result=Outcome::InvalidArgument;return result;
                }
                auto& material=industry.goods_kg[c.target==1?"structural_alloy":"aluminum"];
                const auto required=c.amount*2000; // kilograms, two tonne module
                if(material<required){result.result=Outcome::NoResources;return result;}
                material-=required;industry.modules+=c.amount;
                if(!material)industry.goods_kg.erase(c.target==1?"structural_alloy":"aluminum");
                result.result=Outcome::Applied;
                result.transferred=c.amount*2; // tonnes of formed functional modules
                snapshot_version_=std::max(snapshot_version_,9);
                return result;
            }
            if(c.amount<1 || c.amount>1000 || c.target!=0){result.result=Outcome::InvalidArgument;return result;}
            if(c.action==Action::RefineOre){
                if(actor.cargo<c.amount){result.result=Outcome::NoResources;return result;}
                actor.cargo-=c.amount;industry.refined+=c.amount;
                result.transferred=c.amount;
            }else if(c.action==Action::FabricateModule){
                if(industry.refined<c.amount*2){result.result=Outcome::NoResources;return result;}
                industry.refined-=c.amount*2;industry.modules+=c.amount;
                result.transferred=c.amount*2; // physical ore equivalents
            }else{
                if(actor.dock_state!=DockState::InSpace){result.result=Outcome::NotInSpace;return result;}
                if(actor.velocity.x!=0 || actor.velocity.y!=0){result.result=Outcome::ShipMoving;return result;}
                if(industry.modules<4){result.result=Outcome::NoResources;return result;}
                Vector2 site{actor.position.x+1000,actor.position.y};
                if(!valid_position(site)){result.result=Outcome::OutOfRange;return result;}
                for(const auto& [id,e]:entities_){
                    (void)id;
                    if(e.type==EntityType::Station && e.system_id==actor.system_id &&
                       e.position.x-site.x>=-2500 && e.position.x-site.x<=2500 &&
                       e.position.y-site.y>=-2500 && e.position.y-site.y<=2500){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                }
                industry.modules-=4;
                Entity station;station.type=EntityType::Station;
                station.owner_id=actor.owner_id;station.position=site;
                const auto id=add_entity(station);
                entities_.at(id).system_id=actor.system_id;
                built_outposts_.emplace(id,BuiltOutpost{actor.id,8});
                hangars_.emplace(std::make_pair(id,actor.owner_id),StationHangar{0,1000});
                result.transferred=8;
            }
            snapshot_version_=std::max(snapshot_version_,7);
            result.result=Outcome::Applied;return result;
        }
        default: result.result = Outcome::InvalidArgument; return result;
    }
}

void Simulation::process_automatic_mining() {
    // Stable ship-ID ordering makes simultaneous miners deterministic.
    std::vector<EntityId> ordered_actors;
    for (const auto& [id, job] : mining_) { (void)job; ordered_actors.push_back(id); }
    for (const auto actor_id : ordered_actors) {
        const auto current_job = mining_.find(actor_id);
        if (current_job == mining_.end()) continue; // despawn may have stopped it
        const auto job = current_job->second;
        const auto actor_it = entities_.find(actor_id);
        if (actor_it == entities_.end() || actor_it->second.type != EntityType::Ship) {
            stop_mining(actor_id, Outcome::InvalidActor);
            continue;
        }
        Entity& ship = actor_it->second;
        const auto status = mine_status(ship, job.target);
        if (status != Outcome::Applied) { stop_mining(actor_id, status); continue; }
        auto& rock = entities_.at(job.target);
        const auto n = std::min({job.amount_per_tick, rock.ore, (ship.cargo_capacity*1000-loaded_cargo_liters(ship))/1000});
        if(n>0 && colony_ships_.count(actor_id) &&
           (geology_.count(job.target)||local_deposit_taxonomy(job.target)))
            transfer_geological_minerals(actor_id,job.target,n);
        else ship.cargo += n;
        rock.ore -= n;
        snapshot_version_ = std::max(snapshot_version_, 2);
        activity_events_.push_back({tick_, ActivityKind::AutoMiningTransfer, job.start_command_id,
                                    actor_id, job.target, Outcome::Applied, n});
        const bool depleted = rock.ore == 0;
        if (depleted) despawn(job.target, actor_id, job.start_command_id);
        if (loaded_cargo_liters(ship) > ship.cargo_capacity*1000-1000) stop_mining(actor_id, Outcome::CargoFull);
        else if (depleted) stop_mining(actor_id, Outcome::NoResources);
    }
}

void Simulation::process_station_transitions() {
    for (auto& [id, ship] : entities_) {
        (void)id;
        if (ship.type != EntityType::Ship ||
            (ship.dock_state != DockState::Docking && ship.dock_state != DockState::Undocking)) continue;
        if (ship.transition_ticks == 0) throw std::logic_error("station transition missing countdown");
        if (--ship.transition_ticks != 0) continue;
        const auto station_it = entities_.find(ship.dock_station);
        if (station_it == entities_.end() || station_it->second.type != EntityType::Station || station_it->second.system_id!=ship.system_id)
            throw std::logic_error("station disappeared during transition");
        const auto station_id = ship.dock_station;
        if (ship.dock_state == DockState::Undocking) {
            ship.dock_state = DockState::InSpace;
            ship.dock_station = 0;
            ship.position.x = std::clamp(station_it->second.position.x + kUndockOffsetMm,
                                         -kWorldBoundMm, kWorldBoundMm);
            ship.position.y = station_it->second.position.y;
            activity_events_.push_back({tick_, ActivityKind::Undocked, 0, ship.id, station_id, Outcome::Applied, 0});
        } else {
            ship.dock_state = DockState::Docked;
            ship.position = station_it->second.position;
            activity_events_.push_back({tick_, ActivityKind::Docked, 0, ship.id, station_id, Outcome::Applied, 0});
        }
        ship.velocity = {};
        snapshot_version_ = std::max(snapshot_version_, 3);
    }
}

void Simulation::process_jump_transitions() {
    for(auto& [id,ship]:entities_) {
        if(ship.type!=EntityType::Ship || ship.jump_state==ShipJumpState::Ready)continue;
        if(!ship.jump_ticks)throw std::logic_error("missing jump countdown");
        if(--ship.jump_ticks)continue;
        if(ship.jump_state==ShipJumpState::ArrivalCloak){
            ship.jump_state=ShipJumpState::Ready;continue;
        }
        const auto* gate=find(ship.pending_gate);
        if(!gate || gate->type!=EntityType::Stargate || gate->system_id!=ship.system_id ||
           !systems_.count(gate->gate_destination))throw std::logic_error("committed jump lost endpoint");
        const auto destination=gate->gate_destination;
        const auto gate_id=ship.pending_gate;
        const auto sign=(id%2)?-1:1;
        ship.system_id=destination;
        if(snapshot_version_>=71)ship_frames_.at(id)=ShipFrameCustody{destination,0,{}};
        ship.position={gate->gate_arrival.x+sign*12000,gate->gate_arrival.y};
        ship.velocity={};ship.pending_gate=0;
        ship.jump_state=ShipJumpState::ArrivalCloak;ship.jump_ticks=kArrivalCloakTicks;
        // 1 physical entity whose system partition changes atomically in v5 snapshot.
        // Cargo remains on the same ship; station inventories never move.
        activity_events_.push_back({tick_,ActivityKind::JumpCompleted,0,id,gate_id,Outcome::Applied,0});
    }
}

void Simulation::process_gate_maintenance() {
    // Discrete, deterministic maintenance periods. No randomness and no free parts.
    if (tick_%10!=0) return;
    for (auto& [pair,op]:gate_ops_) {
        auto& a=entities_.at(pair.first);auto& b=entities_.at(pair.second);
        ++op.cycles;
        const bool running=a.gate_online && b.gate_online;
        // Unstaffed operating equipment wears faster. Offline equipment does not
        // incur operating wear, but existing damage remains and must be repaired.
        const auto wear=running ? (op.technicians==0 ? 25 : op.technicians==1 ? 12 : 6) : 0;
        op.condition=std::max<std::int64_t>(0,op.condition-wear);
        if(op.technicians && op.spare_parts && op.condition<1000){
            --op.spare_parts;++op.consumed_parts;
            op.condition=std::min<std::int64_t>(1000,op.condition+20*op.technicians);
        }
        if(op.condition==0){a.gate_online=false;b.gate_online=false;}
        // Repairs never restart a failed / administratively disabled gate.
        // Reopening requires a separate authorized operation.
    }
}
void Simulation::process_farms(){
    for(auto& [id,f]:farms_){(void)id;
        auto& l=f.life;
        if(tick_%20==0){
            const auto slot=static_cast<std::size_t>((tick_/20-1)%3);
            f.recent_food_kg[slot]=f.pending_food_kg;
            f.pending_food_kg=0;
            const auto staff=f.workers;
            if(staff){
                const auto fed=std::min(staff,l.food_kg),
                    watered=std::min(staff,l.water_kg),
                    breathed=std::min(staff,l.oxygen_kg);
                const bool supplied=fed==staff && watered==staff && breathed==staff;
                l.food_kg-=fed;l.water_kg-=watered;l.oxygen_kg-=breathed;
                l.organic_waste_kg+=fed;l.wastewater_kg+=watered;l.co2_kg+=breathed;
                ++l.cycles;
                if(supplied)++l.consecutive_supplied_cycles;
                else{l.consecutive_supplied_cycles=0;++l.shortages;}
            }
        }
        std::int64_t effective_workers=f.workers;
        if(snapshot_version_>=40){
            effective_workers=0;
            for(const auto& [npc_id,n]:npcs_)
                if(n.role==NpcRole::Farm && n.assigned_site==id &&
                   npc_available_for_duty(npc_id))++effective_workers;
        }
        if(!effective_workers)continue;
        if(utilities_.count(id) && !utilities_.at(id).powered)continue;
        if(l.crops_kg && tick_>=l.crops_ready_tick){
            // Consume mature biomass; every three kilograms give 2 food and 1 compost.
            const auto batches=l.crops_kg/3;
            l.crops_kg-=3*batches;
            l.food_kg+=2*batches;l.compost_kg+=batches;
            l.harvested_food_kg+=2*batches;f.pending_food_kg+=2*batches;
            ++f.production_cycles;
            award_farm_experience(id);
        }
        if(tick_%5!=0 || l.crops_kg)continue;
        // Process actual local waste. Ten per cent of recovered water becomes sludge.
        const auto processed=l.wastewater_kg-(l.wastewater_kg%10);
        if(processed){l.wastewater_kg-=processed;l.water_kg+=9*processed/10;
            l.sludge_kg+=processed/10;l.recovered_water_kg+=9*processed/10;}
        l.compost_kg+=l.organic_waste_kg;l.organic_waste_kg=0;
        l.nutrients_kg+=l.compost_kg;l.compost_kg=0;
        // One planted batch per worker per cycle (max eight), 5 ticks maturation.
        const auto batches=std::min({effective_workers,std::int64_t{8},l.water_kg/2,
            l.nutrients_kg,l.co2_kg});
        if(batches>0){
            l.water_kg-=2*batches;l.nutrients_kg-=batches;l.co2_kg-=batches;
            l.crops_kg+=3*batches;l.oxygen_kg+=batches;
            l.crops_ready_tick=tick_+5;
        }
    }
}
void Simulation::process_utilities(){
    for(auto& [site,u]:utilities_){
        const auto f=farms_.find(site);const auto h=habitats_.find(site);
        const auto workers=f==farms_.end()?0:f->second.workers;
        const auto residents=h==habitats_.end()?0:h->second.residents;
        // 3 for staffed hydroponics, 2 for occupied habitation, 1 for staffed community service.
        const auto ci=communities_.find(site);
        const bool community_active=ci!=communities_.end() && ci->second.attendants>0;
        const auto cli=clinics_.find(site);
        const bool clinic_active=cli!=clinics_.end() && cli->second.medics>0;
        const std::int64_t demand=(workers?3:0)+
            ((residents||workers) && h!=habitats_.end()?2:0)+(community_active?1:0)+(clinic_active?1:0)+(reclaimers_.count(site) && (residents||workers)?1:0)
            +(waterworks_.count(site) && (residents||workers) && waterworks_.at(site).raw_feed_kg>=10?1:0)
            +(workshops_.count(site) && workshops_.at(site).operator_npc_id &&
              (workshops_.at(site).aluminum_kg>=20 || workshops_.at(site).reserved_aluminum_kg)?1:0)
            +(workshops_.count(site) && workshops_.at(site).finisher_installed &&
              workshops_.at(site).finisher_npc_id &&
              (workshops_.at(site).panels_kg>=20 || workshops_.at(site).reserved_panels_kg)?1:0);
        if(u.condition>0){
            const auto solar=std::min<std::int64_t>(8,120-u.stored_energy);
            u.stored_energy+=solar;u.generated_energy+=solar;
            u.curtailed_energy+=8-solar;
        }
        u.powered=(u.stored_energy>=demand && (u.condition>0 || u.stored_energy>0));
        if(u.powered){u.stored_energy-=demand;u.consumed_energy+=demand;}
        else u.consecutive_powered_cycles=0; // no hidden intermittent brownouts
        if(tick_%20==0){
            ++u.cycles;
            // NPC maintenance is allowed only by standing policy and consumes a
            // module already delivered to THIS site. No remote ship is necessary.
            // AUTO-04D.3R: emergency self-preservation is distinct from an
            // owner-granted standing maintenance policy. A CRITICAL/EMERGENCY
            // occupied habitat may use EXISTING local technicians and
            // delivered spare modules despite disabled delegation.
            // No supplies, people or repairs are created by the forecast.
            bool emergency_override=false;
            if(u.condition<=40 && h!=habitats_.end() && h->second.residents>0)
                emergency_override=static_cast<int>(
                    colony_safety::assess(*this,u.source_ship,site).risk)>=
                    static_cast<int>(colony_safety::Risk::Critical);
            if((u.delegated_maintenance||emergency_override) && u.condition<=40){
                const bool staffed=u.technicians>0 && h!=habitats_.end() &&
                    h->second.residents>=u.technicians &&
                    h->second.life.consecutive_supplied_cycles>0;
                if(staffed && u.spare_modules>0){
                    --u.spare_modules;u.embodied_ore+=2;
                    u.condition=std::min<std::int64_t>(100,u.condition+70);
                    ++u.repairs;
                    if(u.delegated_maintenance)++u.delegated_repairs;
                }else ++u.blocked_maintenance_cycles;
            }
            if(demand>0 && u.powered)++u.consecutive_powered_cycles;
            else{
                u.consecutive_powered_cycles=0;
                if(demand>0)++u.outages;
            }
            if(demand>0 && u.condition>0)u.condition=std::max<std::int64_t>(0,u.condition-10);
        }
    }
}
void Simulation::process_habitats(){
    if(tick_%20)return;
    for(auto& [id,h]:habitats_){
        const auto fit=farms_.find(id);
        const auto staff=fit==farms_.end()?0:fit->second.workers;
        const auto total=h.residents+staff;
        if(!total)continue;
        auto& l=h.life;
        const auto n=h.residents;
        const auto f=std::min(n,l.food_kg),w=std::min(n,l.water_kg),o=std::min(n,l.oxygen_kg);
        l.food_kg-=f;l.water_kg-=w;l.oxygen_kg-=o;
        l.organic_waste_kg+=f;l.wastewater_kg+=w;l.co2_kg+=o;
        ++l.cycles;++h.cycles;
        const auto local_ok=(f==n && w==n && o==n);
        const auto farm_ok=(!staff || (fit!=farms_.end() &&
            fit->second.life.consecutive_supplied_cycles>0));
        if(local_ok){++l.consecutive_supplied_cycles;}
        else {l.consecutive_supplied_cycles=0;++l.shortages;}
        if(local_ok && farm_ok && total<=h.capacity &&
           (!utilities_.count(id) || utilities_.at(id).powered))
            ++h.consecutive_comfort_cycles;
        else h.consecutive_comfort_cycles=0;
    }
}
void Simulation::process_communities(){
    if(tick_%20)return;
    for(auto& [id,c]:communities_){
        ++c.cycles;
        const auto hi=habitats_.find(id);
        const auto fi=farms_.find(id);
        const auto ui=utilities_.find(id);
        const auto residents=hi==habitats_.end()?0:hi->second.residents;
        const auto workers=fi==farms_.end()?0:fi->second.workers;
        const auto attendees=residents+workers;
        if(attendees==0 || c.attendants==0){
            // An idle hall has no missed appointment: no inactivity penalty.
            c.consecutive_service_cycles=0;
            continue;
        }
        const auto techs=ui==utilities_.end()?0:ui->second.technicians;
        const bool staff_ok=c.attendants>0 && c.attendants+techs<=residents;
        const bool all_served=attendees>0 && staff_ok && c.attendants*4>=attendees;
        // One kg food and water per four visitors for the social meal/service.
        // Local bulk supplies are physically moved and become local waste.
        const auto ration=(attendees+3)/4;
        const bool supplied=all_served && c.food_kg>=ration && c.water_kg>=ration;
        const bool comfortable=hi!=habitats_.end() && hi->second.consecutive_comfort_cycles>0;
        const bool powered=ui!=utilities_.end() && ui->second.powered;
        if(supplied && comfortable && powered){
            c.food_kg-=ration;c.water_kg-=ration;
            c.organic_waste_kg+=ration;c.wastewater_kg+=ration;
            c.served_visits+=static_cast<std::uint64_t>(attendees);
            ++c.consecutive_service_cycles;
        }else{
            c.consecutive_service_cycles=0;
            ++c.shortages; // includes staffing, housing, or power interruption
        }
    }
}
void Simulation::process_clinics(){
    if(tick_%20)return;
    for(auto& [site,c]:clinics_){
        ++c.cycles;
        const auto hi=habitats_.find(site);const auto fi=farms_.find(site);const auto ui=utilities_.find(site);
        const auto ci=communities_.find(site);
        const auto residents=hi==habitats_.end()?0:hi->second.residents;
        const auto workers=fi==farms_.end()?0:fi->second.workers;
        const auto people=residents+workers;
        if(people==0){c.consecutive_care_cycles=0;continue;}
        const auto old=c.active_cases;
        const bool needs_shortage=(residents && hi->second.life.consecutive_supplied_cycles==0) ||
            (workers && fi->second.life.consecutive_supplied_cycles==0);
        if(needs_shortage && c.active_cases<people){++c.active_cases;++c.recorded_cases;}
        const auto assigned_tech=ui==utilities_.end()?0:ui->second.technicians;
        const auto attendants=ci==communities_.end()?0:ci->second.attendants;
        const bool authorized_medics=c.medics>0 &&
            c.medics+assigned_tech+attendants<=residents;
        const bool full_coverage=authorized_medics && c.medics*4>=people;
        const bool powered=ui!=utilities_.end() && ui->second.powered;
        const bool comfortable=hi!=habitats_.end() && hi->second.consecutive_comfort_cycles>0;
        // Under-capacity clinics may still treat an individual case, but they
        // cannot claim all residents have dependable access to care.
        const auto treat=(authorized_medics && powered)?std::min({old,c.medics,c.water_kg,c.substrate_kg}):0;
        if(treat>0){
            c.active_cases-=treat;c.treated_cases+=static_cast<std::uint64_t>(treat);
            c.water_kg-=treat;c.substrate_kg-=treat;
            c.wastewater_kg+=treat;c.medical_waste_kg+=treat;
        }
        // Maintain real reserve for at least one additional treatment.
        if(full_coverage && powered && comfortable && c.water_kg>0 && c.substrate_kg>0 &&
           c.active_cases==0 && !needs_shortage)++c.consecutive_care_cycles;
        else{c.consecutive_care_cycles=0;++c.shortages;}
    }
}
void Simulation::process_waterworks(){
    if(tick_%20)return;
    for(auto& [site,p]:waterworks_){
        const auto hi=habitats_.find(site);const auto fi=farms_.find(site);
        const auto ui=utilities_.find(site);
        const auto residents=hi==habitats_.end()?0:hi->second.residents;
        const auto workers=fi==farms_.end()?0:fi->second.workers;
        const auto slot=static_cast<std::size_t>((tick_/20-1)%3);
        p.recent_output[slot]=0;p.recent_demand[slot]=residents+workers;
        ++p.cycles;
        if(!residents && !workers){p.consecutive_service_cycles=0;continue;}
        if(hi==habitats_.end() || ui==utilities_.end() || !ui->second.powered ||
           p.raw_feed_kg<10){p.consecutive_service_cycles=0;continue;}
        const auto processed=std::min<std::int64_t>(20,(p.raw_feed_kg/10)*10);
        const auto potable=processed*9/10;
        // Installed, co-located water lines feed both the worker greenhouse
        // and the habitat; no water crosses between different outposts.
        const auto to_farm=workers?std::min(potable,workers):0;
        const auto to_hab=potable-to_farm;
        if(hi->second.life.water_kg>100000000-to_hab ||
           (to_farm && (fi==farms_.end() || fi->second.life.water_kg>100000000-to_farm))){
            p.consecutive_service_cycles=0;continue;
        }
        p.raw_feed_kg-=processed;p.brine_kg+=processed-potable;
        hi->second.life.water_kg+=to_hab;
        if(to_farm)fi->second.life.water_kg+=to_farm;
        p.purified_total_kg+=potable;p.recent_output[slot]=potable;
        if(potable>=residents+workers)++p.consecutive_service_cycles;
        else p.consecutive_service_cycles=0;
    }
}
void Simulation::process_reclaimers(){
    if(tick_%20)return;
    for(auto& [site,r]:reclaimers_){
        const auto hi=habitats_.find(site);const auto fi=farms_.find(site);const auto ui=utilities_.find(site);
        const auto residents=hi==habitats_.end()?0:hi->second.residents;
        const auto workers=fi==farms_.end()?0:fi->second.workers;
        const auto persons=residents+workers;
        const bool powered=persons>0 && ui!=utilities_.end() && ui->second.powered;
        const auto slot=static_cast<std::size_t>((tick_/20-1)%3);
        r.water_recent[slot]=0;r.oxygen_recent[slot]=0;
        r.water_demand_recent[slot]=persons;
        r.oxygen_demand_recent[slot]=persons;
        ++r.cycles;
        if(!powered){r.consecutive_powered_cycles=0;continue;}
        ++r.consecutive_powered_cycles;
        // Each compartment keeps its own recovered supplies; no site-wide teleport.
        // The plant is a real shared building, while products return to the exact
        // habitat or farm compartment that supplied its wastewater / CO2 feed.
        auto convert=[&](ColonyLifeSupport& l){
            const auto wastewater=std::min<std::int64_t>(10,l.wastewater_kg/10*10);
            const auto carbon=std::min<std::int64_t>(10,l.co2_kg/10*10);
            l.wastewater_kg-=wastewater;l.water_kg+=wastewater*9/10;
            r.sludge_kg+=wastewater/10;
            l.co2_kg-=carbon;l.oxygen_kg+=carbon*7/10;
            r.carbon_residue_kg+=carbon*3/10;
            r.water_recent[slot]+=wastewater*9/10;
            r.oxygen_recent[slot]+=carbon*7/10;
        };
        if(hi!=habitats_.end())convert(hi->second.life);
        if(fi!=farms_.end())convert(fi->second.life);
        r.water_recovered_total_kg+=r.water_recent[slot];
        r.oxygen_generated_total_kg+=r.oxygen_recent[slot];
    }
}
void Simulation::process_colony_needs(){
    if(tick_%20) return;
    for(auto& [id,l]:life_support_){
        if(entities_.at(id).type==EntityType::Wreck)continue;
        // AUTO-04D.4: passengers physically aboard share this ship's actual
        // food, water and oxygen budget. Their origin ship MUST NOT consume for
        // absent people, and transport movement must not create provisions.
        // Legacy aggregate-only worlds have no named passengers to count.
        const auto crew=colony_ships_.at(id).crew;
        const auto physical=passenger_needs_enabled()?people_physically_aboard(id):0;
        if(crew<0 || physical>static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
            throw std::overflow_error("physical ferry population overflow");
        const auto child_count=snapshot_version_>=59?children_physically_aboard(id):0;
        if(child_count>static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())-physical)
            throw std::overflow_error("dependent ship occupancy overflow");
        const auto people=passenger_needs_enabled()?
            static_cast<std::int64_t>(physical+child_count):crew;
        const auto food=std::min(people,l.food_kg);
        const auto water=std::min(people,l.water_kg);
        const auto oxygen=std::min(people,l.oxygen_kg);
        const bool supplied=food==people && water==people && oxygen==people;
        l.food_kg-=food;l.water_kg-=water;l.oxygen_kg-=oxygen;
        // Aggregate conserved material streams; detailed physiology comes later.
        l.organic_waste_kg+=food;l.wastewater_kg+=water;l.co2_kg+=oxygen;
        ++l.cycles;
        if(supplied)++l.consecutive_supplied_cycles;
        else {l.consecutive_supplied_cycles=0;++l.shortages;}
    }
}
void Simulation::process_workshops(){
    // The same QuantifiedLine kernel powers visible and remote work: we do
    // not animate conveyor items when the colony owner is far away.
    for(auto& [site,w]:workshops_){
        // WO2-J: losing a supervisor suspends the owner's approved quota
        // in place. Never delete the quota and thereby resume unrestricted
        // legacy manufacturing without a new explicit owner decision.
        if(w.supervisor_npc_id){
            const auto n=npcs_.find(w.supervisor_npc_id);
            // A later medical/shipyard assignment or legacy machine command
            // must never let one named NPC provide two kinds of physical labor.
            // Fail closed here even if another subsystem admitted that duty;
            // preserve any owner-approved unfinished job as suspended.
            bool duty_conflict=false;
            for(const auto& [other_site,other]:workshops_){
                (void)other_site;
                if(other.operator_npc_id==w.supervisor_npc_id ||
                   other.finisher_npc_id==w.supervisor_npc_id)
                    duty_conflict=true;
            }
            for(const auto& [shift_id,shift]:medic_shifts_){
                (void)shift_id;
                if(shift.medic_id==w.supervisor_npc_id &&
                   shift.start_tick<=tick_ && tick_<shift.end_tick)
                    duty_conflict=true;
            }
            for(const auto& [yard_id,yard]:shipyard_constructions_){
                (void)yard_id;
                if(!yard.operational() &&
                   (yard.workers[0]==w.supervisor_npc_id ||
                    yard.workers[1]==w.supervisor_npc_id))
                    duty_conflict=true;
            }
            for(const auto& [build_id,build]:freighter_build_orders_){
                (void)build_id;
                if(build.finished())continue;
                const auto yard=shipyard_constructions_.find(build.yard_order);
                if(yard!=shipyard_constructions_.end() &&
                   (yard->second.workers[0]==w.supervisor_npc_id ||
                    yard->second.workers[1]==w.supervisor_npc_id))
                    duty_conflict=true;
            }
            const bool present=!duty_conflict && n!=npcs_.end() &&
                n->second.home_ship==w.source_ship &&
                n->second.location==site && n->second.assigned_site==site &&
                n->second.role==NpcRole::Habitat &&
                !n->second.transport_id && !n->second.mentor_id &&
                npc_available_for_duty(w.supervisor_npc_id);
            if(!present){
                w.supervisor_npc_id=0;
                w.supervisor_appointment_id=0;
                // Existing jobs retain their original supervisor and
                // approval command for accountable owner cancellation.
            }
        }
        // WO2-I: expiry is authoritative and deterministic even when a line
        // has unfinished reserved input; it pauses until real relief arrives.
        for(unsigned slot=0;slot<2;++slot){
            auto& sh=w.shifts[slot];
            if(!sh.command_id)continue;
            auto& assigned=slot==0?w.operator_npc_id:w.finisher_npc_id;
            const auto person=npcs_.find(sh.worker);
            // Expiry or lost physical custody invalidates an installed shift.
            // Never strand a nonexistent person in the durable staff slot.
            const bool lost=person==npcs_.end() ||
                person->second.home_ship!=w.source_ship ||
                person->second.location!=site ||
                person->second.assigned_site!=site ||
                person->second.role!=NpcRole::Habitat ||
                person->second.mentor_id || person->second.transport_id ||
                !npc_available_for_duty(sh.worker);
            if(tick_>=sh.end_tick || lost){
                if(assigned==sh.worker)assigned=0;
                sh={};
            }
        }
        const auto u=utilities_.find(site);
        const auto n=npcs_.find(w.operator_npc_id);
        const auto& cut_job=w.approved_jobs[0];
        const bool cut_quota_available=!cut_job.command_id ||
            (w.supervisor_npc_id==cut_job.supervisor_npc_id &&
             w.supervisor_appointment_id==cut_job.supervisor_appointment_id &&
             cut_job.achieved(w.completed_batches)<cut_job.target_batches);
        const bool staff=cut_quota_available &&
            w.operator_npc_id && n!=npcs_.end() &&
            n->second.home_ship==w.source_ship &&
            n->second.location==site && n->second.role==NpcRole::Habitat &&
            habitats_.count(site) && habitats_.at(site).residents>0 &&
            (snapshot_version_<40 || npc_available_for_duty(w.operator_npc_id)) &&
            (snapshot_version_<105 ||
             std::none_of(n->second.industrial_training.begin(),
                          n->second.industrial_training.end(),
                 [&](const NpcTradeTraining& t){return t.tick==tick_;}));
        const bool power=u!=utilities_.end() && u->second.powered;
        const bool working=u!=utilities_.end() && u->second.condition>0;
        industry::Recipe recipe=industry01::original_workshop_recipe("native_cut_aluminum_panel");
        industry::Machine machine{1,"cut",power,staff,working};
        industry::State state;
        state.feed.capacity_grams=1000000;
        state.finished.capacity_grams=1000000;
        state.scrap.capacity_grams=1000000;
        if(w.aluminum_kg)state.feed.grams.emplace("aluminum",w.aluminum_kg*1000);
        if(w.panels_kg)state.finished.grams.emplace("aluminum_panels",w.panels_kg*1000);
        if(w.scrap_kg)state.scrap.grams.emplace("aluminum_scrap",w.scrap_kg*1000);
        if(w.reserved_aluminum_kg){
            state.has_work_in_progress=true;
            state.reserved_inputs={{"aluminum",w.reserved_aluminum_kg*1000}};
            state.ticks_remaining=w.ticks_remaining;
        }
        state.elapsed_ticks=w.elapsed_ticks;
        state.completed_batches=w.completed_batches;
        industry::QuantifiedLine line(recipe,machine,state);
        line.advance(1);
        const auto& next=line.state();
        w.aluminum_kg=next.feed.amount("aluminum")/1000;
        w.panels_kg=next.finished.amount("aluminum_panels")/1000;
        w.scrap_kg=next.scrap.amount("aluminum_scrap")/1000;
        w.reserved_aluminum_kg=next.has_work_in_progress?20:0;
        w.ticks_remaining=next.ticks_remaining;
        w.completed_batches=next.completed_batches;
        w.elapsed_ticks=next.elapsed_ticks;
        if(w.finisher_installed){
            // Shared finite panel buffer and scrap container create real
            // downstream/upstream backpressure with no virtual delivery.
            const auto fn=npcs_.find(w.finisher_npc_id);
            const auto& shape_job=w.approved_jobs[1];
            const bool shape_quota_available=!shape_job.command_id ||
                (w.supervisor_npc_id==shape_job.supervisor_npc_id &&
                 w.supervisor_appointment_id==shape_job.supervisor_appointment_id &&
                 shape_job.achieved(w.finisher_batches)<shape_job.target_batches);
            const bool fn_staff=shape_quota_available &&
                w.finisher_npc_id && fn!=npcs_.end() &&
                fn->second.home_ship==w.source_ship && fn->second.location==site &&
                fn->second.role==NpcRole::Habitat &&
                habitats_.count(site) && habitats_.at(site).residents>0 &&
                w.finisher_npc_id!=w.operator_npc_id &&
                (snapshot_version_<40 || npc_available_for_duty(w.finisher_npc_id)) &&
                (snapshot_version_<105 ||
                 std::none_of(fn->second.industrial_training.begin(),
                              fn->second.industrial_training.end(),
                     [&](const NpcTradeTraining& t){return t.tick==tick_;}));
            industry::Recipe fr=industry01::original_workshop_recipe("native_shape_aluminum_frame");
            industry::Machine fm{1,"shape",power,fn_staff,working};
            industry::State fs;
            fs.feed.capacity_grams=1000000;
            fs.finished.capacity_grams=1000000;
            fs.scrap.capacity_grams=1000000;
            if(w.panels_kg)fs.feed.grams.emplace("aluminum_panels",w.panels_kg*1000);
            if(w.frames_kg)fs.finished.grams.emplace("aluminum_frames",w.frames_kg*1000);
            if(w.scrap_kg)fs.scrap.grams.emplace("aluminum_scrap",w.scrap_kg*1000);
            if(w.reserved_panels_kg){
                fs.has_work_in_progress=true;
                fs.reserved_inputs={{"aluminum_panels",w.reserved_panels_kg*1000}};
                fs.ticks_remaining=w.finisher_ticks_remaining;
            }
            fs.elapsed_ticks=w.finisher_elapsed_ticks;
            fs.completed_batches=w.finisher_batches;
            industry::QuantifiedLine second(fr,fm,fs);
            second.advance(1);
            const auto& sn=second.state();
            w.panels_kg=sn.feed.amount("aluminum_panels")/1000;
            w.frames_kg=sn.finished.amount("aluminum_frames")/1000;
            w.scrap_kg=sn.scrap.amount("aluminum_scrap")/1000;
            w.reserved_panels_kg=sn.has_work_in_progress?20:0;
            w.finisher_ticks_remaining=sn.ticks_remaining;
            w.finisher_batches=sn.completed_batches;
            w.finisher_elapsed_ticks=sn.elapsed_ticks;
        }
    }
}
const OutpostStorageLink* Simulation::storage_link(EntityId site,std::uint64_t id) const {
    auto st=storages_.find(site);
    if(st==storages_.end())return nullptr;
    auto r=st->second.links.find(id);
    return r==st->second.links.end()?nullptr:&r->second;
}
void Simulation::process_storage_links() {
    static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
    for(auto& [site,storage]:storages_) {
        for(auto& [id,link]:storage.links){
            (void)id;
            if(link.route_required && !link.route_ready)continue;
            const auto name=names[link.item_index];
            const bool from_workshop=link.source==kWorkshopConveyorNode;
            const bool to_workshop=link.destination==kWorkshopConveyorNode;
            const auto stock=[&](std::uint32_t node)->std::map<std::string,std::int64_t>&{
                return node?storage.extra_bins.at(node):storage.goods_kg;
            };
            if(from_workshop && to_workshop)
                throw std::logic_error("factory link connected to itself");
            if((from_workshop || to_workshop) && !workshops_.count(site))
                throw std::logic_error("orphaned factory logistics endpoint");
            auto from=from_workshop ? workshop_bin_inventory_snapshot(site,link.item_index) :
                storage_node_inventory_snapshot(site,link.source);
            auto to=to_workshop ? workshop_bin_inventory_snapshot(site,link.item_index) :
                storage_node_inventory_snapshot(site,link.destination);
            const std::string code=std::string("good/")+name;
            const auto rate=std::min({link.units_per_tick,from.amount(code),
                                     to.free_liters()/inventory::Catalogue::standard().liters_per_unit(code)});
            if(rate<=0)continue;
            if(link.units_moved>std::numeric_limits<std::uint64_t>::max()-static_cast<std::uint64_t>(rate))
                throw std::overflow_error("storage link movement overflow");
            if(from.transfer_to(to,code,rate)!=inventory::TransferStatus::Applied)
                throw std::logic_error("storage link accounting contradiction");
            const auto new_source=from.amount(code),new_dest=to.amount(code);
            const auto commit=[&](std::uint32_t node,std::int64_t qty){
                if(node==kWorkshopConveyorNode){
                    auto& w=workshops_.at(site);
                    std::int64_t* slots[]={&w.aluminum_kg,&w.panels_kg,&w.frames_kg,&w.scrap_kg};
                    *slots[link.item_index]=qty;
                }else{
                    auto& stacks=stock(node);
                    if(qty)stacks[name]=qty;
                    else stacks.erase(name);
                }
            };
            commit(link.source,new_source);
            commit(link.destination,new_dest);
            link.units_moved+=static_cast<std::uint64_t>(rate);
            activity_events_.push_back({tick_,ActivityKind::AutoStorageTransfer,link.id,
                                        site,link.id,Outcome::Applied,rate});
        }
    }
}

bool Simulation::within_deep_probe_link(std::int64_t dx_mm,
    std::int64_t dy_mm) {
    return std::hypot(double(dx_mm),double(dy_mm))<=
           double(kDeepSpaceProbeCommsRangeMm);
}
namespace {
constexpr std::uint64_t kAstronomicalGalaxySeed=0x53475F415354524FULL;
planetary::PlanetaryEnvelope planetary_envelope_for_body(
    const PhysicalCelestialBody& body) {
    const auto selector=astronomy_mix(kAstronomicalGalaxySeed^body.id);
    planetary::PlanetaryEnvelope env{};
    if(body.orbital_radius_milli_au>12000) {
        env.climate=planetary::WorldClimate::Frozen;
        env.water_coverage=std::uint8_t(20+(selector%70));
        env.average_celsius=-25;
    } else if(body.orbital_radius_milli_au<2200) {
        env.climate=planetary::WorldClimate::AirlessRock;
        env.water_coverage=0;
        env.average_celsius=50;
    } else if(selector%5==0) {
        env.climate=planetary::WorldClimate::Arid;
        env.water_coverage=std::uint8_t(2+(selector%15));
        env.average_celsius=27;
    } else if(selector%5==1) {
        env.climate=planetary::WorldClimate::Ocean;
        env.water_coverage=std::uint8_t(75+(selector%21));
        env.average_celsius=16;
    } else {
        env.climate=planetary::WorldClimate::LifeSupporting;
        env.water_coverage=std::uint8_t(25+(selector%45));
        env.average_celsius=17;
    }
    // PGF biosphere genesis has NOT been integrated. Never claim discovered
    // native vegetation merely from a favorable climate classification.
    env.biosphere_present=false;
    return env;
}

constexpr std::uint64_t kAuMm=149597870700000ULL;
constexpr std::uint64_t kCosmicLimitMm=50000000000000000ULL; // 334 AU
// Fixed-point directional placements avoid libc trig drift across OS builds.
CosmicPosition celestial_point(std::uint64_t radius_mm,std::uint64_t selector){
    constexpr std::int64_t diag=707107;
    constexpr std::int64_t scale=1000000;
    std::int64_t dx=0,dy=0;
    switch(selector%8) {
        case 0:dx=scale;break;
        case 1:dx=diag;dy=diag;break;
        case 2:dy=scale;break;
        case 3:dx=-diag;dy=diag;break;
        case 4:dx=-scale;break;
        case 5:dx=-diag;dy=-diag;break;
        case 6:dy=-scale;break;
        default:dx=diag;dy=-diag;break;
    }
    const auto radius=static_cast<std::int64_t>(radius_mm);
    const auto component=[&](std::int64_t direction){
        return (radius/scale)*direction+((radius%scale)*direction)/scale;
    };
    // A small stable inclination distributes bodies above and below the
    // local orbital plane without invoking nondeterministic trigonometry.
    const auto inclination=static_cast<std::int64_t>(selector%7)-3;
    return {component(dx),component(dy),
            (radius/1000)*inclination};
}
std::vector<PhysicalCelestialBody> physical_bodies_for_system(std::uint32_t system){
    const auto profile=generate_astronomical_profile(kAstronomicalGalaxySeed,system);
    const auto key=[system](std::uint32_t ordinal){
        return (std::uint64_t(system)<<32)|ordinal;
    };
    std::vector<PhysicalCelestialBody> out;
    for(std::uint32_t star=0;star<profile.star_count;++star){
        const auto position=celestial_point(star==0?0:3000000000000ULL,star);
        out.push_back({key(star+1),0,system,CelestialKind::Star,0,
            position,700000000000ULL});
    }
    std::uint32_t ordinal=profile.star_count+1;
    for(const auto& band:profile.orbital_bands){
        const auto orbit=band.radius_milli_au;
        if(orbit>300000)throw std::logic_error("celestial orbit exceeds design envelope");
        // Division before multiplication avoids 64-bit overflow for AU-scale orbits.
        const auto radial_mm=orbit*(kAuMm/1000);
        const auto selector=astronomy_mix((std::uint64_t(system)<<32)|band.ordinal)%8;
        const auto position=celestial_point(radial_mm,selector);
        const bool debris=band.zone==OrbitZone::Debris;
        const auto planet_id=key(ordinal++);
        out.push_back({planet_id,key(1),system,
            debris?CelestialKind::DebrisRegion:CelestialKind::Planet,
            orbit,position,debris?4000000000000ULL:6000000000ULL});
        // Approximately one in three orbital planet candidates has a moon,
        // with a durable physical parent and deterministic offset.
        if(!debris && astronomy_mix(planet_id)%3==0){
            const auto offset=celestial_point(380000000000ULL,
                astronomy_mix(planet_id^0x1234ULL)%8);
            out.push_back({key(ordinal++),planet_id,system,CelestialKind::Moon,
                orbit,{position.x_mm+offset.x_mm,position.y_mm+offset.y_mm,
                       position.z_mm+offset.z_mm},
                1700000000ULL});
        }
    }
    return out;
}
}
// PLAY-13C.5: deterministic observational profiles. These describe plausible
// gameplay regions, not scientific measurements or applied physics damage.
namespace {
LocalCelestialEnvironment local_profile_for(const PhysicalCelestialBody& body) {
    LocalCelestialEnvironment e{};
    switch(body.kind) {
        case CelestialKind::Star: e.zone=1;e.hazard=3;break;
        case CelestialKind::DebrisRegion: e.zone=2;e.hazard=2;break;
        case CelestialKind::Planet:
            e.zone=body.orbital_radius_milli_au>80000?4:3;
            e.hazard=body.orbital_radius_milli_au<25000?2:1;
            break;
        case CelestialKind::Moon: e.zone=5;e.hazard=1;break;
    }
    const auto mix=astronomy_mix(body.id^0xC1350005ULL);
    const unsigned base=body.kind==CelestialKind::Star?2:
        body.kind==CelestialKind::DebrisRegion?0:
        body.kind==CelestialKind::Moon?1:e.zone==4?0:1;
    const unsigned primary=(base+unsigned(mix%3))%3;
    const unsigned secondary=(primary+1+unsigned((mix>>8)%2))%3;
    e.first=static_cast<AsteroidClass>(primary);
    e.second=static_cast<AsteroidClass>(secondary);
    return e;
}
const PhysicalCelestialBody* body_for_local_environment(
    const std::map<std::uint32_t,std::vector<PhysicalCelestialBody>>& all,
    std::uint64_t body_id) {
    const auto it=all.find(static_cast<std::uint32_t>(body_id>>32));
    if(it==all.end())return nullptr;
    const auto found=std::find_if(it->second.begin(),it->second.end(),
        [body_id](const PhysicalCelestialBody& b){return b.id==body_id;});
    return found==it->second.end()?nullptr:&*found;
}
bool same_local_profile(const LocalCelestialEnvironment& a,
                        const LocalCelestialEnvironment& b) {
    return a.zone==b.zone&&a.hazard==b.hazard&&
        a.first==b.first&&a.second==b.second;
}
}
void Simulation::provision_oasis_reserve_genesis(std::uint64_t body,
    std::int64_t water_kg,std::int64_t nutrients_kg) {
    if(tick_ || !submitted_ids_.empty() || !commands_.empty() ||
       snapshot_version_<70 || water_kg<1000 || nutrients_kg<1000 ||
       water_kg>100000000 || nutrients_kg>100000000 ||
       (water_kg+nutrients_kg)%1000 || oasis_reserves_.size()>=10000)
        throw std::invalid_argument("oasis reserve must be finite world-genesis matter");
    const auto prospect=celestial_oasis_prospect(body);
    if(!prospect || !prospect->candidate ||
       !oasis_reserves_.emplace(body,PhysicalOasisReserve{
           water_kg,nutrients_kg,water_kg,nutrients_kg}).second)
        throw std::invalid_argument("oasis source requires unique valid prospect");
    snapshot_version_=std::max(snapshot_version_,75);
}
bool Simulation::collect_oasis_resource(PlayerId owner,EntityId ship,
    std::uint64_t body,bool water,std::int64_t kilograms) {
    if(!owner || owner==kServerAuthority || !body ||
       kilograms<1000 || kilograms>10000 || kilograms%1000)
        return false;
    const auto* vessel=find(ship);
    if(!vessel || vessel->type!=EntityType::Ship ||
       vessel->owner_id!=owner || vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       !vessel->fitting.exploration_scanner())return false;
    const auto frame=ship_frames_.find(ship);
    if(frame==ship_frames_.end() || frame->second.anchor_body!=body)return false;
    const auto r=oasis_reserves_.find(body);
    const auto l=life_support_.find(ship);
    if(r==oasis_reserves_.end() || l==life_support_.end())return false;
    auto& remaining=water?r->second.water_kg:r->second.nutrients_kg;
    auto& destination=water?l->second.water_kg:l->second.nutrients_kg;
    if(remaining<kilograms || destination>1000000-kilograms)return false;
    const auto rig=oasis_extractors_.find(ship);
    if(!colony_ships_.count(ship) || rig==oasis_extractors_.end() || rig->second.body!=body ||
       !rig->second.condition || rig->second.assigned_workers<2 ||
       rig->second.energy_units<std::uint64_t(kilograms/1000)*5 ||
       colony_ships_.at(ship).crew<rig->second.assigned_workers ||
       l->second.food_kg<rig->second.assigned_workers ||
       l->second.water_kg<rig->second.assigned_workers ||
       l->second.oxygen_kg<rig->second.assigned_workers)
        return false;
    rig->second.energy_units-=static_cast<std::uint16_t>(kilograms/1000*5);
    // Whole-kilogram identity and world ore-equivalent mass stay unchanged.
    remaining-=kilograms;
    destination+=kilograms;
    snapshot_version_=std::max(snapshot_version_,75);
    return true;
}
bool Simulation::install_oasis_extractor(PlayerId owner,EntityId ship,
    std::uint64_t body) {
    if(!owner || owner==kServerAuthority || !body ||
       oasis_extractors_.count(ship) || oasis_extractors_.size()>=10000)
        return false;
    const auto* vessel=find(ship);
    const auto frame=ship_frames_.find(ship);
    auto colony=colony_ships_.find(ship);
    const auto life=life_support_.find(ship);
    if(!vessel || vessel->type!=EntityType::Ship || vessel->owner_id!=owner ||
       vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       !vessel->fitting.exploration_scanner() ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       !oasis_reserves_.count(body) || colony==colony_ships_.end() ||
       life==life_support_.end() || colony->second.modules<2 ||
       colony->second.crew<2 || life->second.food_kg<2 ||
       life->second.water_kg<2 || life->second.oxygen_kg<2)
        return false;
    // The extractor is fitted to this existing hull and physically consumes
    // two 2-tonne manufactured modules; its staff are already aboard.
    colony->second.modules-=2;
    oasis_extractors_.emplace(ship,OasisExtractor{ship,body,4,2,100,0,0});
    snapshot_version_=std::max(snapshot_version_,76);
    return true;
}
void Simulation::process_oasis_extractors() {
    for(auto& [id,rig]:oasis_extractors_) {
        const auto* ship=find(id);
        const auto frame=ship_frames_.find(id);
        const auto c=colony_ships_.find(id);
        const auto l=life_support_.find(id);
        if(!ship || !c || !l || frame==ship_frames_.end() ||
           frame->second.anchor_body!=rig.body ||
           ship->dock_state!=DockState::InSpace ||
           ship->jump_state!=ShipJumpState::Ready || !rig.condition)
            continue;
        const auto p=celestial_oasis_prospect(rig.body);
        if(!p || !p->energy || c->second.crew<rig.assigned_workers ||
           l->second.food_kg<rig.assigned_workers ||
           l->second.water_kg<rig.assigned_workers ||
           l->second.oxygen_kg<rig.assigned_workers)continue;
        // Power units are energy, not matter. A fitted solar collector charges
        // one game unit per committed tick while conditions permit, capped.
        if(rig.energy_units<120)++rig.energy_units;
        ++rig.cycles;
    }
}
std::optional<CelestialOasisProspect> Simulation::celestial_oasis_prospect(
    std::uint64_t body_id) const {
    const auto* b=body_for_local_environment(physical_celestial_systems_,body_id);
    if(!b)return std::nullopt;
    CelestialOasisProspect o{};
    o.body=body_id;
    // Conditions reflect potential resources and shelter, NOT constructed
    // infrastructure or initialized usable water/food/energy inventory.
    if(b->kind==CelestialKind::Planet) {
        o.water= b->orbital_radius_milli_au>10000?1:0;
        o.energy= b->orbital_radius_milli_au<160000?1:0;
        o.nutrients=1;
        o.shelter=1; // prospects for subsurface/radiation protection
    }else if(b->kind==CelestialKind::Moon) {
        o.water= b->orbital_radius_milli_au>30000?1:0;
        o.energy= b->orbital_radius_milli_au<100000?1:0;
        o.nutrients=1;
        o.shelter=1;
    }else if(b->kind==CelestialKind::DebrisRegion) {
        o.water=1;o.energy=1;o.nutrients=0;o.shelter=0;
    }
    if(!(o.water&&o.energy&&o.nutrients&&o.shelter))return o;
    // A rare SYSTEM-level draw prevents multiple guaranteed oases in
    // one remote region. No frontier wealth/mineral class is consulted.
    const auto system=static_cast<std::uint32_t>(body_id>>32);
    if(astronomy_mix(std::uint64_t(system)^0x0A515500ULL)%10000 >= 200)
        return o; // nominal 2% eligible systems, NOT two percent of bodies
    const auto it=physical_celestial_systems_.find(system);
    if(it==physical_celestial_systems_.end())return o;
    // At most one eligible body in the rare system; prefer planets over moons.
    const PhysicalCelestialBody* selected=nullptr;
    for(const auto& c:it->second) {
        if(c.kind!=CelestialKind::Planet)continue;
        if(c.orbital_radius_milli_au<=10000 ||
           c.orbital_radius_milli_au>=160000)continue;
        selected=&c;break;
    }
    if(!selected)for(const auto& c:it->second) {
        if(c.kind!=CelestialKind::Moon)continue;
        if(c.orbital_radius_milli_au<=30000 ||
           c.orbital_radius_milli_au>=100000)continue;
        selected=&c;break;
    }
    o.candidate=selected&&selected->id==body_id;
    return o;
}
std::vector<CelestialOasisProspect> Simulation::survey_oasis_prospects(
    PlayerId authenticated_owner,EntityId ship) const {
    if(!scan_celestial_profile(authenticated_owner,ship))return {};
    const auto carrier=find(ship);
    if(!carrier)return {};
    const auto system=physical_celestial_systems_.find(carrier->system_id);
    if(system==physical_celestial_systems_.end())return {};
    std::vector<CelestialOasisProspect> out;
    for(const auto& body:system->second) {
        if(const auto p=celestial_oasis_prospect(body.id))
            if(p->candidate)out.push_back(*p);
    }
    return out;
}
void Simulation::ensure_local_environment_profiles() {
    if(snapshot_version_<73)return;
    for(const auto& [body_id,manifest]:generated_local_regions_) {
        (void)manifest;
        const auto* body=body_for_local_environment(physical_celestial_systems_,body_id);
        if(!body)throw std::runtime_error("local profile missing real celestial body");
        const auto expected=local_profile_for(*body);
        const auto [it,inserted]=local_environments_.emplace(body_id,expected);
        if(!inserted&&!same_local_profile(it->second,expected))
            throw std::runtime_error("local environment geometry differs from catalogue");
    }
    snapshot_version_=std::max(snapshot_version_,74);
}
std::optional<AsteroidClass> Simulation::local_deposit_taxonomy(EntityId rock) const {
    const auto it=local_entity_anchors_.find(rock);
    if(it==local_entity_anchors_.end()||!entities_.count(rock))return std::nullopt;
    const auto manifest=generated_local_regions_.find(it->second);
    if(manifest==generated_local_regions_.end())return std::nullopt;
    const auto profile=local_environments_.find(it->second);
    if(profile==local_environments_.end())return std::nullopt;
    if(manifest->second[0]==rock)return profile->second.first;
    if(manifest->second[1]==rock)return profile->second.second;
    return std::nullopt;
}
std::optional<Simulation::ExpeditionProspect> Simulation::assess_expedition(
    PlayerId authenticated_owner,EntityId ship) const {
    const auto* e=find(ship);
    const auto* colony=colony_ship(ship);
    const auto* stocks=life_support(ship);
    if(!authenticated_owner || !e || !colony || !stocks ||
       e->type!=EntityType::Ship || e->owner_id!=authenticated_owner)
        return std::nullopt;
    const auto frame=ship_frames_.find(ship);
    if(frame==ship_frames_.end() || !frame->second.anchor_body)return std::nullopt;
    const auto region=local_environments_.find(frame->second.anchor_body);
    if(region==local_environments_.end())return std::nullopt;
    ExpeditionProspect p{};
    p.body=frame->second.anchor_body;
    p.hazard=region->second.hazard;
    // Same physical population accounting as process_colony_needs; forecast
    // is read-only and excludes any promise of future delivery or harvest.
    const auto physical=passenger_needs_enabled()?people_physically_aboard(ship):0;
    const auto child_count=snapshot_version_>=59?children_physically_aboard(ship):0;
    if(physical>std::size_t(std::numeric_limits<std::int64_t>::max()) ||
       child_count>std::size_t(std::numeric_limits<std::int64_t>::max())-physical)
        throw std::overflow_error("expedition population exceeds integer bounds");
    const auto people=passenger_needs_enabled()?
        std::int64_t(physical+child_count):colony->crew;
    if(people<0)throw std::logic_error("expedition population negative");
    if(people>0) {
        p.food_periods=std::uint64_t(stocks->food_kg/people);
        p.water_periods=std::uint64_t(stocks->water_kg/people);
        p.oxygen_periods=std::uint64_t(stocks->oxygen_kg/people);
    }
    const auto manifest=generated_local_regions_.find(p.body);
    if(manifest==generated_local_regions_.end())return std::nullopt;
    for(const auto site:manifest->second) {
        const auto it=entities_.find(site);
        if(!site || it==entities_.end())continue; // depleted tombstone
        if(it->second.type!=EntityType::Asteroid ||
           !same_local_frame(ship,site))throw std::logic_error("invalid expedition rock custody");
        const auto tonnes=std::uint64_t(it->second.ore);
        if(tonnes>std::numeric_limits<std::uint64_t>::max()-p.ore_tonnes)
            throw std::overflow_error("expedition ore overflow");
        p.ore_tonnes+=tonnes;
        const auto taxonomy=local_deposit_taxonomy(site);
        if(!taxonomy)throw std::logic_error("expedition geology missing");
        // Profiles are integer permille, so tonnes * permille == kilograms.
        for(const auto& mineral:mineral_profile(*taxonomy)) {
            const auto kilograms=tonnes*std::uint64_t(mineral.permille);
            if(std::string(mineral.name)=="nickel_iron")
                p.nickel_iron_kg+=kilograms;
            if(std::string(mineral.name)=="plagioclase")
                p.plagioclase_kg+=kilograms;
        }
    }
    // Physical supply infrastructure must actually be present in this frame.
    // Station in some OTHER frame or a hypothetical supply ship is not support.
    for(const auto& [site,facility]:built_outposts_) {
        (void)facility;
        const auto it=entities_.find(site);
        if(it!=entities_.end()&&it->second.owner_id==authenticated_owner &&
           same_local_frame(ship,site)) {p.local_supply_site=true;break;}
    }
    return p;
}
void Simulation::seed_celestial_local_region(std::uint64_t body_id) {
    if(snapshot_version_<71 || !body_id)
        throw std::invalid_argument("local region requires ship-frame world");
    const auto system=static_cast<std::uint32_t>(body_id>>32);
    const auto stars=physical_celestial_systems_.find(system);
    if(stars==physical_celestial_systems_.end() ||
       std::none_of(stars->second.begin(),stars->second.end(),
           [body_id](const PhysicalCelestialBody& b){return b.id==body_id;}))
        throw std::invalid_argument("nonexistent physical region body");
    if(generated_local_regions_.count(body_id))return;
    if(generated_local_regions_.size()>=10000)
        throw std::runtime_error("too many generated local regions");
    // Region generation is a physically conserved redistribution of a finite
    // existing unanchored asteroid reserve. Without reserve the region is
    // intentionally barren, not an opportunity to mint new ore.
    constexpr std::int64_t allocations[2]={6,8}; // tonnes, 14 total
    constexpr Vector2 locations[2]={{2000,0},{-12000,5000}};
    EntityId donor=0;
    for(const auto& [id,e]:entities_) {
        if(e.type==EntityType::Asteroid && e.system_id==system &&
           e.ore>=15 && !local_entity_anchors_.count(id) &&
           std::none_of(mining_.begin(),mining_.end(),
              [id](const auto& row){return row.second.target==id;})) {
            donor=id;
            break;
        }
    }
    std::array<EntityId,2> manifest{};
    if(donor) {
        if(next_entity_id_>std::numeric_limits<EntityId>::max()-2)
            throw std::overflow_error("local region entity IDs exhausted");
        if(entities_.size()>100000-2)
            throw std::runtime_error("local region entity cap exceeded");
        for(unsigned i=0;i<2;++i) {
            Entity rock{};
            rock.id=next_entity_id_++;
            rock.system_id=system;
            rock.type=EntityType::Asteroid;
            rock.position=locations[i];
            rock.ore=allocations[i];
            entities_.emplace(rock.id,rock);
            local_entity_anchors_.emplace(rock.id,body_id);
            manifest[i]=rock.id;
        }
        entities_.at(donor).ore-=14;
    }
    generated_local_regions_.emplace(body_id,manifest);
    snapshot_version_=std::max(snapshot_version_,73);
    ensure_local_environment_profiles();
}
void Simulation::ensure_ship_frame_registry() {
    // This upgrades existing physical ships only; it does not relocate anything.
    // Future host arrivals must use separately verified transit receipts.
    if(snapshot_version_<70)ensure_physical_celestial_systems();
    for(const auto& [id,entity]:entities_) {
        if(entity.type==EntityType::Ship) {
            const auto [it,inserted]=ship_frames_.emplace(
                id,ShipFrameCustody{entity.system_id,0,{}});
            if(!inserted && it->second.system!=entity.system_id)
                throw std::runtime_error("ship frame custody system mismatch");
        }
    }
    snapshot_version_=std::max(snapshot_version_,71);
}
void Simulation::ensure_physical_celestial_systems() {
    const auto current=physical_celestial_systems_;
    for(const auto system:systems_){
        const auto generated=physical_bodies_for_system(system);
        const auto existing=current.find(system);
        if(existing!=current.end()) {
            if(existing->second.size()!=generated.size())
                throw std::runtime_error("physical astronomy generator mismatch");
            continue;
        }
        physical_celestial_systems_.emplace(system,generated);
    }
    snapshot_version_=std::max(snapshot_version_,70);
}
std::vector<PhysicalCelestialBody> Simulation::scan_physical_celestials(
    PlayerId owner,EntityId ship) const {
    if(!scan_celestial_profile(owner,ship))return {};
    const auto carrier=entities_.find(ship);
    const auto found=physical_celestial_systems_.find(carrier->second.system_id);
    return found==physical_celestial_systems_.end()?
        std::vector<PhysicalCelestialBody>{}:found->second;
}
// PLAY-13D.3: these native game operations use existing canonical module,
// life-support and oasis custody. A survey is informational; no resource is
// allocated merely because terrain resembles a river or fertile ground.
std::optional<planetary::PlanetaryRegionKey>
Simulation::planetary_oasis_source_region(PlayerId owner,EntityId ship,
    std::uint64_t body_id) const {
    if(!oasis_reserves_.count(body_id))return std::nullopt;
    const auto body=celestial_navigation_anchor(owner,ship,body_id);
    if(!body || (body->kind!=CelestialKind::Planet &&
                 body->kind!=CelestialKind::Moon))return std::nullopt;
    const auto env=planetary_envelope_for_body(*body);
    const planetary::PlanetaryWorldKey key{
        kAstronomicalGalaxySeed,body->system,body_id,1};
    const auto map=planetary::generate_watersheds(key,env);
    // The seeded index yields a deterministic location on this physical
    // body, not a source on every tile. Open land is required for rig placement.
    const auto start=std::size_t(astronomy_mix(body_id^
        kAstronomicalGalaxySeed^0x4F41534953534F55ULL)%planetary::kWatershedCells);
    for(std::size_t n=0;n<planetary::kWatershedCells;++n) {
        const auto index=(start+n)%planetary::kWatershedCells;
        if(map.cells[index].ocean)continue;
        return planetary::PlanetaryRegionKey{
            body_id,1,
            std::uint16_t(index/planetary::kWatershedColumns),
            std::uint16_t(index%planetary::kWatershedColumns)};
    }
    return std::nullopt; // all-ocean world requires future underwater rigs
}
namespace {
EntityId surface_custody_id(const PlanetarySiteKey& key) {
    const auto [body,lat,lon,revision]=key;
    return astronomy_mix(body ^ (std::uint64_t(lat)<<32) ^
         (std::uint64_t(lon)<<16) ^ revision ^
         0x5355524641434543ULL) | (1ULL<<63);
}
}
bool Simulation::build_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    std::uint64_t first_npc,std::uint64_t second_npc) {
    const auto site=scan_planetary_geography(owner,ship,body,latitude,longitude);
    const auto frame=ship_frames_.find(ship);
    const auto* vessel=find(ship);
    auto colony=colony_ships_.find(ship);
    auto support=life_support_.find(ship);
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    const auto surface_location=surface_custody_id(key);
    if(!site || !vessel || !owner || owner==kServerAuthority ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       vessel->dock_state!=DockState::InSpace ||
       vessel->jump_state!=ShipJumpState::Ready ||
       colony==colony_ships_.end() || colony->second.modules<2 ||
       colony->second.crew<3 || support==life_support_.end() ||
       support->second.food_kg<60 || support->second.water_kg<60 ||
       support->second.oxygen_kg<60 ||
       planetary_sites_.count(key) || planetary_sites_.size()>=10000 ||
       site->landscape.region.surface_water ||
       entities_.count(surface_location) || npcs_.empty())
        return false;
    // Collision-resistant deterministic custody handle; reject collisions
    // rather than allowing two cells to share their workforce.
    for(const auto& [existing,facility]:planetary_sites_) {
        (void)existing;
        if(facility.surface_location==surface_location)return false;
    }
    std::array<std::uint64_t,2> assigned{{0,0}};
    if((first_npc==0)!=(second_npc==0) ||
       (first_npc && first_npc==second_npc))return false;
    if(snapshot_version_>=20) {
        const auto eligible=[&](std::uint64_t id)->bool {
            const auto it=npcs_.find(id);
            if(it==npcs_.end())return false;
            const auto& n=it->second;
            if(n.home_ship!=ship || n.location!=ship ||
               n.assigned_site!=ship || n.role!=NpcRole::Ship ||
               !npc_available_for_duty(id))return false;
            for(const auto& [child,d]:physical_dependents_) {
                (void)child;
                if(d.guardian_id==id)return false;
            }
            return true;
        };
        if(first_npc) {
            if(!eligible(first_npc) || !eligible(second_npc))return false;
            assigned={{first_npc,second_npc}};
        } else {
            std::size_t ready=0;
            for(const auto& [id,n]:npcs_) {
                (void)n;
                if(!eligible(id))continue;
                assigned[ready++]=id;
                if(ready==2)break;
            }
            if(ready!=2)return false;
        }
    }
    // Two physical named NPCs and manufactured modules leave the ship.
    // This does not create a player landing or a rendered planetary scene.
    colony->second.modules-=2;
    colony->second.crew-=2;
    support->second.food_kg-=60;
    support->second.water_kg-=60;
    support->second.oxygen_kg-=60;
    for(auto id:assigned)if(id) {
        auto& n=npcs_.at(id);
        n.location=surface_location;
        n.assigned_site=surface_location;
        n.role=NpcRole::PlanetarySurface;
        n.transport_id=0;
        n.mentor_id=0;
    }
    PlanetarySiteFacility facility{
        site->key,owner,ship,4,false,false,
        60,0,60,0,0};
    facility.spaceport_phase=1;
    facility.construction_progress=0;
    facility.construction_required=240;
    facility.surface_location=surface_location;
    facility.delegated_npcs=assigned;
    facility.assigned_workers=2;
    facility.oxygen_kg=60;
    // Preserve version-77 already-completed outposts while upgrading the
    // world's canonical snapshot format. No NPCs are fabricated for them.
    for(auto& [legacy_key,legacy]:planetary_sites_) {
        if(legacy.surface_location)continue;
        legacy.surface_location=surface_custody_id(legacy_key);
        legacy.delegated_npcs={{0,0}};
        legacy.assigned_workers=0;
        legacy.spaceport_phase=3;
        legacy.construction_progress=40;
        legacy.construction_required=40;
    }
    planetary_sites_.emplace(key,facility);
    snapshot_version_=std::max(snapshot_version_,78);
    return true;
}

bool Simulation::build_planetary_shuttle(PlayerId owner,EntityId ship,const PlanetarySiteKey& key) {
    const auto site=planetary_sites_.find(key);
    const auto hull=entities_.find(ship);
    const auto frame=ship_frames_.find(ship);
    auto colony=colony_ships_.find(ship);
    if(!owner || std::get<3>(key)!=1 || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || planetary_shuttles_.count(key) ||
       planetary_shuttles_.size()>=10000 || hull==entities_.end() ||
       hull->second.owner_id!=owner || hull->second.dock_state!=DockState::InSpace ||
       hull->second.jump_state!=ShipJumpState::Ready || frame==ship_frames_.end() ||
       frame->second.anchor_body!=std::get<0>(key) ||
       colony==colony_ships_.end() || colony->second.modules<2)return false;
    colony->second.modules-=2;
    planetary_shuttles_.emplace(key,PlanetaryShuttleState{owner,ship,0,4,0,0});
    snapshot_version_=std::max(snapshot_version_,80);
    return true;
}

bool Simulation::transfer_planetary_housing_module(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,bool load) {
    auto site=planetary_sites_.find(key);
    auto shuttle=planetary_shuttles_.find(key);
    auto colony=colony_ships_.find(ship);
    const auto hull=entities_.find(ship);
    const auto frame=ship_frames_.find(ship);
    if(!owner || owner==kServerAuthority || std::get<3>(key)!=1 ||
       site==planetary_sites_.end() || shuttle==planetary_shuttles_.end() ||
       colony==colony_ships_.end() || hull==entities_.end() ||
       frame==ship_frames_.end() || hull->second.owner_id!=owner ||
       frame->second.anchor_body!=std::get<0>(key) ||
       hull->second.dock_state!=DockState::InSpace ||
       hull->second.jump_state!=ShipJumpState::Ready ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=ship ||
       (shuttle->second.flight_phase!=0 && shuttle->second.flight_phase!=2))
        return false;
    // Existing shuttle external construction rack: ONE real two-tonne
    // module, separate from the 1-tonne internal water/food hold.
    auto& rack=shuttle->second.housing_module;
    if(load) {
        if(rack) return false;
        if(shuttle->second.flight_phase==0) {
            if(colony->second.modules<1)return false;
            --colony->second.modules;
        }else {
            if(!site->second.housing_modules)return false;
            --site->second.housing_modules;
        }
        rack=1;
    }else {
        if(rack!=1)return false;
        if(shuttle->second.flight_phase==0) {
            if(colony->second.modules>=100000000)return false;
            ++colony->second.modules;
        }else {
            if(site->second.housing_modules>=2)return false;
            ++site->second.housing_modules;
        }
        rack=0;
    }
    snapshot_version_=std::max(snapshot_version_,109);
    return true;
}

bool Simulation::start_planetary_housing(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t order_id) {
    if(planetary_family_booking_ids_.count(order_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       order_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    const auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || !order_id ||
       order_id==std::numeric_limits<std::uint64_t>::max() ||
       it==planetary_sites_.end() || !colony_ships_.count(ship) ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       it->second.spaceport_phase!=3 || it->second.housing_phase ||
       it->second.housing_modules<2 || it->second.workforce_policy ||
       it->second.food_kg<2 || it->second.water_kg<2 ||
       it->second.oxygen_kg<2 || submitted_ids_.count(order_id) ||
       shuttle_flight_request_ids_.count(order_id) ||
       local_delivery_orders_.count(order_id) ||
       voyages_.count(order_id))return false;
    for(const auto& [site_id,other]:planetary_sites_) {
        (void)site_id;
        if(other.housing_order_id==order_id ||
           other.utility_order_id==order_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==order_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==order_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==order_id)return false;
        for(const auto founded:other.founder_consent_requests)
            if(founded==order_id)return false;
        for(const auto& [visitor,proof]:other.visitor_residency) {
            (void)visitor;
            if(proof[1]==order_id || proof[2]==order_id)return false;
        }
    }
    for(const auto id:it->second.delegated_npcs) {
        const auto n=npcs_.find(id);
        if(!id || n==npcs_.end() || n->second.home_ship!=ship ||
           n->second.location!=it->second.surface_location ||
           n->second.assigned_site!=it->second.surface_location ||
           n->second.role!=NpcRole::PlanetarySurface ||
           !npc_available_for_duty(id))return false;
    }
    auto& site=it->second;
    site.housing_modules-=2;
    site.embodied_ore+=4; // two physically delivered original modules
    site.housing_phase=1;
    site.housing_progress=0;
    site.housing_order_id=order_id;
    site.housing_stalled_ticks=0;
    site.housing_completed_tick=0;
    snapshot_version_=std::max(snapshot_version_,109);
    return true;
}


bool Simulation::set_planetary_housing_pause(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,bool pause) {
    const auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority ||
       it==planetary_sites_.end() ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       it->second.housing_phase!=1 ||
       it->second.housing_paused==pause)return false;
    it->second.housing_paused=pause;
    snapshot_version_=std::max(snapshot_version_,109);
    return true; // materials and original progress stay on the surface
}

// WO3-C/D: no remotely manifested modules or fabricated builders. Only
// real resident, physically deployed, WO2-certified builders can supervise.
bool Simulation::start_planetary_surface_utility(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t order_id,
    std::uint64_t qualified_supervisor) {
    if(planetary_family_booking_ids_.count(order_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       order_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || !order_id ||
       order_id==std::numeric_limits<std::uint64_t>::max() ||
       !qualified_supervisor || it==planetary_sites_.end() ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       !colony_ships_.count(ship) ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       it->second.spaceport_phase!=3 || it->second.housing_phase!=2 ||
       it->second.utility_phase || it->second.housing_modules<2 ||
       it->second.workforce_policy || it->second.food_kg<2 ||
       it->second.water_kg<2 || it->second.oxygen_kg<2 ||
       submitted_ids_.count(order_id) ||
       shuttle_flight_request_ids_.count(order_id) ||
       local_delivery_orders_.count(order_id) ||
       voyages_.count(order_id))return false;
    const auto worker=npcs_.find(qualified_supervisor);
    if(worker==npcs_.end() || worker->second.home_ship!=ship ||
       worker->second.location!=it->second.surface_location ||
       worker->second.assigned_site!=it->second.surface_location ||
       worker->second.role!=NpcRole::PlanetarySurface ||
       !workshop_trade_certified(qualified_supervisor,WorkshopTrade::Shape) ||
       !npc_available_for_duty(qualified_supervisor))return false;
    if(std::find(it->second.delegated_npcs.begin(),
                 it->second.delegated_npcs.end(),
                 qualified_supervisor)==it->second.delegated_npcs.end())
        return false;
    for(const auto id:it->second.delegated_npcs)
        if(!id || !npc_available_for_duty(id))return false;
    for(const auto& [unused,other]:planetary_sites_) {
        (void)unused;
        if(other.housing_order_id==order_id ||
           other.utility_order_id==order_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==order_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==order_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==order_id)return false;
        for(const auto request:other.founder_consent_requests)
            if(request==order_id)return false;
        for(const auto& [visitor,proof]:other.visitor_residency) {
            (void)visitor;
            if(proof[1]==order_id || proof[2]==order_id)return false;
        }
    }
    auto& site=it->second;
    site.housing_modules-=2;
    site.embodied_ore+=4;
    site.utility_phase=1;
    site.utility_progress=0;
    site.utility_order_id=order_id;
    site.utility_supervisor=qualified_supervisor;
    site.utility_stalled_ticks=0;
    site.utility_completed_tick=0;
    snapshot_version_=std::max(snapshot_version_,110);
    return true;
}

bool Simulation::pause_planetary_surface_utility(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,bool pause) {
    auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || it==planetary_sites_.end() ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       it->second.utility_phase!=1 ||
       it->second.utility_paused==pause)return false;
    it->second.utility_paused=pause;
    snapshot_version_=std::max(snapshot_version_,110);
    return true;
}

// WO3-D: real single-owner supervisory handover at an existing constructed
// surface site. Only the delegated qualified worker may take over the
// ORIGINAL unfinished 60-tick job; material and work-in-progress are fixed.
bool Simulation::handover_planetary_surface_utility(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t previous,
    std::uint64_t replacement,std::uint64_t request_id) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || !ship || !previous ||
       !replacement || previous==replacement || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() ||
       it==planetary_sites_.end() || !entities_.count(ship) ||
       entities_.at(ship).owner_id!=owner ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       it->second.spaceport_phase!=3 || it->second.utility_phase!=1 ||
       it->second.utility_supervisor!=previous ||
       it->second.utility_handovers.size()>=16 ||
       it->second.workforce_policy ||
       it->second.food_kg<2 || it->second.water_kg<2 ||
       it->second.oxygen_kg<2 ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       voyages_.count(request_id))return false;
    const auto& s=it->second;
    if(!s.utility_handovers.empty() &&
       s.utility_handovers.back().tick>=tick_)return false;
    const auto worker=npcs_.find(replacement);
    if(worker==npcs_.end() || worker->second.home_ship!=ship ||
       worker->second.location!=s.surface_location ||
       !workshop_trade_certified(replacement,WorkshopTrade::Shape))
        return false;
    const bool original_builder=
        std::find(s.delegated_npcs.begin(),s.delegated_npcs.end(),
                  replacement)!=s.delegated_npcs.end() &&
        worker->second.assigned_site==s.surface_location &&
        worker->second.role==NpcRole::PlanetarySurface &&
        npc_available_for_duty(replacement);
    // A relief worker may instead be a REAL disembarked shuttle visitor with
    // an owner-approved original temporary work duty and individual arrival
    // proof. Ordinary passengers/visitors have no supervisory authority.
    const auto shuttle=planetary_shuttles_.find(key);
    const bool duty_visitor=shuttle!=planetary_shuttles_.end() &&
        worker->second.assigned_site==ship &&
        worker->second.role==NpcRole::Ship &&
        !worker->second.transport_id &&
        std::binary_search(shuttle->second.duty_ids.begin(),
                           shuttle->second.duty_ids.end(),replacement) &&
        std::binary_search(shuttle->second.disembarked_ids.begin(),
                           shuttle->second.disembarked_ids.end(),replacement) &&
        shuttle->second.visitor_arrival_receipts.count(replacement) &&
        npc_transport_arrival(
            shuttle->second.visitor_arrival_receipts.at(replacement),
            replacement).has_value() &&
        (npc_health_status(replacement)==NpcHealthStatus::Healthy ||
         npc_health_status(replacement)==NpcHealthStatus::Treated ||
         npc_health_status(replacement)==NpcHealthStatus::Recovered);
    if(!original_builder && !duty_visitor)return false;
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(request_id==other.housing_order_id ||
           request_id==other.utility_order_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto founding:other.founder_consent_requests)
            if(request_id==founding)return false;
        for(const auto& handover:other.utility_handovers)
            if(handover.request_id==request_id)return false;
        for(const auto& [id,resident]:other.visitor_residency) {
            (void)id;
            if(request_id==resident[1] || request_id==resident[2])
                return false;
        }
    }
    it->second.utility_handovers.push_back({
        request_id,tick_,previous,replacement});
    it->second.utility_supervisor=replacement;
    snapshot_version_=std::max(snapshot_version_,112);
    return true;
}

// WO3-D v113: unlike the old orbit-only instant install, the materials
// for this project have already arrived in the original surface module rack.
// Both original trades build a structural installation under Shape certification;
// the resulting farm/extractor uses the existing real inventories and work policy.
// WO3-D/v117: two more real surface module families. The original
// housing_module rack delivers the SAME physical 2-tonne items to this site;
// no independent or instant facility constructor exists.
bool Simulation::start_planetary_service_build(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool annex,std::uint64_t order_id,
        std::uint64_t supervisor) {
    const auto index=annex?std::size_t{0}:std::size_t{1};
    auto site=planetary_sites_.find(key);
    const auto ship_entity=entities_.find(ship);
    const auto frame=ship_frames_.find(ship);
    if(!owner || owner==kServerAuthority || !order_id ||
       order_id==std::numeric_limits<std::uint64_t>::max() || !supervisor ||
       site==planetary_sites_.end() ||
       ship_entity==entities_.end() || ship_entity->second.owner_id!=owner ||
       !colony_ships_.count(ship) || frame==ship_frames_.end() ||
       frame->second.anchor_body!=std::get<0>(key) ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || site->second.housing_phase!=2 ||
       site->second.utility_phase!=2 || site->second.workforce_policy ||
       site->second.housing_modules!=2 ||
       site->second.food_kg<2 || site->second.water_kg<2 ||
       site->second.oxygen_kg<2 ||
       site->second.service_projects[index].phase ||
       site->second.service_projects[1-index].phase==1 ||
       std::any_of(site->second.production_projects.begin(),
                   site->second.production_projects.end(),
                   [](const auto& p){return p.phase==1;}) ||
       submitted_ids_.count(order_id) ||
       shuttle_flight_request_ids_.count(order_id) ||
       planetary_family_booking_ids_.count(order_id) ||
       pending_celestial_transits_.count(order_id) ||
       completed_celestial_transits_.count(order_id) ||
       local_delivery_orders_.count(order_id) || voyages_.count(order_id) ||
       (legacy_planetary_residence_import_ &&
        legacy_planetary_residence_import_->host_request_id==order_id))
        return false;
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(order_id==other.housing_order_id ||
           order_id==other.utility_order_id)return false;
        for(const auto& project:other.production_projects)
            if(project.order_id==order_id)return false;
        for(const auto& project:other.service_projects)
            if(project.order_id==order_id)return false;
        for(const auto& chain:other.production_handovers)
            for(const auto& r:chain)
                if(r.request_id==order_id)return false;
        for(const auto& chain:other.service_handovers)
            for(const auto& r:chain)
                if(r.request_id==order_id)return false;
        for(const auto& r:other.utility_handovers)
            if(r.request_id==order_id)return false;
        for(const auto id:other.founder_consent_requests)
            if(id==order_id)return false;
        for(const auto& [person,proof]:other.visitor_residency) {
            (void)person;
            if(proof[1]==order_id || proof[2]==order_id)return false;
        }
    }
    const auto person=npcs_.find(supervisor);
    if(person==npcs_.end() || person->second.home_ship!=ship ||
       person->second.location!=site->second.surface_location ||
       !workshop_trade_certified(supervisor,WorkshopTrade::Shape))
        return false;
    const bool delegated=
        std::find(site->second.delegated_npcs.begin(),
                  site->second.delegated_npcs.end(),supervisor)!=
                  site->second.delegated_npcs.end() &&
        person->second.assigned_site==site->second.surface_location &&
        person->second.role==NpcRole::PlanetarySurface &&
        npc_available_for_duty(supervisor);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto health=npc_health_status(supervisor);
    const bool visiting=shuttle!=planetary_shuttles_.end() &&
        person->second.assigned_site==ship &&
        person->second.role==NpcRole::Ship &&
        !person->second.transport_id &&
        std::binary_search(shuttle->second.duty_ids.begin(),
                           shuttle->second.duty_ids.end(),supervisor) &&
        std::binary_search(shuttle->second.disembarked_ids.begin(),
                           shuttle->second.disembarked_ids.end(),supervisor) &&
        shuttle->second.visitor_arrival_receipts.count(supervisor) &&
        npc_transport_arrival(
            shuttle->second.visitor_arrival_receipts.at(supervisor),
            supervisor).has_value() &&
        (health==NpcHealthStatus::Healthy ||
         health==NpcHealthStatus::Treated ||
         health==NpcHealthStatus::Recovered);
    if(!delegated && !visiting)return false;
    for(const auto id:site->second.delegated_npcs)
        if(!id || !npc_available_for_duty(id))return false;
    auto& ground=site->second;
    ground.housing_modules-=2;
    ground.embodied_ore+=4;
    auto& p=ground.service_projects[index];
    p.phase=1;p.progress=0;p.order_id=order_id;
    p.supervisor=supervisor;p.stalled_ticks=0;
    p.completed_tick=0;p.powered_cycles=0;p.paused=false;
    snapshot_version_=std::max(snapshot_version_,117);
    return true;
}

bool Simulation::pause_planetary_service_build(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool annex,bool pause) {
    const auto site=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner)
        return false;
    auto& project=site->second.service_projects[annex?0:1];
    if(project.phase!=1 || project.paused==pause)return false;
    project.paused=pause;
    return true;
}

bool Simulation::handover_planetary_service_build(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,bool annex,std::uint64_t previous,
        std::uint64_t replacement,std::uint64_t request_id) {
    const auto site=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || !previous || !replacement ||
       previous==replacement || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() ||
       site==planetary_sites_.end() ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.workforce_policy ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       planetary_family_booking_ids_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id) ||
       local_delivery_orders_.count(request_id) || voyages_.count(request_id) ||
       (legacy_planetary_residence_import_ &&
        legacy_planetary_residence_import_->host_request_id==request_id))
        return false;
    const auto index=annex?std::size_t{0}:std::size_t{1};
    auto& st=site->second;
    auto& p=st.service_projects[index];
    auto& chain=st.service_handovers[index];
    if(p.phase!=1 || p.supervisor!=previous || chain.size()>=16 ||
       (!chain.empty() && chain.back().tick>=tick_))
        return false;
    for(const auto& [key,other]:planetary_sites_) {
        (void)key;
        if(request_id==other.housing_order_id ||
           request_id==other.utility_order_id)return false;
        for(const auto& work:other.service_projects)
            if(work.order_id==request_id)return false;
        for(const auto& work:other.production_projects)
            if(work.order_id==request_id)return false;
        for(const auto& history:other.service_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==request_id)return false;
        for(const auto id:other.founder_consent_requests)
            if(id==request_id)return false;
        for(const auto& [person,proof]:other.visitor_residency) {
            (void)person;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    const auto npc=npcs_.find(replacement);
    if(npc==npcs_.end() || npc->second.home_ship!=ship ||
       npc->second.location!=st.surface_location ||
       !workshop_trade_certified(replacement,WorkshopTrade::Shape))
        return false;
    const bool delegated=
        std::find(st.delegated_npcs.begin(),st.delegated_npcs.end(),
                  replacement)!=st.delegated_npcs.end() &&
        npc->second.assigned_site==st.surface_location &&
        npc->second.role==NpcRole::PlanetarySurface &&
        npc_available_for_duty(replacement);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto health=npc_health_status(replacement);
    const bool visitor=shuttle!=planetary_shuttles_.end() &&
        npc->second.assigned_site==ship && npc->second.role==NpcRole::Ship &&
        !npc->second.transport_id &&
        std::binary_search(shuttle->second.duty_ids.begin(),
                           shuttle->second.duty_ids.end(),replacement) &&
        std::binary_search(shuttle->second.disembarked_ids.begin(),
                           shuttle->second.disembarked_ids.end(),replacement) &&
        shuttle->second.visitor_arrival_receipts.count(replacement) &&
        npc_transport_arrival(
            shuttle->second.visitor_arrival_receipts.at(replacement),
            replacement).has_value() &&
        (health==NpcHealthStatus::Healthy ||
         health==NpcHealthStatus::Treated ||
         health==NpcHealthStatus::Recovered);
    if(!delegated && !visitor)return false;
    chain.push_back({request_id,tick_,previous,replacement});
    p.supervisor=replacement;
    return true;
}

bool Simulation::start_planetary_production_build(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,bool farm,std::uint64_t order_id,
    std::uint64_t supervisor) {
    if(planetary_family_booking_ids_.count(order_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       order_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto site=planetary_sites_.find(key);
    const auto frame=ship_frames_.find(ship);
    const auto ship_entity=entities_.find(ship);
    const auto geography=scan_planetary_geography(
        owner,ship,std::get<0>(key),std::get<1>(key),std::get<2>(key));
    const auto index=farm?std::size_t{0}:std::size_t{1};
    if(!owner || owner==kServerAuthority || !order_id ||
       order_id==std::numeric_limits<std::uint64_t>::max() ||
       !supervisor || site==planetary_sites_.end() ||
       ship_entity==entities_.end() || ship_entity->second.owner_id!=owner ||
       !colony_ships_.count(ship) || frame==ship_frames_.end() ||
       frame->second.anchor_body!=std::get<0>(key) || !geography ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || site->second.housing_phase!=2 ||
       site->second.utility_phase!=2 || site->second.workforce_policy ||
       site->second.housing_modules!=2 || site->second.food_kg<2 ||
       site->second.water_kg<2 || site->second.oxygen_kg<2 ||
       site->second.production_projects[index].phase ||
       site->second.production_projects[1-index].phase==1 ||
       (farm ? (site->second.farm ||
           !geography->landscape.region.farming_possible) :
           (site->second.extractor ||
            !oasis_reserves_.count(std::get<0>(key)))) ||
       submitted_ids_.count(order_id) ||
       shuttle_flight_request_ids_.count(order_id) ||
       pending_celestial_transits_.count(order_id) ||
       completed_celestial_transits_.count(order_id) ||
       local_delivery_orders_.count(order_id) ||
       voyages_.count(order_id))return false;
    if(!farm) {
        const auto source=planetary_oasis_source_region(
            owner,ship,std::get<0>(key));
        if(!source || source->latitude!=std::get<1>(key) ||
           source->longitude!=std::get<2>(key))return false;
    }
    const auto worker=npcs_.find(supervisor);
    if(worker==npcs_.end() || worker->second.home_ship!=ship ||
       worker->second.location!=site->second.surface_location ||
       !workshop_trade_certified(supervisor,WorkshopTrade::Shape))return false;
    const bool delegate=
        std::find(site->second.delegated_npcs.begin(),
                  site->second.delegated_npcs.end(),supervisor)!=
                  site->second.delegated_npcs.end() &&
        worker->second.assigned_site==site->second.surface_location &&
        worker->second.role==NpcRole::PlanetarySurface &&
        npc_available_for_duty(supervisor);
    const auto shuttle=planetary_shuttles_.find(key);
    const bool visiting_builder=shuttle!=planetary_shuttles_.end() &&
        worker->second.assigned_site==ship &&
        worker->second.role==NpcRole::Ship &&
        !worker->second.transport_id &&
        std::binary_search(shuttle->second.disembarked_ids.begin(),
                           shuttle->second.disembarked_ids.end(),supervisor) &&
        std::binary_search(shuttle->second.duty_ids.begin(),
                           shuttle->second.duty_ids.end(),supervisor) &&
        shuttle->second.visitor_arrival_receipts.count(supervisor) &&
        npc_transport_arrival(
            shuttle->second.visitor_arrival_receipts.at(supervisor),
            supervisor).has_value() &&
        (npc_health_status(supervisor)==NpcHealthStatus::Healthy ||
         npc_health_status(supervisor)==NpcHealthStatus::Treated ||
         npc_health_status(supervisor)==NpcHealthStatus::Recovered);
    if(!delegate && !visiting_builder)return false;
    for(const auto id:site->second.delegated_npcs)
        if(!id || !npc_available_for_duty(id))return false;
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(order_id==other.housing_order_id ||
           order_id==other.utility_order_id)return false;
        for(const auto& project:other.production_projects)
            if(project.order_id==order_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==order_id)return false;
        for(const auto id:other.founder_consent_requests)
            if(id==order_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==order_id)return false;
        for(const auto& [id,resident]:other.visitor_residency) {
            (void)id;
            if(resident[1]==order_id || resident[2]==order_id)return false;
        }
    }
    auto& ground=site->second;
    ground.housing_modules-=2; // exactly two existing landed modules
    ground.embodied_ore+=4; // same conserved mass, now committed to WIP
    auto& project=ground.production_projects[index];
    project.phase=1;
    project.progress=0;
    project.order_id=order_id;
    project.supervisor=supervisor;
    project.stalled_ticks=0;
    project.completed_tick=0;
    project.paused=false;
    snapshot_version_=std::max(snapshot_version_,113);
    return true;
}

bool Simulation::pause_planetary_production_build(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,bool farm,bool pause) {
    const auto site=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner)return false;
    auto& project=site->second.production_projects[farm?0:1];
    if(project.phase!=1 || project.paused==pause)return false;
    project.paused=pause;
    snapshot_version_=std::max(snapshot_version_,113);
    return true;
}

// WO3-D/v114: original project progress and finite deposited material survive
// an owner-approved handover. Replacement is an existing certified named
// delegate or a genuinely disembarked and authorized original shuttle worker.
bool Simulation::handover_planetary_production_build(PlayerId owner,
    EntityId ship,const PlanetarySiteKey& key,bool farm,
    std::uint64_t previous,std::uint64_t replacement,
    std::uint64_t request_id) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto site=planetary_sites_.find(key);
    const auto index=farm?std::size_t{0}:std::size_t{1};
    if(!owner || owner==kServerAuthority || !previous || !replacement ||
       previous==replacement || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() ||
       site==planetary_sites_.end() || !entities_.count(ship) ||
       entities_.at(ship).owner_id!=owner ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       site->second.production_projects[index].phase!=1 ||
       site->second.production_projects[index].supervisor!=previous ||
       site->second.production_handovers[index].size()>=16 ||
       site->second.workforce_policy ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       voyages_.count(request_id))return false;
    auto& st=site->second;
    auto& changes=st.production_handovers[index];
    if(!changes.empty() && changes.back().tick>=tick_)return false;
    const auto person=npcs_.find(replacement);
    if(person==npcs_.end() || person->second.home_ship!=ship ||
       person->second.location!=st.surface_location ||
       !workshop_trade_certified(replacement,WorkshopTrade::Shape))
        return false;
    const bool delegated=
        std::find(st.delegated_npcs.begin(),st.delegated_npcs.end(),
                  replacement)!=st.delegated_npcs.end() &&
        person->second.assigned_site==st.surface_location &&
        person->second.role==NpcRole::PlanetarySurface &&
        npc_available_for_duty(replacement);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto health=npc_health_status(replacement);
    const bool visiting=shuttle!=planetary_shuttles_.end() &&
        person->second.assigned_site==ship &&
        person->second.role==NpcRole::Ship &&
        !person->second.transport_id &&
        std::binary_search(shuttle->second.duty_ids.begin(),
                           shuttle->second.duty_ids.end(),replacement) &&
        std::binary_search(shuttle->second.disembarked_ids.begin(),
                           shuttle->second.disembarked_ids.end(),replacement) &&
        shuttle->second.visitor_arrival_receipts.count(replacement) &&
        npc_transport_arrival(
            shuttle->second.visitor_arrival_receipts.at(replacement),
            replacement).has_value() &&
        (health==NpcHealthStatus::Healthy ||
         health==NpcHealthStatus::Treated ||
         health==NpcHealthStatus::Recovered);
    if(!delegated && !visiting)return false;
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(request_id==other.housing_order_id ||
           request_id==other.utility_order_id)return false;
        for(const auto& project:other.production_projects)
            if(project.order_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto& handover:other.utility_handovers)
            if(handover.request_id==request_id)return false;
        for(const auto& list:other.production_handovers)
            for(const auto& handover:list)
                if(handover.request_id==request_id)return false;
        for(const auto founding:other.founder_consent_requests)
            if(founding==request_id)return false;
        for(const auto& [id,proof]:other.visitor_residency) {
            (void)id;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    changes.push_back({request_id,tick_,previous,replacement});
    st.production_projects[index].supervisor=replacement;
    snapshot_version_=std::max(snapshot_version_,114);
    return true;
}

// WO3-E: one trusted, explicit upgrade from pre-housing v86-v108 legal
// addresses. The original people and their registered legal addresses are
// unchanged. This import is neither a shuttle arrival nor four new beds.
bool Simulation::host_import_legacy_planetary_residences(
        PlayerId host_principal,std::uint64_t request_id) {
    if(host_principal!=kServerAuthority || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() ||
       snapshot_version_<86 || snapshot_version_>=109 ||
       legacy_planetary_residence_import_ ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       voyages_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       npcs_.count(request_id) || casualties_.count(request_id) ||
       physical_dependents_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id))
        return false;
    LegacyPlanetaryResidenceImport imported{};
    imported.source_version=std::uint64_t(snapshot_version_);
    imported.source_hash=state_hash();
    imported.host_request_id=request_id;
    imported.recorded_tick=tick_;
    const auto collect=[&](const NpcRecord& person) {
        if(person.legal_residence().kind!=transport::LocationKind::PlanetarySite)
            return true;
        const auto address=person.legal_residence().id;
        unsigned matched=0;
        for(const auto& [key,site]:planetary_sites_) {
            (void)key;
            if(site.surface_location!=address)continue;
            ++matched;
            const auto ship=entities_.find(site.source_ship);
            if(site.spaceport_phase!=3 || site.owner==0 ||
               person.home_ship!=site.source_ship ||
               ship==entities_.end() || ship->second.owner_id!=site.owner)
                return false;
        }
        if(matched!=1 || !imported.legal_addresses.emplace(person.id,address).second)
            return false;
        return true;
    };
    for(const auto& [id,n]:npcs_) {
        (void)id;
        if(!collect(n))return false;
    }
    for(const auto& [id,casualty]:casualties_) {
        (void)id;
        if(!collect(casualty.identity))return false;
    }
    if(imported.legal_addresses.empty() ||
       imported.legal_addresses.size()>10000)return false;
    legacy_planetary_residence_import_=std::move(imported);
    snapshot_version_=115;
    return true;
}

bool Simulation::settle_planetary_founder(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t npc_id,
    std::uint64_t request_id,bool voluntary_consent_attested) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    const auto it=planetary_sites_.find(key);
    if(!owner || owner==kServerAuthority || !npc_id || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() ||
       !voluntary_consent_attested ||
       it==planetary_sites_.end() ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       !colony_ships_.count(ship) ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       it->second.spaceport_phase!=3 || it->second.housing_phase!=2 ||
       it->second.food_kg<4 || it->second.water_kg<4 ||
       it->second.oxygen_kg<4 ||
       it->second.housing_order_id==request_id ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       voyages_.count(request_id))return false;
    for(const auto& [key_other,other]:planetary_sites_) {
        (void)key_other;
        if(other.housing_order_id==request_id ||
           other.utility_order_id==request_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==request_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto prior:other.founder_consent_requests)
            if(prior==request_id)return false;
        for(const auto& [visitor,proof]:other.visitor_residency) {
            (void)visitor;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    const auto n=npcs_.find(npc_id);
    if(n==npcs_.end() || n->second.home_ship!=ship ||
       n->second.location!=it->second.surface_location ||
       n->second.assigned_site!=it->second.surface_location ||
       n->second.role!=NpcRole::PlanetarySurface ||
       !npc_available_for_duty(npc_id) ||
       n->second.legal_residence()!=transport::Endpoint{
           transport::LocationKind::ColonyShip,ship})return false;
    for(const auto& [dependent_id,dependent]:physical_dependents_) {
        (void)dependent_id;
        if(dependent.guardian_id==npc_id)return false;
    }
    auto& site=it->second;
    for(std::size_t i=0;i<site.delegated_npcs.size();++i) {
        if(site.delegated_npcs[i]!=npc_id)continue;
        if(site.founder_residents[i] || site.founder_consent_requests[i])
            return false;
        site.founder_residents[i]=npc_id;
        site.founder_consent_requests[i]=request_id;
        n->second.residence_kind=transport::LocationKind::PlanetarySite;
        n->second.residence_id=site.surface_location;
        snapshot_version_=std::max(snapshot_version_,109);
        return true;
    }
    return false; // arriving visitors require separate proven migration
}

// WO3-E: original physical shuttle disembarkation is mandatory. The
// owner attests the independent adult's consent; no new NPC or cargo exists.
bool Simulation::settle_planetary_visitor(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t npc_id,
    std::uint64_t request_id,bool consent) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto it=planetary_sites_.find(key);
    auto sh=planetary_shuttles_.find(key);
    auto person=npcs_.find(npc_id);
    if(!owner || owner==kServerAuthority || !npc_id || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() || !consent ||
       it==planetary_sites_.end() || sh==planetary_shuttles_.end() ||
       person==npcs_.end() || !entities_.count(ship) ||
       entities_.at(ship).owner_id!=owner ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       it->second.spaceport_phase!=3 || it->second.housing_phase!=2 ||
       it->second.food_kg<4 || it->second.water_kg<4 ||
       it->second.oxygen_kg<4 ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       it->second.visitor_residency.count(npc_id) ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       voyages_.count(request_id))return false;
    auto& n=person->second;
    if(n.home_ship!=ship || n.location!=it->second.surface_location ||
       n.assigned_site!=ship || n.role!=NpcRole::Ship || n.transport_id ||
       n.legal_residence()!=transport::Endpoint{
           transport::LocationKind::ColonyShip,ship} ||
       !std::binary_search(sh->second.disembarked_ids.begin(),
                           sh->second.disembarked_ids.end(),npc_id))
        return false;
    const auto arrival=sh->second.visitor_arrival_receipts.find(npc_id);
    if(arrival==sh->second.visitor_arrival_receipts.end() ||
       !arrival->second ||
       !npc_transport_arrival(arrival->second,npc_id))return false;
    for(const auto& [child,dependent]:physical_dependents_) {
        (void)child;
        if(dependent.guardian_id==npc_id)return false;
    }
    std::size_t visiting_residents=0;
    for(const auto& [id,record]:it->second.visitor_residency) {
        (void)id;
        if(record[2]==0)++visiting_residents;
    }
    // Two original workers occupy two of FOUR beds, even before their
    // administrative founder decisions. No crowded or fictional household.
    if(visiting_residents>=2)return false;
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(other.housing_order_id==request_id ||
           other.utility_order_id==request_id ||
           other.visitor_residency.count(npc_id))return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==request_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto proof:other.founder_consent_requests)
            if(proof==request_id)return false;
        for(const auto& [id,proof]:other.visitor_residency) {
            (void)id;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    it->second.visitor_residency.emplace(npc_id,
        std::array<std::uint64_t,3>{arrival->second,request_id,0});
    n.residence_kind=transport::LocationKind::PlanetarySite;
    n.residence_id=it->second.surface_location;
    snapshot_version_=std::max(snapshot_version_,111);
    return true;
}

bool Simulation::depart_planetary_visitor(PlayerId owner,EntityId ship,
    const PlanetarySiteKey& key,std::uint64_t npc_id,
    std::uint64_t request_id,bool consent) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto it=planetary_sites_.find(key);
    auto sh=planetary_shuttles_.find(key);
    auto person=npcs_.find(npc_id);
    if(!owner || owner==kServerAuthority || !npc_id || !request_id ||
       request_id==std::numeric_limits<std::uint64_t>::max() || !consent ||
       it==planetary_sites_.end() || sh==planetary_shuttles_.end() ||
       person==npcs_.end() || !entities_.count(ship) ||
       entities_.at(ship).owner_id!=owner ||
       it->second.owner!=owner || it->second.source_ship!=ship ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       sh->second.flight_phase!=2 ||
       sh->second.passenger_ids.size()>=3 ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       voyages_.count(request_id))return false;
    auto record=it->second.visitor_residency.find(npc_id);
    if(record==it->second.visitor_residency.end() ||
       record->second[2] || !record->second[0] || !record->second[1] ||
       person->second.home_ship!=ship ||
       person->second.location!=it->second.surface_location ||
       person->second.assigned_site!=ship ||
       person->second.role!=NpcRole::Ship ||
       person->second.transport_id ||
       person->second.legal_residence()!=transport::Endpoint{
           transport::LocationKind::PlanetarySite,
           it->second.surface_location})return false;
    for(const auto& [child,dependent]:physical_dependents_) {
        (void)child;
        if(dependent.guardian_id==npc_id)return false;
    }
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(other.housing_order_id==request_id ||
           other.utility_order_id==request_id)return false;
        for(const auto& relief:other.utility_handovers)
            if(relief.request_id==request_id)return false;
        for(const auto& production:other.production_projects)
            if(production.order_id==request_id)return false;
        for(const auto& history:other.production_handovers)
            for(const auto& relief:history)
                if(relief.request_id==request_id)return false;
        for(const auto proof:other.founder_consent_requests)
            if(proof==request_id)return false;
        for(const auto& [id,proof]:other.visitor_residency) {
            (void)id;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    const auto landing=sh->second.visitor_arrival_receipts.find(npc_id);
    if(landing==sh->second.visitor_arrival_receipts.end() ||
       landing->second!=record->second[0] ||
       !npc_transport_arrival(landing->second,npc_id))return false;
    // Reboard with the original canonical shuttle operation, moving exactly
    // one physical person into one real seat. Prevent the public reboard API
    // from bypassing legal departure: perform both changes in the same
    // original-world atomic clone, restoring legal status on local failure.
    person->second.residence_kind=transport::LocationKind::ColonyShip;
    person->second.residence_id=ship;
    if(!transfer_planetary_shuttle_visitor(owner,ship,key,npc_id,true)) {
        person->second.residence_kind=transport::LocationKind::PlanetarySite;
        person->second.residence_id=it->second.surface_location;
        return false;
    }
    record->second[2]=request_id;
    snapshot_version_=std::max(snapshot_version_,111);
    return true;
}

bool Simulation::post_local_delivery_order(PlayerId owner,EntityId board_ship,
       const PlanetarySiteKey& key,ShuttleCargo cargo,
       std::int64_t kilograms,std::uint64_t order_id) {
    // This initial job adapter executes only a physical planetary-site ->
    // orbiting parent ship route. Unbuilt facilities and unrelated colony
    // destinations do not get phantom transport capacity.
    const auto site=planetary_sites_.find(key);
    const auto shuttle=planetary_shuttles_.find(key);
    const auto ship=entities_.find(board_ship);
    if(!owner || owner==kServerAuthority || !board_ship || !order_id ||
       order_id>=(std::uint64_t{1}<<50) ||
       kilograms<=0 || kilograms>1000 || cargo>ShuttleCargo::Nutrients ||
       site==planetary_sites_.end() ||
       shuttle==planetary_shuttles_.end() ||
       site->second.owner!=owner || site->second.source_ship!=board_ship ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=board_ship ||
       ship==entities_.end() || ship->second.owner_id!=owner ||
       !colony_ships_.count(board_ship) ||
       local_delivery_orders_.count(order_id) ||
       local_delivery_orders_.size()>=10000)
        return false;
    // Multiple finite jobs may wait at the same local board. Only the
    // actual shuttle execution is exclusive; pending orders hold no stock.
    local_delivery_orders_.emplace(order_id,LocalDeliveryOrder{
        order_id,owner,board_ship,key,cargo,kilograms,0,
        LocalDeliveryStage::Posted,tick_});
    snapshot_version_=std::max(snapshot_version_,88);
    return true;
}
bool Simulation::cancel_local_delivery_order(PlayerId owner,std::uint64_t order_id) {
    auto it=local_delivery_orders_.find(order_id);
    if(!owner || it==local_delivery_orders_.end() ||
       it->second.owner!=owner ||
       it->second.stage!=LocalDeliveryStage::Posted)return false;
    // Once a real flight begins, cargo and pilot remain physically in the
    // shuttle. An in-flight contract cannot erase or teleport either.
    it->second.stage=LocalDeliveryStage::Cancelled;
    // Cancellation of an unstarted pickup releases only its *plan*. It
    // never removes material that has already entered a physical carrier.
    const auto leg=intercolony_freight_.find(order_id);
    if(leg!=intercolony_freight_.end() &&
       leg->second.stage==IntercolonyFreightStage::AwaitingPickup)
        intercolony_freight_.erase(leg);
    // Revoking the original unstarted physical job also revokes any
    // still-active derived coordinator permission. In particular, a
    // preplanned freight leg may have just been erased above; retaining
    // an active delegation to that erased leg would corrupt recovery.
    for(auto& [id,d]:work_directives_){
        (void)id;
        if(d.owner!=owner || d.source_order!=order_id ||
           d.colony_ship!=it->second.board_ship ||
           (d.kind!=WorkTaskKind::LocalDelivery &&
            d.kind!=WorkTaskKind::IntercolonyFreight) ||
           d.status!=WorkDirectiveStatus::Active)continue;
        d.status=WorkDirectiveStatus::Cancelled;
        d.blocked=WorkBlock::None;
        d.resolved_tick=tick_;
        d.last_update_tick=tick_;
    }
    return true;
}
void Simulation::process_local_delivery_board() {
    // Pre-index claimed physical shuttles once per tick. Do not search all
    // jobs for each pending instruction (quadratic at large work boards).
    std::set<PlanetarySiteKey> occupied_sites;
    for(const auto& [id,work]:local_delivery_orders_) {
        (void)id;
        if(work.stage!=LocalDeliveryStage::Posted &&
           work.stage!=LocalDeliveryStage::Complete &&
           work.stage!=LocalDeliveryStage::Cancelled)
            occupied_sites.insert(work.site);
    }
    for(auto& [id,work]:local_delivery_orders_) {
        if(work.stage==LocalDeliveryStage::Complete ||
           work.stage==LocalDeliveryStage::Cancelled)continue;
        const auto site=planetary_sites_.find(work.site);
        const auto shuttle=planetary_shuttles_.find(work.site);
        if(site==planetary_sites_.end() || shuttle==planetary_shuttles_.end())
            continue;
        auto& sh=shuttle->second;
        if(sh.owner!=work.owner || sh.source_ship!=work.board_ship ||
           site->second.owner!=work.owner ||
           site->second.source_ship!=work.board_ship ||
           site->second.spaceport_phase!=3)continue;
        const auto flight_id=[id](bool ascend) {
            return 0x10000000000000ULL | (id<<1) |
                   std::uint64_t(ascend?1:0);
        };
        const auto source_stock=[&]()->std::int64_t {
            return work.cargo==ShuttleCargo::Water?site->second.water_kg:
                   (work.cargo==ShuttleCargo::Food?site->second.food_kg:
                    site->second.nutrients_kg);
        };
        switch(work.stage) {
        case LocalDeliveryStage::Posted: {
            // A claimed job owns the single physical shuttle until it
            // completes. An understocked unclaimed job does not block another.
            if(occupied_sites.count(work.site))break;
            // Only the locally assigned physical shuttle and its NPC pilot
            // may claim this order. No separate transport NPC is invented.
            if(sh.flight_phase!=0 || !sh.passenger_ids.empty() ||
               sh.water_kg<2 || source_stock()<work.kilograms ||
               sh.water_kg+sh.food_kg+sh.nutrients_kg>
                   1000-work.kilograms)break;
            if(!sh.pilot_npc) {
                for(const auto& [npc,person]:npcs_) {
                    if(person.home_ship!=work.board_ship ||
                       person.role!=NpcRole::Ship ||
                       person.location!=work.board_ship ||
                       person.assigned_site!=work.board_ship ||
                       !npc_available_for_duty(npc))continue;
                    if(assign_planetary_shuttle_pilot(
                        work.owner,work.board_ship,work.site,npc))break;
                }
                // Pilot assignment is itself an actual NPC reservation.
                // Avoid launching in the same step as a new assignment.
                // A posted order has no final assignment until a real
                // departure is committed. The pilot may still be changed.
                break;
            }
            if(launch_planetary_shuttle(work.owner,work.board_ship,work.site,
                    flight_id(false),false)) {
                work.assigned_pilot=sh.pilot_npc;
                work.stage=LocalDeliveryStage::Descending;
                occupied_sites.insert(work.site); // exclusive for this tick
            }
            break;
        }
        case LocalDeliveryStage::Descending:
            if(sh.flight_phase==2 && sh.descent_receipt==flight_id(false))
                work.stage=LocalDeliveryStage::Loading;
            break;
        case LocalDeliveryStage::Loading:
            // Actual source stock and physical berth are checked by the
            // existing shuttle load operation. No speculative debits.
            if(sh.flight_phase==2 && sh.descent_receipt==flight_id(false) &&
               load_planetary_shuttle(work.owner,work.board_ship,work.site,
                    work.cargo,work.kilograms))
                work.stage=LocalDeliveryStage::Ascending;
            break;
        case LocalDeliveryStage::Ascending:
            if(sh.flight_phase==2 &&
               launch_planetary_shuttle(work.owner,work.board_ship,work.site,
                    flight_id(true),true))
                work.stage=LocalDeliveryStage::Delivering;
            break;
        case LocalDeliveryStage::Delivering:
            if(sh.flight_phase==0 && sh.ascent_receipt==flight_id(true) &&
               unload_planetary_shuttle(work.owner,work.board_ship,work.site,
                    work.cargo,work.kilograms))
                work.stage=LocalDeliveryStage::Complete;
            break;
        case LocalDeliveryStage::Complete:
        case LocalDeliveryStage::Cancelled:
            break;
        }
    }
    // Same committed simulation tick: only after the planetary shuttle has
    // actually unloaded may a planned second-leg carrier claim real stock.
    // A missing carrier, pilot, berth or source inventory leaves the order
    // awaiting pickup rather than fabricating or teleporting a shipment.
    if(snapshot_version_>=89)
        for(auto& [id,leg]:intercolony_freight_)
            if(leg.stage==IntercolonyFreightStage::AwaitingPickup)
                (void)load_intercolony_freight(
                    leg.owner,id,leg.carrier_ship,leg.destination_ship);
}

namespace {
const char* sealed_freight_code(ShuttleCargo cargo) noexcept {
    switch(cargo) {
    case ShuttleCargo::Water: return "freight_water";
    case ShuttleCargo::Food: return "freight_food";
    case ShuttleCargo::Nutrients: return "freight_nutrients";
    }
    return nullptr;
}
std::int64_t& supply_stack(ColonyLifeSupport& life,ShuttleCargo cargo) {
    switch(cargo) {
    case ShuttleCargo::Water: return life.water_kg;
    case ShuttleCargo::Food: return life.food_kg;
    case ShuttleCargo::Nutrients: return life.nutrients_kg;
    }
    throw std::logic_error("unknown freight resource");
}
bool freight_berth(const Entity& a,const Entity& b) {
    if(a.type!=EntityType::Ship || b.type!=EntityType::Ship ||
       a.dock_state!=DockState::InSpace || b.dock_state!=DockState::InSpace ||
       a.jump_state!=ShipJumpState::Ready || b.jump_state!=ShipJumpState::Ready ||
       a.system_id!=b.system_id)return false;
    const auto dx=a.position.x-b.position.x;
    const auto dy=a.position.y-b.position.y;
    constexpr auto limit=Simulation::kDockingRangeMm;
    return dx>=-limit && dx<=limit && dy>=-limit && dy<=limit &&
           dx*dx+dy*dy<=limit*limit;
}
}
bool Simulation::start_shipyard_construction(PlayerId owner,EntityId colony,
        std::uint64_t order_id,std::array<std::uint64_t,2> workers) {
    // Admission and material conversion happen in the SAME copied Simulation
    // that PersistentWorld journals; the preflight does not reserve resources.
    const auto admission=industry::inspect_shipyard_build(
        *this,owner,colony,order_id,workers);
    if(!admission.permitted() || order_id>=(std::uint64_t{1}<<50) ||
       shipyard_constructions_.size()>=10000 ||
       shipyard_constructions_.count(order_id))return false;
    // One physical yard for this initial colony-scale gate. Expansion will be
    // a separately funded work order rather than cloning free assembly slips.
    for(const auto& [id,project]:shipyard_constructions_) {
        (void)id;
        if(project.colony_ship==colony)return false;
        if(!project.operational())
            for(const auto worker:workers)
                if(worker==project.workers[0] ||
                   worker==project.workers[1])return false;
    }
    const auto e=entities_.find(colony);
    if(e==entities_.end() ||
       e->second.dock_state!=DockState::InSpace ||
       e->second.jump_state!=ShipJumpState::Ready)return false;
    for(const auto& [id,trip]:pending_celestial_transits_){
        (void)id;
        if(trip.ship==colony)return false;
    }
    auto& modules=colony_ships_.at(colony).modules;
    if(modules<4)return false; // guard the exact stock immediately before debit
    modules-=4;
    shipyard_constructions_.emplace(order_id,ShipyardConstruction{
        order_id,owner,colony,workers,8,0,8,tick_,0,0});
    snapshot_version_=std::max(snapshot_version_,90);
    return true;
}
bool Simulation::assign_shipyard_builders(PlayerId owner,std::uint64_t id,
        std::array<std::uint64_t,2> replacement) {
    const auto it=shipyard_constructions_.find(id);
    if(!owner || owner==kServerAuthority ||
       it==shipyard_constructions_.end() ||
       it->second.owner!=owner || it->second.operational() ||
       !replacement[0] || !replacement[1] ||
       replacement[0]==replacement[1] ||
       it->second.workers==replacement)return false;
    const auto ship=it->second.colony_ship;
    const auto entity=entities_.find(ship);
    if(entity==entities_.end() || entity->second.owner_id!=owner ||
       entity->second.type!=EntityType::Ship)return false;
    for(const auto worker:replacement) {
        const auto n=npcs_.find(worker);
        if(n==npcs_.end() || n->second.home_ship!=ship ||
           n->second.location!=ship || n->second.assigned_site!=ship ||
           n->second.role!=NpcRole::Ship || n->second.transport_id ||
           !npc_available_for_duty(worker))return false;
    }
    for(const auto& [other,project]:shipyard_constructions_) {
        if(other==id || project.operational())continue;
        for(const auto worker:replacement)
            if(worker==project.workers[0] ||
               worker==project.workers[1])return false;
    }
    it->second.workers=replacement;
    return true;
}
void Simulation::process_shipyard_construction(){
    for(auto& [id,build]:shipyard_constructions_){
        (void)id;
        if(build.operational())continue;
        const auto entity=entities_.find(build.colony_ship);
        const auto supplies=life_support_.find(build.colony_ship);
        bool active=entity!=entities_.end() &&
            entity->second.type==EntityType::Ship &&
            entity->second.owner_id==build.owner &&
            entity->second.dock_state==DockState::InSpace &&
            entity->second.jump_state==ShipJumpState::Ready &&
            supplies!=life_support_.end() &&
            supplies->second.food_kg>=2 &&
            supplies->second.water_kg>=2 &&
            supplies->second.oxygen_kg>=2;
        for(const auto& [request,trip]:pending_celestial_transits_){
            (void)request;
            if(trip.ship==build.colony_ship){active=false;break;}
        }
        for(const auto worker:build.workers){
            const auto person=npcs_.find(worker);
            if(person==npcs_.end() ||
               person->second.home_ship!=build.colony_ship ||
               person->second.location!=build.colony_ship ||
               person->second.assigned_site!=build.colony_ship ||
               person->second.role!=NpcRole::Ship ||
               person->second.transport_id ||
               !npc_available_for_duty(worker)){
                active=false;break;
            }
        }
        if(active){
            ++build.progress;
            build.last_progress_tick=tick_;
        }else ++build.stalled_ticks;
    }
}
bool Simulation::queue_shipyard_freighter(PlayerId owner,
        std::uint64_t yard_id,std::uint64_t build_id) {
    const auto yard=shipyard_constructions_.find(yard_id);
    if(!owner || owner==kServerAuthority || !build_id ||
       build_id>=(std::uint64_t{1}<<50) || build_id==yard_id ||
       yard==shipyard_constructions_.end() ||
       yard->second.owner!=owner || !yard->second.operational() ||
       freighter_build_orders_.size()>=10000 ||
       freighter_build_orders_.count(build_id))return false;
    for(const auto& [id,contract]:shipyard_customer_orders_) {
        (void)id;
        if(contract.build_order==build_id &&
           contract.stage!=ShipyardCustomerStage::Funded)return false;
        // Once the supplier accepts a customer, the assembly slip belongs
        // to that accepted request until completion or explicit resolution.
        // Direct manufacturing cannot jump ahead of the paid customer's job.
        if(contract.yard_order==yard_id &&
           contract.build_order!=build_id &&
           (contract.stage==ShipyardCustomerStage::Accepted ||
            contract.stage==ShipyardCustomerStage::Funded))
            return false;
    }
    const EntityId ship=yard->second.colony_ship;
    const auto e=entities_.find(ship),inventory=colony_ships_.find(ship);
    const auto life=life_support_.find(ship),frame=ship_frames_.find(ship);
    if(e==entities_.end() || inventory==colony_ships_.end() ||
       life==life_support_.end() || frame==ship_frames_.end() ||
       e->second.type!=EntityType::Ship ||
       e->second.owner_id!=owner ||
       frame->second.system!=e->second.system_id ||
       e->second.dock_state!=DockState::InSpace ||
       e->second.jump_state!=ShipJumpState::Ready ||
       e->second.velocity.x || e->second.velocity.y ||
       inventory->second.modules<6 ||
       life->second.food_kg<2 || life->second.water_kg<2 ||
       life->second.oxygen_kg<2)return false;
    for(const auto& [id,trip]:pending_celestial_transits_){
        (void)id;
        if(trip.ship==ship)return false;
    }
    // One real assembly slip: a previously completed hull no longer
    // occupies it; an unfinished order cannot be superseded or duplicated.
    for(const auto& [id,other]:freighter_build_orders_){
        (void)id;
        if(other.yard_order==yard_id && !other.finished())return false;
    }
    for(const auto worker:yard->second.workers){
        const auto n=npcs_.find(worker);
        if(n==npcs_.end() || n->second.home_ship!=ship ||
           n->second.location!=ship || n->second.assigned_site!=ship ||
           n->second.role!=NpcRole::Ship || n->second.transport_id ||
           !npc_available_for_duty(worker))return false;
    }
    // The 12 ore-equivalent mass is now physically at the assembly yard.
    // No other "ship inventory" or order ledger duplicates those modules.
    inventory->second.modules-=6;
    freighter_build_orders_.emplace(build_id,FreighterBuildOrder{
        build_id,yard_id,owner,ship,0,12,0,12,tick_,0,0,
        frame->second.system,frame->second.anchor_body});
    snapshot_version_=std::max(snapshot_version_,91);
    return true;
}
// SHIP-01F: customer/supplier agreements are real durable work orders.
// Posting an intent never gives the provider free modules, steals an NPC,
// or creates a ship; the buyer's funds are physically delivered later.
bool Simulation::post_shipyard_customer_order(PlayerId customer,
        EntityId buyer_ship,std::uint64_t yard_id,std::uint64_t build_id,
        std::uint64_t contract_id) {
    if(!customer || customer==kServerAuthority ||
       !contract_id || contract_id>=(std::uint64_t{1}<<50) ||
       !build_id || build_id>=(std::uint64_t{1}<<50) ||
       contract_id==build_id || contract_id==yard_id ||
       build_id==yard_id || shipyard_customer_orders_.size()>=10000 ||
       shipyard_customer_orders_.count(contract_id) ||
       freighter_build_orders_.count(build_id))return false;
    const auto yard=shipyard_constructions_.find(yard_id);
    const auto buyer=entities_.find(buyer_ship);
    if(yard==shipyard_constructions_.end() ||
       !yard->second.operational() ||
       buyer==entities_.end() || buyer->second.type!=EntityType::Ship ||
       buyer->second.owner_id!=customer ||
       buyer_ship==yard->second.colony_ship ||
       !colony_ships_.count(buyer_ship) ||
       !life_support_.count(buyer_ship) ||
       !ship_frames_.count(buyer_ship))return false;
    const auto seller=entities_.find(yard->second.colony_ship);
    if(seller==entities_.end() ||
       seller->second.type!=EntityType::Ship ||
       seller->second.owner_id!=yard->second.owner ||
       !colony_ships_.count(yard->second.colony_ship) ||
       !life_support_.count(yard->second.colony_ship))return false;
    // One build ID is immutable from first request to physical hull delivery;
    // unaccepted requests cannot collide silently with another customer's.
    for(const auto& [id,existing]:shipyard_customer_orders_) {
        (void)id;
        if(existing.build_order==build_id)return false;
    }
    shipyard_customer_orders_.emplace(contract_id,ShipyardCustomerOrder{
        contract_id,yard_id,build_id,customer,yard->second.owner,
        buyer_ship,yard->second.colony_ship,
        ShipyardCustomerStage::Requested,tick_,0,0,0,0});
    snapshot_version_=std::max(snapshot_version_,93);
    return true;
}
bool Simulation::accept_shipyard_customer_order(PlayerId supplier,
        std::uint64_t contract_id) {
    const auto it=shipyard_customer_orders_.find(contract_id);
    if(it==shipyard_customer_orders_.end() ||
       !supplier || supplier==kServerAuthority ||
       it->second.supplier_owner!=supplier ||
       it->second.stage!=ShipyardCustomerStage::Requested)return false;
    const auto yard=shipyard_constructions_.find(it->second.yard_order);
    if(yard==shipyard_constructions_.end() ||
       !yard->second.operational() || yard->second.owner!=supplier ||
       yard->second.colony_ship!=it->second.supplier_ship)return false;
    // One active customer assignment at this physical yard. Requests may
    // queue but acceptance must not double-book builder or paid material.
    for(const auto& [id,other]:shipyard_customer_orders_) {
        if(id!=contract_id && other.yard_order==it->second.yard_order &&
           (other.stage==ShipyardCustomerStage::Accepted ||
            other.stage==ShipyardCustomerStage::Funded))return false;
    }
    for(const auto& [id,build]:freighter_build_orders_) {
        (void)id;
        if(build.yard_order==it->second.yard_order && !build.finished())
            return false;
    }
    it->second.stage=ShipyardCustomerStage::Accepted;
    it->second.accepted_tick=tick_;
    return true;
}
// WORK-01B.3: funding PREVIEW for the universal coordinator. It only
// inspects original buyer inventory, supplier yard/slip, ship positions,
// native builder duty and contractual consent. Original fund/queue methods
// still revalidate and make the sole real material transition.
bool Simulation::can_fund_shipyard_customer_order(
        PlayerId customer,std::uint64_t contract_id) const {
    const auto it=shipyard_customer_orders_.find(contract_id);
    if(!customer || customer==kServerAuthority ||
       it==shipyard_customer_orders_.end() ||
       it->second.customer_owner!=customer ||
       it->second.stage!=ShipyardCustomerStage::Accepted ||
       freighter_build_orders_.size()>=10000 ||
       freighter_build_orders_.count(it->second.build_order))
        return false;
    const auto& c=it->second;
    const auto yard=shipyard_constructions_.find(c.yard_order);
    const auto buyer=entities_.find(c.customer_ship);
    const auto seller=entities_.find(c.supplier_ship);
    const auto paid=colony_ships_.find(c.customer_ship);
    const auto provider=colony_ships_.find(c.supplier_ship);
    const auto life=life_support_.find(c.supplier_ship);
    const auto frame=ship_frames_.find(c.supplier_ship);
    if(yard==shipyard_constructions_.end() ||
       !yard->second.operational() ||
       yard->second.owner!=c.supplier_owner ||
       yard->second.colony_ship!=c.supplier_ship ||
       buyer==entities_.end() || seller==entities_.end() ||
       buyer->second.type!=EntityType::Ship ||
       seller->second.type!=EntityType::Ship ||
       buyer->second.owner_id!=customer ||
       seller->second.owner_id!=c.supplier_owner ||
       paid==colony_ships_.end() || provider==colony_ships_.end() ||
       life==life_support_.end() || frame==ship_frames_.end() ||
       paid->second.modules<6 ||
       provider->second.modules>1000000-6 ||
       !same_local_frame(c.customer_ship,c.supplier_ship) ||
       !freight_berth(buyer->second,seller->second) ||
       buyer->second.velocity.x || buyer->second.velocity.y ||
       seller->second.velocity.x || seller->second.velocity.y ||
       frame->second.system!=seller->second.system_id ||
       life->second.food_kg<2 || life->second.water_kg<2 ||
       life->second.oxygen_kg<2)return false;
    for(const auto& [id,trip]:pending_celestial_transits_){
        (void)id;
        if(trip.ship==c.customer_ship || trip.ship==c.supplier_ship)
            return false;
    }
    for(const auto& [id,existing]:shipyard_customer_orders_){
        if(id==contract_id)continue;
        if(existing.yard_order==c.yard_order &&
           (existing.stage==ShipyardCustomerStage::Accepted ||
            existing.stage==ShipyardCustomerStage::Funded))
            return false;
        if(existing.build_order==c.build_order)return false;
    }
    for(const auto& [id,build]:freighter_build_orders_){
        (void)id;
        if(build.yard_order==c.yard_order && !build.finished())return false;
    }
    for(const auto worker:yard->second.workers){
        const auto n=npcs_.find(worker);
        if(!worker || n==npcs_.end() ||
           n->second.home_ship!=c.supplier_ship ||
           n->second.location!=c.supplier_ship ||
           n->second.assigned_site!=c.supplier_ship ||
           n->second.role!=NpcRole::Ship || n->second.transport_id ||
           !npc_available_for_duty(worker))return false;
    }
    return yard->second.workers[0]!=yard->second.workers[1];
}
bool Simulation::fund_shipyard_customer_order(PlayerId customer,
        std::uint64_t contract_id) {
    const auto it=shipyard_customer_orders_.find(contract_id);
    if(it==shipyard_customer_orders_.end() ||
       it->second.customer_owner!=customer ||
       it->second.stage!=ShipyardCustomerStage::Accepted)return false;
    const auto& order=it->second;
    const auto yard=shipyard_constructions_.find(order.yard_order);
    const auto buyer=entities_.find(order.customer_ship);
    const auto seller=entities_.find(order.supplier_ship);
    auto paid=colony_ships_.find(order.customer_ship);
    auto provider=colony_ships_.find(order.supplier_ship);
    if(yard==shipyard_constructions_.end() ||
       !yard->second.operational() ||
       yard->second.owner!=order.supplier_owner ||
       buyer==entities_.end() || seller==entities_.end() ||
       buyer->second.owner_id!=customer ||
       seller->second.owner_id!=order.supplier_owner ||
       paid==colony_ships_.end() || provider==colony_ships_.end() ||
       paid->second.modules<6 ||
       provider->second.modules>1000000-6 ||
       !same_local_frame(order.customer_ship,order.supplier_ship) ||
       !freight_berth(buyer->second,seller->second) ||
       buyer->second.velocity.x || buyer->second.velocity.y ||
       seller->second.velocity.x || seller->second.velocity.y)return false;
    // Fund and queue inside one authoritative world. If the ordinary yard
    // refuses the build, restore both exact material inventories.
    paid->second.modules-=6;
    provider->second.modules+=6;
    it->second.stage=ShipyardCustomerStage::Funded;
    it->second.funded_tick=tick_;
    if(!queue_shipyard_freighter(order.supplier_owner,order.yard_order,
                                order.build_order)){
        it->second.stage=ShipyardCustomerStage::Accepted;
        it->second.funded_tick=0;
        paid->second.modules+=6;
        provider->second.modules-=6;
        return false;
    }
    return true;
}
bool Simulation::deliver_shipyard_customer_order(PlayerId customer,
        std::uint64_t contract_id) {
    const auto it=shipyard_customer_orders_.find(contract_id);
    if(it==shipyard_customer_orders_.end() ||
       it->second.customer_owner!=customer ||
       it->second.stage!=ShipyardCustomerStage::Funded)return false;
    const auto& c=it->second;
    const auto build=freighter_build_orders_.find(c.build_order);
    if(build==freighter_build_orders_.end() ||
       !build->second.finished() || build->second.commissioned() ||
       build->second.owner!=c.supplier_owner ||
       build->second.source_ship!=c.supplier_ship ||
       build->second.yard_order!=c.yard_order)return false;
    const auto buyer=entities_.find(c.customer_ship);
    const auto ship=entities_.find(build->second.launched_ship);
    if(buyer==entities_.end() || ship==entities_.end() ||
       buyer->second.type!=EntityType::Ship ||
       buyer->second.owner_id!=customer ||
       ship->second.type!=EntityType::Ship ||
       ship->second.owner_id!=c.supplier_owner ||
       ship->second.manufactured_order_id!=c.build_order ||
       colony_ships_.count(ship->first) || life_support_.count(ship->first) ||
       !same_local_frame(c.customer_ship,ship->first) ||
       !freight_berth(buyer->second,ship->second) ||
       buyer->second.velocity.x || buyer->second.velocity.y ||
       ship->second.velocity.x || ship->second.velocity.y)return false;
    for(const auto& [id,trip]:pending_celestial_transits_){
        (void)id;
        if(trip.ship==c.customer_ship || trip.ship==ship->first)
            return false;
    }
    // Hand over the one completed, UNCREWED physical hull. The provider's
    // immutable build provenance remains intact. Customer must separately
    // staff, fit and provision it using their own real colony stock.
    ship->second.owner_id=customer;
    it->second.delivered_ship=ship->first;
    it->second.delivered_tick=tick_;
    it->second.stage=ShipyardCustomerStage::Delivered;
    return true;
}
// WORK-01A universal read model: intentionally projects only authoritative
// records. A native work order appears exactly once per typed source, even
// if several NPCs/colonies consume the same physical fulfillment receipt.
std::vector<WorkBoardItem> Simulation::universal_work_board(
        PlayerId owner,EntityId colony_ship) const {
    std::vector<WorkBoardItem> board;
    if(!owner || owner==kServerAuthority || !colony_ship)return board;
    const auto delegate_for=[&](WorkTaskKind kind,std::uint64_t source) {
        for(const auto& [id,delegation]:work_directives_) {
            if(delegation.owner==owner &&
               delegation.colony_ship==colony_ship &&
               delegation.kind==kind && delegation.source_order==source &&
               delegation.status!=WorkDirectiveStatus::Cancelled)return id;
        }
        return std::uint64_t{0};
    };
    const auto add=[&](WorkTaskKind kind,std::uint64_t source,
                       WorkPhase phase,std::uint64_t progress,
                       std::uint64_t required) {
        board.push_back(WorkBoardItem{
            kind,source,owner,colony_ship,phase,progress,required,
            delegate_for(kind,source)});
    };
    for(const auto& [id,job]:local_delivery_orders_){
        if(job.owner!=owner || job.board_ship!=colony_ship)continue;
        const auto phase=job.stage==LocalDeliveryStage::Cancelled?
            WorkPhase::Cancelled:job.stage==LocalDeliveryStage::Complete?
            WorkPhase::Completed:job.stage==LocalDeliveryStage::Posted?
            WorkPhase::Queued:WorkPhase::Executing;
        add(WorkTaskKind::LocalDelivery,id,phase,
            job.stage==LocalDeliveryStage::Complete?1:0,1);
    }
    for(const auto& [id,leg]:intercolony_freight_){
        if(leg.owner!=owner || leg.source_ship!=colony_ship)continue;
        const auto phase=leg.stage==IntercolonyFreightStage::Delivered?
            WorkPhase::Completed:leg.stage==IntercolonyFreightStage::AwaitingPickup?
            WorkPhase::Queued:WorkPhase::Executing;
        add(WorkTaskKind::IntercolonyFreight,id,phase,
            leg.stage==IntercolonyFreightStage::Delivered?1:0,1);
    }
    for(const auto& [id,yard]:shipyard_constructions_){
        if(yard.owner!=owner || yard.colony_ship!=colony_ship)continue;
        add(WorkTaskKind::ShipyardConstruction,id,yard.operational()?
            WorkPhase::Completed:WorkPhase::Executing,
            yard.progress,yard.required);
    }
    for(const auto& [id,build]:freighter_build_orders_){
        if(build.owner!=owner || build.source_ship!=colony_ship)continue;
        add(WorkTaskKind::FreighterBuild,id,build.finished()?
            WorkPhase::Completed:WorkPhase::Executing,
            build.progress,build.required);
    }
    for(const auto& [id,contract]:shipyard_customer_orders_){
        if(!((contract.customer_owner==owner &&
              contract.customer_ship==colony_ship) ||
             (contract.supplier_owner==owner &&
              contract.supplier_ship==colony_ship)))continue;
        const auto job=freighter_build_orders_.find(contract.build_order);
        const auto progress=job==freighter_build_orders_.end()?0:
            job->second.progress;
        const auto required=job==freighter_build_orders_.end()?12:
            job->second.required;
        add(WorkTaskKind::CustomerShipOrder,id,
            contract.stage==ShipyardCustomerStage::Delivered?
                WorkPhase::Completed:
            contract.stage==ShipyardCustomerStage::Requested?
                WorkPhase::Queued:WorkPhase::Executing,
            progress,required);
    }
    std::sort(board.begin(),board.end(),[](const auto& a,const auto& b){
        if(a.kind!=b.kind)return a.kind<b.kind;
        return a.source_order<b.source_order;
    });
    return board;
}
bool Simulation::delegate_customer_ship_order(PlayerId owner,
        EntityId colony_ship,std::uint64_t id,std::uint64_t contract_id,
        std::array<std::uint64_t,2> crew,std::int64_t provisions,
        std::uint64_t dependency){
    if(!owner || owner==kServerAuthority || !id ||
       id>=(std::uint64_t{1}<<50) || work_directives_.size()>=10000 ||
       work_directives_.count(id) || !contract_id ||
       contract_id>=(std::uint64_t{1}<<50) ||
       (dependency && dependency>=id) ||
       ((crew[0]==0)!=(crew[1]==0)) ||
       (crew[0] && crew[0]==crew[1]) ||
       (crew[0] && (provisions<10 || provisions>1000)) ||
       (!crew[0] && provisions))return false;
    const auto contract=shipyard_customer_orders_.find(contract_id);
    const auto colony=entities_.find(colony_ship);
    if(contract==shipyard_customer_orders_.end() ||
       contract->second.customer_owner!=owner ||
       contract->second.customer_ship!=colony_ship ||
       contract->second.stage==ShipyardCustomerStage::Delivered ||
       colony==entities_.end() || colony->second.type!=EntityType::Ship ||
       colony->second.owner_id!=owner ||
       !colony_ships_.count(colony_ship) ||
       !life_support_.count(colony_ship))return false;
    if(dependency){
        const auto earlier=work_directives_.find(dependency);
        if(earlier==work_directives_.end() ||
           earlier->second.owner!=owner ||
           earlier->second.colony_ship!=colony_ship)return false;
    }
    for(const auto& [other_id,other]:work_directives_){
        (void)other_id;
        if(other.kind==WorkTaskKind::CustomerShipOrder &&
           other.source_order==contract_id &&
           other.status!=WorkDirectiveStatus::Cancelled)return false;
    }
    for(const auto person_id:crew){
        if(!person_id)continue;
        const auto n=npcs_.find(person_id);
        if(n==npcs_.end() || n->second.home_ship!=colony_ship)
            return false;
    }
    work_directives_.emplace(id,WorkDirective{
        id,owner,colony_ship,WorkTaskKind::CustomerShipOrder,
        contract_id,dependency,crew,provisions,
        WorkDirectiveStatus::Active,WorkBlock::None,tick_,tick_,0});
    snapshot_version_=std::max(snapshot_version_,94);
    return true;
}
// WORK-01C: authorization is only to advance/reconcile a previously
// posted native transport order. Posting, carrier ownership, flight pilot,
// physical cargo movement and voyage receipts remain in their own systems.
// FLIGHT-01 service: general navigation applies to *any* existing
// player-owned NPC-operated colony ship, regardless of mission type.
bool Simulation::queue_npc_flight(PlayerId owner,EntityId ship,
        std::uint64_t pilot,std::uint64_t destination,
        std::uint64_t id,std::uint64_t transit,
        NpcFlightPurpose purpose){
    const auto e=entities_.find(ship),frame=ship_frames_.find(ship);
    const auto selected_pilot=select_npc_flight_pilot(ship,pilot);
    if(!id || id>=(std::uint64_t{1}<<50) || !transit ||
       transit>=(std::uint64_t{1}<<50) ||
       !owner || owner==kServerAuthority ||
       unsigned(purpose)>unsigned(NpcFlightPurpose::Mining) ||
       npc_flight_orders_.size()>=10000 || npc_flight_orders_.count(id) ||
       pending_celestial_transits_.count(transit) ||
       completed_celestial_transits_.count(transit) ||
       !npc_flight_slot_available(ship,selected_pilot) ||
       e==entities_.end() || e->second.type!=EntityType::Ship ||
       e->second.owner_id!=owner || !colony_ships_.count(ship) ||
       !life_support_.count(ship) ||
       !e->second.fitting.exploration_scanner() ||
       frame==ship_frames_.end() ||
       !destination || frame->second.anchor_body==destination ||
       !plan_celestial_frame_arrival(*this,owner,ship,destination,transit).plan)
        return false;
    for(const auto& [other_id,order]:npc_flight_orders_){
        (void)other_id;
        if(order.transit_request==transit && transit)return false;
    }
    npc_flight_orders_.emplace(id,NpcFlightOrder{
        id,owner,ship,selected_pilot,destination,transit,purpose,
        NpcFlightPhase::Planned,tick_,0,0});
    snapshot_version_=std::max(snapshot_version_,95);
    return true;
}
// FLIGHT-02: native station approach uses the SAME flight ledger/pilot
// reservation as celestial travel, and the EXISTING navigation assist plus
// original ordinary submitted ship controls. No independent flight physics.
bool Simulation::queue_npc_station_docking(
        PlayerId owner,EntityId ship,std::uint64_t pilot,
        EntityId target,std::uint64_t id,NpcFlightPurpose purpose){
    const auto vessel=entities_.find(ship),station=entities_.find(target);
    if(!owner || owner==kServerAuthority || !ship || !target ||
       !id || id>=(std::uint64_t{1}<<40) ||
       unsigned(purpose)>unsigned(NpcFlightPurpose::Mining) ||
       npc_flight_orders_.count(id) || npc_flight_orders_.size()>=10000 ||
       vessel==entities_.end() || station==entities_.end() ||
       vessel->second.type!=EntityType::Ship ||
       station->second.type!=EntityType::Station ||
       vessel->second.owner_id!=owner ||
       vessel->second.system_id!=station->second.system_id ||
       !same_local_frame(ship,target) ||
       !colony_ships_.count(ship) || !life_support_.count(ship) ||
       vessel->second.jump_state!=ShipJumpState::Ready ||
       (vessel->second.dock_state!=DockState::InSpace &&
        vessel->second.dock_state!=DockState::Docked) ||
       (vessel->second.dock_state==DockState::Docked &&
        vessel->second.dock_station==target) ||
       pending_celestial_transits_.size()>=10000 ||
       active_mining().count(ship))return false;
    for(const auto& [id,p]:pending_celestial_transits_){
        (void)id;
        if(p.ship==ship)return false;
    }
    for(const auto& [id,v]:voyages_){
        (void)id;
        if(v.ferry==ship && (v.status==1 || v.status==3))return false;
    }
    for(const auto& [key,shuttle]:planetary_shuttles_){
        (void)key;
        if(shuttle.source_ship==ship && shuttle.flight_phase!=0)return false;
    }
    for(const auto& [when,commands]:commands_){
        (void)when;
        for(const auto& cmd:commands)
            if(cmd.actor==ship || cmd.target==ship)return false;
    }
    const auto selected=select_npc_flight_pilot(ship,pilot);
    if(!npc_flight_slot_available(ship,selected))return false;
    NpcFlightOrder order{};
    order.id=id;order.owner=owner;order.ship=ship;
    order.pilot_npc=selected;order.station=target;
    order.mode=NpcFlightMode::LocalDock;order.purpose=purpose;
    order.posted_tick=tick_;
    npc_flight_orders_.emplace(id,order);
    snapshot_version_=std::max(snapshot_version_,96);
    return true;
}
// NPC-RESP-02: the persistent player is not a ship or an NPC passenger.
// Registration is explicitly authenticated by the durable caller.
bool Simulation::register_player_person(PlayerId owner,EntityId aboard){
    if(!owner || owner==kServerAuthority || !aboard ||
       player_people_.count(owner) || player_people_.size()>=10000)return false;
    const auto it=entities_.find(aboard);
    if(it==entities_.end() || it->second.type!=EntityType::Ship ||
       it->second.owner_id!=owner ||
       it->second.dock_state==DockState::Docking ||
       it->second.dock_state==DockState::Undocking ||
       it->second.jump_state==ShipJumpState::Jumping)return false;
    player_people_.emplace(owner,PlayerPerson{
        owner,aboard,aboard,PlayerPersonState::AboardOwnShip,0,tick_,0,tick_});
    snapshot_version_=std::max(snapshot_version_,98);
    return true;
}
// NPC-RESP-02B: consent to a *specific actual rescue ship*.
// This owner request is not a movement effect or a warrant.
bool Simulation::grant_player_rescue_consent(
        PlayerId owner,EntityId receiver,std::uint64_t consent_id){
    auto it=player_people_.find(owner);
    const auto* vessel=find(receiver);
    if(!owner || owner==kServerAuthority || !receiver || !consent_id ||
       consent_id>=(std::uint64_t{1}<<50) || it==player_people_.end() ||
       it->second.state!=PlayerPersonState::AboardOwnShip ||
       it->second.consent_id || receiver==it->second.physical_ship ||
       !vessel || vessel->type!=EntityType::Ship ||
       !colony_ships_.count(receiver) || !life_support_.count(receiver) ||
       !same_local_frame(it->second.physical_ship,receiver))return false;
    for(const auto& [other,record]:player_people_){
        (void)other;
        if(record.consent_id==consent_id || record.last_receipt==consent_id)
            return false;
    }
    it->second.consent_ship=receiver;
    it->second.consent_id=consent_id;
    it->second.last_update_tick=tick_;
    snapshot_version_=std::max(snapshot_version_,99);
    return true;
}
// Physical rescue requires BOTH a registered consent and a separately
// applied, original FLIGHT-03 ship-motion receipt, plus an independent
// native player-transfer receipt. No ship ownership or NPC custody changes.
bool Simulation::revoke_player_rescue_consent(
        PlayerId owner,std::uint64_t consent_id){
    auto it=player_people_.find(owner);
    if(!owner || owner==kServerAuthority || !consent_id ||
       it==player_people_.end() ||
       it->second.state!=PlayerPersonState::AboardOwnShip ||
       it->second.consent_id!=consent_id ||
       !it->second.consent_ship)return false;
    it->second.consent_ship=0;
    it->second.consent_id=0;
    it->second.last_update_tick=tick_;
    snapshot_version_=std::max(snapshot_version_,99);
    return true;
}
bool Simulation::complete_player_rescue(
        PlayerId owner,EntityId receiver,
        std::uint64_t flight_id,std::uint64_t transfer_receipt){
    auto it=player_people_.find(owner);
    const auto* vessel=find(receiver);
    if(!owner || owner==kServerAuthority || !receiver ||
       !flight_id || !transfer_receipt ||
       transfer_receipt>=(std::uint64_t{1}<<50) ||
       submitted_ids_.count(transfer_receipt) ||
       !vessel || vessel->type!=EntityType::Ship ||
       !colony_ships_.count(receiver) || !life_support_.count(receiver) ||
       it==player_people_.end() ||
       it->second.state!=PlayerPersonState::AboardOwnShip ||
       it->second.consent_ship!=receiver || !it->second.consent_id ||
       it->second.physical_ship==receiver)return false;
    const auto& person=it->second;
    const auto flight=npc_flight_orders_.find(flight_id);
    if(flight==npc_flight_orders_.end() ||
       flight->second.purpose!=NpcFlightPurpose::Rescue ||
       flight->second.mode!=NpcFlightMode::LocalRendezvous ||
       flight->second.ship!=receiver ||
       flight->second.station!=person.physical_ship ||
       flight->second.owner!=vessel->owner_id ||
       ship_interaction::rendezvous_preflight(*this,vessel->owner_id,
           receiver,person.physical_ship,flight_id)!=
           ship_interaction::Readiness::Ready)return false;
    const auto people=people_physically_aboard(receiver);
    const auto dependents=children_physically_aboard(receiver);
    if(people>15 || dependents>15-people)return false;
    const auto* stock=life_support(receiver);
    const auto total=static_cast<std::int64_t>(people+dependents+1);
    if(!stock || stock->food_kg<total*2 ||
       stock->water_kg<total*2 || stock->oxygen_kg<total*2)return false;
    for(const auto& event:activity_events_)
        if(event.start_command_id==transfer_receipt)return false;
    for(const auto& [other,record]:player_people_){
        (void)other;
        if(record.last_receipt==transfer_receipt ||
           record.consent_id==transfer_receipt)return false;
    }
    const auto original=person.physical_ship;
    it->second.physical_ship=receiver;
    it->second.state=PlayerPersonState::Rescued;
    it->second.custodian=vessel->owner_id; // rescuer, not an arresting authority
    it->second.last_receipt=transfer_receipt;
    it->second.rescue_flight=flight_id;
    it->second.consent_ship=0;
    it->second.last_update_tick=tick_;
    activity_events_.push_back({tick_,ActivityKind::PlayerRescued,
        transfer_receipt,receiver,original,Outcome::Applied,0});
    snapshot_version_=std::max(snapshot_version_,99);
    return true;
}
bool Simulation::queue_npc_ship_rendezvous(
        PlayerId owner,EntityId ship,std::uint64_t pilot,
        EntityId target,std::uint64_t id,NpcFlightPurpose purpose){
    const auto vessel=entities_.find(ship),destination=entities_.find(target);
    if(!owner || owner==kServerAuthority || !ship || !target ||
       !id || id>=(std::uint64_t{1}<<40) ||
       unsigned(purpose)>unsigned(NpcFlightPurpose::Mining) ||
       npc_flight_orders_.count(id) || npc_flight_orders_.size()>=10000 ||
       vessel==entities_.end() || destination==entities_.end() ||
       vessel->second.type!=EntityType::Ship ||
       destination->second.type!=EntityType::Ship ||
       vessel->second.owner_id!=owner ||
       vessel->second.system_id!=destination->second.system_id ||
       ship==target || !same_local_frame(ship,target) ||
       !colony_ships_.count(ship) || !life_support_.count(ship) ||
       vessel->second.jump_state!=ShipJumpState::Ready ||
       (vessel->second.dock_state!=DockState::InSpace &&
        vessel->second.dock_state!=DockState::Docked) ||
       destination->second.dock_state!=DockState::InSpace ||
       destination->second.jump_state!=ShipJumpState::Ready ||
       pending_celestial_transits_.size()>=10000 ||
       active_mining().count(ship))return false;
    for(const auto& [id,p]:pending_celestial_transits_){
        (void)id;
        if(p.ship==ship)return false;
    }
    for(const auto& [id,v]:voyages_){
        (void)id;
        if(v.ferry==ship && (v.status==1 || v.status==3))return false;
    }
    for(const auto& [key,shuttle]:planetary_shuttles_){
        (void)key;
        if(shuttle.source_ship==ship && shuttle.flight_phase!=0)return false;
    }
    for(const auto& [when,commands]:commands_){
        (void)when;
        for(const auto& cmd:commands)
            if(cmd.actor==ship || cmd.target==ship)return false;
    }
    const auto selected=select_npc_flight_pilot(ship,pilot);
    if(!npc_flight_slot_available(ship,selected))return false;
    NpcFlightOrder order{};
    order.id=id;order.owner=owner;order.ship=ship;
    order.pilot_npc=selected;order.station=target;
    order.mode=NpcFlightMode::LocalRendezvous;order.purpose=purpose;
    order.posted_tick=tick_;
    npc_flight_orders_.emplace(id,order);
    snapshot_version_=std::max(snapshot_version_,97);
    return true;
}
// One original command per committed world tick; original submit validates
// idempotency, issuer and timing, and original execute performs the actual
// thrust/undock/dock effects on the same authoritative physical ship.
bool Simulation::submit_npc_local_control(NpcFlightOrder& order,
        Action action,EntityId target,int tx,int ty){
    constexpr std::uint64_t kNativeFlightCommandTag=(std::uint64_t{1}<<61);
    if((order.mode!=NpcFlightMode::LocalDock &&
        order.mode!=NpcFlightMode::LocalRendezvous) ||
       order.local_commands>=511 || order.id>=(std::uint64_t{1}<<40) ||
       tick_==std::numeric_limits<std::uint64_t>::max())return false;
    const auto next=static_cast<std::uint16_t>(order.local_commands+1);
    const auto command_id=kNativeFlightCommandTag |
        (order.id<<9) | next;
    if(submitted_ids_.count(command_id))return false;
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=tick_+1;
    c.issuer=order.owner;c.actor=order.ship;c.target=target;
    c.action=action;c.thrust_x=tx;c.thrust_y=ty;
    if(!submit(c))return false;
    order.local_commands=next;
    order.last_local_command=command_id;
    if(order.phase==NpcFlightPhase::Planned){
        order.phase=NpcFlightPhase::InTransit;
        order.started_tick=tick_;
    }
    return true;
}
bool Simulation::cancel_npc_flight(PlayerId owner,std::uint64_t id){
    const auto it=npc_flight_orders_.find(id);
    if(!owner || owner==kServerAuthority ||
       it==npc_flight_orders_.end() || it->second.owner!=owner ||
       it->second.phase!=NpcFlightPhase::Planned)return false;
    // A real physical transit cannot be recalled merely by deleting
    // intent. Only an unstarted order can be cancelled.
    it->second.phase=NpcFlightPhase::Cancelled;
    it->second.resolved_tick=tick_;
    return true;
}
// Shared physical departure gate: identical original host-owned
// scanner/geometry/docking/shuttle/command and vessel restrictions.
bool Simulation::begin_native_celestial_transit(
        PlayerId owner,EntityId ship,std::uint64_t body,std::uint64_t id){
    for(const auto& [key,sh]:planetary_shuttles_){
        (void)key;
        if(sh.source_ship==ship && sh.flight_phase!=0)return false;
    }
    if(!id || !owner || owner==kServerAuthority ||
       ship_frames_.empty() || snapshot_version_<71 ||
       pending_celestial_transits_.size()>=10000 ||
       completed_celestial_transits_.size()>=10000 ||
       completed_celestial_transits_.count(id) ||
       pending_celestial_transits_.count(id) ||
       tick_>std::numeric_limits<std::uint64_t>::max()-4)return false;
    for(const auto& [key,p]:pending_celestial_transits_){
        (void)key;
        if(p.ship==ship)return false;
    }
    const auto* carrier=find(ship);
    if(!carrier || !carrier->fitting.exploration_scanner())return false;
    const auto decision=plan_celestial_frame_arrival(*this,owner,ship,body,id);
    if(!decision.plan)return false;
    const auto frame=ship_frames_.find(ship);
    if(frame==ship_frames_.end() || frame->second.anchor_body==body)return false;
    for(const auto& [voyage_id,v]:voyages_){
        (void)voyage_id;
        if(v.ferry==ship && (v.status==1 || v.status==3))return false;
    }
    for(const auto& [when,commands]:commands_){
        (void)when;
        for(const auto& c:commands)
            if(c.actor==ship || c.target==ship)return false;
    }
    entities_.at(ship).velocity={};
    pending_celestial_transits_.emplace(id,PendingCelestialTransit{
        owner,ship,frame->second.system,frame->second.anchor_body,body,
        tick_,tick_+4});
    snapshot_version_=std::max(snapshot_version_,72);
    return true;
}
bool Simulation::complete_native_celestial_transit(std::uint64_t id){
    if(!id || completed_celestial_transits_.count(id))return false;
    const auto it=pending_celestial_transits_.find(id);
    if(it==pending_celestial_transits_.end())return false;
    const auto p=it->second;
    if(tick_<p.due_tick)return false;
    const auto current=ship_frames_.find(p.ship);
    if(current==ship_frames_.end() ||
       current->second.system!=p.system ||
       current->second.anchor_body!=p.source_anchor)return false;
    const auto decision=plan_celestial_frame_arrival(
        *this,p.owner,p.ship,p.destination_body,id);
    if(!decision.plan)return false;
    const auto& plan=*decision.plan;
    if(plan.system!=p.system || plan.owner!=p.owner ||
       plan.ship_id!=p.ship)return false;
    seed_celestial_local_region(p.destination_body);
    auto& ship=entities_.at(p.ship);
    auto& frame=ship_frames_.at(p.ship);
    frame={plan.destination_frame.system,plan.destination_frame.anchor_body,
           plan.destination_frame.origin};
    ship.position=plan.arrival_position;
    ship.velocity=plan.arrival_velocity;
    pending_celestial_transits_.erase(id);
    completed_celestial_transits_.emplace(id,CompletedCelestialTransit{
        p.owner,p.ship,p.system,p.destination_body,tick_});
    snapshot_version_=std::max(snapshot_version_,72);
    for(auto& [flight_id,nav]:npc_flight_orders_){
        (void)flight_id;
        if(nav.transit_request==id && nav.phase==NpcFlightPhase::InTransit &&
           nav.owner==p.owner && nav.ship==p.ship &&
           nav.destination_body==p.destination_body){
            nav.phase=NpcFlightPhase::Arrived;
            nav.resolved_tick=tick_;
        }
    }
    return true;
}
void Simulation::process_npc_flight_navigation(){
    if(snapshot_version_<95)return;
    for(auto& [id,order]:npc_flight_orders_){
        (void)id;
        if(order.mode==NpcFlightMode::LocalDock ||
           order.mode==NpcFlightMode::LocalRendezvous){
            if(order.phase!=NpcFlightPhase::Planned &&
               order.phase!=NpcFlightPhase::InTransit)continue;
            const auto ship=entities_.find(order.ship);
            const auto station=entities_.find(order.station);
            const bool rendezvous=order.mode==NpcFlightMode::LocalRendezvous;
            if(ship==entities_.end() || station==entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               station->second.type!=(rendezvous?EntityType::Ship:EntityType::Station) ||
               ship->second.owner_id!=order.owner ||
               ship->second.system_id!=station->second.system_id ||
               order.ship==order.station ||
               !same_local_frame(order.ship,order.station)){
                if(rendezvous){
                    order.phase=NpcFlightPhase::Cancelled;
                    order.resolved_tick=tick_;
                }
                continue;
            }
            if(rendezvous && (station->second.dock_state!=DockState::InSpace ||
                              station->second.jump_state!=ShipJumpState::Ready)){
                order.phase=NpcFlightPhase::Cancelled;
                order.resolved_tick=tick_;
                continue;
            }
            // Original station transition supplies the actual arrival.
            // A scheduled Dock command is NOT proof of being docked.
            if(!rendezvous && ship->second.dock_state==DockState::Docked &&
               ship->second.dock_station==order.station){
                if(order.phase==NpcFlightPhase::InTransit){
                    order.phase=NpcFlightPhase::Arrived;
                    order.resolved_tick=tick_;
                }
                continue;
            }
            const auto pilot=npcs_.find(order.pilot_npc);
            if(pilot==npcs_.end() || pilot->second.home_ship!=order.ship ||
               pilot->second.role!=NpcRole::Ship){
                // Revoke an unstarted plan; never delete native commands or
                // instant-stop a moving hull after physical departure.
                if(order.phase==NpcFlightPhase::Planned){
                    order.phase=NpcFlightPhase::Cancelled;
                    order.resolved_tick=tick_;
                }
                continue;
            }
            if(!qualified_npc_flight_pilot(order.ship,order.pilot_npc))
                continue;
            // A command from any other authority already targeting or
            // controlling the ship blocks this NPC control tick.
            bool pending=false;
            for(const auto& [when,commands]:commands_){
                (void)when;
                for(const auto& c:commands)
                    if(c.actor==order.ship || c.target==order.ship){
                        pending=true;break;
                    }
                if(pending)break;
            }
            if(pending)continue;
            if(order.local_commands>=511){
                // Bounded native command budget. Release the pilot and
                // allow human intervention; this NEVER moves the ship,
                // edits cargo or fabricates a station arrival.
                order.phase=NpcFlightPhase::Cancelled;
                order.resolved_tick=tick_;
                continue;
            }
            const auto& actual=ship->second;
            if(actual.jump_state!=ShipJumpState::Ready ||
               actual.dock_state==DockState::Docking ||
               actual.dock_state==DockState::Undocking)continue;
            if(actual.dock_state==DockState::Docked){
                (void)submit_npc_local_control(order,
                    Action::RequestUndock,actual.dock_station);
                continue;
            }
            if(actual.dock_state!=DockState::InSpace)continue;
            const auto nav=assist_to_object(actual,station->second,1500);
            if(nav.arrived){
                if(rendezvous){
                    // Already at the berth still requires an ORIGINAL
                    // applied physical-control receipt before arrival.
                    if(order.phase==NpcFlightPhase::Planned){
                        (void)submit_npc_local_control(
                            order,Action::Thrust,0,0,0);
                        continue;
                    }
                    bool applied=false;
                    for(const auto& ev:events_)
                        if(ev.command_id==order.last_local_command &&
                           ev.result==Outcome::Applied)applied=true;
                    if(applied && order.phase==NpcFlightPhase::InTransit){
                        order.phase=NpcFlightPhase::Arrived;
                        order.resolved_tick=tick_;
                        activity_events_.push_back({tick_,
                            ActivityKind::NpcRendezvous,order.last_local_command,
                            order.ship,order.station,Outcome::Applied,0});
                    }
                }else
                    (void)submit_npc_local_control(
                        order,Action::RequestDock,order.station);
            }else
                (void)submit_npc_local_control(
                    order,Action::Thrust,0,nav.thrust_x,nav.thrust_y);
            continue;
        }
        if(order.phase==NpcFlightPhase::InTransit){
            // Once a true departure is committed, only the original
            // completed physical transit receipt can finish the order.
            (void)complete_native_celestial_transit(order.transit_request);
            continue;
        }
        if(order.phase!=NpcFlightPhase::Planned)continue;
        const auto pilot=npcs_.find(order.pilot_npc);
        if(pilot==npcs_.end() ||
           pilot->second.home_ship!=order.ship ||
           pilot->second.role!=NpcRole::Ship){
            // A genuinely lost/reassigned pilot must not leave a perpetual
            // stale reservation; only the unstarted flight is revoked.
            // No original physical departure or cargo transfer has occurred.
            order.phase=NpcFlightPhase::Cancelled;
            order.resolved_tick=tick_;
            continue;
        }
        if(!qualified_npc_flight_pilot(order.ship,order.pilot_npc))
            continue; // injured or occupied: wait for native availability
        if(begin_native_celestial_transit(order.owner,order.ship,
                order.destination_body,order.transit_request)){
            order.phase=NpcFlightPhase::InTransit;
            order.started_tick=tick_;
        }
    }
}
bool Simulation::delegate_transport_work_order(PlayerId owner,EntityId origin,
        std::uint64_t id,WorkTaskKind kind,std::uint64_t source,
        std::uint64_t dependency){
    if(!owner || owner==kServerAuthority || !id ||
       id>=(std::uint64_t{1}<<50) ||
       !source || source>=(std::uint64_t{1}<<50) ||
       (kind!=WorkTaskKind::LocalDelivery &&
        kind!=WorkTaskKind::IntercolonyFreight) ||
       work_directives_.count(id) || work_directives_.size()>=10000 ||
       (dependency && dependency>=id))return false;
    const auto colony=entities_.find(origin);
    const auto pickup=local_delivery_orders_.find(source);
    if(colony==entities_.end() || colony->second.type!=EntityType::Ship ||
       colony->second.owner_id!=owner || !colony_ships_.count(origin) ||
       !life_support_.count(origin) ||
       pickup==local_delivery_orders_.end() ||
       pickup->second.owner!=owner ||
       pickup->second.board_ship!=origin ||
       pickup->second.stage==LocalDeliveryStage::Cancelled)return false;
    if(kind==WorkTaskKind::LocalDelivery &&
       pickup->second.stage==LocalDeliveryStage::Complete)return false;
    if(kind==WorkTaskKind::IntercolonyFreight){
        const auto leg=intercolony_freight_.find(source);
        if(leg==intercolony_freight_.end() ||
           leg->second.owner!=owner || leg->second.source_ship!=origin ||
           leg->second.cargo!=pickup->second.cargo ||
           leg->second.kilograms!=pickup->second.kilograms ||
           !leg->second.carrier_ship || !leg->second.destination_ship ||
           leg->second.stage==IntercolonyFreightStage::Delivered)return false;
    }
    if(dependency){
        const auto prior=work_directives_.find(dependency);
        if(prior==work_directives_.end() ||
           prior->second.owner!=owner ||
           prior->second.colony_ship!=origin)return false;
    }
    for(const auto& [other_id,other]:work_directives_){
        (void)other_id;
        if(other.kind==kind && other.source_order==source &&
           other.status!=WorkDirectiveStatus::Cancelled)return false;
    }
    work_directives_.emplace(id,WorkDirective{
        id,owner,origin,kind,source,dependency,{},0,
        WorkDirectiveStatus::Active,WorkBlock::None,tick_,tick_,0});
    snapshot_version_=std::max(snapshot_version_,94);
    return true;
}
bool Simulation::cancel_work_directive(PlayerId owner,std::uint64_t id){
    const auto it=work_directives_.find(id);
    if(!owner || owner==kServerAuthority ||
       it==work_directives_.end() || it->second.owner!=owner ||
       it->second.status!=WorkDirectiveStatus::Active)return false;
    it->second.status=WorkDirectiveStatus::Cancelled;
    it->second.blocked=WorkBlock::None;
    it->second.last_update_tick=tick_;
    it->second.resolved_tick=tick_;
    // This revokes FUTURE delegation only; it never reverses real paid
    // material transfers or rewinds an already-built physical vessel.
    return true;
}
void Simulation::process_universal_work(){
    if(snapshot_version_<94)return;
    // WORK-01B: plan from ORIGINAL canonical source/worker records at each
    // committed tick; never store a competing queue or reserve invented NPCs.
    // The original native effect gates below remain the sole authorities.
    const work::NativeCapacityContext capacity{
        &shipyard_constructions_,&freighter_build_orders_,
        [this](std::uint64_t worker){
            return npc_available_for_duty(worker);
        }};
    // Life-preserving priority is grounded in the SAME canonical
    // compartment risk model used by the native colony safety system.
    // Only new elective funding or crew reassignment is preempted;
    // existing paid construction and hull handover are never reversed.
    std::set<EntityId> critical_colonies;
    std::set<EntityId> inspected_colonies;
    for(const auto& [id,d]:work_directives_){
        (void)id;
        if(d.status!=WorkDirectiveStatus::Active ||
           d.kind!=WorkTaskKind::CustomerShipOrder ||
           !inspected_colonies.insert(d.colony_ship).second)continue;
        const auto entity=entities_.find(d.colony_ship);
        if(entity==entities_.end() || entity->second.owner_id!=d.owner ||
           !colony_ships_.count(d.colony_ship) ||
           !life_support_.count(d.colony_ship))continue;
        const auto urgent=[&](EntityId site){
            return colony_safety::assess(*this,d.colony_ship,site).
                emergency_override;
        };
        bool emergency=urgent(d.colony_ship);
        if(!emergency)for(const auto& [site,farm]:farms_)
            if(farm.source_ship==d.colony_ship && farm.workers>0 &&
               urgent(site)){emergency=true;break;}
        if(!emergency)for(const auto& [site,habitat]:habitats_)
            if(habitat.source_ship==d.colony_ship && habitat.residents>0 &&
               urgent(site)){emergency=true;break;}
        if(emergency)critical_colonies.insert(d.colony_ship);
    }
    const work::WorkPolicyContext policy{
        [critical_colonies](EntityId colony){
            return critical_colonies.count(colony)!=0;
        },
        [this](PlayerId owner,std::uint64_t contract){
            return can_fund_shipyard_customer_order(owner,contract);
        },1};
    const work::NativeTransportContext native_transport{
        &local_delivery_orders_,&intercolony_freight_};
    const auto schedule=work::plan(
        work_directives_,shipyard_customer_orders_,npcs_,tick_,
        capacity,policy,native_transport);
    for(const auto& candidate:schedule){
        auto active=work_directives_.find(candidate.id);
        if(active==work_directives_.end() ||
           active->second.status!=WorkDirectiveStatus::Active)continue;
        auto& directive=active->second;
        const auto block=[&](WorkBlock reason){
            if(directive.blocked!=reason){
                directive.blocked=reason;
                directive.last_update_tick=tick_;
            }
        };
        const auto finish=[&](){
            directive.status=WorkDirectiveStatus::Fulfilled;
            directive.blocked=WorkBlock::None;
            directive.resolved_tick=tick_;
            directive.last_update_tick=tick_;
        };
        // A shared named NPC may be selected by only one READY
        // commissioning directive this tick. No future work reserves
        // anyone indefinitely, and all original final validations remain.
        if(candidate.gate==work::Gate::WorkerReserved ||
           candidate.gate==work::Gate::WorkerUnavailable ||
           candidate.gate==work::Gate::WorkerOnNativeJob){
            block(WorkBlock::NamedCrewAndSupplies);
            continue;
        }
        if(candidate.gate==work::Gate::EmergencyHold){
            // Existing v94 wait codes remain compatible with SQLite.
            // Do not pay/build or transfer named crew under a verified
            // critical life-support hazard; re-check on every native tick.
            const auto contract=shipyard_customer_orders_.find(
                directive.source_order);
            block(contract!=shipyard_customer_orders_.end() &&
                  contract->second.stage==ShipyardCustomerStage::Delivered?
                  WorkBlock::NamedCrewAndSupplies:WorkBlock::PhysicalFunding);
            continue;
        }
        if(candidate.gate==work::Gate::FundingNotReady ||
           candidate.gate==work::Gate::YardUnavailable ||
           candidate.gate==work::Gate::AssemblySlipBusy ||
           candidate.gate==work::Gate::BuilderUnavailable){
            // Existing 94 snapshot wait code: no speculative debit.
            // Original supplier yard remains the only build authority.
            block(WorkBlock::PhysicalFunding);
            continue;
        }
        if(candidate.gate==work::Gate::Dependency){
            block(WorkBlock::Dependency);
            continue;
        }
        if(candidate.gate==work::Gate::MissingSource ||
           candidate.gate==work::Gate::UnsupportedExecutor){
            block(WorkBlock::SupplierApproval);
            continue;
        }
        if(directive.requires_directive){
            const auto prerequisite=work_directives_.find(
                directive.requires_directive);
            if(prerequisite==work_directives_.end() ||
               prerequisite->second.status!=WorkDirectiveStatus::Fulfilled){
                block(WorkBlock::Dependency);
                continue;
            }
        }
        // Existing planetary shuttle automation has ALREADY run this tick:
        // these records are physical flight receipts, not a second engine.
        if(directive.kind==WorkTaskKind::LocalDelivery){
            const auto pickup=local_delivery_orders_.find(
                directive.source_order);
            if(pickup==local_delivery_orders_.end() ||
               pickup->second.owner!=directive.owner ||
               pickup->second.board_ship!=directive.colony_ship ||
               pickup->second.stage==LocalDeliveryStage::Cancelled){
                block(WorkBlock::PhysicalDelivery);
            }else if(pickup->second.stage==LocalDeliveryStage::Complete){
                // The only completion proof is the native shuttle unload.
                finish();
            }else block(WorkBlock::NativeConstruction);
            continue;
        }
        if(directive.kind==WorkTaskKind::IntercolonyFreight){
            const auto leg=intercolony_freight_.find(directive.source_order);
            const auto pickup=local_delivery_orders_.find(
                directive.source_order);
            if(leg==intercolony_freight_.end() ||
               pickup==local_delivery_orders_.end() ||
               pickup->second.owner!=directive.owner ||
               pickup->second.board_ship!=directive.colony_ship ||
               pickup->second.stage==LocalDeliveryStage::Cancelled ||
               leg->second.owner!=directive.owner ||
               leg->second.source_ship!=directive.colony_ship ||
               leg->second.cargo!=pickup->second.cargo ||
               leg->second.kilograms!=pickup->second.kilograms){
                block(WorkBlock::PhysicalDelivery);
            }else if(leg->second.stage==IntercolonyFreightStage::Delivered){
                // Arrival and unloaded cargo were committed by original
                // freight system, never inferred from journey intentions.
                finish();
            }else if(leg->second.stage==IntercolonyFreightStage::AwaitingPickup){
                // Original local board owns exact shuttle load and any
                // automatic near-berth load onto the preplanned carrier.
                block(WorkBlock::NativeConstruction);
            }else if(intercolony_transport_manifest(directive.source_order) &&
                     deliver_intercolony_freight(
                         directive.owner,directive.source_order)){
                // This calls the ORIGINAL native completed-arrival, berth,
                // carrier inventory and receipt validation and commits in
                // the same PersistentWorld copied-state step.
                finish();
            }else block(WorkBlock::PhysicalDelivery);
            continue;
        }
        if(directive.kind!=WorkTaskKind::CustomerShipOrder)continue;
        const auto it=shipyard_customer_orders_.find(directive.source_order);
        if(it==shipyard_customer_orders_.end() ||
           it->second.customer_owner!=directive.owner ||
           it->second.customer_ship!=directive.colony_ship){
            block(WorkBlock::SupplierApproval);
            continue;
        }
        switch(it->second.stage){
        case ShipyardCustomerStage::Requested:
            block(WorkBlock::SupplierApproval);
            break;
        case ShipyardCustomerStage::Accepted:
            // The original physical funding gate is the *only* method
            // allowed to debit customer modules or occupy the supplier slip.
            if(fund_shipyard_customer_order(
                    directive.owner,directive.source_order))
                block(WorkBlock::NativeConstruction);
            else block(WorkBlock::PhysicalFunding);
            break;
        case ShipyardCustomerStage::Funded:{
            const auto build=freighter_build_orders_.find(
                it->second.build_order);
            if(build==freighter_build_orders_.end() ||
               !build->second.finished()){
                block(WorkBlock::NativeConstruction);
                break;
            }
            if(deliver_shipyard_customer_order(
                    directive.owner,directive.source_order)){
                if(!directive.commissioning_crew[0])finish();
                else block(WorkBlock::NamedCrewAndSupplies);
            }else block(WorkBlock::PhysicalDelivery);
            break;
        }
        case ShipyardCustomerStage::Delivered:{
            if(!directive.commissioning_crew[0]){
                finish();break;
            }
            const auto build=freighter_build_orders_.find(
                it->second.build_order);
            if(build!=freighter_build_orders_.end() &&
               ((build->second.commissioned() &&
                 build->second.commissioning_crew==
                     directive.commissioning_crew &&
                 build->second.provisions_kg_each==
                     directive.provisions_kg_each) ||
                (!build->second.commissioned() &&
                 commission_shipyard_freighter(directive.owner,
                    it->second.build_order,directive.commissioning_crew,
                    directive.provisions_kg_each))))
                finish();
            else block(WorkBlock::NamedCrewAndSupplies);
            break;
        }
        }
    }
}
void Simulation::process_freighter_builds() {
    for(auto& [id,order]:freighter_build_orders_) {
        (void)id;
        if(order.finished())continue;
        const auto yard=shipyard_constructions_.find(order.yard_order);
        if(yard==shipyard_constructions_.end() ||
           !yard->second.operational())continue;
        const auto ship=entities_.find(order.source_ship);
        const auto frame=ship_frames_.find(order.source_ship);
        const auto life=life_support_.find(order.source_ship);
        bool available=ship!=entities_.end() &&
            ship->second.type==EntityType::Ship &&
            ship->second.owner_id==order.owner &&
            ship->second.system_id==order.system &&
            ship->second.dock_state==DockState::InSpace &&
            ship->second.jump_state==ShipJumpState::Ready &&
            !ship->second.velocity.x && !ship->second.velocity.y &&
            frame!=ship_frames_.end() &&
            frame->second.system==order.system &&
            frame->second.anchor_body==order.anchor_body &&
            life!=life_support_.end() && life->second.food_kg>=2 &&
            life->second.water_kg>=2 && life->second.oxygen_kg>=2;
        for(const auto& [request,trip]:pending_celestial_transits_){
            (void)request;
            if(trip.ship==order.source_ship){available=false;break;}
        }
        for(const auto worker:yard->second.workers){
            const auto n=npcs_.find(worker);
            if(n==npcs_.end() ||
               n->second.home_ship!=order.source_ship ||
               n->second.location!=order.source_ship ||
               n->second.assigned_site!=order.source_ship ||
               n->second.role!=NpcRole::Ship || n->second.transport_id ||
               !npc_available_for_duty(worker)){
                available=false;break;
            }
        }
        if(!available)continue; // cargo and work remain in real yard
        ++order.progress;
        order.last_progress_tick=tick_;
        if(order.progress!=order.required)continue;
        // The real vessel is created *only* on the completed native tick.
        // It is a normal physical Entity with a 100m3 finite hold and a
        // genuine frame origin. No crew, scanner, weapons, or supplies.
        Entity hull{};
        hull.type=EntityType::Ship;hull.owner_id=order.owner;
        hull.position=ship->second.position;
        if(hull.position.x<=kWorldBoundMm-1000)hull.position.x+=1000;
        else hull.position.x-=1000;
        hull.cargo_capacity=100;
        hull.hull=100;
        hull.weapon_charges=0;
        hull.manufactured_order_id=order.order_id;
        const auto actual_system=ship->second.system_id;
        const auto custody=frame->second;
        const EntityId manufactured=add_entity(hull);
        entities_.at(manufactured).system_id=actual_system;
        ship_frames_[manufactured]=custody;
        order.launched_ship=manufactured;
        order.launched_tick=tick_;
        // The build order retains exactly 12 embodied material units as
        // permanent physical hull provenance for total_ore conservation.
    }
}
bool Simulation::commission_shipyard_freighter(PlayerId owner,
        std::uint64_t build_id,std::array<std::uint64_t,2> crew,
        std::int64_t provisions_kg_each) {
    // The two existing ships must actually be at the same small berth.
    // A commissioning request never creates new NPC identities or stock.
    const auto job=freighter_build_orders_.find(build_id);
    if(!owner || owner==kServerAuthority || !build_id ||
       crew[0]==0 || crew[1]==0 || crew[0]==crew[1] ||
       provisions_kg_each<10 || provisions_kg_each>1000 ||
       job==freighter_build_orders_.end() || !job->second.finished() ||
       job->second.commissioned())return false;
    // For a customer's physical handover, the immutable build receipt
    // still names the original supplier. The buyer staffs the delivered
    // hull from their own colony; the supplier's workers and stock are
    // never conscripted for commissioning.
    EntityId commissioned_from=job->second.source_ship;
    bool customer_build=false;
    for(const auto& [id,contract]:shipyard_customer_orders_){
        (void)id;
        if(contract.build_order!=build_id)continue;
        // A customer purchase requires the transfer receipt even when the
        // customer and supplier happen to share the same player owner.
        if(contract.stage!=ShipyardCustomerStage::Delivered ||
           contract.customer_owner!=owner ||
           contract.delivered_ship!=job->second.launched_ship)
            return false;
        commissioned_from=contract.customer_ship;
        customer_build=true;
        break;
    }
    if(!customer_build && job->second.owner!=owner)return false;
    const auto source_id=commissioned_from;
    const auto hull_id=job->second.launched_ship;
    const auto source=entities_.find(source_id),hull=entities_.find(hull_id);
    const auto stock=colony_ships_.find(source_id);
    const auto supply=life_support_.find(source_id);
    const auto yard=shipyard_constructions_.find(job->second.yard_order);
    if(source==entities_.end() || hull==entities_.end() ||
       stock==colony_ships_.end() || supply==life_support_.end() ||
       yard==shipyard_constructions_.end() ||
       source->second.owner_id!=owner || hull->second.owner_id!=owner ||
       hull->second.type!=EntityType::Ship ||
       hull->second.manufactured_order_id!=build_id ||
       colony_ships_.count(hull_id) || life_support_.count(hull_id) ||
       people_physically_aboard(hull_id) || children_physically_aboard(hull_id) ||
       hull->second.fitting.exploration_scanner() ||
       hull->second.fitting.probe_launcher() ||
       !same_local_frame(source_id,hull_id) ||
       !freight_berth(source->second,hull->second) ||
       source->second.velocity.x || source->second.velocity.y ||
       hull->second.velocity.x || hull->second.velocity.y ||
       stock->second.crew<3 || stock->second.modules<1 ||
       colony_departures_.count(source_id) ||
       colony_departures_.count(hull_id))return false;
    // Preserve at least two kilograms per remaining source crew member.
    const auto source_reserve=2*(stock->second.crew-2);
    if(supply->second.food_kg<provisions_kg_each+source_reserve ||
       supply->second.water_kg<provisions_kg_each+source_reserve ||
       supply->second.oxygen_kg<provisions_kg_each+source_reserve)return false;
    for(const auto& [id,trip]:pending_celestial_transits_){
        (void)id;
        if(trip.ship==source_id || trip.ship==hull_id)return false;
    }
    for(const auto& [id,v]:voyages_) {
        (void)id;
        if((v.ferry==source_id || v.ferry==hull_id) &&
           (v.status==1 || v.status==3))return false;
    }
    for(const auto id:crew){
        const auto n=npcs_.find(id);
        if(n==npcs_.end() || n->second.home_ship!=source_id ||
           n->second.location!=source_id || n->second.assigned_site!=source_id ||
           n->second.role!=NpcRole::Ship || n->second.transport_id ||
           n->second.mentor_id ||
           n->second.residence_kind!=transport::LocationKind::ColonyShip ||
           (n->second.residence_id && n->second.residence_id!=source_id) ||
           (!customer_build &&
             (id==yard->second.workers[0] || id==yard->second.workers[1])) ||
           !npc_available_for_duty(id))return false;
        // Do not strand a dependent aboard the originating colony.
        for(const auto& [child,d]:physical_dependents_){
            (void)child;
            if(d.guardian_id==id)return false;
        }
    }
    // All mutations occur together on PersistentWorld's copied Simulation.
    --stock->second.modules;
    stock->second.crew-=2;
    ColonyShip installed{};installed.crew=2;
    colony_ships_.emplace(hull_id,std::move(installed));
    ColonyLifeSupport onboard{};
    onboard.food_kg=provisions_kg_each;
    onboard.water_kg=provisions_kg_each;
    onboard.oxygen_kg=provisions_kg_each;
    supply->second.food_kg-=provisions_kg_each;
    supply->second.water_kg-=provisions_kg_each;
    supply->second.oxygen_kg-=provisions_kg_each;
    life_support_.emplace(hull_id,onboard);
    hull->second.fitting.hardpoints[0]=1; // paid physical scanner module
    for(const auto id:crew){
        auto& n=npcs_.at(id);
        n.home_ship=hull_id;
        n.location=hull_id;
        n.assigned_site=hull_id;
        n.residence_kind=transport::LocationKind::ColonyShip;
        n.residence_id=hull_id;
    }
    job->second.commissioned_tick=tick_;
    job->second.commissioning_crew=crew;
    job->second.provisions_kg_each=provisions_kg_each;
    snapshot_version_=std::max(snapshot_version_,92);
    return true;
}
bool Simulation::plan_intercolony_freight(PlayerId owner,
        std::uint64_t local_order_id,EntityId carrier,EntityId destination) {
    const auto job=local_delivery_orders_.find(local_order_id);
    if(!owner || owner==kServerAuthority || job==local_delivery_orders_.end() ||
       job->second.owner!=owner ||
       job->second.stage==LocalDeliveryStage::Cancelled ||
       !carrier || !destination || carrier==destination ||
       carrier==job->second.board_ship || destination==job->second.board_ship ||
       intercolony_freight_.count(local_order_id) ||
       intercolony_freight_.size()>=10000)return false;
    const auto origin=job->second.board_ship;
    const auto a=entities_.find(origin),b=entities_.find(carrier),
               c=entities_.find(destination);
    const auto af=ship_frames_.find(origin),cf=ship_frames_.find(destination);
    if(a==entities_.end() || b==entities_.end() || c==entities_.end() ||
       a->second.type!=EntityType::Ship ||
       b->second.type!=EntityType::Ship ||
       c->second.type!=EntityType::Ship ||
       a->second.owner_id!=owner || b->second.owner_id!=owner ||
       c->second.owner_id!=owner || !colony_ships_.count(origin) ||
       !colony_ships_.count(carrier) || !colony_ships_.count(destination) ||
       !life_support_.count(origin) || !life_support_.count(carrier) ||
       !life_support_.count(destination) ||
       !colony_ships_.at(carrier).crew ||
       !b->second.fitting.exploration_scanner() ||
       (snapshot_version_>=20 && people_physically_aboard(carrier)==0) ||
       af==ship_frames_.end() || cf==ship_frames_.end() ||
       af->second.anchor_body!=std::get<0>(job->second.site) ||
       !cf->second.anchor_body ||
       af->second.anchor_body==cf->second.anchor_body ||
       af->second.system!=cf->second.system)return false;
    for(const auto& [id,other]:intercolony_freight_)
        if(other.carrier_ship==carrier &&
           other.stage!=IntercolonyFreightStage::Delivered)return false;
    intercolony_freight_.emplace(local_order_id,IntercolonyFreightLeg{
        local_order_id,owner,origin,carrier,destination,job->second.cargo,
        job->second.kilograms,af->second.anchor_body,0,0,
        IntercolonyFreightStage::AwaitingPickup});
    snapshot_version_=std::max(snapshot_version_,89);
    return true;
}
bool Simulation::load_intercolony_freight(PlayerId owner,
        std::uint64_t local_order_id,EntityId carrier,EntityId destination) {
    const auto job=local_delivery_orders_.find(local_order_id);
    if(!owner || owner==kServerAuthority || job==local_delivery_orders_.end() ||
       job->second.owner!=owner || job->second.stage!=LocalDeliveryStage::Complete ||
       !carrier || !destination || carrier==destination ||
       carrier==job->second.board_ship || destination==job->second.board_ship)
        return false;
    auto planned=intercolony_freight_.find(local_order_id);
    if(planned!=intercolony_freight_.end() &&
       (planned->second.stage!=IntercolonyFreightStage::AwaitingPickup ||
        planned->second.owner!=owner ||
        planned->second.carrier_ship!=carrier ||
        planned->second.destination_ship!=destination))return false;
    if(planned==intercolony_freight_.end() &&
       intercolony_freight_.size()>=10000)return false;
    const auto& pickup=job->second;
    const EntityId origin=pickup.board_ship;
    const auto a=entities_.find(origin),b=entities_.find(carrier),
               c=entities_.find(destination);
    const auto af=ship_frames_.find(origin),bf=ship_frames_.find(carrier),
               cf=ship_frames_.find(destination);
    const auto from=life_support_.find(origin),to=life_support_.find(destination);
    const auto source=colony_ships_.find(origin),freighter=colony_ships_.find(carrier);
    const auto receiving=colony_ships_.find(destination);
    if(a==entities_.end() || b==entities_.end() || c==entities_.end() ||
       a->second.owner_id!=owner || b->second.owner_id!=owner ||
       c->second.owner_id!=owner || source==colony_ships_.end() ||
       freighter==colony_ships_.end() || receiving==colony_ships_.end() ||
       !freighter->second.crew || !b->second.fitting.exploration_scanner() ||
       (snapshot_version_>=20 && people_physically_aboard(carrier)==0) ||
       from==life_support_.end() || to==life_support_.end() ||
       !life_support_.count(carrier) ||
       af==ship_frames_.end() || bf==ship_frames_.end() ||
       cf==ship_frames_.end() || !af->second.anchor_body ||
       af->second.anchor_body==cf->second.anchor_body ||
       af->second.system!=cf->second.system ||
       !same_local_frame(origin,carrier) ||
       !freight_berth(a->second,b->second))return false;
    for(const auto& [id,p]:pending_celestial_transits_) {
        (void)id;
        if(p.ship==origin || p.ship==carrier || p.ship==destination)return false;
    }
    // A ship already committed to a passenger voyage is not an available
    // NPC freight carrier: do not hijack or strand its real passenger manifest.
    for(const auto& [id,v]:voyages_) {
        (void)id;
        if(v.ferry==carrier && (v.status==1 || v.status==3))return false;
    }
    for(const auto& [id,other]:intercolony_freight_)
        if(id!=local_order_id &&
           other.stage!=IntercolonyFreightStage::Delivered &&
           other.carrier_ship==carrier)return false; // one claimed hull
    const char* code=sealed_freight_code(pickup.cargo);
    if(!code || pickup.kilograms<1 || pickup.kilograms>1000 ||
       supply_stack(from->second,pickup.cargo)<pickup.kilograms ||
       freighter->second.goods_kg.count(code))return false;
    // Carrier's real finite cargo bay, not life-support stock or a parallel ledger.
    if(ship_inventory_snapshot(carrier).free_liters()<pickup.kilograms)return false;
    supply_stack(from->second,pickup.cargo)-=pickup.kilograms;
    freighter->second.goods_kg.emplace(code,pickup.kilograms);
    if(planned!=intercolony_freight_.end()) {
        planned->second.loaded_tick=tick_;
        planned->second.stage=IntercolonyFreightStage::Loaded;
    }else
        intercolony_freight_.emplace(local_order_id,IntercolonyFreightLeg{
            local_order_id,owner,origin,carrier,destination,
            pickup.cargo,pickup.kilograms,af->second.anchor_body,tick_,0,
            IntercolonyFreightStage::Loaded});
    snapshot_version_=std::max(snapshot_version_,89);
    return true;
}
bool Simulation::deliver_intercolony_freight(PlayerId owner,
        std::uint64_t local_order_id) {
    auto it=intercolony_freight_.find(local_order_id);
    if(!owner || owner==kServerAuthority || it==intercolony_freight_.end() ||
       it->second.owner!=owner ||
       it->second.stage!=IntercolonyFreightStage::Loaded)return false;
    auto& leg=it->second;
    auto carrier=colony_ships_.find(leg.carrier_ship);
    auto recipient=life_support_.find(leg.destination_ship);
    const auto actual=entities_.find(leg.carrier_ship),
               destination=entities_.find(leg.destination_ship);
    const auto frame=ship_frames_.find(leg.carrier_ship),
               dest_frame=ship_frames_.find(leg.destination_ship);
    if(carrier==colony_ships_.end() || recipient==life_support_.end() ||
       actual==entities_.end() || destination==entities_.end() ||
       actual->second.owner_id!=owner || destination->second.owner_id!=owner ||
       frame==ship_frames_.end() || dest_frame==ship_frames_.end() ||
       !frame->second.anchor_body ||
       frame->second.anchor_body==leg.origin_body ||
       frame->second.anchor_body!=dest_frame->second.anchor_body ||
       !same_local_frame(leg.carrier_ship,leg.destination_ship) ||
       !freight_berth(actual->second,destination->second))return false;
    for(const auto& [id,p]:pending_celestial_transits_) {
        (void)id;
        if(p.ship==leg.carrier_ship || p.ship==leg.destination_ship)return false;
    }
    // An actual completed voyage of THIS hull to THIS physical destination
    // must postdate the source-to-carrier material handoff.
    std::uint64_t receipt=0;
    for(const auto& [id,p]:completed_celestial_transits_)
        if(p.ship==leg.carrier_ship && p.owner==owner &&
           p.destination_body==frame->second.anchor_body &&
           p.completed_tick>=leg.loaded_tick)receipt=id;
    const auto code=sealed_freight_code(leg.cargo);
    if(!receipt || !code)return false;
    const auto held=carrier->second.goods_kg.find(code);
    auto& stock=supply_stack(recipient->second,leg.cargo);
    if(held==carrier->second.goods_kg.end() ||
       held->second!=leg.kilograms || stock>1000000-leg.kilograms)return false;
    carrier->second.goods_kg.erase(held);
    stock+=leg.kilograms;
    leg.arrival_receipt=receipt;
    leg.stage=IntercolonyFreightStage::Delivered;
    return true;
}

std::optional<transport::Manifest> Simulation::intercolony_transport_manifest(
        std::uint64_t id) const {
    const auto it=intercolony_freight_.find(id);
    if(it==intercolony_freight_.end())return std::nullopt;
    const auto& leg=it->second;
    const auto actual=entities_.find(leg.carrier_ship);
    const auto ship=colony_ships_.find(leg.carrier_ship);
    if(actual==entities_.end() || ship==colony_ships_.end())return std::nullopt;
    transport::Manifest view{};
    view.vehicle=leg.carrier_ship;view.owner=leg.owner;
    view.mode=transport::Mode::OtherVehicle;
    view.berth={transport::LocationKind::ColonyShip,leg.source_ship};
    view.other_end={transport::LocationKind::ColonyShip,leg.destination_ship};
    view.seat_capacity=0;
    view.cargo_capacity_kg=std::uint64_t(actual->second.cargo_capacity)*1000;
    for(const auto& [request,p]:pending_celestial_transits_)
        if(p.ship==leg.carrier_ship){
            view.in_transit=true;
            view.flight_receipt=request;
            break;
        }
    if(leg.stage==IntercolonyFreightStage::Loaded){
        const auto code=sealed_freight_code(leg.cargo);
        if(!code)return std::nullopt;
        const auto goods=ship->second.goods_kg.find(code);
        if(goods==ship->second.goods_kg.end() ||
           goods->second!=leg.kilograms)return std::nullopt;
        const auto resource=leg.cargo==ShuttleCargo::Water?
            transport::ResourceGood::Water:
            leg.cargo==ShuttleCargo::Food?
            transport::ResourceGood::Food:transport::ResourceGood::Nutrients;
        view.contents.push_back({transport::GoodKind::Resource,
            static_cast<std::uint8_t>(resource),
            std::uint64_t(leg.kilograms),std::uint64_t(leg.kilograms)});
    }
    return view.valid()?std::optional<transport::Manifest>(view):std::nullopt;
}

std::optional<transport::Manifest> Simulation::planetary_transport_manifest(
       const PlanetarySiteKey& key) const {
    const auto sh=planetary_shuttles_.find(key);
    const auto site=planetary_sites_.find(key);
    if(sh==planetary_shuttles_.end() || site==planetary_sites_.end())
        return std::nullopt;
    const auto& vessel=sh->second;
    const bool returning=vessel.flight_phase==2 || vessel.flight_phase==3;
    const transport::Endpoint orbit{transport::LocationKind::ColonyShip,
                                     vessel.source_ship};
    const transport::Endpoint ground{transport::LocationKind::PlanetarySite,
                                      site->second.surface_location};
    transport::Manifest manifest{};
    manifest.vehicle=shuttle_transit_custody_id(key); // stable per physical shuttle
    manifest.owner=vessel.owner;
    manifest.mode=transport::Mode::PlanetaryShuttle;
    manifest.berth=returning?ground:orbit;
    manifest.other_end=returning?orbit:ground;
    manifest.pilot=vessel.pilot_npc;
    manifest.in_transit=vessel.flight_phase==1 || vessel.flight_phase==3;
    manifest.flight_receipt=manifest.in_transit?vessel.flight_request:
        (returning?vessel.descent_receipt:vessel.ascent_receipt);
    manifest.seat_capacity=3;
    manifest.cargo_capacity_kg=vessel.housing_module?3000:1000;
    for(const auto id:vessel.passenger_ids)
        manifest.contents.push_back({transport::GoodKind::Npc,id,1,0});
    if(vessel.family.stage>=1 && vessel.family.stage<=3)
        for(const auto id:vessel.family.child_ids)
            manifest.contents.push_back({transport::GoodKind::Npc,id,1,0});
    std::sort(manifest.contents.begin(),manifest.contents.end(),
        transport::before_good_key);
    const auto add_stock=[&](transport::ResourceGood kind,std::int64_t kilograms){
        if(kilograms>0)manifest.contents.push_back({
            transport::GoodKind::Resource,static_cast<std::uint8_t>(kind),
            static_cast<std::uint64_t>(kilograms),
            static_cast<std::uint64_t>(kilograms)});
    };
    add_stock(transport::ResourceGood::Water,vessel.water_kg);
    add_stock(transport::ResourceGood::Food,vessel.food_kg);
    add_stock(transport::ResourceGood::Nutrients,vessel.nutrients_kg);
    add_stock(transport::ResourceGood::Oxygen,vessel.oxygen_kg);
    if(vessel.housing_module)
        manifest.contents.push_back({transport::GoodKind::Equipment,
            transport::kPlanetaryHousingModuleEquipmentId,1,2000});
    // The order in which canonical inventories are enumerated is fixed.
    // No separate NPC roster, cargo stock or delivery receipt is created.
    return manifest.valid()?std::optional<transport::Manifest>(std::move(manifest)):
                            std::nullopt;
}

bool Simulation::load_planetary_shuttle(PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key,bool water,std::int64_t kg) {
    return load_planetary_shuttle(owner,ship,key,
        water?ShuttleCargo::Water:ShuttleCargo::Food,kg);
}
bool Simulation::load_planetary_shuttle(PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key,ShuttleCargo cargo,std::int64_t kg) {
    auto shuttle=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    const auto frame=ship_frames_.find(ship);
    const auto hull=entities_.find(ship);
    auto life=life_support_.find(ship);
    if(!owner || std::get<3>(key)!=1 ||
       shuttle==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.source_ship!=ship || shuttle->second.owner!=owner ||
       frame==ship_frames_.end() || frame->second.anchor_body!=std::get<0>(key) ||
       hull==entities_.end() || hull->second.owner_id!=owner ||
       hull->second.dock_state!=DockState::InSpace ||
       hull->second.jump_state!=ShipJumpState::Ready ||
       life==life_support_.end() || kg<=0 || kg>1000 ||
       cargo>ShuttleCargo::Oxygen ||
       (shuttle->second.flight_phase!=0 && shuttle->second.flight_phase!=2))
       return false;
    auto& held=cargo==ShuttleCargo::Water?shuttle->second.water_kg:
        cargo==ShuttleCargo::Food?shuttle->second.food_kg:
        cargo==ShuttleCargo::Nutrients?shuttle->second.nutrients_kg:
                                       shuttle->second.oxygen_kg;
    auto& from=shuttle->second.flight_phase==0?
        (cargo==ShuttleCargo::Water?life->second.water_kg:
         cargo==ShuttleCargo::Food?life->second.food_kg:
         cargo==ShuttleCargo::Nutrients?life->second.nutrients_kg:
                                        life->second.oxygen_kg):
        (cargo==ShuttleCargo::Water?site->second.water_kg:
         cargo==ShuttleCargo::Food?site->second.food_kg:
         cargo==ShuttleCargo::Nutrients?site->second.nutrients_kg:
                                        site->second.oxygen_kg);
    const auto& state=shuttle->second;
    if(from<kg || held>1000-kg ||
       state.water_kg+state.food_kg+state.nutrients_kg+
        state.oxygen_kg>1000-kg)return false;
    from-=kg;held+=kg;
    if(cargo==ShuttleCargo::Nutrients)
        snapshot_version_=std::max(snapshot_version_,87);
    if(cargo==ShuttleCargo::Oxygen)
        snapshot_version_=std::max(snapshot_version_,109);
    return true;
}
bool Simulation::unload_planetary_shuttle(PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key,bool water,std::int64_t kg) {
    return unload_planetary_shuttle(owner,ship,key,
        water?ShuttleCargo::Water:ShuttleCargo::Food,kg);
}
bool Simulation::unload_planetary_shuttle(PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key,ShuttleCargo cargo,std::int64_t kg) {
    auto shuttle=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    const auto frame=ship_frames_.find(ship);
    const auto hull=entities_.find(ship);
    auto life=life_support_.find(ship);
    if(!owner || std::get<3>(key)!=1 ||
       shuttle==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.source_ship!=ship || shuttle->second.owner!=owner ||
       frame==ship_frames_.end() || frame->second.anchor_body!=std::get<0>(key) ||
       hull==entities_.end() || hull->second.owner_id!=owner ||
       hull->second.dock_state!=DockState::InSpace ||
       hull->second.jump_state!=ShipJumpState::Ready ||
       life==life_support_.end() || kg<=0 || kg>1000 ||
       cargo>ShuttleCargo::Oxygen ||
       (shuttle->second.flight_phase!=0 && shuttle->second.flight_phase!=2))
       return false;
    auto& held=cargo==ShuttleCargo::Water?shuttle->second.water_kg:
        cargo==ShuttleCargo::Food?shuttle->second.food_kg:
        cargo==ShuttleCargo::Nutrients?shuttle->second.nutrients_kg:
                                       shuttle->second.oxygen_kg;
    auto& to=shuttle->second.flight_phase==0?
        (cargo==ShuttleCargo::Water?life->second.water_kg:
         cargo==ShuttleCargo::Food?life->second.food_kg:
         cargo==ShuttleCargo::Nutrients?life->second.nutrients_kg:
                                        life->second.oxygen_kg):
        (cargo==ShuttleCargo::Water?site->second.water_kg:
         cargo==ShuttleCargo::Food?site->second.food_kg:
         cargo==ShuttleCargo::Nutrients?site->second.nutrients_kg:
                                        site->second.oxygen_kg);
    if(held<kg || to>1000000-kg)return false;
    held-=kg;to+=kg;
    if(cargo==ShuttleCargo::Nutrients)
        snapshot_version_=std::max(snapshot_version_,87);
    if(cargo==ShuttleCargo::Oxygen)
        snapshot_version_=std::max(snapshot_version_,109);
    return true;
}
bool Simulation::assign_planetary_shuttle_pilot(PlayerId owner,EntityId ship,
       const PlanetarySiteKey& key,std::uint64_t npc) {
    auto shuttle=planetary_shuttles_.find(key);
    const auto crew=npcs_.find(npc);
    if(!owner || shuttle==planetary_shuttles_.end() ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=ship ||
       shuttle->second.flight_phase!=0 || // pilot changes only at orbital berth
       !npc || crew==npcs_.end() || crew->second.home_ship!=ship ||
       crew->second.location!=ship || crew->second.role!=NpcRole::Ship ||
       crew->second.assigned_site!=ship || crew->second.transport_id ||
       !npc_available_for_duty(npc))return false;
    for(const auto& [child,dependent]:physical_dependents_) {
        (void)child;
        if(dependent.guardian_id==npc)return false;
    }
    if(std::binary_search(shuttle->second.passenger_ids.begin(),
                          shuttle->second.passenger_ids.end(),npc))return false;
    for(const auto& [other,record]:planetary_shuttles_)
        if(record.pilot_npc==npc && other!=key ||
           std::binary_search(record.passenger_ids.begin(),
                              record.passenger_ids.end(),npc) ||
           std::binary_search(record.disembarked_ids.begin(),
                              record.disembarked_ids.end(),npc))return false;
    shuttle->second.pilot_npc=npc;
    return true; // no boarding or NPC transfer before flight scheduler
}


// WO3-E: booking is private host authority, not a new ART02 parentage
// registry. PersistentWorld proves the original birth/custody SQLite rows
// before invoking this, in the same single-writer world operation.
bool Simulation::book_planetary_shuttle_family(PlayerId host_principal,
        PlayerId owner,EntityId ship,const PlanetarySiteKey& key,
        std::uint64_t guardian,std::uint64_t co_parent,
        const std::vector<std::uint64_t>& children,std::uint64_t request_id) {
    if(host_principal!=kServerAuthority || !owner || owner==kServerAuthority ||
       !request_id || request_id==std::numeric_limits<std::uint64_t>::max() ||
       !guardian || guardian==co_parent || children.empty() ||
       children.size()>2 || !std::is_sorted(children.begin(),children.end()) ||
       std::adjacent_find(children.begin(),children.end())!=children.end() ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       planetary_family_booking_ids_.count(request_id) ||
       planetary_family_booking_ids_.size()>=100000 ||
       voyages_.count(request_id) || local_delivery_orders_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id) ||
       (legacy_planetary_residence_import_ &&
        legacy_planetary_residence_import_->host_request_id==request_id))
        return false;
    const auto sh=planetary_shuttles_.find(key);
    const auto site=planetary_sites_.find(key);
    const auto hull=entities_.find(ship);
    if(sh==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       hull==entities_.end() || hull->second.owner_id!=owner ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || sh->second.flight_phase!=0 ||
       sh->second.family.stage || !sh->second.pilot_npc ||
       sh->second.pilot_npc==guardian ||
       sh->second.pilot_npc==co_parent)return false;
    for(const auto& [other_key,other]:planetary_shuttles_) {
        (void)other_key;
        if(other.family.booking_request==request_id ||
           other.family.outbound_receipt==request_id ||
           other.family.return_receipt==request_id)return false;
    }
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(other.housing_order_id==request_id ||
           other.utility_order_id==request_id)return false;
        for(const auto& p:other.production_projects)
            if(p.order_id==request_id)return false;
        for(const auto& [person,proof]:other.visitor_residency) {
            (void)person;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    const auto permitted=[&](std::uint64_t adult) {
        const auto person=npcs_.find(adult);
        if(person==npcs_.end() || person->second.home_ship!=ship ||
           person->second.location!=ship ||
           person->second.assigned_site!=ship ||
           person->second.role!=NpcRole::Ship ||
           person->second.transport_id || !npc_available_for_duty(adult))
            return false;
        for(const auto& [other_key,other]:planetary_shuttles_) {
            if(other_key==key)continue;
            if(other.pilot_npc==adult ||
               std::binary_search(other.passenger_ids.begin(),
                                  other.passenger_ids.end(),adult) ||
               std::binary_search(other.disembarked_ids.begin(),
                                  other.disembarked_ids.end(),adult))
                return false;
        }
        return true;
    };
    if(!permitted(guardian) || (co_parent && !permitted(co_parent)))
        return false;
    const auto& aboard=sh->second.passenger_ids;
    if(std::binary_search(aboard.begin(),aboard.end(),guardian) ||
       (co_parent && std::binary_search(aboard.begin(),aboard.end(),co_parent)) ||
       aboard.size()+children.size()+1+(co_parent?1:0)>3)return false;
    for(auto id:children){
        const auto dep=physical_dependents_.find(id);
        if(id<(1ULL<<62) || dep==physical_dependents_.end() ||
           dep->second.home_ship!=ship || dep->second.location!=ship ||
           (dep->second.guardian_id!=guardian &&
            dep->second.guardian_id!=co_parent) ||
           npcs_.count(id) || casualties_.count(id))return false;
    }
    // A guardian cannot board without every one of their physically
    // registered co-located dependents. SQLite checks both birth parents.
    for(const auto& [id,dep]:physical_dependents_) {
        if(dep.guardian_id!=guardian &&
           (!co_parent || dep.guardian_id!=co_parent))continue;
        if(dep.home_ship==ship && dep.location==ship &&
           !std::binary_search(children.begin(),children.end(),id))
            return false;
        if(dep.home_ship==ship && dep.location!=ship)return false;
    }
    auto& loaded=sh->second.passenger_ids;
    loaded.insert(std::lower_bound(loaded.begin(),loaded.end(),guardian),guardian);
    if(co_parent)
        loaded.insert(std::lower_bound(loaded.begin(),loaded.end(),co_parent),
                      co_parent);
    auto& family=sh->second.family;
    family.guardian_id=guardian;family.co_parent_id=co_parent;
    family.booking_request=request_id;family.child_ids=children;
    family.stage=1;
    planetary_family_booking_ids_.insert(request_id);
    snapshot_version_=std::max(snapshot_version_,116);
    return true;
}

// WO3-E/v118: the house AND the real child must be on this planet.
// The matching ART02 census transfer is performed by the sole SQLite
// writer; this in-memory operation alone is never a legal proof of custody.
bool Simulation::settle_planetary_household(PlayerId host_principal,
        PlayerId owner,EntityId ship,const PlanetarySiteKey& key,
        std::uint64_t request_id,bool guardian_consent,bool co_parent_consent) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    if(host_principal!=kServerAuthority || !owner || owner==kServerAuthority ||
       !guardian_consent || !co_parent_consent || !request_id ||
       request_id>std::numeric_limits<std::uint64_t>::max()-3 ||
       request_id%16!=0 || sh==planetary_shuttles_.end() ||
       site==planetary_sites_.end() || !entities_.count(ship) ||
       entities_.at(ship).owner_id!=owner ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       sh->second.flight_phase!=2 || sh->second.family.stage!=2 ||
       site->second.spaceport_phase!=3 ||
       site->second.housing_phase!=2 || site->second.utility_phase!=2 ||
       site->second.service_projects[0].phase!=2 ||
       site->second.stored_electricity<3 ||
       site->second.food_kg<3 || site->second.water_kg<3 ||
       site->second.oxygen_kg<3 ||
       site->second.households.size()>=32 ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       planetary_family_booking_ids_.count(request_id) ||
       voyages_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id) ||
       (legacy_planetary_residence_import_ &&
        legacy_planetary_residence_import_->host_request_id==request_id))
        return false;
    const auto& fam=sh->second.family;
    if(!fam.guardian_id || !fam.co_parent_id ||
       fam.child_ids.size()!=1 || !fam.outbound_receipt ||
       fam.return_receipt || !fam.booking_request ||
       site->second.households.count(fam.guardian_id) ||
       sh->second.passenger_ids.size()!=2 ||
       !std::binary_search(sh->second.passenger_ids.begin(),
                           sh->second.passenger_ids.end(),fam.guardian_id) ||
       !std::binary_search(sh->second.passenger_ids.begin(),
                           sh->second.passenger_ids.end(),fam.co_parent_id))
        return false;
    const auto child=physical_dependents_.find(fam.child_ids[0]);
    if(child==physical_dependents_.end() ||
       child->second.home_ship!=ship ||
       child->second.location!=site->second.surface_location ||
       (child->second.guardian_id!=fam.guardian_id &&
        child->second.guardian_id!=fam.co_parent_id))
        return false;
    std::int64_t adult_residents=0,other_children=0;
    for(const auto& [id,n]:npcs_) {
        if(n.legal_residence()!=transport::Endpoint{
           transport::LocationKind::PlanetarySite,
           site->second.surface_location})continue;
        if(legacy_planetary_residence_import_ &&
           legacy_planetary_residence_import_->legal_addresses.count(id))
            continue; // grandfathered address does not invent an actual bed
        ++adult_residents;
    }
    for(const auto& [person,h]:site->second.households) {
        (void)person;
        if(!h.return_request)
            other_children+=std::int64_t(h.child_ids.size());
        if(h.admission_request==request_id || h.return_request==request_id)
            return false;
    }
    const auto beds=std::int64_t{4}+4;
    if(std::max<std::int64_t>(2,adult_residents)+other_children+3>beds)
        return false;
    for(const auto adult:{fam.guardian_id,fam.co_parent_id}) {
        const auto it=npcs_.find(adult);
        if(it==npcs_.end() || it->second.home_ship!=ship ||
           it->second.location!=site->second.surface_location ||
           it->second.role!=NpcRole::Ship ||
           it->second.assigned_site!=ship ||
           it->second.transport_id ||
           it->second.legal_residence()!=transport::Endpoint{
               transport::LocationKind::ColonyShip,ship} ||
           std::binary_search(sh->second.disembarked_ids.begin(),
                              sh->second.disembarked_ids.end(),adult) ||
           site->second.visitor_residency.count(adult))
            return false;
    }
    for(const auto& [other_key,other]:planetary_sites_) {
        (void)other_key;
        if(other.housing_order_id==request_id ||
           other.utility_order_id==request_id)return false;
        for(const auto& p:other.production_projects)
            if(p.order_id==request_id)return false;
        for(const auto& p:other.service_projects)
            if(p.order_id==request_id)return false;
        for(const auto& h:other.utility_handovers)
            if(h.request_id==request_id)return false;
        for(const auto& chain:other.production_handovers)
            for(const auto& h:chain)
                if(h.request_id==request_id)return false;
        for(const auto& chain:other.service_handovers)
            for(const auto& h:chain)
                if(h.request_id==request_id)return false;
        for(const auto& [adult,proof]:other.visitor_residency) {
            (void)adult;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    PlanetaryHouseholdResidence resident{};
    resident.co_parent_id=fam.co_parent_id;
    resident.booking_request=fam.booking_request;
    resident.arrival_receipt=fam.outbound_receipt;
    resident.admission_request=request_id;
    resident.admission_tick=tick_;
    resident.consent_mask=3; // signed two-parent host attestations
    resident.child_ids=fam.child_ids;
    site->second.households.emplace(fam.guardian_id,std::move(resident));
    for(const auto adult:{fam.guardian_id,fam.co_parent_id}) {
        auto& npc=npcs_.at(adult);
        npc.residence_kind=transport::LocationKind::PlanetarySite;
        npc.residence_id=site->second.surface_location;
        sh->second.passenger_ids.erase(
            std::lower_bound(sh->second.passenger_ids.begin(),
                             sh->second.passenger_ids.end(),adult));
        sh->second.disembarked_ids.insert(
            std::lower_bound(sh->second.disembarked_ids.begin(),
                             sh->second.disembarked_ids.end(),adult),adult);
        sh->second.visitor_arrival_receipts.emplace(
            adult,fam.outbound_receipt);
    }
    sh->second.family={};
    snapshot_version_=std::max(snapshot_version_,118);
    return true;
}

// Physically board the SAME existing parent+child household for a legal
// return. Adults have not lost their existing surface residence merely by
// entering the shuttle; ART02 reverse migration waits for real ship arrival.
bool Simulation::book_planetary_household_return(PlayerId host_principal,
        PlayerId owner,EntityId ship,const PlanetarySiteKey& key,
        std::uint64_t guardian,std::uint64_t request_id) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    if(host_principal!=kServerAuthority || !owner || owner==kServerAuthority ||
       !guardian || !request_id ||
       sh==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       sh->second.flight_phase!=2 || sh->second.family.stage ||
       site->second.spaceport_phase!=3 ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       planetary_family_booking_ids_.count(request_id) ||
       voyages_.count(request_id) ||
       local_delivery_orders_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id))
        return false;
    const auto resident=site->second.households.find(guardian);
    if(resident==site->second.households.end() ||
       resident->second.return_request ||
       resident->second.child_ids.size()!=1 ||
       !resident->second.co_parent_id ||
       sh->second.passenger_ids.size()>0 ||
       !sh->second.pilot_npc)return false;
    const auto co_parent=resident->second.co_parent_id;
    const auto child_id=resident->second.child_ids[0];
    const auto child=physical_dependents_.find(child_id);
    if(child==physical_dependents_.end() ||
       child->second.home_ship!=ship ||
       child->second.location!=site->second.surface_location ||
       (child->second.guardian_id!=guardian &&
        child->second.guardian_id!=co_parent))
        return false;
    for(const auto adult:{guardian,co_parent}) {
        const auto n=npcs_.find(adult);
        if(n==npcs_.end() || n->second.home_ship!=ship ||
           n->second.location!=site->second.surface_location ||
           n->second.role!=NpcRole::Ship || n->second.assigned_site!=ship ||
           n->second.transport_id ||
           n->second.legal_residence()!=transport::Endpoint{
               transport::LocationKind::PlanetarySite,
               site->second.surface_location} ||
           !std::binary_search(sh->second.disembarked_ids.begin(),
                               sh->second.disembarked_ids.end(),adult))
            return false;
    }
    for(const auto adult:{guardian,co_parent}) {
        sh->second.disembarked_ids.erase(
            std::lower_bound(sh->second.disembarked_ids.begin(),
                             sh->second.disembarked_ids.end(),adult));
        sh->second.visitor_arrival_receipts.erase(adult);
        sh->second.passenger_ids.insert(
            std::lower_bound(sh->second.passenger_ids.begin(),
                             sh->second.passenger_ids.end(),adult),adult);
    }
    auto& family=sh->second.family;
    family.guardian_id=guardian;family.co_parent_id=co_parent;
    family.booking_request=resident->second.booking_request;
    family.outbound_receipt=resident->second.arrival_receipt;
    family.child_ids=resident->second.child_ids;
    family.stage=5;
    // Keep return-boarding approval immutable across crash/restart, separate
    // from the later consented legal return request and flight receipt.
    planetary_family_booking_ids_.insert(request_id);
    snapshot_version_=std::max(snapshot_version_,118);
    return true;
}

// Host may reverse residence ONLY after the original vehicle has actually
// returned every family member aboard the owner ship. The corresponding
// ART02+regional population change is inseparable from SQLite commit.
bool Simulation::finalize_planetary_household_return(
        PlayerId host_principal,PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t guardian,
        std::uint64_t request_id) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    if(host_principal!=kServerAuthority || !owner || owner==kServerAuthority ||
       !request_id || request_id>std::numeric_limits<std::uint64_t>::max()-3 ||
       request_id%16!=0 || !guardian ||
       sh==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       !entities_.count(ship) || entities_.at(ship).owner_id!=owner ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       sh->second.flight_phase!=0 || sh->second.family.stage!=7 ||
       sh->second.family.guardian_id!=guardian ||
       submitted_ids_.count(request_id) ||
       shuttle_flight_request_ids_.count(request_id) ||
       planetary_family_booking_ids_.count(request_id) ||
       voyages_.count(request_id) ||
       local_delivery_orders_.count(request_id))
        return false;
    auto found=site->second.households.find(guardian);
    if(found==site->second.households.end() ||
       found->second.return_request ||
       found->second.co_parent_id!=sh->second.family.co_parent_id ||
       found->second.child_ids!=sh->second.family.child_ids)
        return false;
    const auto co_parent=found->second.co_parent_id;
    for(const auto adult:{guardian,co_parent}) {
        const auto n=npcs_.find(adult);
        if(n==npcs_.end() || n->second.location!=ship ||
           n->second.home_ship!=ship ||
           n->second.legal_residence()!=transport::Endpoint{
               transport::LocationKind::PlanetarySite,
               site->second.surface_location})return false;
    }
    for(const auto child_id:found->second.child_ids) {
        const auto d=physical_dependents_.find(child_id);
        if(d==physical_dependents_.end() || d->second.home_ship!=ship ||
           d->second.location!=ship)return false;
    }
    for(const auto adult:{guardian,co_parent}) {
        auto& n=npcs_.at(adult);
        n.residence_kind=transport::LocationKind::ColonyShip;
        n.residence_id=ship;
    }
    found->second.return_request=request_id;
    found->second.return_tick=tick_;
    sh->second.family.stage=4; // existing guarded release now applies
    return true;
}

bool Simulation::release_planetary_shuttle_family(PlayerId host_principal,
        PlayerId owner,EntityId ship,const PlanetarySiteKey& key) {
    const auto sh=planetary_shuttles_.find(key);
    if(host_principal!=kServerAuthority || !owner || owner==kServerAuthority ||
       sh==planetary_shuttles_.end() || sh->second.owner!=owner ||
       sh->second.source_ship!=ship || sh->second.flight_phase!=0 ||
       sh->second.family.stage!=4)return false;
    const auto& family=sh->second.family;
    if(!planetary_family_booking_ids_.count(family.booking_request))
        return false;
    for(const auto child:family.child_ids) {
        const auto dep=physical_dependents_.find(child);
        if(dep==physical_dependents_.end() ||
           dep->second.location!=ship || dep->second.home_ship!=ship)
            return false;
    }
    for(const auto parent:{family.guardian_id,family.co_parent_id}) {
        if(!parent)continue;
        const auto npc=npcs_.find(parent);
        if(npc==npcs_.end() || npc->second.location!=ship ||
           npc->second.home_ship!=ship)return false;
    }
    // Physically deboard the completed original household at the real
    // parent-ship berth. No adult remains silently reserved as a passenger.
    for(const auto adult:{family.guardian_id,family.co_parent_id}) {
        if(!adult)continue;
        auto& passengers=sh->second.passenger_ids;
        const auto it=std::lower_bound(passengers.begin(),passengers.end(),adult);
        if(it==passengers.end() || *it!=adult)return false;
    }
    for(const auto adult:{family.guardian_id,family.co_parent_id}) {
        if(!adult)continue;
        auto& passengers=sh->second.passenger_ids;
        passengers.erase(std::lower_bound(passengers.begin(),
                                           passengers.end(),adult));
    }
    sh->second.family={}; // immutable original request ID remains recorded
    return true;
}
bool Simulation::book_planetary_shuttle_passenger(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool board) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    auto person=npcs_.find(npc);
    if(!owner || !npc || sh==planetary_shuttles_.end() ||
       site==planetary_sites_.end() || person==npcs_.end() ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       sh->second.flight_phase!=0 ||
       person->second.home_ship!=ship || person->second.location!=ship ||
       person->second.role!=NpcRole::Ship ||
       person->second.assigned_site!=ship || person->second.transport_id ||
       npc==sh->second.pilot_npc)return false;
    auto& ids=sh->second.passenger_ids;
    auto at=std::lower_bound(ids.begin(),ids.end(),npc);
    // A bound guardian cannot be unbooked independently of their child.
    const auto& family=sh->second.family;
    if(family.stage && (npc==family.guardian_id ||
                        npc==family.co_parent_id))return false;
    if(board && family.stage>=1 && family.stage<=3 &&
       ids.size()+family.child_ids.size()>=3)return false;
    if(!board) {
        if(at==ids.end() || *at!=npc)return false;
        ids.erase(at);
        snapshot_version_=std::max(snapshot_version_,82);
        return true;
    }
    if(ids.size()>=3 || (at!=ids.end() && *at==npc) ||
       !npc_available_for_duty(npc))return false;
    for(const auto& [id,child]:physical_dependents_) {
        (void)id;
        if(child.guardian_id==npc)return false;
    }
    for(const auto& [other,flight]:planetary_shuttles_)
        if(other!=key && (flight.pilot_npc==npc ||
            std::binary_search(flight.passenger_ids.begin(),
                               flight.passenger_ids.end(),npc) ||
            std::binary_search(flight.disembarked_ids.begin(),
                               flight.disembarked_ids.end(),npc)))return false;
    ids.insert(at,npc);
    snapshot_version_=std::max(snapshot_version_,82);
    return true;
}


bool Simulation::assign_planetary_shuttle_visitor_duty(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool assign) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    auto person=npcs_.find(npc);
    if(!owner || !npc || sh==planetary_shuttles_.end() ||
       site==planetary_sites_.end() || person==npcs_.end() ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       !std::binary_search(sh->second.disembarked_ids.begin(),
                           sh->second.disembarked_ids.end(),npc) ||
       (sh->second.visitor_arrival_receipts.count(npc) &&
        !npc_transport_arrival(sh->second.visitor_arrival_receipts.at(npc),npc)) ||
       person->second.home_ship!=ship ||
       person->second.location!=site->second.surface_location ||
       person->second.assigned_site!=ship ||
       person->second.role!=NpcRole::Ship || person->second.transport_id)
        return false;
    auto& duty=sh->second.duty_ids;
    auto at=std::lower_bound(duty.begin(),duty.end(),npc);
    if(assign) {
        if(at!=duty.end() && *at==npc)return false;
        const auto health=npc_health_status(npc);
        if(health!=NpcHealthStatus::Healthy &&
           health!=NpcHealthStatus::Treated &&
           health!=NpcHealthStatus::Recovered)return false;
        duty.insert(at,npc);
    }else {
        if(at==duty.end() || *at!=npc)return false;
        duty.erase(at);
    }
    snapshot_version_=std::max(snapshot_version_,84);
    return true;
}

bool Simulation::transfer_planetary_shuttle_visitor(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t npc,bool reboard) {
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    auto person=npcs_.find(npc);
    // A booked ART02 parent, including the second birth parent, cannot
    // disembark alone from a physically accompanied dependent.
    if(sh!=planetary_shuttles_.end() && sh->second.family.stage &&
       (npc==sh->second.family.guardian_id ||
        npc==sh->second.family.co_parent_id))return false;
    if(!owner || !npc || sh==planetary_shuttles_.end() ||
       site==planetary_sites_.end() || person==npcs_.end() ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || sh->second.flight_phase!=2 ||
       person->second.home_ship!=ship ||
       person->second.location!=site->second.surface_location ||
       person->second.assigned_site!=ship ||
       person->second.role!=NpcRole::Ship ||
       person->second.transport_id || npc==sh->second.pilot_npc)
        return false;
    for(const auto& [child,dependent]:physical_dependents_) {
        (void)child;
        if(dependent.guardian_id==npc)return false;
    }
    // Legal settlers cannot bypass the original consent-and-departure
    // receipt path by requesting an ordinary shuttle reboard.
    if(reboard && person->second.legal_residence()==transport::Endpoint{
          transport::LocationKind::PlanetarySite,
          site->second.surface_location})return false;
    auto& aboard=sh->second.passenger_ids;
    auto& visitors=sh->second.disembarked_ids;
    if(reboard) {
        auto at=std::lower_bound(visitors.begin(),visitors.end(),npc);
        const auto individual=sh->second.visitor_arrival_receipts.find(npc);
        // Pre-v85 visitors have no individual receipt; retain their physical
        // return-pickup capability, but do not grant immigration proof.
        const bool legacy_visitor=individual==sh->second.visitor_arrival_receipts.end();
        if((!legacy_visitor &&
            !npc_transport_arrival(individual->second,npc)) ||
           at==visitors.end() || *at!=npc || aboard.size()>=3 ||
           std::binary_search(aboard.begin(),aboard.end(),npc))
            return false;
        auto place=std::lower_bound(aboard.begin(),aboard.end(),npc);
        aboard.insert(place,npc);
        visitors.erase(at);
        sh->second.visitor_arrival_receipts.erase(npc);
        const auto duty=std::lower_bound(sh->second.duty_ids.begin(),
                                         sh->second.duty_ids.end(),npc);
        if(duty!=sh->second.duty_ids.end() && *duty==npc)
            sh->second.duty_ids.erase(duty);
    }else {
        auto at=std::lower_bound(aboard.begin(),aboard.end(),npc);
        if(at==aboard.end() || *at!=npc || visitors.size()>=64 ||
           std::binary_search(visitors.begin(),visitors.end(),npc))
            return false;
        auto place=std::lower_bound(visitors.begin(),visitors.end(),npc);
        visitors.insert(place,npc);
        aboard.erase(at);
        if(!sh->second.descent_receipt ||
           !shuttle_flight_request_ids_.count(sh->second.descent_receipt))
            throw std::logic_error("missing durable disembarkation flight receipt");
        sh->second.visitor_arrival_receipts.emplace(npc,sh->second.descent_receipt);
        snapshot_version_=std::max(snapshot_version_,85);
    }
    snapshot_version_=std::max(snapshot_version_,83);
    return true;
}


namespace {
EntityId shuttle_transit_custody_id(const PlanetarySiteKey& key) {
    // Distinct from every named surface custody location; collisions rejected
    // against real entities and all other shuttle transit locations.
    return surface_custody_id(key) ^ (std::uint64_t{1}<<62);
}
}
bool Simulation::launch_planetary_shuttle(PlayerId owner,EntityId ship,
        const PlanetarySiteKey& key,std::uint64_t request_id,bool ascend) {
    if(planetary_family_booking_ids_.count(request_id))
        return false; // no flight/construction/consent may replay family approval
    if(legacy_planetary_residence_import_ &&
       request_id==legacy_planetary_residence_import_->host_request_id)
        return false; // immutable host migration provenance, not reusable
    auto sh=planetary_shuttles_.find(key);
    auto site=planetary_sites_.find(key);
    auto hull=entities_.find(ship);
    auto frame=ship_frames_.find(ship);
    auto life=life_support_.find(ship);
    if(!owner || !request_id || request_id==std::numeric_limits<std::uint64_t>::max() ||
       sh==planetary_shuttles_.end() || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 || sh->second.owner!=owner ||
       sh->second.source_ship!=ship || !sh->second.pilot_npc ||
       sh->second.flight_phase!=(ascend?2:0) ||
       sh->second.water_kg<1 || tick_>std::numeric_limits<std::uint64_t>::max()-3 ||
       hull==entities_.end() || hull->second.owner_id!=owner ||
       hull->second.dock_state!=DockState::InSpace ||
       hull->second.jump_state!=ShipJumpState::Ready ||
       frame==ship_frames_.end() || frame->second.anchor_body!=std::get<0>(key) ||
       life==life_support_.end() ||
       (!ascend && npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Healthy &&
        npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Treated &&
        npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Recovered) ||
       (ascend && npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Healthy &&
        npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Treated &&
        npc_health_status(sh->second.pilot_npc)!=NpcHealthStatus::Recovered))
        return false;
    // Preserve the complete accepted ID set across restart and future flights.
    // Existing ferry, celestial transit and queued commands own their IDs too.
    if(shuttle_flight_request_ids_.size()>=100000 ||
       shuttle_flight_request_ids_.count(request_id) ||
       voyages_.count(request_id) ||
       submitted_ids_.count(request_id) ||
       pending_celestial_transits_.count(request_id) ||
       completed_celestial_transits_.count(request_id))return false;
    // House, utility and legal-residence requests are durable original
    // command identities too. A new flight cannot impersonate past consent.
    for(const auto& [other_key,existing]:planetary_sites_) {
        (void)other_key;
        if(existing.housing_order_id==request_id ||
           existing.utility_order_id==request_id)return false;
        for(const auto proof:existing.founder_consent_requests)
            if(proof==request_id)return false;
        for(const auto& relief:existing.utility_handovers)
            if(relief.request_id==request_id)return false;
        for(const auto& project:existing.production_projects)
            if(project.order_id==request_id)return false;
        for(const auto& project:existing.service_projects)
            if(project.order_id==request_id)return false;
        for(const auto& history:existing.service_handovers)
            for(const auto& h:history)
                if(h.request_id==request_id)return false;
        for(const auto& history:existing.production_handovers)
            for(const auto& handover:history)
                if(handover.request_id==request_id)return false;
        for(const auto& [person,proof]:existing.visitor_residency) {
            (void)person;
            if(proof[1]==request_id || proof[2]==request_id)return false;
        }
    }
    // Request IDs and pilot identities cannot be reused across shuttles/legs.
    for(const auto& [other,r]:planetary_shuttles_) {
        (void)other;
        if(r.flight_request==request_id || r.descent_receipt==request_id ||
           r.ascent_receipt==request_id)return false;
    }
    auto pilot=npcs_.find(sh->second.pilot_npc);
    if(pilot==npcs_.end() || pilot->second.home_ship!=ship ||
       pilot->second.role!=NpcRole::Ship ||
       pilot->second.assigned_site!=ship || pilot->second.transport_id ||
       pilot->second.location!=(ascend?site->second.surface_location:ship))
        return false;
    // Shared transport manifest discipline: parents remain original named
    // NPCs; ART02 children occupy actual passenger seats as registered
    // PhysicalDependent identities rather than fabricated worker records.
    const auto& family=sh->second.family;
    if(family.stage) {
        if((!ascend && family.stage!=1) ||
           (ascend && family.stage!=2 && family.stage!=5) ||
           sh->second.passenger_ids.size()+family.child_ids.size()>3 ||
           !std::binary_search(sh->second.passenger_ids.begin(),
                               sh->second.passenger_ids.end(),family.guardian_id) ||
           (family.co_parent_id &&
            !std::binary_search(sh->second.passenger_ids.begin(),
                                sh->second.passenger_ids.end(),
                                family.co_parent_id)))return false;
        for(const auto child:family.child_ids) {
            const auto d=physical_dependents_.find(child);
            if(d==physical_dependents_.end() || d->second.home_ship!=ship ||
               d->second.location!=(ascend?
                  site->second.surface_location:ship))return false;
        }
    }
    if(sh->second.passenger_ids.size()>3)return false;
    for(auto passenger_id:sh->second.passenger_ids) {
        const auto it=npcs_.find(passenger_id);
        if(it==npcs_.end() || passenger_id==sh->second.pilot_npc ||
           it->second.home_ship!=ship || it->second.role!=NpcRole::Ship ||
           it->second.assigned_site!=ship || it->second.transport_id ||
           it->second.location!=(ascend?site->second.surface_location:ship))
            return false;
        if(!ascend && npc_health_status(passenger_id)!=NpcHealthStatus::Healthy &&
           npc_health_status(passenger_id)!=NpcHealthStatus::Treated &&
           npc_health_status(passenger_id)!=NpcHealthStatus::Recovered)return false;
    }
    // One kg of physically loaded water is the simplified water propellant
    // per trip; spent propellant remains wastewater at the departure endpoint.
    auto& residue=ascend?site->second.wastewater_kg:life->second.wastewater_kg;
    if(residue>=1000000)return false;
    const auto transit=shuttle_transit_custody_id(key);
    if(!transit || entities_.count(transit))return false;
    // Every physical custody address must identify exactly one location.
    for(const auto& [other,facility]:planetary_sites_) {
        (void)other;
        if(facility.surface_location==transit)return false;
    }
    for(const auto& [other,r]:planetary_shuttles_)
        if(other!=key && r.flight_phase%2==1 &&
           transit==shuttle_transit_custody_id(other))return false;
    --sh->second.water_kg;
    ++residue;
    sh->second.flight_phase=ascend?3:1;
    sh->second.flight_request=request_id;
    sh->second.arrival_tick=tick_+3;
    if(ascend)sh->second.ascent_receipt=request_id;
    else sh->second.descent_receipt=request_id;
    pilot->second.location=transit;
    for(const auto passenger_id:sh->second.passenger_ids)
        npcs_.at(passenger_id).location=transit;
    if(sh->second.family.stage) {
        for(const auto child:sh->second.family.child_ids)
            physical_dependents_.at(child).location=transit;
        if(ascend) {
            sh->second.family.stage=sh->second.family.stage==5?6:3;
            sh->second.family.return_receipt=request_id;
        } else sh->second.family.outbound_receipt=request_id;
    }
    shuttle_flight_request_ids_.insert(request_id);
    snapshot_version_=std::max(snapshot_version_,81);
    return true;
}
void Simulation::process_planetary_shuttle_flights() {
    for(auto& [key,sh]:planetary_shuttles_) {
        if((sh.flight_phase!=1 && sh.flight_phase!=3) ||
           tick_<sh.arrival_tick)continue;
        auto site=planetary_sites_.find(key);
        auto npc=npcs_.find(sh.pilot_npc);
        if(site==planetary_sites_.end() || npc==npcs_.end() ||
           npc->second.location!=shuttle_transit_custody_id(key))
            throw std::logic_error("missing committed shuttle or pilot custody");
        npc->second.location=sh.flight_phase==1?
            site->second.surface_location:sh.source_ship;
        for(const auto passenger_id:sh.passenger_ids) {
            auto& passenger=npcs_.at(passenger_id);
            if(passenger.location!=shuttle_transit_custody_id(key))
                throw std::logic_error("shuttle manifest passenger missing custody");
            passenger.location=sh.flight_phase==1?
                site->second.surface_location:sh.source_ship;
        }
        if(sh.family.stage) {
            for(const auto child:sh.family.child_ids) {
                auto& person=physical_dependents_.at(child);
                if(person.location!=shuttle_transit_custody_id(key))
                    throw std::logic_error("bound child lost during shuttle flight");
                person.location=sh.flight_phase==1?
                    site->second.surface_location:sh.source_ship;
            }
            sh.family.stage=sh.flight_phase==1?2:
                sh.family.stage==6?7:4;
        }
        sh.flight_phase=sh.flight_phase==1?2:0;
        sh.flight_request=0;
        sh.arrival_tick=0;
    }
}

bool Simulation::planetary_spaceport_operational(std::uint64_t body,
    std::uint16_t latitude,std::uint16_t longitude) const {
    const auto it=planetary_sites_.find(
        PlanetarySiteKey{body,latitude,longitude,1});
    return it!=planetary_sites_.end() && it->second.spaceport_phase==3;
}
bool Simulation::set_planetary_workforce_policy(PlayerId owner,std::uint64_t body,
    std::uint16_t latitude,std::uint16_t longitude,std::uint8_t policy) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    const auto it=planetary_sites_.find(key);
    if(!owner || policy>4 || it==planetary_sites_.end() ||
       it->second.owner!=owner || it->second.spaceport_phase!=3)
        return false;
    auto& site=it->second;
    if(policy==1 && !site.farm)return false;
    if((policy==2 || policy==3) && (!site.extractor ||
       !oasis_reserves_.count(body)))return false;
    // A grandfathered v77 site without named NPCs cannot operate itself.
    if(policy && (site.assigned_workers!=2 ||
        !site.delegated_npcs[0] || !site.delegated_npcs[1]))return false;
    site.workforce_policy=policy;
    snapshot_version_=std::max(snapshot_version_,79);
    return true;
}
void Simulation::process_planetary_surface_operations() {
    // A shuttle pilot resident on the surface is not aboard the parent ship.
    // Use the existing physical NPC record; local provisions become local
    // waste, never a second crew instance or free consumable.
    if(snapshot_version_>=81 && tick_%20==0)
        for(const auto& [key,sh]:planetary_shuttles_) {
            auto site=planetary_sites_.find(key);
            if(site==planetary_sites_.end())continue;
            auto& ground=site->second;
            // Disembarked visitors remain physically on the surface even
            // while the shuttle returns to orbit or makes another trip.
            const auto locals=std::int64_t(sh.disembarked_ids.size());
            const auto landing=sh.flight_phase==2?
                1+std::int64_t(sh.passenger_ids.size()):0;
            // Only a LANDED shuttle is a physical passenger consumer.
            // A legally settled child may remain on the landing manifest:
            // changing residence never duplicates a person or ration.
            const std::int64_t flight_children=
                sh.flight_phase==2 && sh.family.stage==2?
                static_cast<std::int64_t>(sh.family.child_ids.size()):0;
            std::int64_t resident_children=0;
            for(const auto& [guardian,household]:site->second.households){
                (void)guardian;
                if(household.return_request)continue;
                for(const auto child:household.child_ids) {
                    const auto dep=physical_dependents_.find(child);
                    if(dep==physical_dependents_.end() ||
                       dep->second.location!=site->second.surface_location)
                        continue;
                    const bool on_landed_manifest=
                        sh.flight_phase==2 && sh.family.stage==2 &&
                        std::find(sh.family.child_ids.begin(),
                                  sh.family.child_ids.end(),child)!=
                            sh.family.child_ids.end();
                    if(!on_landed_manifest)++resident_children;
                }
            }
            const std::int64_t people=locals+landing+flight_children+
                resident_children;
            if(!people)continue;
            if(ground.food_kg<people || ground.water_kg<people ||
               ground.oxygen_kg<people) {
                ++ground.stalled_cycles;
                continue; // shortage; never synthesize supplies
            }
            ground.food_kg-=people;ground.water_kg-=people;
            ground.oxygen_kg-=people;
            ground.organic_waste_kg+=people;
            ground.wastewater_kg+=people;ground.co2_kg+=people;
        }
    for(auto& [key,site]:planetary_sites_) {
        (void)key;
        if(site.spaceport_phase==0 || site.assigned_workers==0)continue;
        // Deterministic bounded solar period. Generation has no created
        // matter; electricity is a 0..120 service buffer. HVAC needs one
        // unit every tick, even while people are unable to work.
        if(site.utility_phase==2) {
            if(tick_%20<14 && site.equipment_condition>0) {
                site.stored_electricity=std::min<std::int64_t>(
                    120,site.stored_electricity+3);
                ++site.utility_powered_cycles;
            }
            if(site.stored_electricity>=1) {
                --site.stored_electricity;
                ++site.hvac_cycles;
            } else ++site.utility_outages;
            // Loss-bearing, never perfect closure. Recycled water and
            // oxygen come only from actual on-site waste streams. The
            // brine/solid residue remains physically conserved locally.
            if(tick_%10==0 && site.hvac_cycles>0) {
                if(site.wastewater_kg>=10 &&
                   site.water_kg<=1000000-8 && site.brine_kg<=1000000-2 &&
                   site.stored_electricity>=3) {
                    site.wastewater_kg-=10;site.water_kg+=8;
                    site.brine_kg+=2;site.stored_electricity-=3;
                    site.water_recycled_kg=std::min<std::int64_t>(
                        1000000,site.water_recycled_kg+8);
                }
                if(site.co2_kg>=10 &&
                   site.oxygen_kg<=1000000-8 &&
                   site.organic_waste_kg<=1000000-2 &&
                   site.stored_electricity>=3) {
                    site.co2_kg-=10;site.oxygen_kg+=8;
                    site.organic_waste_kg+=2;site.stored_electricity-=3;
                    site.oxygen_recycled_kg=std::min<std::int64_t>(
                        1000000,site.oxygen_recycled_kg+8);
                }
            }
        }
        // A built annex or medical-support room is an ACTUAL powered
        // facility; neither duplicates the world energy inventory nor
        // generates medical supplies or magically heals injured people.
        if(site.utility_phase==2)
            for(auto& project:site.service_projects)
                if(project.phase==2 && site.stored_electricity>=1) {
                    --site.stored_electricity;
                    ++project.powered_cycles;
                }
        std::int64_t ready=0;
        for(auto npc:site.delegated_npcs) {
            const auto it=npcs_.find(npc);
            if(it!=npcs_.end() && it->second.home_ship==site.source_ship &&
               it->second.location==site.surface_location &&
               it->second.assigned_site==site.surface_location &&
               it->second.role==NpcRole::PlanetarySurface &&
               npc_available_for_duty(npc))++ready;
        }
        // Temporary NPC visitors supplement the two permanent delegated
        // workers. No permanent employment or immigration is inferred.
        const auto sh=planetary_shuttles_.find(key);
        if(sh!=planetary_shuttles_.end())
            for(const auto npc:sh->second.duty_ids) {
                const auto person=npcs_.find(npc);
                if(person==npcs_.end() ||
                   person->second.location!=site.surface_location ||
                   person->second.home_ship!=site.source_ship ||
                   person->second.role!=NpcRole::Ship ||
                   person->second.assigned_site!=site.source_ship)continue;
                const auto health=npc_health_status(npc);
                if(health==NpcHealthStatus::Healthy ||
                   health==NpcHealthStatus::Treated ||
                   health==NpcHealthStatus::Recovered)++ready;
            }
        if(ready<2) {
            if(site.utility_phase==1)++site.utility_stalled_ticks;
            if(site.housing_phase==1)++site.housing_stalled_ticks;
            for(auto& job:site.production_projects)
                if(job.phase==1)++job.stalled_ticks;
            for(auto& job:site.service_projects)
                if(job.phase==1)++job.stalled_ticks;
            if(site.housing_phase==2 && tick_%20==0)
                ++site.housing_shortages;
            if(site.spaceport_phase==3 && site.workforce_policy &&
               tick_%20==0){++site.stalled_cycles;site.last_work_tick=tick_;}
            continue; // injured, missing or unavailable crew pause work
        }
        if(site.food_kg<2 || site.water_kg<2 || site.oxygen_kg<2) {
            if(site.utility_phase==1)++site.utility_stalled_ticks;
            if(site.housing_phase==1)++site.housing_stalled_ticks;
            for(auto& job:site.production_projects)
                if(job.phase==1)++job.stalled_ticks;
            for(auto& job:site.service_projects)
                if(job.phase==1)++job.stalled_ticks;
            if(site.housing_phase==2 && tick_%20==0)
                ++site.housing_shortages;
            if(site.spaceport_phase==3 && site.workforce_policy &&
               tick_%20==0){++site.stalled_cycles;site.last_work_tick=tick_;}
            continue; // resupply before construction or work can resume
        }
        if(tick_%10==0) {
            site.food_kg-=2;
            site.organic_waste_kg+=2;
            site.water_kg-=2;
            site.wastewater_kg+=2;
            site.oxygen_kg-=2;
            site.co2_kg+=2;
            ++site.worker_support_cycles;
        }
        if(site.housing_phase==1) {
            // No manufacturing on acceptance: only real original world
            // ticks, locally provisioned named staff and paused competing
            // planetary workforce policy advance actual building.
            if(site.spaceport_phase==3 && !site.workforce_policy &&
               !site.housing_paused) {
                if(++site.housing_progress>=80) {
                    site.housing_progress=80;
                    site.housing_phase=2;
                    site.housing_completed_tick=tick_;
                }
            }else ++site.housing_stalled_ticks;
        }
        if(site.utility_phase==1) {
            const auto supervisor=npcs_.find(site.utility_supervisor);
            if(site.utility_paused || site.workforce_policy ||
               supervisor==npcs_.end() ||
               supervisor->second.location!=site.surface_location ||
               !workshop_trade_certified(site.utility_supervisor,
                                         WorkshopTrade::Shape) ||
               !([&]() {
                   const bool deployed=
                       supervisor->second.assigned_site==site.surface_location &&
                       supervisor->second.role==NpcRole::PlanetarySurface &&
                       npc_available_for_duty(site.utility_supervisor);
                   if(deployed)return true;
                   const auto shuttle=planetary_shuttles_.find(key);
                   if(shuttle==planetary_shuttles_.end() ||
                      supervisor->second.assigned_site!=site.source_ship ||
                      supervisor->second.role!=NpcRole::Ship ||
                      supervisor->second.transport_id ||
                      !std::binary_search(
                          shuttle->second.duty_ids.begin(),
                          shuttle->second.duty_ids.end(),
                          site.utility_supervisor))
                       return false;
                   const auto arrival=shuttle->second.visitor_arrival_receipts.find(
                       site.utility_supervisor);
                   const auto health=npc_health_status(site.utility_supervisor);
                   return arrival!=shuttle->second.visitor_arrival_receipts.end() &&
                       npc_transport_arrival(arrival->second,
                                             site.utility_supervisor).has_value() &&
                       (health==NpcHealthStatus::Healthy ||
                        health==NpcHealthStatus::Treated ||
                        health==NpcHealthStatus::Recovered);
               }()))
                ++site.utility_stalled_ticks;
            else if(++site.utility_progress>=60) {
                site.utility_progress=60;
                site.utility_phase=2;
                site.utility_completed_tick=tick_;
            }
        }
        for(std::size_t i=0;i<site.production_projects.size();++i) {
            auto& project=site.production_projects[i];
            if(project.phase!=1)continue;
            const auto builder=npcs_.find(project.supervisor);
            const bool real_delegate=builder!=npcs_.end() &&
                builder->second.assigned_site==site.surface_location &&
                builder->second.role==NpcRole::PlanetarySurface &&
                std::find(site.delegated_npcs.begin(),site.delegated_npcs.end(),
                          project.supervisor)!=site.delegated_npcs.end() &&
                npc_available_for_duty(project.supervisor);
            const auto shuttle=planetary_shuttles_.find(key);
            const bool actual_visitor=builder!=npcs_.end() &&
                shuttle!=planetary_shuttles_.end() &&
                builder->second.assigned_site==site.source_ship &&
                builder->second.role==NpcRole::Ship &&
                !builder->second.transport_id &&
                std::binary_search(shuttle->second.duty_ids.begin(),
                                   shuttle->second.duty_ids.end(),
                                   project.supervisor) &&
                std::binary_search(shuttle->second.disembarked_ids.begin(),
                                   shuttle->second.disembarked_ids.end(),
                                   project.supervisor) &&
                shuttle->second.visitor_arrival_receipts.count(
                    project.supervisor) &&
                npc_transport_arrival(
                    shuttle->second.visitor_arrival_receipts.at(
                        project.supervisor),project.supervisor).has_value() &&
                (npc_health_status(project.supervisor)==NpcHealthStatus::Healthy ||
                 npc_health_status(project.supervisor)==NpcHealthStatus::Treated ||
                 npc_health_status(project.supervisor)==NpcHealthStatus::Recovered);
            if(project.paused || site.workforce_policy ||
               builder==npcs_.end() ||
               builder->second.home_ship!=site.source_ship ||
               builder->second.location!=site.surface_location ||
               !workshop_trade_certified(project.supervisor,
                                         WorkshopTrade::Shape) ||
               (!real_delegate && !actual_visitor)) {
                ++project.stalled_ticks;
                continue;
            }
            if(++project.progress>=40) {
                project.progress=40;
                project.phase=2;
                project.completed_tick=tick_;
                // These are the existing original production mechanisms,
                // not new material sources or independent industry copies.
                if(i==0)site.farm=true;
                else site.extractor=true;
            }
        }
        for(auto& project:site.service_projects) {
            if(project.phase!=1)continue;
            const auto builder=npcs_.find(project.supervisor);
            const bool deployed=builder!=npcs_.end() &&
                builder->second.assigned_site==site.surface_location &&
                builder->second.role==NpcRole::PlanetarySurface &&
                std::find(site.delegated_npcs.begin(),site.delegated_npcs.end(),
                          project.supervisor)!=site.delegated_npcs.end() &&
                npc_available_for_duty(project.supervisor);
            const auto shuttle=planetary_shuttles_.find(key);
            const auto health=npc_health_status(project.supervisor);
            const bool visiting=builder!=npcs_.end() &&
                shuttle!=planetary_shuttles_.end() &&
                builder->second.assigned_site==site.source_ship &&
                builder->second.role==NpcRole::Ship &&
                !builder->second.transport_id &&
                std::binary_search(shuttle->second.duty_ids.begin(),
                                   shuttle->second.duty_ids.end(),
                                   project.supervisor) &&
                std::binary_search(shuttle->second.disembarked_ids.begin(),
                                   shuttle->second.disembarked_ids.end(),
                                   project.supervisor) &&
                shuttle->second.visitor_arrival_receipts.count(
                    project.supervisor) &&
                npc_transport_arrival(
                    shuttle->second.visitor_arrival_receipts.at(
                        project.supervisor),project.supervisor).has_value() &&
                (health==NpcHealthStatus::Healthy ||
                 health==NpcHealthStatus::Treated ||
                 health==NpcHealthStatus::Recovered);
            if(project.paused || site.workforce_policy ||
               builder==npcs_.end() ||
               builder->second.home_ship!=site.source_ship ||
               builder->second.location!=site.surface_location ||
               !workshop_trade_certified(project.supervisor,
                                         WorkshopTrade::Shape) ||
               (!deployed && !visiting)) {
                ++project.stalled_ticks;
                continue;
            }
            if(++project.progress>=40) {
                project.progress=40;project.phase=2;
                project.completed_tick=tick_;
            }
        }
        if(site.housing_phase==2 && tick_%20==0) {
            // The two named deployed residents already consume original
            // supplies in the ten-tick worker-support rule above. This is
            // an observational health-of-provisions cycle, not an invented
            // second ration withdrawal or a self-sufficiency certificate.
            if(site.food_kg>=2 && site.water_kg>=2 &&
               site.oxygen_kg>=2)++site.housing_supported_cycles;
            else ++site.housing_shortages;
        }
        if(site.spaceport_phase==3) {
            // Autonomous policies run on a bounded world-tick cadence. No
            // ship proximity is required; only the actually deployed NPCs
            // and the local physical inventories can sustain work.
            if(site.workforce_policy==0 || tick_%20!=0)continue;
            bool worked=false;
            if(site.workforce_policy==1 && site.farm &&
               site.equipment_condition>0 &&
               site.water_kg>=10 && site.nutrients_kg>=5 &&
               site.food_kg<=1000000-12 &&
               site.wastewater_kg<=1000000-3) {
                site.water_kg-=10;site.nutrients_kg-=5;
                site.food_kg+=12;site.wastewater_kg+=3;
                ++site.harvest_cycles;worked=true;
            } else if((site.workforce_policy==2 || site.workforce_policy==3) &&
                      site.extractor && site.equipment_condition>0) {
                auto r=oasis_reserves_.find(site.region.body);
                if(r!=oasis_reserves_.end()){
                    auto& source=site.workforce_policy==2?r->second.water_kg:
                                                      r->second.nutrients_kg;
                    auto& dest=site.workforce_policy==2?site.water_kg:
                                                    site.nutrients_kg;
                    if(source>=1000 && dest<=1000000-1000){
                        source-=1000;dest+=1000;
                        site.auto_extracted_kg+=1000;worked=true;
                    }
                }
            } else if(site.workforce_policy==4 &&
                      site.equipment_condition<100 &&
                      site.nutrients_kg>=1 &&
                      site.wastewater_kg<1000000) {
                // Finite consumable repair stock, not free healing:
                // nutrients are converted into local material waste.
                --site.nutrients_kg;++site.wastewater_kg;
                site.equipment_condition=std::uint8_t(
                    std::min(100,int(site.equipment_condition)+10));
                ++site.repair_cycles;worked=true;
            }
            site.last_work_tick=tick_;
            if(worked) {
                ++site.autonomous_cycles;
                if(site.workforce_policy!=4 &&
                   site.autonomous_cycles%5==0 && site.equipment_condition>0)
                    --site.equipment_condition;
            } else {
                ++site.stalled_cycles;
            }
            continue;
        } // operational NPC workforce still consumes supplies

        // First four committed ticks represent the abstract NPC descent and
        // equipment emplacement; building then advances on actual world ticks.
        if(site.spaceport_phase==1) {
            if(++site.construction_progress>=4) {
                site.spaceport_phase=2;
                site.construction_progress=0;
            }
        } else if(site.spaceport_phase==2) {
            if(++site.construction_progress>=site.construction_required) {
                site.construction_progress=site.construction_required;
                site.spaceport_phase=3;
            }
        }
    }
}
bool Simulation::equip_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,bool farm) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto it=planetary_sites_.find(key);
    const auto site=scan_planetary_geography(owner,ship,body,latitude,longitude);
    const auto frame=ship_frames_.find(ship);
    auto colony=colony_ships_.find(ship);
    const auto source=farm?std::optional<planetary::PlanetaryRegionKey>{}:
        planetary_oasis_source_region(owner,ship,body);
    if(!site || !owner || it==planetary_sites_.end() ||
       // v113 worlds use on-site construction with immutable proof, not
       // the historical immediate orbit-module installation action.
       snapshot_version_>=113 ||
       // Once physical v113 production begins, do not bypass it with an
       // instant orbit-side install of the other building.
       it->second.production_projects[0].phase!=0 ||
       it->second.production_projects[1].phase!=0 ||
       it->second.owner!=owner || it->second.spaceport_phase!=3 || it->second.source_ship!=ship ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       colony==colony_ships_.end() || colony->second.modules<2 ||
       colony->second.crew<2 || (farm?it->second.farm:it->second.extractor) ||
       (farm && !site->landscape.region.farming_possible) ||
       (!farm && (!oasis_reserves_.count(body) || !source ||
                  source->latitude!=latitude || source->longitude!=longitude)))
        return false;
    colony->second.modules-=2;
    it->second.embodied_ore+=4;
    if(farm)it->second.farm=true;else it->second.extractor=true;
    return true;
}
bool Simulation::supply_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    bool water,std::int64_t kg) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto site=planetary_sites_.find(key);
    auto life=life_support_.find(ship);
    const auto frame=ship_frames_.find(ship);
    if(!owner || kg<=0 || kg>10000 || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       life==life_support_.end())return false;
    auto& origin=water?life->second.water_kg:life->second.nutrients_kg;
    auto& dest=water?site->second.water_kg:site->second.nutrients_kg;
    if(origin<kg || dest>1000000-kg)return false;
    origin-=kg;dest+=kg;
    return true;
}
bool Simulation::support_planetary_crew(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    bool oxygen,std::int64_t kg) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto site=planetary_sites_.find(key);
    auto life=life_support_.find(ship);
    const auto frame=ship_frames_.find(ship);
    const auto* hull=find(ship);
    if(!owner || kg<=0 || kg>10000 ||
       site==planetary_sites_.end() || site->second.owner!=owner ||
       site->second.source_ship!=ship ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       !hull || hull->owner_id!=owner || hull->dock_state!=DockState::InSpace ||
       hull->jump_state!=ShipJumpState::Ready ||
       life==life_support_.end())return false;
    auto& origin=oxygen?life->second.oxygen_kg:life->second.food_kg;
    auto& dest=oxygen?site->second.oxygen_kg:site->second.food_kg;
    if(origin<kg || dest>1000000-kg)return false;
    origin-=kg;dest+=kg;
    return true;
}
bool Simulation::cultivate_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    std::int64_t batches) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto site=planetary_sites_.find(key);
    const auto frame=ship_frames_.find(ship);
    if(!owner || batches<1 || batches>100 || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.spaceport_phase!=3 || site->second.source_ship!=ship ||
       !site->second.farm ||
       (site->second.assigned_workers==2 &&
        (site->second.food_kg<2 || site->second.water_kg<2 ||
         site->second.oxygen_kg<2)) ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body)
        return false;
    auto& facility=site->second;
    if(facility.water_kg<10*batches || facility.nutrients_kg<5*batches ||
       facility.food_kg>1000000-12*batches ||
       facility.wastewater_kg>1000000-3*batches)return false;
    facility.water_kg-=10*batches;
    facility.nutrients_kg-=5*batches;
    facility.food_kg+=12*batches;
    facility.wastewater_kg+=3*batches;
    facility.harvest_cycles+=std::uint64_t(batches);
    return true;
}
bool Simulation::extract_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    bool water,std::int64_t kg) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto site=planetary_sites_.find(key);
    auto reserve=oasis_reserves_.find(body);
    const auto frame=ship_frames_.find(ship);
    const auto source_region=planetary_oasis_source_region(owner,ship,body);
    if(!owner || kg<1000 || kg>10000 || kg%1000 ||
       site==planetary_sites_.end() || reserve==oasis_reserves_.end() ||
       site->second.owner!=owner || site->second.spaceport_phase!=3 || site->second.source_ship!=ship ||
       !site->second.extractor ||
       (site->second.assigned_workers==2 &&
        (site->second.food_kg<2 || site->second.water_kg<2 ||
         site->second.oxygen_kg<2)) ||
       !source_region || source_region->latitude!=latitude ||
       source_region->longitude!=longitude ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body)return false;
    auto& source=water?reserve->second.water_kg:reserve->second.nutrients_kg;
    auto& dest=water?site->second.water_kg:site->second.nutrients_kg;
    if(source<kg || dest>1000000-kg)return false;
    source-=kg;dest+=kg;
    return true;
}
bool Simulation::retrieve_planetary_site(PlayerId owner,EntityId ship,
    std::uint64_t body,std::uint16_t latitude,std::uint16_t longitude,
    bool food,std::int64_t kg) {
    const auto key=PlanetarySiteKey{body,latitude,longitude,1};
    auto site=planetary_sites_.find(key);
    auto life=life_support_.find(ship);
    const auto frame=ship_frames_.find(ship);
    if(!owner || kg<=0 || kg>10000 || site==planetary_sites_.end() ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       frame==ship_frames_.end() || frame->second.anchor_body!=body ||
       life==life_support_.end())return false;
    auto& origin=food?site->second.food_kg:site->second.water_kg;
    auto& dest=food?life->second.food_kg:life->second.water_kg;
    if(origin<kg || dest>1000000-kg)return false;
    origin-=kg;dest+=kg;
    return true;
}
std::optional<planetary::GeographicSite> Simulation::scan_planetary_geography(
    PlayerId owner,EntityId ship,std::uint64_t body_id,
    std::uint16_t latitude,std::uint16_t longitude) const {
    // Reuse native ship ownership + fitted scanner authority, never a standalone
    // client-generated body. Only physical planets and moons have surfaces.
    const auto body=celestial_navigation_anchor(owner,ship,body_id);
    if(!body || (body->kind!=CelestialKind::Planet &&
                 body->kind!=CelestialKind::Moon) ||
       latitude>=planetary::kWatershedRows ||
       longitude>=planetary::kWatershedColumns) return std::nullopt;
    const auto env=planetary_envelope_for_body(*body);
    const planetary::PlanetaryWorldKey key{
        kAstronomicalGalaxySeed,body->system,body->id,1};
    const auto terrain=planetary::generate_watersheds(key,env);
    return planetary::survey_geographic_site(
        terrain,key,env,latitude,longitude);
}
std::optional<PhysicalCelestialBody> Simulation::celestial_navigation_anchor(
    PlayerId owner,EntityId ship,std::uint64_t body_id) const {
    if(!body_id)return std::nullopt;
    const auto bodies=scan_physical_celestials(owner,ship);
    const auto it=std::find_if(bodies.begin(),bodies.end(),
        [body_id](const PhysicalCelestialBody& b){return b.id==body_id;});
    return it==bodies.end()?std::nullopt:std::optional<PhysicalCelestialBody>(*it);
}
std::optional<AstronomicalProfile> Simulation::scan_celestial_profile(
    PlayerId owner,EntityId ship) const {
    const auto it=entities_.find(ship);
    if(!owner||it==entities_.end()||it->second.type!=EntityType::Ship||
       it->second.owner_id!=owner||!it->second.fitting.exploration_scanner() ||
       !hardpoint_effective_on(ship,it->second.fitting.hardpoints[0]==1?0:1))
        return std::nullopt;
    // Shared versioned single-galaxy seed, never the mutable resource RNG.
    return generate_astronomical_profile(kAstronomicalGalaxySeed,it->second.system_id);
}
std::optional<Simulation::EnvironmentSurvey> Simulation::scan_environment(
    PlayerId owner,EntityId ship) const {
    const auto it=entities_.find(ship);
    if(!owner||it==entities_.end()||it->second.type!=EntityType::Ship||
       it->second.owner_id!=owner||!it->second.fitting.exploration_scanner() ||
       !hardpoint_effective_on(ship,it->second.fitting.hardpoints[0]==1?0:1))
        return std::nullopt;
    const auto profile=scan_celestial_profile(owner,ship);
    if(!profile)return std::nullopt;
    const auto has=[&](AstronomicalFeature feature){
        return std::find(profile->features.begin(),profile->features.end(),feature)!=
               profile->features.end();
    };
    const auto& v=it->second.velocity;
    const auto speed=static_cast<std::int64_t>(std::llround(
        std::hypot(double(v.x),double(v.y))));
    return EnvironmentSurvey{it->second.system_id,
        has(AstronomicalFeature::MagnetizedEnvironment),
        has(AstronomicalFeature::DustArc),speed};
}
std::vector<Simulation::SignatureHint> Simulation::coarse_probe_signatures(
    PlayerId owner,EntityId ship,std::int64_t range_mm) const {
    if(range_mm<5000000||range_mm>500000000)
        throw std::invalid_argument("invalid signature survey radius");
    const auto carrier=entities_.find(ship);
    if(!owner||carrier==entities_.end()||carrier->second.type!=EntityType::Ship||
       carrier->second.owner_id!=owner||
       !carrier->second.fitting.exploration_scanner() ||
       !hardpoint_effective_on(ship,carrier->second.fitting.hardpoints[0]==1?0:1))
        return {};
    constexpr std::int64_t q=5000000; // 5 km coarse bins; no secret exact coordinates
    const double radius=double(range_mm); // host-enforced discovery envelope
    std::vector<SignatureHint> result;
    for(const auto& [id,e]:entities_) {
        if(e.type!=EntityType::Asteroid||e.system_id!=carrier->second.system_id||
           !same_local_frame(ship,id)||discoveries_.count({owner,id}))continue;
        if(std::hypot(double(e.position.x)-carrier->second.position.x,
                      double(e.position.y)-carrier->second.position.y)>radius)continue;
        const auto quant=[](std::int64_t value){
            return (value>=0 ? (value+q/2)/q : -((-value+q/2)/q))*q;
        };
        result.push_back({id,{quant(e.position.x),quant(e.position.y)},5000000});
        if(result.size()>=128)break;
    }
    return result;
}
void Simulation::configure_probe_report_tick_ms(std::uint64_t tick_ms) {
    if(tick_ms<20||tick_ms>5000)
        throw std::invalid_argument("invalid probe timing period");
    probe_report_delay_ticks_=(10000ULL+tick_ms-1)/tick_ms;
    snapshot_version_=std::max(snapshot_version_,68);
}
bool Simulation::hardpoint_effective_on(EntityId ship,std::uint8_t slot) const {
    const auto* vessel=find(ship);
    if(!vessel || vessel->type!=EntityType::Ship ||
       slot>=2 || !vessel->fitting.hardpoints[slot])return false;
    std::array<bool,2> exposed{{false,false}};
    for(const auto& e:ship_status_.records()){
        if(e.target_ship!=ship || tick_<e.started_tick ||
           tick_>=e.expires_tick)continue;
        for(const auto& k:e.targets)
            if(k.rack==0 && k.slot<2 &&
               vessel->fitting.hardpoints[k.slot]==k.hardware)
                exposed[k.slot]=true;
    }
    const auto modules=ship_status::fitted_controls(*vessel,
        vessel->hardpoint_requested_on,exposed,vessel->hardpoint_isolated);
    for(const auto& m:ship_status_.project(ship,tick_,modules))
        if(m.key.rack==0 && m.key.slot==slot)return m.effective_on;
    return false;
}

bool Simulation::install_testing_confusion(EntityId source,EntityId target,
    std::uint64_t effect_id,std::uint64_t random_seed,
    std::uint64_t duration_ticks,std::uint32_t pulse_ticks,
    std::uint8_t intensity,std::uint8_t resistance,
    std::uint8_t exposed_hardpoint_mask) {
    // Not a player action or evidence of real hacking equipment.
    const auto* a=find(source),*b=find(target);
    if(!a || !b || a->type!=EntityType::Ship ||
       b->type!=EntityType::Ship || !a->owner_id || !b->owner_id ||
       a->owner_id==b->owner_id || source==target ||
       a->system_id!=b->system_id || !same_local_frame(source,target) ||
       std::hypot(double(a->position.x)-b->position.x,
                  double(a->position.y)-b->position.y)>
                  double(kTrainingWeaponRangeMm) ||
       !duration_ticks || duration_ticks>ship_status::Engine::kMaxDurationTicks ||
       tick_>std::numeric_limits<std::uint64_t>::max()-duration_ticks ||
       !exposed_hardpoint_mask || (exposed_hardpoint_mask&~std::uint8_t{3}) ||
       submitted_ids_.count(effect_id))
        return false;
    const std::array<bool,2> exposed{{
        bool(exposed_hardpoint_mask&1),bool(exposed_hardpoint_mask&2)}};
    ship_status::Effect attack{};
    attack.id=effect_id;attack.source_ship=source;attack.target_ship=target;
    attack.started_tick=tick_;attack.expires_tick=tick_+duration_ticks;
    attack.seed=random_seed;attack.pulse_ticks=pulse_ticks;
    attack.intensity=intensity;attack.resistance=resistance;
    const auto roster=ship_status::fitted_controls(
        *b,b->hardpoint_requested_on,exposed,{{false,false}});
    if(!ship_status_.inflict_confusion(attack,roster,tick_))return false;
    snapshot_version_=std::max(snapshot_version_,100);
    return true;
}

void Simulation::step() {
    if (tick_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("tick counter exhausted");
    ++tick_;
    // WO2-I: end-exclusive releases precede this tick's ordered commands.
    // Another preapproved shift can start at the exact former end without
    // double-booking, a phantom interim worker or an extra production tick.
    for(auto& [id,shop]:workshops_){
        (void)id;
        for(unsigned slot=0;slot<2;++slot){
            auto& sh=shop.shifts[slot];
            if(!sh.command_id || tick_<sh.end_tick)continue;
            auto& assigned=slot==0?shop.operator_npc_id:shop.finisher_npc_id;
            if(assigned==sh.worker)assigned=0;
            sh={};
        }
    }
    // A newly authorized transition begins on the command tick; completion is
    // automatic after two further committed ticks, without another command.
    process_station_transitions();
    process_jump_transitions();
    auto it = commands_.find(tick_);
    if (it != commands_.end()) {
        auto& commands = it->second;
        std::sort(commands.begin(), commands.end(), [](const Command& a, const Command& b) {
            if (a.sequence != b.sequence) return a.sequence < b.sequence;
            return a.id < b.id;
        });
        for (const auto& c : commands) events_.push_back(execute(c));
        commands_.erase(it);
    }
    // PLAY-13B v69: native autonomous lock-on missions. Fixed bounded motion
    // occurs in the committed simulation, not browser interpolation. Probe
    // destination is coarse; only a real sensor-distance check identifies it.
    if(snapshot_version_>=69)for(auto& [id,probe]:survey_probes_) {
        if(!probe.focus_site||probe.drone_phase)continue;
        auto& p=probe.position;
        const auto waypoint=probe.focus_waypoint;
        constexpr std::int64_t step_mm=5000000; // 5 km per simulation tick
        const auto dx=waypoint.x-p.x,dy=waypoint.y-p.y;
        const double remaining=std::hypot(double(dx),double(dy));
        if(remaining<=double(step_mm))p=waypoint;
        else {
            // True radial step limit; diagonals cannot exceed speed by sqrt(2).
            p.x+=static_cast<std::int64_t>(double(dx)*step_mm/remaining);
            p.y+=static_cast<std::int64_t>(double(dy)*step_mm/remaining);
        }
        if(p.x!=waypoint.x||p.y!=waypoint.y)continue;
        const auto target=entities_.find(probe.focus_site);
        if(target!=entities_.end()&&target->second.type==EntityType::Asteroid&&
           target->second.system_id==probe.system) {
            const auto distance=std::hypot(double(target->second.position.x)-p.x,
                                           double(target->second.position.y)-p.y);
            // Only once the probe physically reaches the coarse area can
            // local instruments refine that region to a new 25 m waypoint.
            // No accurate location is revealed to the player beforehand.
            if(distance>double(kProbeScanRangeMm) && distance<=5000000.0){
                constexpr std::int64_t fine=25000;
                const auto refine=[](std::int64_t value) {
                    return (value>=0 ? (value+fine/2)/fine :
                        -((-value+fine/2)/fine))*fine;
                };
                probe.focus_waypoint={refine(target->second.position.x),
                                      refine(target->second.position.y)};
                continue;
            }
            const auto key=std::make_pair(probe.owner,probe.focus_site);
            if(distance<=double(kProbeScanRangeMm)&&
               !discoveries_.count(key)&&!pending_probe_discoveries_.count(key)&&
               pending_probe_discoveries_.size()+discoveries_.size()<10000 &&
               probe_report_delay_ticks_<=std::numeric_limits<std::uint64_t>::max()-tick_) {
                DiscoveryRecord report{probe.owner,probe.focus_site,probe.system,
                    target->second.position,tick_,geology_.count(probe.focus_site)!=0};
                pending_probe_discoveries_.emplace(key,
                    PendingProbeDiscovery{report,probe.carrier,id,
                        tick_+probe_report_delay_ticks_});
            }
        }
        // Completion clears the mission even if its subject has disappeared.
        // A missing finding must not be hallucinated as a successful discovery.
        probe.focus_site=0;probe.focus_waypoint={};
    }
    // PLAY-13: deterministic host-owned drone flight, not browser interpolation.
    if(snapshot_version_>=66)for(auto& [id,p]:survey_probes_){
        (void)id;
        if(p.drone_phase!=1 && p.drone_phase!=2)continue;
        const auto& destination=p.drone_phase==1?
            (entities_.count(p.drone_target)?entities_.at(p.drone_target).position:p.drone_home):
            p.drone_home;
        const auto dx=destination.x-p.position.x,dy=destination.y-p.position.y;
        p.position.x+=std::clamp(dx,std::int64_t{-2000},std::int64_t{2000});
        p.position.y+=std::clamp(dy,std::int64_t{-2000},std::int64_t{2000});
        if(p.position.x!=destination.x || p.position.y!=destination.y)continue;
        if(p.drone_phase==2){p.drone_phase=3;continue;}
        const auto it=entities_.find(p.drone_target);
        if(it!=entities_.end() && it->second.type==EntityType::Asteroid &&
           it->second.system_id==p.system && geology_.count(p.drone_target)){
            // Non-destructive geological characterization. No simulated ore
            // extraction, mass transfer, or cargo creation is authorized.
            p.sample_ore=0;
            p.taxonomy=1+static_cast<std::uint8_t>(geology_.at(p.drone_target).taxonomy);
            // Store the native observation on the physical drone until the
            // carrier returns to retrieve the report. No remote revelation.
        }
        p.drone_phase=2;
    }
    // Autonomous survey reports transmit only after a drone returns and its
    // owning ship is in real 10 km radio range. Equipment recovery is separate.
    if(snapshot_version_>=67)for(const auto& [id,p]:survey_probes_){
        (void)id;
        if(p.drone_phase!=3 || !p.taxonomy)continue;
        const auto ship=entities_.find(p.carrier);
        if(ship==entities_.end() || ship->second.type!=EntityType::Ship ||
           ship->second.owner_id!=p.owner || ship->second.system_id!=p.system)
            continue;
        const auto dx=p.position.x-ship->second.position.x;
        const auto dy=p.position.y-ship->second.position.y;
        constexpr auto range=kDroneControlRangeMm;
        if(dx<-range || dx>range || dy<-range || dy>range ||
           dx*dx+dy*dy>range*range)continue;
        const auto key=std::make_pair(p.owner,p.drone_target);
        if(auto d=discoveries_.find(key);d!=discoveries_.end()){
            d->second.survey_taxonomy=p.taxonomy;
        }else if(discoveries_.size()<10000){
            const auto rock=entities_.find(p.drone_target);
            const auto position=rock!=entities_.end()?rock->second.position:p.position;
            discoveries_.emplace(key,DiscoveryRecord{p.owner,p.drone_target,
                p.system,position,tick_,true,p.taxonomy});
        }
    }
    // v68: release verified discoveries after the ten-second nominal delay.
    // A disconnected physical probe retains its pending results. Recovery of
    // that probe into its owning ship does not destroy data custody.
    if(snapshot_version_>=68)for(auto it=pending_probe_discoveries_.begin();
       it!=pending_probe_discoveries_.end();) {
        const auto& pending=it->second;
        if(tick_<pending.ready_tick){++it;continue;}
        const auto carrier=entities_.find(pending.carrier);
        if(carrier==entities_.end()||carrier->second.type!=EntityType::Ship||
           carrier->second.owner_id!=pending.report.owner||
           carrier->second.system_id!=pending.report.system) {
            ++it;continue;
        }
        const auto probe=survey_probes_.find(pending.probe_id);
        if(probe!=survey_probes_.end()) {
            if(probe->second.owner!=pending.report.owner||
               probe->second.carrier!=pending.carrier||
               probe->second.system!=pending.report.system) {
                ++it;continue;
            }
            const auto dx=probe->second.position.x-carrier->second.position.x;
            const auto dy=probe->second.position.y-carrier->second.position.y;
            if(!within_deep_probe_link(dx,dy)){
                ++it;continue;
            }
        }
        if(!discoveries_.count(it->first)) {
            // A full journal cannot cause a verified, undelivered report to
            // disappear. Keep it durable and retry on a later committed tick.
            if(discoveries_.size()>=10000){++it;continue;}
            discoveries_.emplace(it->first,pending.report);
        }
        it=pending_probe_discoveries_.erase(it);
    }
    process_gate_maintenance();
    process_utilities();
    process_storage_links();
    process_workshops();
    process_waterworks();
    process_colony_needs();
    process_oasis_extractors();
    process_planetary_shuttle_flights();
    process_planetary_surface_operations();
    if(snapshot_version_>=88)process_local_delivery_board();
    if(snapshot_version_>=90)process_shipyard_construction();
    if(snapshot_version_>=91)process_freighter_builds();
    if(snapshot_version_>=94)process_universal_work();
    if(snapshot_version_>=95)process_npc_flight_navigation();
    process_farms();
    process_habitats();
    process_communities();
    process_clinics();
    // Scheduled recovery runs in the same authoritative, journaled step.
    if(snapshot_version_>=39)for(auto& [id,care]:medical_cases_){
        // Death is terminal; an automatic timer must never heal a tombstone.
        if(npcs_.count(id) && care.recovery_due_tick &&
           tick_>=care.recovery_due_tick && !care.recovered_tick)
            care.recovered_tick=tick_;
    }
    process_reclaimers();
    for (auto& pair : entities_) {
        Entity& entity = pair.second;
        if (entity.type != EntityType::Ship || entity.dock_state != DockState::InSpace || entity.jump_state != ShipJumpState::Ready) continue;
        if(snapshot_version_>=119){
            // True native three-axis flight: the old +/-1000 km local
            // rectangle is no longer a collision or velocity wall.
            // Integral overflow is a checked representability failure,
            // NOT a gameplay border; higher galactic sectors are separate.
            auto advance_axis=[](std::int64_t p,std::int64_t v){
                if((v>0 && p>std::numeric_limits<std::int64_t>::max()-v) ||
                   (v<0 && p<std::numeric_limits<std::int64_t>::min()-v))
                    throw std::overflow_error("native 3D coordinate overflow");
                return p+v;
            };
            entity.position.x=advance_axis(entity.position.x,entity.velocity.x);
            entity.position.y=advance_axis(entity.position.y,entity.velocity.y);
            entity.position_z_mm=advance_axis(
                entity.position_z_mm,entity.velocity_z_mm_per_tick);
        }else{
            entity.position.x = std::clamp(entity.position.x + entity.velocity.x, -kWorldBoundMm, kWorldBoundMm);
            entity.position.y = std::clamp(entity.position.y + entity.velocity.y, -kWorldBoundMm, kWorldBoundMm);
            if (entity.position.x == kWorldBoundMm && entity.velocity.x > 0) entity.velocity.x = 0;
            if (entity.position.x == -kWorldBoundMm && entity.velocity.x < 0) entity.velocity.x = 0;
            if (entity.position.y == kWorldBoundMm && entity.velocity.y > 0) entity.velocity.y = 0;
            if (entity.position.y == -kWorldBoundMm && entity.velocity.y < 0) entity.velocity.y = 0;
        }
    }
    // New mining cycles use the moved position to check actual range per tick.
    process_automatic_mining();
    // Legacy v1 checkpoints can contain zero-ore rocks; clean them up on the next tick.
    for (auto i = entities_.begin(); i != entities_.end();) {
        if (i->second.type == EntityType::Asteroid && i->second.ore == 0) {
            const auto id = i->first;
            ++i;
            despawn(id, 0, 0);
        } else ++i;
    }
}

void Simulation::steps(std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) step();
}

std::string Simulation::canonical_state() const {
    std::ostringstream out;
    out << ( snapshot_version_ >= 119 ? "galaxy-kernel-v119|" : snapshot_version_ >= 118 ? "galaxy-kernel-v118|" : snapshot_version_ >= 117 ? "galaxy-kernel-v117|" : snapshot_version_ >= 116 ? "galaxy-kernel-v116|" : snapshot_version_ >= 115 ? "galaxy-kernel-v115|" : snapshot_version_ >= 114 ? "galaxy-kernel-v114|" : snapshot_version_ >= 113 ? "galaxy-kernel-v113|" : snapshot_version_ >= 112 ? "galaxy-kernel-v112|" : snapshot_version_ >= 111 ? "galaxy-kernel-v111|" : snapshot_version_ >= 110 ? "galaxy-kernel-v110|" : snapshot_version_ >= 109 ? "galaxy-kernel-v109|" : snapshot_version_ >= 108 ? "galaxy-kernel-v108|" : snapshot_version_ >= 107 ? "galaxy-kernel-v107|" : snapshot_version_ >= 106 ? "galaxy-kernel-v106|" : snapshot_version_ >= 105 ? "galaxy-kernel-v105|" : snapshot_version_ >= 104 ? "galaxy-kernel-v104|" : snapshot_version_ >= 103 ? "galaxy-kernel-v103|" : snapshot_version_ >= 102 ? "galaxy-kernel-v102|" : snapshot_version_ >= 101 ? "galaxy-kernel-v101|" : snapshot_version_ >= 100 ? "galaxy-kernel-v100|" : snapshot_version_ >= 99 ? "galaxy-kernel-v99|" : snapshot_version_ >= 98 ? "galaxy-kernel-v98|" : snapshot_version_ >= 97 ? "galaxy-kernel-v97|" : snapshot_version_ >= 96 ? "galaxy-kernel-v96|" : snapshot_version_ >= 95 ? "galaxy-kernel-v95|" : snapshot_version_ >= 94 ? "galaxy-kernel-v94|" : snapshot_version_ >= 93 ? "galaxy-kernel-v93|" : snapshot_version_ >= 92 ? "galaxy-kernel-v92|" : snapshot_version_ >= 91 ? "galaxy-kernel-v91|" : snapshot_version_ >= 90 ? "galaxy-kernel-v90|" : snapshot_version_ >= 89 ? "galaxy-kernel-v89|" : snapshot_version_ >= 88 ? "galaxy-kernel-v88|" : snapshot_version_ >= 87 ? "galaxy-kernel-v87|" : snapshot_version_ >= 86 ? "galaxy-kernel-v86|" : snapshot_version_ >= 85 ? "galaxy-kernel-v85|" : snapshot_version_ >= 84 ? "galaxy-kernel-v84|" : snapshot_version_ >= 83 ? "galaxy-kernel-v83|" : snapshot_version_ >= 82 ? "galaxy-kernel-v82|" : snapshot_version_ >= 81 ? "galaxy-kernel-v81|" : snapshot_version_ >= 80 ? "galaxy-kernel-v80|" : snapshot_version_ >= 79 ? "galaxy-kernel-v79|" : snapshot_version_ >= 78 ? "galaxy-kernel-v78|" : snapshot_version_ >= 77 ? "galaxy-kernel-v77|" : snapshot_version_ >= 76 ? "galaxy-kernel-v76|" : snapshot_version_ >= 75 ? "galaxy-kernel-v75|" : snapshot_version_ >= 74 ? "galaxy-kernel-v74|" : snapshot_version_ >= 73 ? "galaxy-kernel-v73|" : snapshot_version_ >= 72 ? "galaxy-kernel-v72|" : snapshot_version_ >= 71 ? "galaxy-kernel-v71|" : snapshot_version_ >= 70 ? "galaxy-kernel-v70|" : snapshot_version_ >= 69 ? "galaxy-kernel-v69|" : snapshot_version_ >= 68 ? "galaxy-kernel-v68|" : snapshot_version_ >= 67 ? "galaxy-kernel-v67|" : snapshot_version_ >= 66 ? "galaxy-kernel-v66|" : snapshot_version_ >= 65 ? "galaxy-kernel-v65|" : snapshot_version_ >= 64 ? "galaxy-kernel-v64|" : snapshot_version_ >= 63 ? "galaxy-kernel-v63|" : snapshot_version_ >= 62 ? "galaxy-kernel-v62|" : snapshot_version_ >= 61 ? "galaxy-kernel-v61|" : snapshot_version_ >= 60 ? "galaxy-kernel-v60|" : snapshot_version_ >= 59 ? "galaxy-kernel-v59|" : snapshot_version_ >= 58 ? "galaxy-kernel-v58|" : snapshot_version_ >= 57 ? "galaxy-kernel-v57|" : snapshot_version_ >= 53 ? "galaxy-kernel-v53|" : snapshot_version_ >= 52 ? "galaxy-kernel-v52|" : snapshot_version_ >= 49 ? "galaxy-kernel-v49|" : snapshot_version_ >= 48 ? "galaxy-kernel-v48|" : snapshot_version_ >= 44 ? "galaxy-kernel-v44|" : snapshot_version_ >= 41 ? "galaxy-kernel-v41|" : snapshot_version_ >= 40 ? "galaxy-kernel-v40|" : snapshot_version_ >= 39 ? "galaxy-kernel-v39|" : snapshot_version_ >= 38 ? "galaxy-kernel-v38|" : snapshot_version_ >= 37 ? "galaxy-kernel-v37|" : snapshot_version_ >= 36 ? "galaxy-kernel-v36|" : snapshot_version_ >= 35 ? "galaxy-kernel-v35|" : snapshot_version_ >= 34 ? "galaxy-kernel-v34|" : snapshot_version_ >= 33 ? "galaxy-kernel-v33|" : snapshot_version_ >= 32 ? "galaxy-kernel-v32|" : snapshot_version_ >= 31 ? "galaxy-kernel-v31|" : snapshot_version_ >= 30 ? "galaxy-kernel-v30|" : snapshot_version_ >= 29 ? "galaxy-kernel-v29|" : snapshot_version_ >= 28 ? "galaxy-kernel-v28|" : snapshot_version_ >= 27 ? "galaxy-kernel-v27|" : snapshot_version_ >= 26 ? "galaxy-kernel-v26|" : snapshot_version_ >= 25 ? "galaxy-kernel-v25|" : snapshot_version_ >= 24 ? "galaxy-kernel-v24|" : snapshot_version_ >= 23 ? "galaxy-kernel-v23|" : snapshot_version_ >= 22 ? "galaxy-kernel-v22|" : snapshot_version_ >= 21 ? "galaxy-kernel-v21|" : snapshot_version_ >= 20 ? "galaxy-kernel-v20|" : snapshot_version_ >= 19 ? "galaxy-kernel-v19|" : snapshot_version_ >= 18 ? "galaxy-kernel-v18|" : snapshot_version_ >= 17 ? "galaxy-kernel-v17|" : snapshot_version_ >= 16 ? "galaxy-kernel-v16|" : snapshot_version_ >= 15 ? "galaxy-kernel-v15|" : snapshot_version_ >= 14 ? "galaxy-kernel-v14|" : snapshot_version_ >= 13 ? "galaxy-kernel-v13|" : snapshot_version_ >= 12 ? "galaxy-kernel-v12|" : snapshot_version_ >= 11 ? "galaxy-kernel-v11|" : snapshot_version_ >= 10 ? "galaxy-kernel-v10|" : snapshot_version_ >= 9 ? "galaxy-kernel-v9|" : snapshot_version_ >= 8 ? "galaxy-kernel-v8|" : snapshot_version_ >= 7 ? "galaxy-kernel-v7|" : snapshot_version_ >= 6 ? "galaxy-kernel-v6|" : snapshot_version_ >= 5 ? "galaxy-kernel-v5|" : snapshot_version_ >= 4 ? "galaxy-kernel-v4|" : snapshot_version_ == 3 ? "galaxy-kernel-v3|" : snapshot_version_ == 2 ? "galaxy-kernel-v2|" : "galaxy-kernel-v1|") << system_id_ << '|' << tick_ << '|' << next_entity_id_
        << '|' << rng_state_ << '\n';
    if (snapshot_version_>=5) for(const auto system:systems_)out<<"system|"<<system<<'\n';
    if(snapshot_version_>=68)out<<"probelatency|"<<probe_report_delay_ticks_<<'\n';
    if(snapshot_version_>=70)for(const auto& [system,bodies]:physical_celestial_systems_)
        for(const auto& body:bodies)
            out<<"celestial|"<<system<<'|'<<body.id<<'|'<<unsigned(body.kind)<<'|'
               <<body.parent_id<<'|'<<body.orbital_radius_milli_au<<'|'
               <<body.position.x_mm<<'|'<<body.position.y_mm<<'|'
               <<body.position.z_mm<<'|'<<body.physical_radius_mm<<'\n';
    if(snapshot_version_>=71)for(const auto& [ship,frame]:ship_frames_)
        out<<"shipframe|"<<ship<<'|'<<frame.system<<'|'<<frame.anchor_body<<'|'
           <<frame.origin.x_mm<<'|'<<frame.origin.y_mm<<'|'
           <<frame.origin.z_mm<<'\n';
    if(snapshot_version_>=77)
        for(const auto& [key,site]:planetary_sites_) {
            (void)key;
            out<<"planetsite|"<<site.region.body<<'|'<<site.region.latitude<<'|'<<
                site.region.longitude<<'|'<<site.region.revision<<'|'<<
                site.owner<<'|'<<site.source_ship<<'|'<<site.embodied_ore<<'|'<<
                unsigned(site.farm)<<'|'<<unsigned(site.extractor)<<'|'<<
                site.water_kg<<'|'<<site.nutrients_kg<<'|'<<site.food_kg<<'|'<<
                site.wastewater_kg<<'|'<<site.harvest_cycles;
            if(snapshot_version_>=78)
                out<<'|'<<unsigned(site.spaceport_phase)<<'|'<<
                    site.construction_progress<<'|'<<site.construction_required<<'|'<<
                    site.surface_location<<'|'<<site.delegated_npcs[0]<<'|'<<
                    site.delegated_npcs[1]<<'|'<<site.assigned_workers<<'|'<<
                    site.oxygen_kg<<'|'<<site.co2_kg<<'|'<<site.organic_waste_kg<<'|'<<
                    site.worker_support_cycles;
            if(snapshot_version_>=79)
                out<<'|'<<unsigned(site.workforce_policy)<<'|'<<
                    unsigned(site.equipment_condition)<<'|'<<
                    site.last_work_tick<<'|'<<site.autonomous_cycles<<'|'<<
                    site.stalled_cycles<<'|'<<site.auto_extracted_kg<<'|'<<
                    site.repair_cycles;
            if(snapshot_version_>=109)
                out<<'|'<<unsigned(site.housing_modules)<<'|'<<
                    unsigned(site.housing_phase)<<'|'<<site.housing_progress<<'|'<<
                    site.housing_order_id<<'|'<<site.housing_stalled_ticks<<'|'<<
                    site.housing_completed_tick<<'|'<<
                    unsigned(site.housing_paused)<<'|'<<
                    site.housing_supported_cycles<<'|'<<
                    site.housing_shortages<<'|'<<site.founder_residents[0]<<'|'<<
                    site.founder_consent_requests[0]<<'|'<<
                    site.founder_residents[1]<<'|'<<
                    site.founder_consent_requests[1];
            if(snapshot_version_>=110)
                out<<'|'<<unsigned(site.utility_phase)<<'|'<<
                    site.utility_progress<<'|'<<site.utility_order_id<<'|'<<
                    site.utility_supervisor<<'|'<<site.utility_stalled_ticks<<'|'<<
                    site.utility_completed_tick<<'|'<<unsigned(site.utility_paused)<<'|'<<
                    site.stored_electricity<<'|'<<site.brine_kg<<'|'<<
                    site.utility_powered_cycles<<'|'<<site.hvac_cycles<<'|'<<
                    site.utility_outages<<'|'<<site.water_recycled_kg<<'|'<<
                    site.oxygen_recycled_kg;
            out<<'\n';
        }
    if(snapshot_version_>=115 && legacy_planetary_residence_import_) {
        const auto& m=*legacy_planetary_residence_import_;
        out<<"planetlegacyimport|"<<m.source_version<<'|'<<m.source_hash<<'|'<<
            m.host_request_id<<'|'<<m.recorded_tick<<'|'<<
            m.legal_addresses.size();
        for(const auto& [person,address]:m.legal_addresses)
            out<<'|'<<person<<'|'<<address;
        out<<'\n';
    }
    if(snapshot_version_>=113)
        for(const auto& [key,site]:planetary_sites_)
            for(std::size_t i=0;i<site.production_projects.size();++i) {
                const auto& p=site.production_projects[i];
                if(!p.phase)continue;
                out<<"planetproduction|"<<std::get<0>(key)<<'|'<<
                    std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                    std::get<3>(key)<<'|'<<i<<'|'<<unsigned(p.phase)<<'|'<<
                    p.progress<<'|'<<p.order_id<<'|'<<p.supervisor<<'|'<<
                    p.stalled_ticks<<'|'<<p.completed_tick<<'|'<<
                    unsigned(p.paused)<<'\n';
            }
    if(snapshot_version_>=117)
        for(const auto& [key,site]:planetary_sites_)
            for(std::size_t i=0;i<site.service_projects.size();++i) {
                const auto& p=site.service_projects[i];
                if(!p.phase)continue;
                out<<"planetservice|"<<std::get<0>(key)<<'|'<<
                    std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                    std::get<3>(key)<<'|'<<i<<'|'<<unsigned(p.phase)<<'|'<<
                    p.progress<<'|'<<p.order_id<<'|'<<p.supervisor<<'|'<<
                    p.stalled_ticks<<'|'<<p.completed_tick<<'|'<<
                    unsigned(p.paused)<<'|'<<p.powered_cycles<<'\n';
            }
    if(snapshot_version_>=117)
        for(const auto& [key,site]:planetary_sites_)
            for(std::size_t i=0;i<site.service_handovers.size();++i)
                for(const auto& h:site.service_handovers[i])
                    out<<"planetservicehandover|"<<std::get<0>(key)<<'|'<<
                        std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                        std::get<3>(key)<<'|'<<i<<'|'<<h.request_id<<'|'<<
                        h.tick<<'|'<<h.previous<<'|'<<h.replacement<<'\n';
    if(snapshot_version_>=114)
        for(const auto& [key,site]:planetary_sites_)
            for(std::size_t i=0;i<site.production_handovers.size();++i)
                for(const auto& h:site.production_handovers[i])
                    out<<"planetproductionhandover|"<<std::get<0>(key)<<'|'<<
                        std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                        std::get<3>(key)<<'|'<<i<<'|'<<h.request_id<<'|'<<
                        h.tick<<'|'<<h.previous<<'|'<<h.replacement<<'\n';
    if(snapshot_version_>=112)
        for(const auto& [key,site]:planetary_sites_)
            for(const auto& h:site.utility_handovers)
                out<<"planetutilityhandover|"<<std::get<0>(key)<<'|'<<
                    std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                    std::get<3>(key)<<'|'<<h.request_id<<'|'<<h.tick<<'|'<<
                    h.previous<<'|'<<h.replacement<<'\n';
    // v118 immutable original-household admission and reverse departure.
    if(snapshot_version_>=118)
        for(const auto& [key,site]:planetary_sites_)
            for(const auto& [guardian,house]:site.households) {
                out<<"planethousehold|"<<std::get<0>(key)<<'|'<<
                    std::get<1>(key)<<'|'<<std::get<2>(key)<<'|'<<
                    std::get<3>(key)<<'|'<<guardian<<'|'<<house.co_parent_id
                   <<'|'<<house.booking_request<<'|'<<house.arrival_receipt
                   <<'|'<<house.admission_request<<'|'<<house.admission_tick
                   <<'|'<<house.return_request<<'|'<<house.return_tick
                   <<'|'<<unsigned(house.consent_mask)<<'|'<<house.child_ids.size();
                for(const auto child:house.child_ids)out<<'|'<<child;
                out<<'\n';
            }
    if(snapshot_version_>=111)
        for(const auto& [key,site]:planetary_sites_)
            for(const auto& [npc,proof]:site.visitor_residency)
                out<<"planetsettler|"<<std::get<0>(key)<<'|'<<std::get<1>(key)<<'|'<<
                   std::get<2>(key)<<'|'<<std::get<3>(key)<<'|'<<npc<<'|'<<
                   proof[0]<<'|'<<proof[1]<<'|'<<proof[2]<<'\n';
    if(snapshot_version_>=80)
        for(const auto& [key,v]:planetary_shuttles_) {
            out<<"planetshuttle|"<<std::get<0>(key)<<'|'<<std::get<1>(key)<<'|'<<
                std::get<2>(key)<<'|'<<std::get<3>(key)<<'|'<<v.owner<<'|'<<
                v.source_ship<<'|'<<v.pilot_npc<<'|'<<v.dry_mass_ore<<'|'<<
                v.water_kg<<'|'<<v.food_kg<<
                (snapshot_version_>=81?
                    ("|"+std::to_string(unsigned(v.flight_phase))+"|"+
                     std::to_string(v.flight_request)+"|"+
                     std::to_string(v.arrival_tick)+"|"+
                     std::to_string(v.descent_receipt)+"|"+
                     std::to_string(v.ascent_receipt)):"");
            if(snapshot_version_>=82) {
                out<<'|'<<v.passenger_ids.size();
                for(const auto id:v.passenger_ids)out<<'|'<<id;
            }
            if(snapshot_version_>=83) {
                out<<'|'<<v.disembarked_ids.size();
                for(const auto id:v.disembarked_ids)out<<'|'<<id;
            }
            if(snapshot_version_>=84) {
                out<<'|'<<v.duty_ids.size();
                for(const auto id:v.duty_ids)out<<'|'<<id;
            }
            if(snapshot_version_>=85) {
                out<<'|'<<v.visitor_arrival_receipts.size();
                for(const auto& [npc,receipt]:v.visitor_arrival_receipts)
                    out<<'|'<<npc<<'|'<<receipt;
            }
            if(snapshot_version_>=87)out<<'|'<<v.nutrients_kg;
            if(snapshot_version_>=109)
                out<<'|'<<v.oxygen_kg<<'|'<<unsigned(v.housing_module);
            out<<'\n';
        }
    // v116 named original child custody, never a separate NPC population.
    if(snapshot_version_>=116)
        for(const auto& [key,sh]:planetary_shuttles_) {
            const auto& family=sh.family;
            if(!family.stage)continue;
            out<<"planetfamily|"<<std::get<0>(key)<<'|'<<std::get<1>(key)
               <<'|'<<std::get<2>(key)<<'|'<<std::get<3>(key)
               <<'|'<<family.guardian_id<<'|'<<family.co_parent_id
               <<'|'<<family.booking_request<<'|'<<family.outbound_receipt
               <<'|'<<family.return_receipt<<'|'<<unsigned(family.stage)
               <<'|'<<family.child_ids.size();
            for(const auto id:family.child_ids)out<<'|'<<id;
            out<<'\n';
        }
    if(snapshot_version_>=88)
        for(const auto& [id,work]:local_delivery_orders_) {
            out<<"localdelivery|"<<id<<'|'<<work.owner<<'|'<<work.board_ship<<'|'<<
                std::get<0>(work.site)<<'|'<<std::get<1>(work.site)<<'|'<<
                std::get<2>(work.site)<<'|'<<std::get<3>(work.site)<<'|'<<
                unsigned(work.cargo)<<'|'<<work.kilograms<<'|'<<
                work.assigned_pilot<<'|'<<unsigned(work.stage)<<'|'<<
                work.created_tick<<'\n';
        }
    if(snapshot_version_>=89)
        for(const auto& [id,leg]:intercolony_freight_)
            out<<"interfreight|"<<id<<'|'<<leg.owner<<'|'<<leg.source_ship
               <<'|'<<leg.carrier_ship<<'|'<<leg.destination_ship
               <<'|'<<unsigned(leg.cargo)<<'|'<<leg.kilograms
               <<'|'<<leg.origin_body<<'|'<<leg.loaded_tick
               <<'|'<<leg.arrival_receipt<<'|'<<unsigned(leg.stage)<<'\n';
    if(snapshot_version_>=90)
        for(const auto& [id,project]:shipyard_constructions_)
            out<<"shipyard|"<<id<<'|'<<project.owner<<'|'<<project.colony_ship
               <<'|'<<project.workers[0]<<'|'<<project.workers[1]
               <<'|'<<project.embodied_ore<<'|'<<project.progress
               <<'|'<<project.required<<'|'<<project.posted_tick
               <<'|'<<project.last_progress_tick<<'|'<<project.stalled_ticks<<'\n';
    if(snapshot_version_>=91) {
        for(const auto& [id,order]:freighter_build_orders_) {
            out<<"freighterbuild|"<<id<<'|'<<order.yard_order
               <<'|'<<order.owner<<'|'<<order.source_ship
               <<'|'<<order.launched_ship<<'|'<<order.embodied_ore
               <<'|'<<order.progress<<'|'<<order.required
               <<'|'<<order.posted_tick<<'|'<<order.last_progress_tick
               <<'|'<<order.launched_tick<<'|'<<order.system
               <<'|'<<order.anchor_body;
            if(snapshot_version_>=92)
                out<<'|'<<order.commissioned_tick<<'|'
                   <<order.commissioning_crew[0]<<'|'
                   <<order.commissioning_crew[1]<<'|'
                   <<order.provisions_kg_each;
            out<<'\n';
        }
    }
    if(snapshot_version_>=93)
        for(const auto& [id,order]:shipyard_customer_orders_) {
            out<<"yardcustomer|"<<id<<'|'<<order.yard_order
               <<'|'<<order.build_order<<'|'<<order.customer_owner
               <<'|'<<order.supplier_owner<<'|'<<order.customer_ship
               <<'|'<<order.supplier_ship<<'|'<<unsigned(order.stage)
               <<'|'<<order.posted_tick<<'|'<<order.accepted_tick
               <<'|'<<order.funded_tick<<'|'<<order.delivered_tick
               <<'|'<<order.delivered_ship<<'\n';
        }
    if(snapshot_version_>=94)
        for(const auto& [id,directive]:work_directives_) {
            out<<"workdirective|"<<id<<'|'<<directive.owner
               <<'|'<<directive.colony_ship
               <<'|'<<unsigned(directive.kind)
               <<'|'<<directive.source_order
               <<'|'<<directive.requires_directive
               <<'|'<<directive.commissioning_crew[0]
               <<'|'<<directive.commissioning_crew[1]
               <<'|'<<directive.provisions_kg_each
               <<'|'<<unsigned(directive.status)
               <<'|'<<unsigned(directive.blocked)
               <<'|'<<directive.posted_tick
               <<'|'<<directive.last_update_tick
               <<'|'<<directive.resolved_tick<<'\n';
        }
    if(snapshot_version_>=95)
        for(const auto& [id,f]:npc_flight_orders_){
            out<<"npcflight|"<<id<<'|'<<f.owner<<'|'<<f.ship
               <<'|'<<f.pilot_npc<<'|'<<f.destination_body
               <<'|'<<f.transit_request<<'|'<<unsigned(f.purpose)
               <<'|'<<unsigned(f.phase)<<'|'<<f.posted_tick
               <<'|'<<f.started_tick<<'|'<<f.resolved_tick;
            if(snapshot_version_>=96)
                out<<'|'<<unsigned(f.mode)<<'|'<<f.station
                   <<'|'<<f.local_commands<<'|'<<f.last_local_command;
            out<<'\n';
        }
    if(snapshot_version_>=98)
        for(const auto& [owner,p]:player_people_){
            out<<"playerperson|"<<owner<<'|'<<p.home_ship<<'|'
               <<p.physical_ship<<'|'<<unsigned(p.state)<<'|'
               <<p.custodian<<'|'<<p.registered_tick<<'|'
               <<p.last_receipt<<'|'<<p.last_update_tick;
            if(snapshot_version_>=99)
                out<<'|'<<p.consent_ship<<'|'<<p.consent_id
                   <<'|'<<p.rescue_flight;
            out<<'\n';
        }
    if(snapshot_version_>=81)
        for(const auto id:shuttle_flight_request_ids_)
            out<<"planetflightreceipt|"<<id<<'\n';
    if(snapshot_version_>=116)
        for(const auto id:planetary_family_booking_ids_)
            out<<"planetfamilyreceipt|"<<id<<'\n';
    if(snapshot_version_>=76)
        for(const auto& [ship,r]:oasis_extractors_)
            out<<"oasisextractor|"<<ship<<'|'<<r.body<<'|'<<r.embodied_ore<<'|'<<
                unsigned(r.assigned_workers)<<'|'<<unsigned(r.condition)<<'|'<<
                r.energy_units<<'|'<<r.cycles<<'\n';
    if(snapshot_version_>=75)
        for(const auto& [body,r]:oasis_reserves_)
            out<<"oasisreserve|"<<body<<'|'<<r.water_kg<<'|'<<r.nutrients_kg<<'|'<<
                r.initial_water_kg<<'|'<<r.initial_nutrients_kg<<'\n';
    if(snapshot_version_>=74) {
        for(const auto& [body,e]:local_environments_)
            out<<"localenvironment|"<<body<<'|'<<unsigned(e.zone)<<'|'<<
                unsigned(e.hazard)<<'|'<<unsigned(e.first)<<'|'<<unsigned(e.second)<<'\n';
    }
    if(snapshot_version_>=73) {
        for(const auto& [body,manifest]:generated_local_regions_)
            out<<"localregion|"<<body<<'|'<<manifest[0]<<'|'<<manifest[1]<<'\n';
        for(const auto& [entity,body]:local_entity_anchors_)
            out<<"localanchor|"<<entity<<'|'<<body<<'\n';
    }
    if(snapshot_version_>=72) {
        for(const auto& [id,t]:pending_celestial_transits_)
            out<<"frametransit_pending|"<<id<<'|'<<t.owner<<'|'<<t.ship<<'|'
               <<t.system<<'|'<<t.source_anchor<<'|'<<t.destination_body<<'|'
               <<t.started_tick<<'|'<<t.due_tick<<'\n';
        for(const auto& [id,t]:completed_celestial_transits_)
            out<<"frametransit_done|"<<id<<'|'<<t.owner<<'|'<<t.ship<<'|'
               <<t.system<<'|'<<t.destination_body<<'|'<<t.completed_tick<<'\n';
    }
    for (const auto& entry : entities_) {
        const auto& e = entry.second;
        out << "entity|" << e.id << '|' << e.system_id << '|' << static_cast<int>(e.type)
            << '|' << e.owner_id << '|' << e.position.x << '|' << e.position.y
            << '|' << e.velocity.x << '|' << e.velocity.y << '|' << e.ore
            << '|' << e.cargo << '|' << e.cargo_capacity;
        if (snapshot_version_ >= 3) out << '|' << static_cast<int>(e.dock_state)
            << '|' << e.dock_station << '|' << e.transition_ticks;
        if (snapshot_version_>=5) out << '|' << static_cast<int>(e.jump_state)
            << '|' << e.pending_gate << '|' << e.jump_ticks
            << '|' << e.gate_destination << '|' << e.gate_arrival.x << '|' << e.gate_arrival.y
            << '|' << (e.gate_online?1:0);
        if(snapshot_version_>=61) out << '|' << e.heading_deg;
        if(snapshot_version_>=62) out << '|' << e.hull << '|' << e.weapon_charges << '|' << e.last_weapon_tick << '|' << (e.training_drone?1:0);
        if(snapshot_version_>=64){
            for(auto item:e.fitting.hardpoints)out<<'|'<<unsigned(item);
            for(auto item:e.fitting.support)out<<'|'<<unsigned(item);
            for(auto item:e.fitting.subsystems)out<<'|'<<unsigned(item);
            for(auto item:e.fitting.rigging)out<<'|'<<unsigned(item);
        }
        if(snapshot_version_>=65)out<<'|'<<unsigned(e.survey_probes_carried);
        if(snapshot_version_>=91)out<<'|'<<e.manufactured_order_id;
        if(snapshot_version_>=100)
            for(const auto on:e.hardpoint_requested_on)out<<'|'<<(on?1:0);
        if(snapshot_version_>=101){
            for(const auto isolated:e.hardpoint_isolated)out<<'|'<<(isolated?1:0);
            out<<'|'<<e.last_hack_tick;
        }
        if(snapshot_version_>=119)out<<'|'<<e.position_z_mm<<'|'<<e.velocity_z_mm_per_tick;
        out << '\n';
    }
    if(snapshot_version_>=100)for(const auto& e:ship_status_.records()){
        out<<"shipstatus|"<<e.id<<'|'<<e.source_ship<<'|'<<e.target_ship
           <<'|'<<e.started_tick<<'|'<<e.expires_tick<<'|'<<e.seed
           <<'|'<<e.pulse_ticks<<'|'<<unsigned(e.intensity)
           <<'|'<<unsigned(e.resistance)<<'|'<<e.targets.size();
        for(const auto& key:e.targets)
            out<<'|'<<unsigned(key.rack)<<'|'<<unsigned(key.slot)
               <<'|'<<unsigned(key.hardware);
        out<<'\n';
    }
    if(snapshot_version_>=102)for(const auto& [id,o]:npc_cyber_operators_) {
        (void)id;
        out<<"npccyber|"<<o.command_id<<'|'<<o.owner<<'|'
           <<o.attacking_ship<<'|'<<o.pilot_npc<<'|'<<o.observer_npc
           <<'|'<<o.target_ship<<'|'<<o.admitted_tick<<'\n';
    }
    if (snapshot_version_ >= 6) {
        for(const auto& [pair,op]:gate_ops_)
            out << "gateops|" << pair.first << '|' << pair.second << '|'
                << op.condition << '|' << op.spare_parts << '|'
                << op.technicians << '|' << op.cycles << '|' << op.consumed_parts << '\n';
    }
    if (snapshot_version_ >= 7) {
        for(const auto& [id,c]:colony_ships_)
            out<<"colony|"<<id<<'|'<<c.crew<<'|'<<c.refined<<'|'<<c.modules<<'\n';
        for(const auto& [id,site]:built_outposts_)
            out<<"outpost|"<<id<<'|'<<site.source_colony_ship<<'|'<<site.embodied_ore<<'\n';
    }
    if(snapshot_version_>=22)for(const auto& [id,w]:workshops_){
        out<<"workshop|"<<id<<'|'<<w.source_ship<<'|'<<w.embodied_ore
           <<'|'<<w.aluminum_kg<<'|'<<w.panels_kg<<'|'<<w.scrap_kg
           <<'|'<<w.reserved_aluminum_kg<<'|'<<w.ticks_remaining
           <<'|'<<w.completed_batches<<'|'<<w.elapsed_ticks
           <<'|'<<w.operator_npc_id;
        if(snapshot_version_>=23)out<<'|'<<(w.finisher_installed?1:0)
            <<'|'<<w.finisher_npc_id<<'|'<<w.reserved_panels_kg
            <<'|'<<w.finisher_ticks_remaining<<'|'<<w.frames_kg
            <<'|'<<w.finisher_batches<<'|'<<w.finisher_elapsed_ticks;
        out<<'\n';
        if(snapshot_version_>=106){
            for(unsigned slot=0;slot<2;++slot){
                const auto& sh=w.shifts[slot];
                if(sh.command_id)
                    out<<"workshopshift|"<<id<<'|'<<(slot+1)<<'|'
                       <<sh.command_id<<'|'<<sh.worker<<'|'
                       <<sh.start_tick<<'|'<<sh.end_tick
                       <<(snapshot_version_>=107?"|"+std::to_string(sh.relief_command_id):"")
                       <<'\n';
            }
            for(const auto pending:w.cancelled_pending_shifts)
                out<<"workshopshiftcancel|"<<id<<'|'<<pending<<'\n';
        }
        if(snapshot_version_>=108){
            if(w.supervisor_npc_id || w.last_mentored_tick)
                out<<"workshopmanager|"<<id<<'|'<<w.supervisor_npc_id<<'|'
                   <<w.supervisor_appointment_id<<'|'
                   <<unsigned(w.supervisor_trade)<<'|'
                   <<w.last_mentored_tick<<'\n';
            for(unsigned slot=0;slot<2;++slot){
                const auto& job=w.approved_jobs[slot];
                if(job.command_id)
                    out<<"workshopjob|"<<id<<'|'<<(slot+1)<<'|'
                       <<job.command_id<<'|'<<job.supervisor_appointment_id
                       <<'|'<<job.supervisor_npc_id<<'|'<<job.approved_tick
                       <<'|'<<job.baseline_batches<<'|'<<job.target_batches<<'\n';
            }
        }
    }
    if(snapshot_version_>=25)for(const auto& [site,st]:storages_){
        out<<"storage|"<<site<<'|'<<st.source_ship<<'|'<<st.embodied_ore;
        static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
        for(const auto* name:names){const auto i=st.goods_kg.find(name);
            out<<'|'<<(i==st.goods_kg.end()?0:i->second);}
        if(snapshot_version_>=52)out<<'|'<<st.network_escrow_kg;
        out<<'\n';
        if(snapshot_version_>=26){
            for(const auto& [node,stacks]:st.extra_bins){
                out<<"storage_node|"<<site<<'|'<<node;
                for(const auto* name:names){const auto i=stacks.find(name);
                    out<<'|'<<(i==stacks.end()?0:i->second);}
                out<<'\n';
            }
        }
        if(snapshot_version_>=29){
            for(const auto& [node,pos]:st.equipment_positions)
                out<<"factory_placement|"<<site<<'|'<<node<<'|'<<pos.x<<'|'<<pos.y<<'\n';
        }
        if(snapshot_version_>=27){
            for(const auto& [id,link]:st.links){
                out<<"storage_link|"<<site<<'|'<<id<<'|'<<link.source<<'|'
                   <<link.destination<<'|'<<static_cast<unsigned>(link.item_index)
                   <<'|'<<link.units_per_tick<<'|'<<link.units_moved<<'\n';
                if(snapshot_version_>=30 && link.route_required){
                    out<<"factory_conveyor|"<<site<<'|'<<id<<'|'<<(link.route_ready?1:0)<<'|';
                    bool first=true;
                    for(const auto& cell:link.route_cells){
                        if(!first)out<<';';
                        first=false;
                        out<<cell.x<<':'<<cell.y;
                    }
                    out<<'\n';
                }
            }
        }
    }
    if (snapshot_version_>=8) {
        for(const auto& [id,g]:geology_)
            out<<"geology|"<<id<<'|'<<static_cast<int>(g.taxonomy)<<'|'<<g.radius_m
               <<'|'<<g.density_kg_m3<<'|'<<g.original_tonnes<<'\n';
    }
    if(snapshot_version_>=9)for(const auto& [id,c]:colony_ships_){
        for(const auto& [phase,kg]:c.minerals_kg)if(kg>0)out<<"mineral|"<<id<<'|'<<phase<<'|'<<kg<<'\n';
        for(const auto& [good,kg]:c.goods_kg)if(kg>0)out<<"good|"<<id<<'|'<<good<<'|'<<kg<<'\n';
        if(c.tailings_kg)out<<"tailings|"<<id<<'|'<<c.tailings_kg<<'\n';
    }
    if(snapshot_version_>=10)for(const auto& [id,l]:life_support_){
        out<<"lifesupport|"<<id<<'|'<<l.food_kg<<'|'<<l.water_kg<<'|'<<l.oxygen_kg
           <<'|'<<l.nutrients_kg<<'|'<<l.co2_kg<<'|'<<l.crops_kg
           <<'|'<<l.compost_kg<<'|'<<l.wastewater_kg<<'|'<<l.organic_waste_kg
           <<'|'<<l.sludge_kg<<'|'<<l.hydrogen_kg<<'|'<<l.crops_ready_tick
           <<'|'<<l.cycles<<'|'<<l.consecutive_supplied_cycles<<'|'<<l.shortages
           <<'|'<<l.harvested_food_kg<<'|'<<l.recovered_water_kg<<'\n';
    }
    if(snapshot_version_>=11)for(const auto& [id,f]:farms_){
        const auto& l=f.life;
        out<<"farm|"<<id<<'|'<<f.source_ship<<'|'<<f.embodied_ore<<'|'<<f.workers
            <<'|'<<f.production_cycles<<'|'<<f.pending_food_kg
            <<'|'<<f.recent_food_kg[0]<<'|'<<f.recent_food_kg[1]<<'|'<<f.recent_food_kg[2]
            <<'|'<<l.food_kg<<'|'<<l.water_kg<<'|'<<l.oxygen_kg<<'|'<<l.nutrients_kg
            <<'|'<<l.co2_kg<<'|'<<l.crops_kg<<'|'<<l.compost_kg<<'|'<<l.wastewater_kg
            <<'|'<<l.organic_waste_kg<<'|'<<l.sludge_kg<<'|'<<l.hydrogen_kg
            <<'|'<<l.crops_ready_tick<<'|'<<l.cycles<<'|'<<l.consecutive_supplied_cycles
            <<'|'<<l.shortages<<'|'<<l.harvested_food_kg<<'|'<<l.recovered_water_kg<<'\n';
    }
    if(snapshot_version_>=12)for(const auto& [id,h]:habitats_){
        const auto& l=h.life;
        out<<"habitat|"<<id<<'|'<<h.source_ship<<'|'<<h.embodied_ore<<'|'<<h.capacity
           <<'|'<<h.residents<<'|'<<h.cycles<<'|'<<h.consecutive_comfort_cycles
           <<'|'<<l.food_kg<<'|'<<l.water_kg<<'|'<<l.oxygen_kg<<'|'<<l.nutrients_kg
           <<'|'<<l.co2_kg<<'|'<<l.crops_kg<<'|'<<l.compost_kg<<'|'<<l.wastewater_kg
           <<'|'<<l.organic_waste_kg<<'|'<<l.sludge_kg<<'|'<<l.hydrogen_kg
           <<'|'<<l.crops_ready_tick<<'|'<<l.cycles<<'|'<<l.consecutive_supplied_cycles
           <<'|'<<l.shortages<<'|'<<l.harvested_food_kg<<'|'<<l.recovered_water_kg<<'\n';
    }
    if(snapshot_version_>=15)for(const auto& [id,c]:communities_){
        out<<"community|"<<id<<'|'<<c.source_ship<<'|'<<c.embodied_ore
           <<'|'<<c.food_kg<<'|'<<c.water_kg<<'|'<<c.organic_waste_kg
           <<'|'<<c.wastewater_kg<<'|'<<c.attendants<<'|'<<c.cycles
           <<'|'<<c.consecutive_service_cycles<<'|'<<c.shortages
           <<'|'<<c.served_visits<<'\n';
    }
    if(snapshot_version_>=16)for(const auto& [id,c]:clinics_){
        out<<"clinic|"<<id<<'|'<<c.source_ship<<'|'<<c.embodied_ore<<'|'
           <<c.water_kg<<'|'<<c.substrate_kg<<'|'<<c.wastewater_kg<<'|'
           <<c.medical_waste_kg<<'|'<<c.medics<<'|'<<c.active_cases<<'|'
           <<c.cycles<<'|'<<c.consecutive_care_cycles<<'|'<<c.shortages<<'|'
           <<c.treated_cases<<'|'<<c.recorded_cases;
        if(snapshot_version_>=38)out<<'|'<<c.training_units<<'|'<<c.ems_treatment_units
            <<'|'<<c.ems_treated_cases<<'|'<<c.last_ems_tick<<'|'<<c.ems_cases_this_tick;
        out<<'\n';
    }
    if(snapshot_version_>=17)for(const auto& [id,r]:reclaimers_){
        out<<"reclaimer|"<<id<<'|'<<r.source_ship<<'|'<<r.embodied_ore<<'|'
           <<r.sludge_kg<<'|'<<r.carbon_residue_kg<<'|'
           <<r.water_recovered_total_kg<<'|'<<r.oxygen_generated_total_kg<<'|'
           <<r.cycles<<'|'<<r.consecutive_powered_cycles;
        for(const auto x:r.water_recent)out<<'|'<<x;
        for(const auto x:r.oxygen_recent)out<<'|'<<x;
        for(const auto x:r.water_demand_recent)out<<'|'<<x;
        for(const auto x:r.oxygen_demand_recent)out<<'|'<<x;
        out<<'\n';
    }
    if(snapshot_version_>=18)for(const auto& [id,p]:waterworks_){
        out<<"waterworks|"<<id<<'|'<<p.source_ship<<'|'<<p.embodied_ore<<'|'
           <<p.raw_feed_kg<<'|'<<p.brine_kg<<'|'<<p.purified_total_kg<<'|'
           <<p.cycles<<'|'<<p.consecutive_service_cycles;
        for(const auto x:p.recent_output)out<<'|'<<x;
        for(const auto x:p.recent_demand)out<<'|'<<x;
        out<<'\n';
    }
    if(snapshot_version_>=20){
        out<<"npcnext|"<<next_npc_id_<<'\n';
        for(const auto& [id,n]:npcs_){
            out<<"npc|"<<id<<'|'<<n.home_ship<<'|'<<n.location<<'|'
               <<static_cast<int>(n.role)<<'|'<<n.farm_xp<<'|'<<n.mentor_id;
            if(snapshot_version_>=31)out<<'|'<<n.transport_id;
            if(snapshot_version_>=32)out<<'|'<<n.assigned_site;
            if(snapshot_version_>=86) {
                const auto residence=n.legal_residence();
                out<<'|'<<unsigned(residence.kind)<<'|'<<residence.id;
            }
            out<<'\n';
        }
        for(const auto& [site,xp]:farm_team_xp_)
            out<<"teamxp|"<<site<<'|'<<xp<<'\n';
    }
    if(snapshot_version_>=19)for(const auto& [id,site]:policy_sites_){
        out<<"policy|"<<id<<'|'<<site.source_ship;
        for(const auto& p:site.domains)
            out<<'|'<<static_cast<int>(p.declared)<<'|'<<p.inspectors;
        out<<'\n';
    }
    if(snapshot_version_>=13)for(const auto& [id,u]:utilities_){
        out << "utility|" << id << '|' << u.source_ship << '|' << u.embodied_ore
            << '|' << u.condition << '|' << u.stored_energy << '|' << (u.powered?1:0)
            << '|' << u.cycles << '|' << u.consecutive_powered_cycles
            << '|' << u.outages << '|' << u.repairs << '|' << u.generated_energy
            << '|' << u.consumed_energy << '|' << u.curtailed_energy;
        if(snapshot_version_>=14)
            out << '|' << u.spare_modules << '|' << u.technicians
                << '|' << (u.delegated_maintenance?1:0)
                << '|' << u.delegated_repairs << '|' << u.blocked_maintenance_cycles;
        out << '\n';
    }
    if (snapshot_version_ >= 3) {
        for (const auto& [key, h] : hangars_)
            out << "hangar|" << key.first << '|' << key.second << '|' << h.ore << '|' << h.capacity << '\n';
    }
    if (snapshot_version_ >= 4) {
        for (const auto& [id,m] : markets_)
            out << "market|" << id << '|' << m.ore << '|' << m.credits << '|' << m.price_per_ore << '\n';
        for (const auto& [owner, credits] : wallets_)
            out << "wallet|" << owner << '|' << credits << '\n';
    }
    if(snapshot_version_>=65)for(const auto& [id,p]:survey_probes_){
        (void)id;
        out<<"surveyprobe|"<<p.id<<'|'<<p.carrier<<'|'<<p.owner<<'|'<<p.system
            <<'|'<<p.position.x<<'|'<<p.position.y<<'|'<<p.launched_tick;
        if(snapshot_version_>=66)
            out<<'|'<<unsigned(p.drone_phase)<<'|'<<p.drone_target
               <<'|'<<p.drone_home.x<<'|'<<p.drone_home.y
               <<'|'<<p.sample_ore<<'|'<<unsigned(p.taxonomy);
        if(snapshot_version_>=69)
            out<<'|'<<p.focus_site<<'|'<<p.focus_waypoint.x<<'|'<<p.focus_waypoint.y;
        out<<'\n';
    }
    if(snapshot_version_>=63)for(const auto& [key,d]:discoveries_){
        (void)key;
        out<<"discovery|"<<d.owner<<'|'<<d.site<<'|'<<d.system<<'|'<<d.location.x
           <<'|'<<d.location.y<<'|'<<d.first_tick<<'|'<<(d.geological?1:0);
        if(snapshot_version_>=67)out<<'|'<<unsigned(d.survey_taxonomy);
        out<<'\n';
    }
    if(snapshot_version_>=68)for(const auto& [key,p]:pending_probe_discoveries_){
        (void)key;
        const auto& d=p.report;
        out<<"pendingprobe|"<<d.owner<<'|'<<d.site<<'|'<<d.system
           <<'|'<<d.location.x<<'|'<<d.location.y<<'|'<<d.first_tick
           <<'|'<<(d.geological?1:0)<<'|'<<p.carrier<<'|'<<p.probe_id
           <<'|'<<p.ready_tick<<'\n';
    }
    if (snapshot_version_ >= 2) {
        for (auto id : depleted_) out << "depleted|" << id << '\n';
        for (const auto& [actor, job] : mining_)
            out << "automine|" << actor << '|' << job.target << '|' << job.amount_per_tick
                << '|' << job.start_command_id << '\n';
    }
    for (const auto& tick_commands : commands_) {
        auto pending = tick_commands.second;
        std::sort(pending.begin(), pending.end(), [](const Command& a, const Command& b) {
            if (a.sequence != b.sequence) return a.sequence < b.sequence;
            return a.id < b.id;
        });
        for (const auto& c : pending) {
            out << "pending|" << c.id << '|' << c.execute_tick << '|' << c.sequence
                << '|' << c.issuer << '|' << c.actor << '|' << c.target << '|'
                << static_cast<int>(c.action) << '|' << c.thrust_x << '|' << c.thrust_y
                << '|' << c.amount;
            if (snapshot_version_ >= 31) {
                out << '|' << c.transport_id << '|' << c.passenger_ids.size();
                for (auto npc : c.passenger_ids) out << '|' << npc;
                if(c.action==Action::HostSplitEvacuatePassengers)
                    out<<'|'<<c.secondary_receiver<<'|'<<c.secondary_transport;
            }
            out << '\n';
        }
    }
    if(snapshot_version_>=59)for(const auto& [id,d]:physical_dependents_)
        out<<"physicaldependent|"<<id<<'|'<<d.guardian_id<<'|'<<d.registration_command
           <<'|'<<d.registered_tick<<'|'<<d.home_ship<<'|'<<d.location<<'\n';
    if(snapshot_version_>=58)for(const auto& [ship,d]:colony_departures_){
        out<<"colonydeparture|"<<ship<<'|'<<d.receiver<<'|'<<d.command_id
           <<'|'<<d.tick<<'|'<<d.relocated_npcs.size();
        for(auto id:d.relocated_npcs)out<<'|'<<id;
        if(snapshot_version_>=60){
            out<<'|'<<d.relocated_dependents.size();
            for(auto id:d.relocated_dependents)out<<'|'<<id;
        }
        out<<'\n';
    }
    if(snapshot_version_>=33)for(const auto& [id,v]:voyages_){
        out<<"voyage|"<<id<<'|'<<v.ferry<<'|'<<v.origin<<'|'<<v.status<<'|'<<v.passengers.size();
        if(snapshot_version_>=34)out<<'|'<<v.destination;
        if(snapshot_version_>=35)out<<'|'<<v.last_incident_id<<'|'<<v.stranded_tick
                                     <<'|'<<v.stranded_reason<<'|'<<v.rescue_transport_id;
        if(snapshot_version_>=48)out<<'|'<<v.secondary_rescue_transport_id;
        for(auto npc:v.passengers)out<<'|'<<npc;
        out<<'\n';
    }
    if(snapshot_version_>=38){
        for(const auto& [id,q]:named_medics_)out<<"medic|"<<id<<'|'<<q.clinic_site<<'|'<<q.units<<'\n';
        const auto emit_case=[&](const char* type,std::uint64_t id,const MedicalCase& m){
            out<<type<<'|'<<id<<'|'<<m.rescue_voyage<<'|'<<m.triage_command
               <<'|'<<m.triage_tick<<'|'<<m.severity<<'|'<<m.treatment_command
               <<'|'<<m.treatment_tick<<'|'<<m.clinic_site<<'|'<<m.medic_id;
            if(snapshot_version_>=39)out<<'|'<<m.stabilization_command<<'|'
                <<m.stabilized_tick<<'|'<<m.recovery_due_tick<<'|'<<m.recovered_tick;
            out<<'\n';
        };
        for(const auto& [id,m]:medical_cases_)emit_case("patient",id,m);
        if(snapshot_version_>=39){
            for(const auto& [key,m]:medical_history_)emit_case("patienthistory",key.first,m);
            for(const auto& [id,sh]:medic_shifts_)out<<"medicshift|"<<id<<'|'
                <<sh.clinic_site<<'|'<<sh.medic_id<<'|'<<sh.start_tick<<'|'<<sh.end_tick<<'\n';
            for(const auto& [id,k]:mobile_medical_kits_)out<<"medicalkit|"<<id<<'|'
                <<k.water_kg<<'|'<<k.substrate_kg<<'|'<<k.wastewater_kg<<'|'<<k.medical_waste_kg<<'\n';
        }
        for(const auto& [id,h]:crew_rehomes_)
            out<<"crewrehome|"<<id<<'|'<<h.wreck<<'|'<<h.rescue_ship
               <<'|'<<h.incident_command<<'|'<<h.tick<<'\n';
    }
    if(snapshot_version_>=36)for(const auto& [id,record]:casualties_){
        const auto& n=record.identity;
        out<<"deceased|"<<id<<'|'<<n.home_ship<<'|'<<n.location<<'|'
           <<static_cast<unsigned>(n.role)<<'|'<<n.farm_xp<<'|'<<n.mentor_id<<'|'
           <<n.transport_id<<'|'<<n.assigned_site<<'|'<<record.death_tick<<'|'
           <<record.command_id<<'|'<<record.ferry;
        if(snapshot_version_>=86){
            const auto residence=n.legal_residence();
            out<<'|'<<unsigned(residence.kind)<<'|'<<residence.id;
        }
        out<<'\n';
    }
    // WO2-H: durable training proof follows the original named-NPC identity,
    // including casualties. No second workforce registry is introduced.
    if(snapshot_version_>=105){
        const auto emit=[&](const NpcRecord& n){
            for(const auto& t:n.industrial_training){
                out<<"npctraining|"<<n.id<<'|'<<t.command_id<<'|'<<t.tick
                   <<'|'<<t.workshop_site<<'|'<<unsigned(t.trade);
                if(snapshot_version_>=108)
                    out<<'|'<<t.mentor_id<<'|'<<t.supervisor_appointment_id;
                out<<'\n';
            }
        };
        for(const auto& [id,n]:npcs_){(void)id;emit(n);}
        for(const auto& [id,dead]:casualties_){(void)id;emit(dead.identity);}
    }
    for (auto id : submitted_ids_) out << "accepted|" << id << '\n';
    for (const auto& ev : events_) {
        out << "event|" << ev.tick << '|' << ev.command_id << '|'
            << static_cast<int>(ev.result) << '|' << ev.transferred << '\n';
    }
    if (snapshot_version_ >= 2) {
        for (const auto& e : activity_events_)
            out << "activity|" << e.tick << '|' << static_cast<int>(e.kind) << '|'
                << e.start_command_id << '|' << e.actor << '|' << e.target << '|'
                << static_cast<int>(e.result) << '|' << e.transferred << '\n';
    }
    return out.str();
}

std::uint64_t Simulation::state_hash() const {
    const auto bytes = canonical_state();
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : bytes) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string to_string(Outcome o) {
    switch (o) {
        case Outcome::Applied: return "APPLIED";
        case Outcome::InvalidActor: return "INVALID_ACTOR";
        case Outcome::Unauthorized: return "UNAUTHORIZED";
        case Outcome::InvalidTarget: return "INVALID_TARGET";
        case Outcome::OutOfRange: return "OUT_OF_RANGE";
        case Outcome::InvalidArgument: return "INVALID_ARGUMENT";
        case Outcome::NoResources: return "NO_RESOURCES";
        case Outcome::CargoFull: return "CARGO_FULL";
        case Outcome::NotMining: return "NOT_MINING";
        case Outcome::NotInSpace: return "NOT_IN_SPACE";
        case Outcome::NotDocked: return "NOT_DOCKED";
        case Outcome::InTransition: return "IN_TRANSITION";
        case Outcome::ShipMoving: return "SHIP_MOVING";
        case Outcome::HangarFull: return "HANGAR_FULL";
        case Outcome::InsufficientFunds: return "INSUFFICIENT_FUNDS";
        case Outcome::WrongSystem: return "WRONG_SYSTEM";
        case Outcome::GateOffline: return "GATE_OFFLINE";
        case Outcome::NotColonyShip: return "NOT_COLONY_SHIP";
        case Outcome::InsufficientCrew: return "INSUFFICIENT_CREW";
        case Outcome::ModuleOffline: return "MODULE_OFFLINE";
        case Outcome::CooldownActive: return "COOLDOWN_ACTIVE";
        case Outcome::AttackResisted: return "ATTACK_RESISTED";
    }
    return "UNKNOWN";
}

} // namespace galaxy

namespace galaxy {
namespace {
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        auto end = line.find('|', start);
        out.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return out;
        start = end + 1;
    }
}
void require_fields(const std::vector<std::string>& a, std::size_t n) {
    if (a.size() != n) throw std::runtime_error("snapshot: invalid field count");
}
std::uint64_t u64(const std::string& s) {
    if (s.empty() || s[0] == '-' || s[0] == '+' || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("snapshot: invalid unsigned integer");
    std::size_t used = 0;
    const auto n = std::stoull(s, &used, 10);
    if (used != s.size()) throw std::runtime_error("snapshot: invalid unsigned integer suffix");
    return n;
}
std::int64_t i64(const std::string& s) {
    if (s.empty() || s[0] == '+' || (s[0] == '-' && s.size() == 1))
        throw std::runtime_error("snapshot: invalid signed integer");
    for (std::size_t i = s[0] == '-' ? 1 : 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') throw std::runtime_error("snapshot: invalid signed integer");
    }
    std::size_t used = 0;
    const auto n = std::stoll(s, &used, 10);
    if (used != s.size()) throw std::runtime_error("snapshot: invalid signed integer suffix");
    return n;
}
}
Simulation Simulation::from_snapshot(const std::string& bytes) {
    if (bytes.empty() || bytes.size() > 16 * 1024 * 1024 || bytes.back() != '\n' ||
        bytes.find('\0') != std::string::npos || bytes.find('\r') != std::string::npos)
        throw std::runtime_error("snapshot: invalid size or binary framing");
    std::istringstream in(bytes);
    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("snapshot: missing header");
    auto h = fields(line);
    require_fields(h, 5);
    if (h[0] != "galaxy-kernel-v119" && h[0] != "galaxy-kernel-v118" && h[0] != "galaxy-kernel-v117" && h[0] != "galaxy-kernel-v116" && h[0] != "galaxy-kernel-v115" && h[0] != "galaxy-kernel-v114" && h[0] != "galaxy-kernel-v113" && h[0] != "galaxy-kernel-v112" && h[0] != "galaxy-kernel-v111" && h[0] != "galaxy-kernel-v110" && h[0] != "galaxy-kernel-v109" && h[0] != "galaxy-kernel-v108" && h[0] != "galaxy-kernel-v107" && h[0] != "galaxy-kernel-v106" && h[0] != "galaxy-kernel-v105" && h[0] != "galaxy-kernel-v1" && h[0] != "galaxy-kernel-v2" && h[0] != "galaxy-kernel-v3" && h[0] != "galaxy-kernel-v4" && h[0] != "galaxy-kernel-v5" && h[0] != "galaxy-kernel-v6" && h[0] != "galaxy-kernel-v7" && h[0] != "galaxy-kernel-v8" && h[0] != "galaxy-kernel-v9" && h[0] != "galaxy-kernel-v10" && h[0] != "galaxy-kernel-v11" && h[0] != "galaxy-kernel-v12" && h[0] != "galaxy-kernel-v13" && h[0] != "galaxy-kernel-v14" && h[0] != "galaxy-kernel-v15" && h[0] != "galaxy-kernel-v16" && h[0] != "galaxy-kernel-v17" && h[0] != "galaxy-kernel-v18" && h[0] != "galaxy-kernel-v19" && h[0] != "galaxy-kernel-v20" && h[0] != "galaxy-kernel-v21" && h[0] != "galaxy-kernel-v22" && h[0] != "galaxy-kernel-v23" && h[0] != "galaxy-kernel-v24" && h[0] != "galaxy-kernel-v25" && h[0] != "galaxy-kernel-v26" && h[0] != "galaxy-kernel-v27" && h[0] != "galaxy-kernel-v28" && h[0] != "galaxy-kernel-v29" && h[0] != "galaxy-kernel-v30" && h[0] != "galaxy-kernel-v31" && h[0] != "galaxy-kernel-v32" && h[0] != "galaxy-kernel-v33" && h[0] != "galaxy-kernel-v34" && h[0] != "galaxy-kernel-v35" && h[0] != "galaxy-kernel-v36" && h[0] != "galaxy-kernel-v37" && h[0] != "galaxy-kernel-v38" && h[0] != "galaxy-kernel-v39" && h[0] != "galaxy-kernel-v40" && h[0] != "galaxy-kernel-v41" && h[0] != "galaxy-kernel-v44" && h[0] != "galaxy-kernel-v48" && h[0] != "galaxy-kernel-v49" && h[0] != "galaxy-kernel-v52" && h[0] != "galaxy-kernel-v53" && h[0] != "galaxy-kernel-v57" && h[0] != "galaxy-kernel-v58" && h[0] != "galaxy-kernel-v59" && h[0] != "galaxy-kernel-v60" && h[0] != "galaxy-kernel-v61" && h[0] != "galaxy-kernel-v62" && h[0] != "galaxy-kernel-v63" && h[0] != "galaxy-kernel-v64" && h[0] != "galaxy-kernel-v65" && h[0] != "galaxy-kernel-v66" && h[0] != "galaxy-kernel-v67" && h[0] != "galaxy-kernel-v68" && h[0] != "galaxy-kernel-v69" && h[0] != "galaxy-kernel-v70" && h[0] != "galaxy-kernel-v71" && h[0] != "galaxy-kernel-v72" && h[0] != "galaxy-kernel-v73" && h[0] != "galaxy-kernel-v74" && h[0] != "galaxy-kernel-v75" && h[0] != "galaxy-kernel-v76" && h[0] != "galaxy-kernel-v77" && h[0] != "galaxy-kernel-v78" && h[0] != "galaxy-kernel-v79" && h[0] != "galaxy-kernel-v80" && h[0] != "galaxy-kernel-v81" && h[0] != "galaxy-kernel-v82" && h[0] != "galaxy-kernel-v83" && h[0] != "galaxy-kernel-v84" && h[0] != "galaxy-kernel-v85" && h[0] != "galaxy-kernel-v86" && h[0] != "galaxy-kernel-v87" && h[0] != "galaxy-kernel-v88" && h[0] != "galaxy-kernel-v89" && h[0] != "galaxy-kernel-v90" && h[0] != "galaxy-kernel-v91" && h[0] != "galaxy-kernel-v92" && h[0] != "galaxy-kernel-v93" && h[0] != "galaxy-kernel-v94" && h[0] != "galaxy-kernel-v95" && h[0] != "galaxy-kernel-v96" && h[0] != "galaxy-kernel-v97" && h[0] != "galaxy-kernel-v98" && h[0] != "galaxy-kernel-v99" && h[0] != "galaxy-kernel-v100" && h[0] != "galaxy-kernel-v101" && h[0] != "galaxy-kernel-v102" && h[0] != "galaxy-kernel-v103" && h[0] != "galaxy-kernel-v104") throw std::runtime_error("snapshot: unsupported version");
    const auto sys = u64(h[1]);
    if (sys == 0 || sys > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("snapshot: invalid system");
    Simulation result(u64(h[4]), static_cast<std::uint32_t>(sys));
    result.snapshot_version_ = h[0] == "galaxy-kernel-v119" ? 119 : h[0] == "galaxy-kernel-v118" ? 118 : h[0] == "galaxy-kernel-v117" ? 117 : h[0] == "galaxy-kernel-v116" ? 116 : h[0] == "galaxy-kernel-v115" ? 115 : h[0] == "galaxy-kernel-v114" ? 114 : h[0] == "galaxy-kernel-v113" ? 113 : h[0] == "galaxy-kernel-v112" ? 112 : h[0] == "galaxy-kernel-v111" ? 111 : h[0] == "galaxy-kernel-v110" ? 110 : h[0] == "galaxy-kernel-v109" ? 109 : h[0] == "galaxy-kernel-v108" ? 108 : h[0] == "galaxy-kernel-v107" ? 107 : h[0] == "galaxy-kernel-v106" ? 106 : h[0] == "galaxy-kernel-v105" ? 105 : h[0] == "galaxy-kernel-v104" ? 104 : h[0] == "galaxy-kernel-v103" ? 103 : h[0] == "galaxy-kernel-v102" ? 102 : h[0] == "galaxy-kernel-v101" ? 101 : h[0] == "galaxy-kernel-v100" ? 100 : h[0] == "galaxy-kernel-v99" ? 99 : h[0] == "galaxy-kernel-v98" ? 98 : h[0] == "galaxy-kernel-v97" ? 97 : h[0] == "galaxy-kernel-v96" ? 96 : h[0] == "galaxy-kernel-v95" ? 95 : h[0] == "galaxy-kernel-v94" ? 94 : h[0] == "galaxy-kernel-v93" ? 93 : h[0] == "galaxy-kernel-v92" ? 92 : h[0] == "galaxy-kernel-v91" ? 91 : h[0] == "galaxy-kernel-v90" ? 90 : h[0] == "galaxy-kernel-v89" ? 89 : h[0] == "galaxy-kernel-v88" ? 88 : h[0] == "galaxy-kernel-v87" ? 87 : h[0] == "galaxy-kernel-v86" ? 86 : h[0] == "galaxy-kernel-v85" ? 85 : h[0] == "galaxy-kernel-v84" ? 84 : h[0] == "galaxy-kernel-v83" ? 83 : h[0] == "galaxy-kernel-v82" ? 82 : h[0] == "galaxy-kernel-v81" ? 81 : h[0] == "galaxy-kernel-v80" ? 80 : h[0] == "galaxy-kernel-v79" ? 79 : h[0] == "galaxy-kernel-v78" ? 78 : h[0] == "galaxy-kernel-v77" ? 77 : h[0] == "galaxy-kernel-v76" ? 76 : h[0] == "galaxy-kernel-v75" ? 75 : h[0] == "galaxy-kernel-v74" ? 74 : h[0] == "galaxy-kernel-v73" ? 73 : h[0] == "galaxy-kernel-v72" ? 72 : h[0] == "galaxy-kernel-v71" ? 71 : h[0] == "galaxy-kernel-v70" ? 70 : h[0] == "galaxy-kernel-v69" ? 69 : h[0] == "galaxy-kernel-v68" ? 68 : h[0] == "galaxy-kernel-v67" ? 67 : h[0] == "galaxy-kernel-v66" ? 66 : h[0] == "galaxy-kernel-v65" ? 65 : h[0] == "galaxy-kernel-v64" ? 64 : h[0] == "galaxy-kernel-v63" ? 63 : h[0] == "galaxy-kernel-v62" ? 62 : h[0] == "galaxy-kernel-v61" ? 61 : h[0] == "galaxy-kernel-v60" ? 60 : h[0] == "galaxy-kernel-v59" ? 59 : h[0] == "galaxy-kernel-v58" ? 58 : h[0] == "galaxy-kernel-v57" ? 57 : h[0] == "galaxy-kernel-v53" ? 53 : h[0] == "galaxy-kernel-v52" ? 52 : h[0] == "galaxy-kernel-v49" ? 49 : h[0] == "galaxy-kernel-v48" ? 48 : h[0] == "galaxy-kernel-v44" ? 44 : h[0] == "galaxy-kernel-v41" ? 41 : h[0] == "galaxy-kernel-v40" ? 40 : h[0] == "galaxy-kernel-v39" ? 39 : h[0] == "galaxy-kernel-v38" ? 38 : h[0] == "galaxy-kernel-v37" ? 37 : h[0] == "galaxy-kernel-v36" ? 36 : h[0] == "galaxy-kernel-v35" ? 35 : h[0] == "galaxy-kernel-v34" ? 34 : h[0] == "galaxy-kernel-v33" ? 33 : h[0] == "galaxy-kernel-v32" ? 32 : h[0] == "galaxy-kernel-v31" ? 31 : h[0] == "galaxy-kernel-v30" ? 30 : h[0] == "galaxy-kernel-v29" ? 29 : h[0] == "galaxy-kernel-v28" ? 28 : h[0] == "galaxy-kernel-v27" ? 27 : h[0] == "galaxy-kernel-v26" ? 26 : h[0] == "galaxy-kernel-v25" ? 25 : h[0] == "galaxy-kernel-v24" ? 24 : h[0] == "galaxy-kernel-v23" ? 23 : h[0] == "galaxy-kernel-v22" ? 22 : h[0] == "galaxy-kernel-v21" ? 21 : h[0] == "galaxy-kernel-v20" ? 20 : h[0] == "galaxy-kernel-v19" ? 19 : h[0] == "galaxy-kernel-v18" ? 18 : h[0] == "galaxy-kernel-v17" ? 17 : h[0] == "galaxy-kernel-v16" ? 16 : h[0] == "galaxy-kernel-v15" ? 15 : h[0] == "galaxy-kernel-v14" ? 14 : h[0] == "galaxy-kernel-v13" ? 13 : h[0] == "galaxy-kernel-v12" ? 12 : h[0] == "galaxy-kernel-v11" ? 11 : h[0] == "galaxy-kernel-v10" ? 10 : h[0] == "galaxy-kernel-v9" ? 9 : h[0] == "galaxy-kernel-v8" ? 8 : h[0] == "galaxy-kernel-v7" ? 7 : h[0] == "galaxy-kernel-v6" ? 6 : h[0] == "galaxy-kernel-v5" ? 5 : h[0] == "galaxy-kernel-v4" ? 4 : h[0] == "galaxy-kernel-v3" ? 3 : h[0] == "galaxy-kernel-v2" ? 2 : 1;
    result.tick_ = u64(h[2]);
    result.next_entity_id_ = u64(h[3]);
    if (result.next_entity_id_ == 0) throw std::runtime_error("snapshot: invalid next entity ID");
    std::set<std::uint64_t> pending_ids, event_ids;
    std::ostringstream parsed_status;
    parsed_status<<"ship-status-v1\n";
    std::set<std::uint32_t> parsed_systems;
    std::size_t records = 0;
    while (std::getline(in, line)) {
        if (++records > 400000) throw std::runtime_error("snapshot: excessive record count");
        const auto f = fields(line);
        if(f[0]=="system" && result.snapshot_version_>=5){
            require_fields(f,2);
            const auto id=u64(f[1]);
            if(!id || id>std::numeric_limits<std::uint32_t>::max() ||
               !parsed_systems.insert(static_cast<std::uint32_t>(id)).second)
                throw std::runtime_error("snapshot: invalid system registry");
            result.systems_.insert(static_cast<std::uint32_t>(id));
        } else if(f[0]=="planetshuttle" && result.snapshot_version_>=80) {
            if(result.snapshot_version_>=82) {
                if(f.size()<17)throw std::runtime_error("snapshot: short shuttle manifest");
                const auto count=u64(f[16]);
                if(count>3)throw std::runtime_error("snapshot: shuttle seating overflow");
                if(result.snapshot_version_>=83) {
                    const auto offset=17+static_cast<std::size_t>(count);
                    if(f.size()<=offset)
                        throw std::runtime_error("snapshot: missing visitor roster");
                    const auto visitors=u64(f[offset]);
                    if(visitors>64)
                        throw std::runtime_error("snapshot: too many surface visitors");
                    if(result.snapshot_version_>=84) {
                        const auto duty_offset=offset+1+static_cast<std::size_t>(visitors);
                        if(f.size()<=duty_offset)
                            throw std::runtime_error("snapshot: missing visitor duty roster");
                        const auto duty=u64(f[duty_offset]);
                        if(duty>visitors)
                            throw std::runtime_error("snapshot: excess visitor duties");
                        if(result.snapshot_version_>=85) {
                            const auto receipts_offset=duty_offset+1+static_cast<std::size_t>(duty);
                            if(f.size()<=receipts_offset)
                                throw std::runtime_error("snapshot: missing individual arrival receipts");
                            const auto receipts=u64(f[receipts_offset]);
                            if(receipts>visitors)
                                throw std::runtime_error("snapshot: too many visitor receipts");
                            require_fields(f,receipts_offset+1+2*static_cast<std::size_t>(receipts)+
                                (result.snapshot_version_>=87?1:0)+
                                (result.snapshot_version_>=109?2:0));
                        }else require_fields(f,duty_offset+1+static_cast<std::size_t>(duty));
                    }else require_fields(f,offset+1+static_cast<std::size_t>(visitors));
                }else require_fields(f,17+static_cast<std::size_t>(count));
            }else require_fields(f,result.snapshot_version_>=81?16:11);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),rev=u64(f[4]);
            const auto owner=u64(f[5]),ship=u64(f[6]),pilot=u64(f[7]);
            const auto mass=i64(f[8]),water=i64(f[9]),food=i64(f[10]);
            const PlanetarySiteKey key{body,std::uint16_t(lat),std::uint16_t(lon),
                                       std::uint32_t(rev)};
            PlanetaryShuttleState state{owner,ship,pilot,mass,water,food};
            if(result.snapshot_version_>=87)
                state.nutrients_kg=i64(f[result.snapshot_version_>=109?
                                          f.size()-3:f.size()-1]);
            if(result.snapshot_version_>=109) {
                state.oxygen_kg=i64(f[f.size()-2]);
                if(state.oxygen_kg<0 || state.oxygen_kg>1000)
                    throw std::runtime_error("snapshot: invalid shuttle oxygen");
                const auto module=u64(f.back());
                if(module>1)
                    throw std::runtime_error("snapshot: invalid shuttle module rack");
                state.housing_module=std::uint8_t(module);
            }
            if(result.snapshot_version_>=81) {
                const auto phase=u64(f[11]);
                if(phase>3)throw std::runtime_error("snapshot: invalid flight phase");
                state.flight_phase=std::uint8_t(phase);
                state.flight_request=u64(f[12]);
                state.arrival_tick=u64(f[13]);
                state.descent_receipt=u64(f[14]);
                state.ascent_receipt=u64(f[15]);
                if(((phase==1 || phase==3) &&
                    (!pilot || !state.flight_request ||
                     state.arrival_tick<=result.tick_ ||
                     state.arrival_tick>result.tick_+3)) ||
                   ((phase==0 || phase==2) &&
                     (state.flight_request || state.arrival_tick)) ||
                   (phase>=1 && !pilot) ||
                   (phase>=1 && !state.descent_receipt) ||
                   (phase>=2 && !state.descent_receipt) ||
                   (phase==3 && !state.ascent_receipt) ||
                   (state.flight_request &&
                    state.flight_request!=(phase==1?state.descent_receipt:state.ascent_receipt)))
                    throw std::runtime_error("snapshot: incoherent shuttle flight");
            }
            if(result.snapshot_version_>=82) {
                const auto count=u64(f[16]);
                for(std::size_t j=0;j<count;++j) {
                    const auto id=u64(f[17+j]);
                    if(!id || id==pilot || (!state.passenger_ids.empty() &&
                       id<=state.passenger_ids.back()))
                        throw std::runtime_error("snapshot: invalid ordered shuttle roster");
                    state.passenger_ids.push_back(id);
                }
                if(result.snapshot_version_>=83) {
                    const auto offset=17+static_cast<std::size_t>(count);
                    const auto visitors=u64(f[offset]);
                    for(std::size_t j=0;j<visitors;++j) {
                        const auto id=u64(f[offset+1+j]);
                        if(!id || id==pilot ||
                           std::binary_search(state.passenger_ids.begin(),
                                              state.passenger_ids.end(),id) ||
                           (!state.disembarked_ids.empty() &&
                            id<=state.disembarked_ids.back()))
                            throw std::runtime_error("snapshot: invalid surface visitor");
                        state.disembarked_ids.push_back(id);
                    }
                    if(result.snapshot_version_>=84) {
                        const auto duty_offset=offset+1+static_cast<std::size_t>(visitors);
                        const auto count_duty=u64(f[duty_offset]);
                        for(std::size_t j=0;j<count_duty;++j) {
                            const auto id=u64(f[duty_offset+1+j]);
                            if(!id || !std::binary_search(state.disembarked_ids.begin(),
                                                           state.disembarked_ids.end(),id) ||
                               (!state.duty_ids.empty() && id<=state.duty_ids.back()))
                                throw std::runtime_error("snapshot: invalid visiting worker");
                            state.duty_ids.push_back(id);
                        }
                        if(result.snapshot_version_>=85) {
                            const auto receipt_offset=duty_offset+1+static_cast<std::size_t>(count_duty);
                            const auto receipt_count=u64(f[receipt_offset]);
                            for(std::size_t j=0;j<receipt_count;++j) {
                                const auto id=u64(f[receipt_offset+1+2*j]);
                                const auto receipt=u64(f[receipt_offset+2+2*j]);
                                if(!id || !receipt ||
                                   !std::binary_search(state.disembarked_ids.begin(),
                                                       state.disembarked_ids.end(),id) ||
                                   !state.visitor_arrival_receipts.emplace(id,receipt).second)
                                    throw std::runtime_error("snapshot: invalid individual arrival receipt");
                            }
                        }
                    }
                }
            }
            if(!body || lat>=36 || lon>=72 || rev!=1 || !owner || !ship ||
               mass!=4 || water<0 || food<0 || state.nutrients_kg<0 ||
               water>1000 || food>1000-water ||
               state.nutrients_kg>1000-water-food ||
               result.planetary_shuttles_.size()>=10000 ||
               !result.planetary_shuttles_.emplace(key,state).second)
                throw std::runtime_error("snapshot: invalid shuttle custody");
        } else if(f[0]=="planetfamily" && result.snapshot_version_>=116) {
            if(f.size()<13)throw std::runtime_error("snapshot: truncated dependent flight");
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                       revision=u64(f[4]),guardian=u64(f[5]),
                       co_parent=u64(f[6]),booked=u64(f[7]),
                       outbound=u64(f[8]),returned=u64(f[9]),
                       stage=u64(f[10]),count=u64(f[11]);
            if(!body || lat>=36 || lon>=72 || revision!=1 ||
               !guardian || guardian==co_parent || !booked ||
               booked==std::numeric_limits<std::uint64_t>::max() ||
               stage<1 || stage>7 || count==0 || count>2 ||
               f.size()!=12+count)
                throw std::runtime_error("snapshot: invalid family-flight fields");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                std::uint16_t(lon),1};
            const auto shuttle=result.planetary_shuttles_.find(key);
            if(shuttle==result.planetary_shuttles_.end() ||
               shuttle->second.family.stage ||
               (!outbound && stage>1) ||
               (stage==1 && shuttle->second.flight_phase!=(outbound?1:0)) ||
               (stage==2 && shuttle->second.flight_phase!=2) ||
               (stage==3 && shuttle->second.flight_phase!=3) ||
               (stage==4 && shuttle->second.flight_phase!=0) ||
               (stage==5 && shuttle->second.flight_phase!=2) ||
               (stage==6 && shuttle->second.flight_phase!=3) ||
               (stage==7 && shuttle->second.flight_phase!=0) ||
               ((stage==1 || stage==2 || stage==5) && returned) ||
               ((stage==3 || stage==4 || stage==6 || stage==7) && !returned) ||
               (outbound && stage<=4 &&
                shuttle->second.descent_receipt!=outbound) ||
               (returned && shuttle->second.ascent_receipt!=returned))
                throw std::runtime_error("snapshot: impossible shuttle household phase");
            PlanetaryFamilyShuttleJourney family{};
            family.guardian_id=guardian;family.co_parent_id=co_parent;
            family.booking_request=booked;family.outbound_receipt=outbound;
            family.return_receipt=returned;family.stage=std::uint8_t(stage);
            for(std::size_t i=0;i<count;++i) {
                const auto child=u64(f[12+i]);
                if(child<(1ULL<<62) ||
                   (!family.child_ids.empty() && child<=family.child_ids.back()))
                    throw std::runtime_error("snapshot: duplicated child passenger");
                family.child_ids.push_back(child);
            }
            if(stage>=5 && (!co_parent || count!=1))
                throw std::runtime_error("snapshot: return lacks complete two-parent family");
            shuttle->second.family=std::move(family);
        } else if(f[0]=="localdelivery" && result.snapshot_version_>=88) {
            require_fields(f,13);
            const auto id=u64(f[1]),owner=u64(f[2]),ship=u64(f[3]);
            const auto body=u64(f[4]),lat=u64(f[5]),lon=u64(f[6]),rev=u64(f[7]);
            const auto cargo=u64(f[8]),kg=i64(f[9]),pilot=u64(f[10]);
            const auto stage=u64(f[11]),created=u64(f[12]);
            if(!id || id>=(std::uint64_t{1}<<50) ||
               !owner || owner==kServerAuthority || !ship || !body ||
               lat>=36 || lon>=72 || rev!=1 || cargo>2 ||
               kg<=0 || kg>1000 || stage>6 || created>result.tick_ ||
               result.local_delivery_orders_.size()>=10000 ||
               !result.local_delivery_orders_.emplace(id,LocalDeliveryOrder{
                    id,owner,ship,PlanetarySiteKey{body,
                        std::uint16_t(lat),std::uint16_t(lon),1},
                    static_cast<ShuttleCargo>(cargo),kg,pilot,
                    static_cast<LocalDeliveryStage>(stage),created}).second)
                throw std::runtime_error("snapshot: invalid local delivery order");
        } else if(f[0]=="playerperson" && result.snapshot_version_>=98) {
            require_fields(f,result.snapshot_version_>=99?12:9);
            const auto owner=u64(f[1]),home=u64(f[2]),physical=u64(f[3]),
                state=u64(f[4]),custodian=u64(f[5]),registered=u64(f[6]),
                receipt=u64(f[7]),updated=u64(f[8]);
            const auto consent_ship=result.snapshot_version_>=99?u64(f[9]):0;
            const auto consent_id=result.snapshot_version_>=99?u64(f[10]):0;
            const auto flight=result.snapshot_version_>=99?u64(f[11]):0;
            if(!owner || owner==kServerAuthority || !home || !physical ||
               state>unsigned(PlayerPersonState::Rescued) ||
               (result.snapshot_version_<99 && state!=0) ||
               registered>result.tick_ || updated<registered ||
               updated>result.tick_ ||
               (state==0 && (custodian || receipt || flight ||
                 home!=physical ||
                 ((consent_ship==0)!=(consent_id==0)))) ||
               (state==1 && (!custodian || !receipt || !flight ||
                 consent_ship || !consent_id || home==physical ||
                 !updated)) ||
               result.player_people_.size()>=10000 ||
               !result.player_people_.emplace(owner,PlayerPerson{
                   owner,home,physical,static_cast<PlayerPersonState>(state),
                   custodian,registered,receipt,updated,
                   consent_ship,consent_id,flight}).second)
                throw std::runtime_error("snapshot: invalid player persona");
        } else if(f[0]=="npcflight" && result.snapshot_version_>=95) {
            require_fields(f,result.snapshot_version_>=96?16:12);
            const auto id=u64(f[1]),owner=u64(f[2]),ship=u64(f[3]),
                pilot=u64(f[4]),body=u64(f[5]),request=u64(f[6]),
                purpose=u64(f[7]),phase=u64(f[8]),posted=u64(f[9]),
                started=u64(f[10]),resolved=u64(f[11]);
            const auto mode=result.snapshot_version_>=96?u64(f[12]):0;
            const auto station=result.snapshot_version_>=96?u64(f[13]):0;
            const auto cmds=result.snapshot_version_>=96?u64(f[14]):0;
            const auto last=result.snapshot_version_>=96?u64(f[15]):0;
            constexpr std::uint64_t kNativeFlightCommandTag=(std::uint64_t{1}<<61);
            if(!id || id>=(std::uint64_t{1}<<50) ||
               !owner || owner==kServerAuthority || !ship || !pilot ||
               purpose>unsigned(NpcFlightPurpose::Mining) ||
               phase>unsigned(NpcFlightPhase::Cancelled) ||
               mode>(result.snapshot_version_>=97?unsigned(NpcFlightMode::LocalRendezvous):unsigned(NpcFlightMode::LocalDock)) ||
               posted>result.tick_ || started>result.tick_ ||
               resolved>result.tick_ ||
               (phase==0 && (started || resolved)) ||
               (phase==3 && resolved<posted) ||
               ((phase==1 || phase==2) &&
                (started<=posted || !started)) ||
               (phase==1 && resolved) ||
               (phase==2 && resolved<started) ||
               (mode==0 && (!request || request>=(std::uint64_t{1}<<50) ||
                            !body || station || cmds || last ||
                            (phase==3 && started))) ||
               ((mode==1 || mode==2) && (id>=(std::uint64_t{1}<<40) ||
                            request || body || !station || cmds>511 ||
                            ((cmds==0)!=(last==0)) ||
                            (cmds && last!=(kNativeFlightCommandTag |
                                (id<<9) | cmds)) ||
                            ((phase==0 || (phase==3 && !started)) && cmds) ||
                            ((phase==1 || phase==2) && !cmds))) ||
               result.npc_flight_orders_.size()>=10000 ||
               !result.npc_flight_orders_.emplace(id,NpcFlightOrder{
                    id,owner,ship,pilot,body,request,
                    static_cast<NpcFlightPurpose>(purpose),
                    static_cast<NpcFlightPhase>(phase),posted,started,resolved,
                    static_cast<NpcFlightMode>(mode),station,
                    static_cast<std::uint16_t>(cmds),last}).second)
                throw std::runtime_error("snapshot: invalid npc flight order");
        } else if(f[0]=="workdirective" && result.snapshot_version_>=94) {
            require_fields(f,15);
            const auto id=u64(f[1]),owner=u64(f[2]),
                ship=u64(f[3]),kind=u64(f[4]),
                source=u64(f[5]),dependency=u64(f[6]),
                first=u64(f[7]),second=u64(f[8]),
                provisions=i64(f[9]),status=u64(f[10]),
                blocked=u64(f[11]),posted=u64(f[12]),
                updated=u64(f[13]),resolved=u64(f[14]);
            if(!id || id>=(std::uint64_t{1}<<50) ||
               !owner || owner==kServerAuthority || !ship ||
               (kind!=unsigned(WorkTaskKind::LocalDelivery) &&
                kind!=unsigned(WorkTaskKind::IntercolonyFreight) &&
                kind!=unsigned(WorkTaskKind::CustomerShipOrder)) ||
               !source || source>=(std::uint64_t{1}<<50) ||
               (kind!=unsigned(WorkTaskKind::CustomerShipOrder) &&
                (first || second || provisions)) ||
               (dependency && dependency>=id) ||
               ((first==0)!=(second==0)) ||
               (first && first==second) ||
               (first && (provisions<10 || provisions>1000)) ||
               (!first && provisions) ||
               status>2 || blocked>6 ||
               ((status!=0) && blocked) ||
               posted>result.tick_ || updated<posted ||
               updated>result.tick_ || resolved>result.tick_ ||
               ((status==0) && resolved) ||
               ((status!=0) && resolved<posted) ||
               result.work_directives_.size()>=10000 ||
               !result.work_directives_.emplace(id,WorkDirective{
                   id,owner,ship,static_cast<WorkTaskKind>(kind),
                   source,dependency,{first,second},provisions,
                   static_cast<WorkDirectiveStatus>(status),
                   static_cast<WorkBlock>(blocked),
                   posted,updated,resolved}).second)
                throw std::runtime_error("snapshot: invalid work delegation");
        } else if(f[0]=="yardcustomer" && result.snapshot_version_>=93) {
            require_fields(f,14);
            const auto id=u64(f[1]),yard=u64(f[2]),build=u64(f[3]),
                customer=u64(f[4]),supplier=u64(f[5]),
                buyer_ship=u64(f[6]),supplier_ship=u64(f[7]),
                stage=u64(f[8]),posted=u64(f[9]),
                accepted=u64(f[10]),funded=u64(f[11]),
                delivered=u64(f[12]),hull=u64(f[13]);
            if(!id || id>=(std::uint64_t{1}<<50) || !yard ||
               !build || build>=(std::uint64_t{1}<<50) ||
               id==build || id==yard || build==yard ||
               !customer || customer==kServerAuthority ||
               !supplier || supplier==kServerAuthority ||
               !buyer_ship || !supplier_ship ||
               buyer_ship==supplier_ship || stage>3 ||
               posted>result.tick_ || accepted>result.tick_ ||
               funded>result.tick_ || delivered>result.tick_ ||
               (stage>=1 && accepted<posted) ||
               (stage>=2 && funded<accepted) ||
               (stage==3 && delivered<funded) ||
               (stage==0 && (accepted || funded || delivered || hull)) ||
               (stage==1 && (funded || delivered || hull)) ||
               (stage==2 && (delivered || hull)) ||
               (stage==3 && !hull) ||
               result.shipyard_customer_orders_.size()>=10000 ||
               !result.shipyard_customer_orders_.emplace(id,
                   ShipyardCustomerOrder{id,yard,build,customer,supplier,
                       buyer_ship,supplier_ship,
                       static_cast<ShipyardCustomerStage>(stage),
                       posted,accepted,funded,delivered,hull}).second)
                throw std::runtime_error("snapshot: invalid shipyard customer order");
        } else if(f[0]=="freighterbuild" && result.snapshot_version_>=91) {
            require_fields(f,result.snapshot_version_>=92?18:14);
            const auto id=u64(f[1]),yard=u64(f[2]),owner=u64(f[3]),
                source=u64(f[4]),launched=u64(f[5]),progress=u64(f[7]),
                required=u64(f[8]),posted=u64(f[9]),last=u64(f[10]),
                arrived=u64(f[11]),system=u64(f[12]),anchor=u64(f[13]);
            const auto mass=i64(f[6]);
            const auto commissioned=result.snapshot_version_>=92?u64(f[14]):0;
            const auto first=result.snapshot_version_>=92?u64(f[15]):0;
            const auto second=result.snapshot_version_>=92?u64(f[16]):0;
            const auto provision=result.snapshot_version_>=92?i64(f[17]):0;
            if((commissioned && (!launched || commissioned<arrived ||
                                  commissioned>result.tick_ || !first ||
                                  !second || first==second ||
                                  provision<10 || provision>1000)) ||
               (!commissioned && (first || second || provision)) ||
               !id || id>=(std::uint64_t{1}<<50) || id==yard ||
               !yard || !owner || owner==kServerAuthority || !source ||
               mass!=12 || required!=12 || progress>required ||
               (progress==0 && last!=0) ||
               (progress>0 && (!last || last<posted || last>result.tick_)) ||
               (launched && (progress!=required || !arrived ||
                              arrived!=last || arrived>result.tick_)) ||
               (!launched && (progress==required || arrived)) ||
               posted>result.tick_ || !system ||
               system>std::numeric_limits<std::uint32_t>::max() ||
               result.freighter_build_orders_.size()>=10000 ||
               !result.freighter_build_orders_.emplace(id,FreighterBuildOrder{
                   id,yard,owner,source,launched,mass,progress,required,
                   posted,last,arrived,static_cast<std::uint32_t>(system),
                   anchor,commissioned,{first,second},provision}).second)
                throw std::runtime_error("snapshot: invalid freighter build record");
        } else if(f[0]=="shipyard" && result.snapshot_version_>=90) {
            require_fields(f,12);
            const auto id=u64(f[1]),owner=u64(f[2]),ship=u64(f[3]),
                w1=u64(f[4]),w2=u64(f[5]),progress=u64(f[7]),
                required=u64(f[8]),posted=u64(f[9]),
                last=u64(f[10]),stalled=u64(f[11]);
            const auto mass=i64(f[6]);
            if(!id || id>=(std::uint64_t{1}<<50) || !owner ||
               owner==kServerAuthority || !ship || !w1 || !w2 || w1==w2 ||
               mass!=8 || required!=8 || progress>required ||
               posted>result.tick_ || last>result.tick_ ||
               (progress==0 && last!=0) ||
               (progress>0 && (last<posted || last==0)) ||
               stalled>result.tick_-posted ||
               result.shipyard_constructions_.size()>=10000 ||
               !result.shipyard_constructions_.emplace(id,ShipyardConstruction{
                   id,owner,ship,{{w1,w2}},mass,progress,required,
                   posted,last,stalled}).second)
                throw std::runtime_error("snapshot: invalid shipyard build record");
        } else if(f[0]=="interfreight" && result.snapshot_version_>=89) {
            require_fields(f,12);
            const auto id=u64(f[1]),owner=u64(f[2]),src=u64(f[3]),
                       carrier=u64(f[4]),dst=u64(f[5]),cargo=u64(f[6]),
                       origin=u64(f[8]),loaded=u64(f[9]),receipt=u64(f[10]),
                       stage=u64(f[11]);
            const auto kg=i64(f[7]);
            if(!id || id>=(std::uint64_t{1}<<50) || !owner ||
               owner==kServerAuthority || !src || !carrier || !dst ||
               src==carrier || src==dst || carrier==dst || cargo>2 ||
               kg<1 || kg>1000 || !origin || loaded>result.tick_ ||
               stage>2 || (stage==0 && (loaded || receipt)) ||
               (stage==1 && receipt) || (stage==2 && !receipt) ||
               result.intercolony_freight_.size()>=10000 ||
               !result.intercolony_freight_.emplace(id,IntercolonyFreightLeg{
                   id,owner,src,carrier,dst,static_cast<ShuttleCargo>(cargo),
                   kg,origin,loaded,receipt,
                   static_cast<IntercolonyFreightStage>(stage)}).second)
                throw std::runtime_error("snapshot: invalid intercolony freight record");
        } else if(f[0]=="planetflightreceipt" && result.snapshot_version_>=81) {
            require_fields(f,2);
            const auto id=u64(f[1]);
            if(!id || result.shuttle_flight_request_ids_.size()>=100000 ||
               !result.shuttle_flight_request_ids_.insert(id).second)
                throw std::runtime_error("snapshot: invalid shuttle flight receipt");
        } else if(f[0]=="planetfamilyreceipt" &&
                   result.snapshot_version_>=116) {
            require_fields(f,2);
            const auto id=u64(f[1]);
            if(!id || id==std::numeric_limits<std::uint64_t>::max() ||
               result.planetary_family_booking_ids_.size()>=100000 ||
               !result.planetary_family_booking_ids_.insert(id).second)
                throw std::runtime_error("snapshot: duplicate family approval");
        } else if(f[0]=="planetsite" && result.snapshot_version_>=77) {
            require_fields(f,result.snapshot_version_>=110?60:
                result.snapshot_version_>=109?46:
                (result.snapshot_version_>=79?33:(result.snapshot_version_>=78?26:15)));
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                       rev=u64(f[4]),owner=u64(f[5]),ship=u64(f[6]);
            const auto mass=i64(f[7]),farm=u64(f[8]),extractor=u64(f[9]);
            const auto water=i64(f[10]),nutrients=i64(f[11]),
                       food=i64(f[12]),wastewater=i64(f[13]);
            const auto cycles=u64(f[14]);
            if(!body || !owner || !ship || rev!=1 || lat>=36 || lon>=72 ||
               farm>1 || extractor>1 ||
                (mass!=4+4*std::int64_t(farm+extractor) &&
                 !(result.snapshot_version_>=109 &&
                   mass==8+4*std::int64_t(farm+extractor)) &&
                 !(result.snapshot_version_>=110 &&
                   mass==12+4*std::int64_t(farm+extractor)) &&
                 !(result.snapshot_version_>=113 &&
                   mass>=4+4*std::int64_t(farm+extractor) &&
                   mass<=(result.snapshot_version_>=117?28:20)+
                   4*std::int64_t(farm+extractor) &&
                   (mass-4)%4==0)) ||
               water<0 || nutrients<0 || food<0 || wastewater<0 ||
               water>1000000 || nutrients>1000000 || food>1000000 ||
               wastewater>1000000 || result.planetary_sites_.size()>=10000 ||
               !result.planetary_sites_.emplace(
                   PlanetarySiteKey{body,std::uint16_t(lat),std::uint16_t(lon),1},
                   PlanetarySiteFacility{
                       planetary::PlanetaryRegionKey{body,1,std::uint16_t(lat),std::uint16_t(lon)},
                       owner,ship,mass,bool(farm),bool(extractor),
                       water,nutrients,food,wastewater,cycles}).second)
                throw std::runtime_error("snapshot: invalid planetary site");
            if(result.snapshot_version_>=78) {
                auto& site=result.planetary_sites_.at(
                    PlanetarySiteKey{body,std::uint16_t(lat),std::uint16_t(lon),1});
                const auto phase=u64(f[15]),progress=u64(f[16]),needed=u64(f[17]),
                           location=u64(f[18]),npc1=u64(f[19]),npc2=u64(f[20]),
                           workers=i64(f[21]),oxygen=i64(f[22]),co2=i64(f[23]),
                           organic=i64(f[24]),support=u64(f[25]);
                if(phase<1 || phase>3 || needed!=240 || progress>240 ||
                   !location || (workers!=2 && !(workers==0 && phase==3 &&
                     npc1==0 && npc2==0)) || oxygen<0 || co2<0 ||
                   organic<0 || oxygen>1000000 || co2>1000000 ||
                   organic>1000000 || (npc1==npc2 && npc1!=0) ||
                   ((npc1==0)!=(npc2==0)) ||
                   (workers==2 && (!npc1 || !npc2)) ||
                   (phase==3 && progress!=240))
                    throw std::runtime_error("snapshot: invalid planetary crew construction state");
                site.spaceport_phase=std::uint8_t(phase);
                site.construction_progress=progress;
                site.construction_required=needed;
                site.surface_location=location;
                site.delegated_npcs={{npc1,npc2}};
                site.assigned_workers=workers;
                site.oxygen_kg=oxygen;
                site.co2_kg=co2;
                site.organic_waste_kg=organic;
                site.worker_support_cycles=support;
                if(result.snapshot_version_>=79) {
                    const auto policy=u64(f[26]),condition=u64(f[27]),
                        last=u64(f[28]),done=u64(f[29]),stalled=u64(f[30]),
                        extracted=u64(f[31]),repaired=u64(f[32]);
                    if(policy>4 || condition>100 || last>result.tick_ ||
                       done>result.tick_ || stalled>result.tick_ ||
                       extracted%1000 || extracted/1000>done ||
                       repaired>done ||
                       ((policy==1 && !site.farm) ||
                        ((policy==2 || policy==3) && !site.extractor)) ||
                       (policy && (!site.delegated_npcs[0] ||
                                   !site.delegated_npcs[1] ||
                                   site.spaceport_phase!=3)))
                        throw std::runtime_error("snapshot: invalid NPC workforce policy");
                    site.workforce_policy=std::uint8_t(policy);
                    site.equipment_condition=std::uint8_t(condition);
                    site.last_work_tick=last;
                    site.autonomous_cycles=done;
                    site.stalled_cycles=stalled;
                    site.auto_extracted_kg=extracted;
                    site.repair_cycles=repaired;
                }
                if(result.snapshot_version_>=109) {
                    const auto modules=u64(f[33]),phase=u64(f[34]),
                        progress=u64(f[35]),order=u64(f[36]),
                        stalled=u64(f[37]),completed=u64(f[38]),
                        paused=u64(f[39]),supported=u64(f[40]),
                        shortages=u64(f[41]);
                    if(modules>2 || phase>2 || progress>80 ||
                       paused>1 || supported>result.tick_ ||
                       shortages>result.tick_ ||
                       (phase!=2 && (supported || shortages)) ||
                       (phase!=1 && paused) ||
                       stalled>result.tick_ || completed>result.tick_ ||
                       ((phase==0)!=(order==0)) ||
                       (phase==0 && (progress || completed)) ||
                       (phase==1 && completed) ||
                       (phase==2 && (progress!=80 || !completed)) ||
                       (phase!=0 && site.spaceport_phase!=3) ||
                       (result.snapshot_version_<110 &&
                        mass!=4+4*std::int64_t(farm+extractor)+(phase?4:0)))
                        throw std::runtime_error("snapshot: invalid planetary housing");
                    site.housing_modules=std::uint8_t(modules);
                    site.housing_phase=std::uint8_t(phase);
                    site.housing_progress=progress;
                    site.housing_order_id=order;
                    site.housing_stalled_ticks=stalled;
                    site.housing_completed_tick=completed;
                    site.housing_paused=paused==1;
                    site.housing_supported_cycles=supported;
                    site.housing_shortages=shortages;
                    const auto first=u64(f[42]),first_request=u64(f[43]),
                               second=u64(f[44]),second_request=u64(f[45]);
                    if((first==0)!=(first_request==0) ||
                       (second==0)!=(second_request==0) ||
                       (first && first==second) ||
                       (first_request && first_request==second_request) ||
                       ((first || second) && phase!=2) ||
                       (first && first!=site.delegated_npcs[0]) ||
                       (second && second!=site.delegated_npcs[1]))
                        throw std::runtime_error("snapshot: invalid founder residency");
                    site.founder_residents={{first,second}};
                    site.founder_consent_requests={{first_request,second_request}};
                    if(result.snapshot_version_>=110) {
                        const auto stage=u64(f[46]),built=u64(f[47]),
                            order_id=u64(f[48]),supervisor=u64(f[49]),
                            stalled=u64(f[50]),finished=u64(f[51]),
                            paused=u64(f[52]),energy=i64(f[53]),
                            brine=i64(f[54]),powered=u64(f[55]),
                            hvac=u64(f[56]),outages=u64(f[57]),
                            water_out=i64(f[58]),oxygen_out=i64(f[59]);
                        if(stage>2 || built>60 || paused>1 ||
                           energy<0 || energy>120 || brine<0 || brine>1000000 ||
                           water_out<0 || oxygen_out<0 ||
                           water_out>1000000 || oxygen_out>1000000 ||
                           powered>result.tick_ || hvac>result.tick_ ||
                           outages>result.tick_ || stalled>result.tick_ ||
                           finished>result.tick_ ||
                           ((stage==0)!=(order_id==0)) ||
                           (stage==0 && (built || supervisor || stalled ||
                                         finished || paused || energy || brine ||
                                         powered || hvac || outages ||
                                         water_out || oxygen_out)) ||
                           (stage!=0 && (!supervisor || phase!=2 ||
                                          order_id==site.housing_order_id)) ||
                           (stage==1 && (finished || built>=60)) ||
                           (stage==2 && (built!=60 || !finished || paused)) ||
                           (stage!=1 && paused) ||
                           (result.snapshot_version_<113 &&
                            mass!=4+4*std::int64_t(farm+extractor)+
                                  (phase?4:0)+(stage?4:0)) ||
                           (result.snapshot_version_>=113 &&
                            (mass<4+4*std::int64_t(farm+extractor)+
                                  (phase?4:0)+(stage?4:0) ||
                             mass>(result.snapshot_version_>=117?20:12)+
                                  4*std::int64_t(farm+extractor)+
                                  (phase?4:0)+(stage?4:0))))
                            throw std::runtime_error("snapshot: invalid surface utility");
                        site.utility_phase=std::uint8_t(stage);
                        site.utility_progress=built;
                        site.utility_order_id=order_id;
                        site.utility_supervisor=supervisor;
                        site.utility_stalled_ticks=stalled;
                        site.utility_completed_tick=finished;
                        site.utility_paused=paused!=0;
                        site.stored_electricity=energy;
                        site.brine_kg=brine;
                        site.utility_powered_cycles=powered;
                        site.hvac_cycles=hvac;
                        site.utility_outages=outages;
                        site.water_recycled_kg=water_out;
                        site.oxygen_recycled_kg=oxygen_out;
                    }
                }
            } else {
                // Existing v77 instant facilities retain old constructed
                // behavior; never retroactively strand legacy sites.
                auto& site=result.planetary_sites_.at(
                    PlanetarySiteKey{body,std::uint16_t(lat),std::uint16_t(lon),1});
                site.spaceport_phase=3;
                site.construction_progress=240;
                site.assigned_workers=0;
                site.surface_location=surface_custody_id(
                    PlanetarySiteKey{body,std::uint16_t(lat),std::uint16_t(lon),1});
            }
        } else if(f[0]=="planetlegacyimport" &&
                   result.snapshot_version_>=115) {
            if(f.size()<6 || result.legacy_planetary_residence_import_)
                throw std::runtime_error("snapshot: malformed or duplicate legacy import");
            LegacyPlanetaryResidenceImport m{};
            m.source_version=u64(f[1]);m.source_hash=u64(f[2]);
            m.host_request_id=u64(f[3]);m.recorded_tick=u64(f[4]);
            const auto size=u64(f[5]);
            if(m.source_version<86 || m.source_version>108 ||
               !m.source_hash || !m.host_request_id ||
               m.host_request_id==std::numeric_limits<std::uint64_t>::max() ||
               m.recorded_tick>result.tick_ || !size || size>10000)
                throw std::runtime_error("snapshot: invalid legacy import authority");
            require_fields(f,static_cast<std::size_t>(6+2*size));
            for(std::size_t j=0;j<size;++j) {
                const auto person=u64(f[6+2*j]),address=u64(f[7+2*j]);
                if(!person || !address ||
                   !m.legal_addresses.emplace(person,address).second)
                    throw std::runtime_error("snapshot: repeated legacy resident");
            }
            result.legacy_planetary_residence_import_=std::move(m);
        } else if(f[0]=="planetservice" &&
                   result.snapshot_version_>=117) {
            require_fields(f,14);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                revision=u64(f[4]),kind=u64(f[5]),phase=u64(f[6]),
                progress=u64(f[7]),order=u64(f[8]),supervisor=u64(f[9]),
                stalled=u64(f[10]),done=u64(f[11]),paused=u64(f[12]),
                powered=u64(f[13]);
            if(!body || lat>=36 || lon>=72 || revision!=1 || kind>1 ||
               phase<1 || phase>2 || progress>40 || !order || !supervisor ||
               paused>1 || stalled>result.tick_ || done>result.tick_ ||
               powered>result.tick_ ||
               (phase==1 && (done || progress>=40 || powered)) ||
               (phase==2 && (progress!=40 || !done || paused)))
                throw std::runtime_error("snapshot: invalid service project");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                std::uint16_t(lon),1};
            const auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               site->second.housing_phase!=2 ||
               site->second.utility_phase!=2 ||
               site->second.service_projects[kind].phase)
                throw std::runtime_error("snapshot: orphan or repeated service");
            auto& p=site->second.service_projects[kind];
            p.phase=std::uint8_t(phase);p.progress=progress;
            p.order_id=order;p.supervisor=supervisor;
            p.stalled_ticks=stalled;p.completed_tick=done;
            p.paused=paused!=0;p.powered_cycles=powered;
        } else if(f[0]=="planetservicehandover" &&
                   result.snapshot_version_>=117) {
            require_fields(f,10);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                revision=u64(f[4]),kind=u64(f[5]),id=u64(f[6]),
                when=u64(f[7]),previous=u64(f[8]),replacement=u64(f[9]);
            if(!body || lat>=36 || lon>=72 || revision!=1 || kind>1 ||
               !id || !when || when>result.tick_ || !previous ||
               !replacement || previous==replacement)
                throw std::runtime_error("snapshot: invalid service handover");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                std::uint16_t(lon),1};
            const auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               !site->second.service_projects[kind].phase ||
               site->second.service_handovers[kind].size()>=16)
                throw std::runtime_error("snapshot: orphan or overflowed service handover");
            auto& chain=site->second.service_handovers[kind];
            if(!chain.empty() &&
               (chain.back().tick>=when ||
                chain.back().replacement!=previous))
                throw std::runtime_error("snapshot: broken service succession");
            chain.push_back({id,when,previous,replacement});
        } else if(f[0]=="planetproduction" &&
                   result.snapshot_version_>=113) {
            require_fields(f,13);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                       revision=u64(f[4]),kind=u64(f[5]),phase=u64(f[6]),
                       progress=u64(f[7]),order=u64(f[8]),builder=u64(f[9]),
                       stalled=u64(f[10]),done=u64(f[11]),paused=u64(f[12]);
            if(!body || lat>=36 || lon>=72 || revision!=1 || kind>1 ||
               phase<1 || phase>2 || progress>40 || !order || !builder ||
               paused>1 || stalled>result.tick_ || done>result.tick_ ||
               (phase==1 && (done || progress>=40)) ||
               (phase==2 && (progress!=40 || !done || paused)))
                throw std::runtime_error("snapshot: invalid production build state");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                                       std::uint16_t(lon),1};
            const auto it=result.planetary_sites_.find(key);
            if(it==result.planetary_sites_.end() ||
               it->second.housing_phase!=2 || it->second.utility_phase!=2 ||
               it->second.production_projects[kind].phase)
                throw std::runtime_error("snapshot: orphan or duplicate production build");
            auto& p=it->second.production_projects[kind];
            p.phase=std::uint8_t(phase);
            p.progress=progress;p.order_id=order;p.supervisor=builder;
            p.stalled_ticks=stalled;p.completed_tick=done;p.paused=paused!=0;
        } else if(f[0]=="planetproductionhandover" &&
                   result.snapshot_version_>=114) {
            require_fields(f,10);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                revision=u64(f[4]),kind=u64(f[5]),id=u64(f[6]),
                when=u64(f[7]),previous=u64(f[8]),replacement=u64(f[9]);
            if(!body || lat>=36 || lon>=72 || revision!=1 || kind>1 ||
               !id || !when || when>result.tick_ || !previous ||
               !replacement || previous==replacement)
                throw std::runtime_error("snapshot: invalid production handover fields");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                                       std::uint16_t(lon),1};
            const auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               !site->second.production_projects[kind].phase ||
               site->second.production_handovers[kind].size()>=16)
                throw std::runtime_error("snapshot: orphan production handover");
            auto& history=site->second.production_handovers[kind];
            if(!history.empty() &&
               (history.back().tick>=when ||
                history.back().replacement!=previous))
                throw std::runtime_error("snapshot: broken production handover sequence");
            history.push_back({id,when,previous,replacement});
        } else if(f[0]=="planetutilityhandover" &&
                   result.snapshot_version_>=112) {
            require_fields(f,9);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                       revision=u64(f[4]),id=u64(f[5]),when=u64(f[6]),
                       previous=u64(f[7]),replacement=u64(f[8]);
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                                       std::uint16_t(lon),1};
            const auto site=result.planetary_sites_.find(key);
            if(!body || !id || !when || when>result.tick_ ||
               !previous || !replacement || previous==replacement ||
               lat>=36 || lon>=72 || revision!=1 ||
               site==result.planetary_sites_.end() ||
               !site->second.utility_phase ||
               site->second.utility_handovers.size()>=16 ||
               (!site->second.utility_handovers.empty() &&
                (site->second.utility_handovers.back().tick>=when ||
                 site->second.utility_handovers.back().replacement!=previous)))
                throw std::runtime_error("snapshot: invalid supervisor handover");
            site->second.utility_handovers.push_back({
                id,when,previous,replacement});
        } else if(f[0]=="planethousehold" &&
                    result.snapshot_version_>=118) {
            if(f.size()<16)
                throw std::runtime_error("snapshot: truncated legal household");
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                rev=u64(f[4]),guardian=u64(f[5]),parent=u64(f[6]),
                booking=u64(f[7]),arrival=u64(f[8]),
                admission=u64(f[9]),at=u64(f[10]),
                returned=u64(f[11]),returned_at=u64(f[12]),
                consent=u64(f[13]),count=u64(f[14]);
            if(!body || lat>=36 || lon>=72 || rev!=1 ||
               !guardian || !parent || guardian==parent ||
               !booking || !arrival || !admission || !at ||
               at>result.tick_ || consent!=3 || count!=1 ||
               (returned==0)!=(returned_at==0) ||
               (returned && (returned==booking || returned==arrival ||
                             returned==admission || returned_at<at ||
                             returned_at>result.tick_)) ||
               f.size()!=15+count)
                throw std::runtime_error("snapshot: invalid legal household receipt");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                std::uint16_t(lon),1};
            auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               site->second.housing_phase!=2 ||
               site->second.service_projects[0].phase!=2 ||
               site->second.households.size()>=32)
                throw std::runtime_error("snapshot: no physical legal household site");
            PlanetaryHouseholdResidence house{};
            house.co_parent_id=parent;house.booking_request=booking;
            house.arrival_receipt=arrival;house.admission_request=admission;
            house.admission_tick=at;house.return_request=returned;
            house.return_tick=returned_at;
            house.consent_mask=std::uint8_t(consent);
            for(std::size_t i=0;i<count;++i){
                const auto child=u64(f[15+i]);
                if(child<(1ULL<<62))
                    throw std::runtime_error("snapshot: malformed real child ID");
                house.child_ids.push_back(child);
            }
            if(!site->second.households.emplace(guardian,std::move(house)).second)
                throw std::runtime_error("snapshot: repeated household admission");
        } else if(f[0]=="planetsettler" && result.snapshot_version_>=111) {
            require_fields(f,9);
            const auto body=u64(f[1]),lat=u64(f[2]),lon=u64(f[3]),
                       rev=u64(f[4]),npc=u64(f[5]),arrival=u64(f[6]),
                       admission=u64(f[7]),departure=u64(f[8]);
            if(!body || rev!=1 || lat>=36 || lon>=72 || !npc ||
               !arrival || !admission || arrival==admission ||
               (departure && (departure==arrival || departure==admission)))
                throw std::runtime_error("snapshot: invalid visitor residence receipt");
            const PlanetarySiteKey key{body,std::uint16_t(lat),
                                       std::uint16_t(lon),1};
            auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               site->second.housing_phase!=2 ||
               site->second.visitor_residency.size()>=10000 ||
               !site->second.visitor_residency.emplace(npc,
                   std::array<std::uint64_t,3>{arrival,admission,departure}).second)
                throw std::runtime_error("snapshot: duplicate visitor residence");
        } else if(f[0]=="oasisextractor" && result.snapshot_version_>=76) {
            require_fields(f,8);
            const auto ship=u64(f[1]),body=u64(f[2]),mass=i64(f[3]);
            const auto workers=u64(f[4]),condition=u64(f[5]),
                       energy=u64(f[6]),cycles=u64(f[7]);
            if(!ship || !body || mass!=4 || workers!=2 || condition>100 ||
               energy>120 || result.oasis_extractors_.size()>=10000 ||
               !result.oasis_extractors_.emplace(ship,OasisExtractor{
                   ship,body,mass,static_cast<std::uint8_t>(workers),
                   static_cast<std::uint8_t>(condition),
                   static_cast<std::uint16_t>(energy),cycles}).second)
                throw std::runtime_error("snapshot: invalid extractor record");
        } else if(f[0]=="oasisreserve" && result.snapshot_version_>=75) {
            require_fields(f,6);
            const auto body=u64(f[1]);
            const auto water=i64(f[2]),nutrients=i64(f[3]);
            const auto initial_water=i64(f[4]),initial_nutrients=i64(f[5]);
            if(!body || water<0 || nutrients<0 || initial_water<1000 ||
               initial_nutrients<1000 || initial_water>100000000 ||
               initial_nutrients>100000000 ||
               (water+nutrients)%1000 ||
               (initial_water+initial_nutrients)%1000 ||
               water>initial_water || nutrients>initial_nutrients ||
               result.oasis_reserves_.size()>=10000 ||
               !result.oasis_reserves_.emplace(body,PhysicalOasisReserve{
                   water,nutrients,initial_water,initial_nutrients}).second)
                throw std::runtime_error("snapshot: invalid oasis source");
        } else if(f[0]=="localenvironment" && result.snapshot_version_>=74) {
            require_fields(f,6);
            const auto body=u64(f[1]),zone=u64(f[2]),hazard=u64(f[3]);
            const auto first=u64(f[4]),second=u64(f[5]);
            if(!body||zone<1||zone>5||hazard>3||first>2||second>2||
               first==second||result.local_environments_.size()>=10000||
               !result.local_environments_.emplace(body,LocalCelestialEnvironment{
                   static_cast<std::uint8_t>(zone),
                   static_cast<std::uint8_t>(hazard),
                   static_cast<AsteroidClass>(first),
                   static_cast<AsteroidClass>(second)}).second)
                throw std::runtime_error("snapshot: invalid local environment");
        } else if(f[0]=="localregion" && result.snapshot_version_>=73) {
            require_fields(f,4);
            const auto body=u64(f[1]),a=u64(f[2]),b=u64(f[3]);
            if(!body || ((a==0)!=(b==0)) || (a && a>=b) ||
               !result.generated_local_regions_.emplace(body,std::array<EntityId,2>{a,b}).second ||
               result.generated_local_regions_.size()>10000)
                throw std::runtime_error("snapshot: duplicate or invalid local region");
        } else if(f[0]=="localanchor" && result.snapshot_version_>=73) {
            require_fields(f,3);
            const auto entity=u64(f[1]),body=u64(f[2]);
            if(!entity || !body || result.local_entity_anchors_.size()>=20000 ||
               !result.local_entity_anchors_.emplace(entity,body).second)
                throw std::runtime_error("snapshot: invalid local entity anchor");
        } else if(f[0]=="frametransit_pending" && result.snapshot_version_>=72) {
            require_fields(f,9);
            const auto id=u64(f[1]),owner=u64(f[2]),ship=u64(f[3]);
            const auto system=u64(f[4]),source=u64(f[5]),destination=u64(f[6]);
            const auto started=u64(f[7]),due=u64(f[8]);
            if(!id||!owner||owner==kServerAuthority||!ship||!system||
               system>std::numeric_limits<std::uint32_t>::max()||!destination||
               due<started||due-started!=4||started>result.tick_||
               result.pending_celestial_transits_.size()>=10000||
               !result.pending_celestial_transits_.emplace(id,PendingCelestialTransit{
                   owner,ship,static_cast<std::uint32_t>(system),
                   source,destination,started,due}).second)
                throw std::runtime_error("snapshot: invalid pending frame transit");
        } else if(f[0]=="frametransit_done" && result.snapshot_version_>=72) {
            require_fields(f,7);
            const auto id=u64(f[1]),owner=u64(f[2]),ship=u64(f[3]);
            const auto system=u64(f[4]),destination=u64(f[5]),completed=u64(f[6]);
            if(!id||!owner||owner==kServerAuthority||!ship||!system||
               system>std::numeric_limits<std::uint32_t>::max()||!destination||
               completed>result.tick_ ||
               result.completed_celestial_transits_.size()>=10000||
               !result.completed_celestial_transits_.emplace(id,CompletedCelestialTransit{
                   owner,ship,static_cast<std::uint32_t>(system),
                   destination,completed}).second)
                throw std::runtime_error("snapshot: invalid completed frame transit");
        } else if(f[0]=="shipframe" && result.snapshot_version_>=71) {
            require_fields(f,7);
            const auto ship=u64(f[1]),system=u64(f[2]),anchor=u64(f[3]);
            const auto x=i64(f[4]),y=i64(f[5]),z=i64(f[6]);
            if(!ship || !system || system>std::numeric_limits<std::uint32_t>::max() ||
               std::abs(static_cast<long double>(x))>kCosmicLimitMm ||
               std::abs(static_cast<long double>(y))>kCosmicLimitMm ||
               std::abs(static_cast<long double>(z))>kCosmicLimitMm ||
               (anchor==0 && (x||y||z)) ||
               !result.ship_frames_.emplace(ship,ShipFrameCustody{
                   static_cast<std::uint32_t>(system),anchor,{x,y,z}}).second)
                throw std::runtime_error("snapshot: invalid or duplicate ship frame");
        } else if(f[0]=="celestial" && result.snapshot_version_>=70) {
            require_fields(f,10);
            const auto system=u64(f[1]),id=u64(f[2]),kind=u64(f[3]),parent=u64(f[4]);
            const auto orbit=u64(f[5]),x=i64(f[6]),y=i64(f[7]),z=i64(f[8]),radius=u64(f[9]);
            if(!system||system>std::numeric_limits<std::uint32_t>::max()||
               !id||kind>3||!radius||radius>kCosmicLimitMm||
               orbit>300000||
               std::abs(static_cast<long double>(x))>kCosmicLimitMm||
               std::abs(static_cast<long double>(y))>kCosmicLimitMm||
               std::abs(static_cast<long double>(z))>kCosmicLimitMm)
                throw std::runtime_error("snapshot: invalid celestial geometry");
            const auto sid=static_cast<std::uint32_t>(system);
            auto& bodies=result.physical_celestial_systems_[sid];
            if(bodies.size()>=50)throw std::runtime_error("snapshot: too many celestial bodies");
            if(!bodies.empty()&&bodies.back().id>=id)
                throw std::runtime_error("snapshot: celestial IDs out of order");
            bodies.push_back({id,parent,sid,static_cast<CelestialKind>(kind),
                orbit,{x,y,z},radius});
        } else if(f[0]=="probelatency" && result.snapshot_version_>=68) {
            require_fields(f,2);
            const auto n=u64(f[1]);
            if(n<2||n>500)throw std::runtime_error("snapshot: invalid probe response latency");
            result.probe_report_delay_ticks_=n;
        } else if(f[0]=="pendingprobe" && result.snapshot_version_>=68) {
            require_fields(f,11);
            PendingProbeDiscovery pending{};
            auto& d=pending.report;
            d.owner=u64(f[1]);d.site=u64(f[2]);
            const auto system=u64(f[3]);
            d.location={i64(f[4]),i64(f[5])};
            d.first_tick=u64(f[6]);
            const auto geology=u64(f[7]);
            pending.carrier=u64(f[8]);pending.probe_id=u64(f[9]);
            pending.ready_tick=u64(f[10]);
            d.geological=geology==1;
            if(!d.owner||!d.site||d.site>=result.next_entity_id_||
               !system||system>std::numeric_limits<std::uint32_t>::max()||
               !d.first_tick||d.first_tick>result.tick_||
               !pending.carrier||!pending.probe_id||geology>1||
               pending.ready_tick<=d.first_tick||
               pending.ready_tick-d.first_tick>500||
               !result.valid_position(d.location)||
               result.pending_probe_discoveries_.size()>=10000)
                throw std::runtime_error("snapshot: invalid pending probe observation");
            d.system=static_cast<std::uint32_t>(system);
            if(!result.pending_probe_discoveries_.emplace(std::make_pair(d.owner,d.site),pending).second)
                throw std::runtime_error("snapshot: duplicate pending probe observation");
        } else if(f[0]=="surveyprobe" && result.snapshot_version_>=65){
            require_fields(f,result.snapshot_version_>=69?17:result.snapshot_version_>=66?14:8);
            DeployedSurveyProbe p{};
            p.id=u64(f[1]);p.carrier=u64(f[2]);p.owner=u64(f[3]);
            const auto system=u64(f[4]);p.position={i64(f[5]),i64(f[6])};
            p.launched_tick=u64(f[7]);
            if(result.snapshot_version_>=66){
                const auto phase=u64(f[8]),sample=u64(f[12]),taxonomy=u64(f[13]);
                p.drone_target=u64(f[9]);
                p.drone_home={i64(f[10]),i64(f[11])};
                if(phase>3 || sample>0 || taxonomy>3)
                    throw std::runtime_error("snapshot: invalid geological drone state");
                p.sample_ore=static_cast<std::int64_t>(sample);
                p.drone_phase=static_cast<std::uint8_t>(phase);
                p.taxonomy=static_cast<std::uint8_t>(taxonomy);
                if((phase==0 && (p.drone_target || sample || taxonomy || p.drone_home.x || p.drone_home.y)) ||
                   (phase!=0 && (!p.drone_target ||
                    !result.valid_position(p.drone_home) ||
                    (sample!=0))))
                    throw std::runtime_error("snapshot: invalid geological drone state");
            }
            if(result.snapshot_version_>=69){
                p.focus_site=u64(f[14]);
                p.focus_waypoint={i64(f[15]),i64(f[16])};
                if((p.focus_site && (p.drone_phase||
                       p.focus_site>=result.next_entity_id_||
                       !result.valid_position(p.focus_waypoint)))||
                   (!p.focus_site && (p.focus_waypoint.x||p.focus_waypoint.y)))
                    throw std::runtime_error("snapshot: invalid probe focus");
            }
            if(p.focus_site){
                const auto site=result.entities_.find(p.focus_site);
                if(site!=result.entities_.end()){
                    if(site->second.type!=EntityType::Asteroid||
                       site->second.system_id!=system)
                        throw std::runtime_error("snapshot: focused probe site mismatch");
                    const auto quant=[](std::int64_t value,std::int64_t quantum){
                        return (value>=0 ? (value+quantum/2)/quantum :
                            -((-value+quantum/2)/quantum))*quantum;
                    };
                    const Vector2 coarse{quant(site->second.position.x,5000000),
                                         quant(site->second.position.y,5000000)};
                    const Vector2 refined{quant(site->second.position.x,25000),
                                          quant(site->second.position.y,25000)};
                    const bool coarse_match=p.focus_waypoint.x==coarse.x&&
                                            p.focus_waypoint.y==coarse.y;
                    const bool refined_match=p.focus_waypoint.x==refined.x&&
                                             p.focus_waypoint.y==refined.y;
                    if(!coarse_match&&!refined_match)
                        throw std::runtime_error("snapshot: forged probe waypoint");
                }
            }
            if(!p.id || !p.carrier || !p.owner ||
               !system || system>std::numeric_limits<std::uint32_t>::max() ||
               !p.launched_tick || p.launched_tick>result.tick_ ||
               !result.valid_position(p.position) ||
               result.survey_probes_.size()>=1000)
                throw std::runtime_error("snapshot: invalid survey probe record");
            p.system=static_cast<std::uint32_t>(system);
            if(!result.survey_probes_.emplace(p.id,p).second)
                throw std::runtime_error("snapshot: duplicate survey probe");
        } else if(f[0]=="discovery" && result.snapshot_version_>=63){
            require_fields(f,result.snapshot_version_>=67?9:8);
            DiscoveryRecord d{};
            d.owner=u64(f[1]);d.site=u64(f[2]);
            const auto system=u64(f[3]),geo=u64(f[7]);
            d.location={i64(f[4]),i64(f[5])};
            d.first_tick=u64(f[6]);d.geological=geo==1;
            if(result.snapshot_version_>=67){
                const auto klass=u64(f[8]);
                if(klass>3 || (klass && !d.geological))
                    throw std::runtime_error("snapshot: invalid survey report");
                d.survey_taxonomy=static_cast<std::uint8_t>(klass);
            }
            if(!d.owner || !d.site || d.site>=result.next_entity_id_ ||
               !system || system>std::numeric_limits<std::uint32_t>::max() ||
               !d.first_tick || d.first_tick>result.tick_ || geo>1 ||
               !result.valid_position(d.location) || result.discoveries_.size()>=10000)
                throw std::runtime_error("snapshot: invalid discovery");
            d.system=static_cast<std::uint32_t>(system);
            if(!result.discoveries_.emplace(std::make_pair(d.owner,d.site),d).second)
                throw std::runtime_error("snapshot: duplicate discovery");
        } else if(f[0]=="shipstatus" && result.snapshot_version_>=100) {
            if(f.size()<11)throw std::runtime_error("snapshot: truncated ship status");
            const auto count=u64(f[10]);
            if(count==0 || count>ship_status::Engine::kMaxAffectedModules)
                throw std::runtime_error("snapshot: invalid status slot count");
            require_fields(f,11+3*static_cast<std::size_t>(count));
            parsed_status<<"C";
            for(std::size_t j=1;j<f.size();++j)parsed_status<<' '<<f[j];
            parsed_status<<'\n';
        } else if(f[0]=="npccyber" && result.snapshot_version_>=102) {
            require_fields(f,8);
            NpcCyberOperator order{};
            order.command_id=u64(f[1]);
            order.owner=u64(f[2]);
            order.attacking_ship=u64(f[3]);
            order.pilot_npc=u64(f[4]);
            order.observer_npc=u64(f[5]);
            order.target_ship=u64(f[6]);
            order.admitted_tick=u64(f[7]);
            if(!order.command_id || !order.owner ||
               !order.attacking_ship || !order.target_ship ||
               !order.pilot_npc || !order.observer_npc ||
               order.attacking_ship==order.target_ship ||
               order.admitted_tick>result.tick_ ||
               result.npc_cyber_operators_.size()>=10000 ||
               !result.npc_cyber_operators_.emplace(
                   order.command_id,order).second)
                throw std::runtime_error("snapshot: invalid NPC cyber operator receipt");
        } else if (f[0] == "entity") {
            require_fields(f, result.snapshot_version_>=119 ? 43 : result.snapshot_version_>=101 ? 41 : result.snapshot_version_>=100 ? 38 : result.snapshot_version_>=91 ? 36 : result.snapshot_version_>=65 ? 35 : result.snapshot_version_>=64 ? 34 : result.snapshot_version_>=62 ? 27 : result.snapshot_version_>=61 ? 23 : result.snapshot_version_>=5 ? 22 : result.snapshot_version_ >= 3 ? 15 : 12);
            Entity e;
            e.id = u64(f[1]);
            const auto es = u64(f[2]), type = u64(f[3]);
            if ((result.snapshot_version_<5 && es!=sys) || es==0 || es>std::numeric_limits<std::uint32_t>::max() ||
                type > (result.snapshot_version_>=21?4U:result.snapshot_version_>=5?3U:2U) || e.id == 0 || e.id >= result.next_entity_id_)
                throw std::runtime_error("snapshot: invalid entity header");
            e.system_id = static_cast<std::uint32_t>(es);
            e.type = static_cast<EntityType>(type);
            e.owner_id = u64(f[4]);
            e.position = {i64(f[5]), i64(f[6])};
            e.velocity = {i64(f[7]), i64(f[8])};
            e.ore = i64(f[9]);
            e.cargo = i64(f[10]);
            e.cargo_capacity = i64(f[11]);
            if (result.snapshot_version_ >= 3) {
                const auto state = u64(f[12]), ticks = u64(f[14]);
                if (state > static_cast<unsigned>(DockState::Docking) || ticks > kDockTransitionTicks)
                    throw std::runtime_error("snapshot: invalid dock state/countdown");
                e.dock_state = static_cast<DockState>(state);
                e.dock_station = u64(f[13]);
                e.transition_ticks = static_cast<std::uint32_t>(ticks);
            }
            if(result.snapshot_version_>=5){
                const auto js=u64(f[15]), gate_dest=u64(f[18]),online=u64(f[21]);
                if(js>2 || u64(f[17])>kArrivalCloakTicks || gate_dest>std::numeric_limits<std::uint32_t>::max() || online>1)
                    throw std::runtime_error("snapshot: invalid jump/gate record");
                e.jump_state=static_cast<ShipJumpState>(js);e.pending_gate=u64(f[16]);
                e.jump_ticks=static_cast<std::uint32_t>(u64(f[17]));
                e.gate_destination=static_cast<std::uint32_t>(gate_dest);
                e.gate_arrival={i64(f[19]),i64(f[20])};e.gate_online=online!=0;
            }
            if(result.snapshot_version_>=61){const auto h=u64(f[22]);if(h>=360)throw std::runtime_error("snapshot: invalid heading");e.heading_deg=static_cast<std::uint16_t>(h);}
            if(result.snapshot_version_>=62){
                const auto drone=u64(f[26]);
                if(drone>1)throw std::runtime_error("snapshot: invalid combat training flag");
                e.hull=i64(f[23]);e.weapon_charges=i64(f[24]);
                e.last_weapon_tick=u64(f[25]);e.training_drone=drone!=0;
                if(e.hull<0 || e.hull>100 || e.weapon_charges<0 || e.weapon_charges>12 ||
                   e.last_weapon_tick>result.tick_ ||
                   (e.type!=EntityType::Ship &&
                     (e.hull || e.weapon_charges || e.training_drone || e.last_weapon_tick)) ||
                   (e.type==EntityType::Ship &&
                     (e.hull<1 || (e.training_drone && (e.owner_id!=8 ||
                        e.weapon_charges!=0 || result.snapshot_version_<62)))))
                    throw std::runtime_error("snapshot: invalid combat state");
            }else if(e.type==EntityType::Ship){e.hull=100;e.weapon_charges=12;}
            if(result.snapshot_version_>=64){
                for(int j=0;j<7;++j){
                    const auto value=u64(f[27+j]);
                    const unsigned maximum=j<2?
                        (result.snapshot_version_>=101?3u:
                         result.snapshot_version_>=65?2u:1u):
                        (result.snapshot_version_>=103 && j<4?3u:
                         result.snapshot_version_>=101 && j<4?1u:0u);
                    if(value>maximum)
                        throw std::runtime_error("snapshot: unavailable ship module type");
                    if(e.type!=EntityType::Ship && e.type!=EntityType::Wreck && value)
                        throw std::runtime_error("snapshot: modules installed on nonship");
                    if(j<2)e.fitting.hardpoints[j]=static_cast<std::uint8_t>(value);
                    else if(j<4)e.fitting.support[j-2]=static_cast<std::uint8_t>(value);
                    else if(j<6)e.fitting.subsystems[j-4]=static_cast<std::uint8_t>(value);
                    else e.fitting.rigging[0]=static_cast<std::uint8_t>(value);
                }
                if(result.snapshot_version_>=65){
                    const auto count=u64(f[34]);
                    if(count>8 || (e.type!=EntityType::Ship && e.type!=EntityType::Wreck && count))
                        throw std::runtime_error("snapshot: invalid probe inventory");
                    e.survey_probes_carried=static_cast<std::uint8_t>(count);
                    if(e.fitting.hardpoints[0]==2 && e.fitting.hardpoints[1]==2)
                        throw std::runtime_error("snapshot: duplicate probe launcher");
                if(result.snapshot_version_>=101 &&
                   ((e.fitting.hardpoints[0]==3 && e.fitting.hardpoints[1]==3) ||
                    (e.fitting.support[0]!=0 &&
                     e.fitting.support[0]==e.fitting.support[1])))
                    throw std::runtime_error("snapshot: duplicated physical cyber equipment");
                }
                if(e.fitting.hardpoints[0]==1 && e.fitting.hardpoints[1]==1)
                    throw std::runtime_error("snapshot: duplicate unique exploration scanner");
                if(e.training_drone && e.fitting.exploration_scanner())
                    throw std::runtime_error("snapshot: drone has colony survey scanner");
            }
            if(result.snapshot_version_>=100){
                for(std::size_t slot=0;slot<2;++slot){
                    const auto requested=u64(f[36+slot]);
                    if(requested>1 || (!e.fitting.hardpoints[slot] && !requested))
                        throw std::runtime_error("snapshot: invalid hardpoint intent");
                    e.hardpoint_requested_on[slot]=requested!=0;
                }
            }
            if(result.snapshot_version_>=101){
                for(std::size_t slot=0;slot<2;++slot){
                    const auto bit=u64(f[38+slot]);
                    if(bit>1 || (bit && !e.fitting.hardpoints[slot]))
                        throw std::runtime_error("snapshot: forged cyber isolation");
                    e.hardpoint_isolated[slot]=bit!=0;
                }
                e.last_hack_tick=u64(f[40]);
                if(e.last_hack_tick>result.tick_ ||
                   (e.last_hack_tick && e.type!=EntityType::Ship &&
                    e.type!=EntityType::Wreck))
                    throw std::runtime_error("snapshot: forged cyber emission receipt");
            }
            if(result.snapshot_version_>=91){
                e.manufactured_order_id=u64(f[35]);
                if(e.manufactured_order_id>=(std::uint64_t{1}<<50) ||
                   (e.manufactured_order_id && e.type!=EntityType::Ship &&
                    e.type!=EntityType::Wreck))
                    throw std::runtime_error("snapshot: invalid manufactured hull identity");
            }
            if(result.snapshot_version_>=119){
                e.position_z_mm=i64(f[41]);
                e.velocity_z_mm_per_tick=i64(f[42]);
                if((e.type!=EntityType::Ship &&
                   (e.position_z_mm || e.velocity_z_mm_per_tick)) ||
                   e.velocity_z_mm_per_tick < -kMaxVelocityMmPerTick ||
                   e.velocity_z_mm_per_tick > kMaxVelocityMmPerTick)
                    throw std::runtime_error("snapshot: invalid 3D physical axis");
            }
            if ((result.snapshot_version_<119 || e.type!=EntityType::Ship) &&
                !result.valid_position(e.position) ||
                e.velocity.x < -kMaxVelocityMmPerTick || e.velocity.x > kMaxVelocityMmPerTick ||
                e.velocity.y < -kMaxVelocityMmPerTick || e.velocity.y > kMaxVelocityMmPerTick ||
                e.ore < 0 || e.cargo < 0 || e.cargo_capacity < 0 ||
                (e.type == EntityType::Ship && (e.owner_id == 0 || e.ore != 0 || e.cargo_capacity == 0 || e.cargo > e.cargo_capacity)) ||
                (e.type == EntityType::Asteroid && (e.owner_id != 0 || e.cargo != 0 || e.cargo_capacity != 0)) ||
                (e.type == EntityType::Wreck && (e.owner_id == 0 || e.ore != 0 || e.cargo_capacity == 0 || e.cargo > e.cargo_capacity)) ||
                (e.type == EntityType::Station && ((result.snapshot_version_<7 && e.owner_id != 0) || e.ore != 0 || e.cargo != 0 || e.cargo_capacity != 0)) ||
                (e.type != EntityType::Ship && (e.velocity.x != 0 || e.velocity.y != 0)) ||
                (e.type != EntityType::Ship && (e.dock_state != DockState::InSpace || e.dock_station != 0 || e.transition_ticks != 0)) ||
                (e.type==EntityType::Stargate && (e.owner_id!=0 || e.ore || e.cargo || e.cargo_capacity ||
                      !e.gate_destination || e.gate_destination==e.system_id ||
                      !result.valid_position(e.gate_arrival))) ||
                (e.type!=EntityType::Stargate && (e.gate_destination || e.gate_arrival.x || e.gate_arrival.y || e.gate_online)) ||
                (e.type!=EntityType::Ship && (e.jump_state!=ShipJumpState::Ready || e.pending_gate || e.jump_ticks)) ||
                (e.type==EntityType::Ship && ((e.jump_state==ShipJumpState::Ready && (e.pending_gate || e.jump_ticks)) ||
                     (e.jump_state==ShipJumpState::Jumping && (!e.pending_gate || !e.jump_ticks ||
                      e.jump_ticks>kJumpTransitTicks || e.dock_state!=DockState::InSpace ||
                      e.velocity.x || e.velocity.y)) ||
                     (e.jump_state==ShipJumpState::ArrivalCloak && (e.pending_gate || !e.jump_ticks ||
                      e.jump_ticks>kArrivalCloakTicks || e.dock_state!=DockState::InSpace ||
                      e.velocity.x || e.velocity.y)))) ||
                (e.type == EntityType::Ship &&
                 ((e.dock_state == DockState::InSpace && (e.dock_station != 0 || e.transition_ticks != 0)) ||
                  (e.dock_state == DockState::Docked && (e.dock_station == 0 || e.transition_ticks != 0)) ||
                  ((e.dock_state == DockState::Docking || e.dock_state == DockState::Undocking) &&
                   (e.dock_station == 0 || e.transition_ticks == 0)) ||
                  (e.dock_state != DockState::InSpace && (e.velocity.x != 0 || e.velocity.y != 0)))))
                throw std::runtime_error("snapshot: invalid entity state");
            if (!result.entities_.emplace(e.id, e).second) throw std::runtime_error("snapshot: duplicate entity");
        } else if (f[0]=="gateops" && result.snapshot_version_>=6) {
            require_fields(f,8);
            const auto a=u64(f[1]),b=u64(f[2]);
            const auto cond=i64(f[3]),parts=i64(f[4]),tech=i64(f[5]);
            const auto cycles=u64(f[6]),used=u64(f[7]);
            if(!a || a>=b || cond<0 || cond>1000 || parts<0 || parts>1000000 ||
               tech<0 || tech>16 || used>cycles ||
               !result.gate_ops_.emplace(std::make_pair(a,b),GateOperations{cond,parts,tech,cycles,used}).second)
                throw std::runtime_error("snapshot: invalid gate maintenance");
        } else if(f[0]=="colony" && result.snapshot_version_>=7){
            require_fields(f,5);
            const auto id=u64(f[1]);auto crew=i64(f[2]),refined=i64(f[3]),modules=i64(f[4]);
            if(!id || crew<(result.snapshot_version_>=36?0:1) || crew>1000 || refined<0 || modules<0 ||
               refined>1000000000LL || modules>500000000LL ||
               !result.colony_ships_.emplace(id,ColonyShip{crew,refined,modules,{},{},0}).second)
                throw std::runtime_error("snapshot: invalid colony inventory");
        } else if(f[0]=="outpost" && result.snapshot_version_>=7){
            require_fields(f,4);
            const auto id=u64(f[1]),source=u64(f[2]);const auto mass=i64(f[3]);
            if(!id || !source || mass!=8 ||
               !result.built_outposts_.emplace(id,BuiltOutpost{source,mass}).second)
                throw std::runtime_error("snapshot: invalid outpost");
        } else if(f[0]=="workshop" && result.snapshot_version_>=22){
            require_fields(f,result.snapshot_version_>=23?19:12);
            const auto id=u64(f[1]);OutpostWorkshop w{};
            w.source_ship=u64(f[2]);w.embodied_ore=i64(f[3]);
            w.aluminum_kg=i64(f[4]);w.panels_kg=i64(f[5]);w.scrap_kg=i64(f[6]);
            w.reserved_aluminum_kg=i64(f[7]);w.ticks_remaining=static_cast<std::uint32_t>(u64(f[8]));
            w.completed_batches=u64(f[9]);w.elapsed_ticks=u64(f[10]);w.operator_npc_id=u64(f[11]);
            if(result.snapshot_version_>=23){
                const auto installed=u64(f[12]);
                if(installed>1)throw std::runtime_error("snapshot: invalid finisher installation");
                w.finisher_installed=installed==1;
                w.finisher_npc_id=u64(f[13]);w.reserved_panels_kg=i64(f[14]);
                w.finisher_ticks_remaining=static_cast<std::uint32_t>(u64(f[15]));
                w.frames_kg=i64(f[16]);w.finisher_batches=u64(f[17]);
                w.finisher_elapsed_ticks=u64(f[18]);
            }
            if(!id || !w.source_ship || w.embodied_ore!=(w.finisher_installed?4:2) ||
               w.aluminum_kg<0 || w.aluminum_kg>1000 || w.panels_kg<0 || w.panels_kg>1000 ||
               w.scrap_kg<0 || w.scrap_kg>1000 ||
               (w.reserved_aluminum_kg!=0 && w.reserved_aluminum_kg!=20) ||
               (w.reserved_aluminum_kg==0 && w.ticks_remaining!=0) ||
               w.ticks_remaining>5 || w.elapsed_ticks>result.tick_ ||
               w.completed_batches>result.tick_/5 ||
               w.frames_kg<0 || w.frames_kg>1000 ||
               (w.reserved_panels_kg!=0 && w.reserved_panels_kg!=20) ||
               (w.reserved_panels_kg==0 && w.finisher_ticks_remaining!=0) ||
               w.finisher_ticks_remaining>5 ||
               w.finisher_elapsed_ticks>result.tick_ ||
               w.finisher_batches>w.finisher_elapsed_ticks/5 ||
               (!w.finisher_installed && (w.finisher_npc_id || w.reserved_panels_kg ||
                  w.finisher_ticks_remaining || w.frames_kg || w.finisher_batches ||
                  w.finisher_elapsed_ticks)) ||
               (w.finisher_npc_id && w.finisher_npc_id==w.operator_npc_id) ||
               !result.workshops_.emplace(id,w).second)
                throw std::runtime_error("snapshot: invalid outpost workshop");
        } else if(f[0]=="workshopshift" && result.snapshot_version_>=106){
            require_fields(f,result.snapshot_version_>=107?8:7);
            const auto site=u64(f[1]),slot=u64(f[2]);
            WorkshopShift sh{};
            sh.command_id=u64(f[3]);sh.worker=u64(f[4]);
            sh.start_tick=u64(f[5]);sh.end_tick=u64(f[6]);
            if(result.snapshot_version_>=107)sh.relief_command_id=u64(f[7]);
            auto it=result.workshops_.find(site);
            if(it==result.workshops_.end() || slot<1 || slot>2 ||
               !sh.command_id || !sh.worker || sh.worker>1000000 ||
               !sh.start_tick || sh.start_tick>result.tick_ ||
               sh.end_tick<=result.tick_ || sh.end_tick<=sh.start_tick ||
               sh.end_tick-sh.start_tick>10000 ||
               (slot==2 && !it->second.finisher_installed) ||
               it->second.shifts[slot-1].command_id)
                throw std::runtime_error("snapshot: invalid workshop shift");
            auto& assigned=slot==1?it->second.operator_npc_id:
                                    it->second.finisher_npc_id;
            if(assigned!=sh.worker)
                throw std::runtime_error("snapshot: shift worker custody mismatch");
            it->second.shifts[slot-1]=sh;
        } else if(f[0]=="workshopshiftcancel" && result.snapshot_version_>=106){
            require_fields(f,3);
            const auto site=u64(f[1]),pending=u64(f[2]);
            auto it=result.workshops_.find(site);
            if(it==result.workshops_.end() || !pending ||
               it->second.cancelled_pending_shifts.size()>=256 ||
               !it->second.cancelled_pending_shifts.insert(pending).second)
                throw std::runtime_error("snapshot: invalid cancelled workshop shift");
        } else if(f[0]=="workshopmanager" && result.snapshot_version_>=108){
            require_fields(f,6);
            const auto site=u64(f[1]),worker=u64(f[2]),appointment=u64(f[3]);
            const auto trade=u64(f[4]),when=u64(f[5]);
            auto it=result.workshops_.find(site);
            if(it==result.workshops_.end() ||
               ((worker==0)!=(appointment==0)) ||
               worker>1000000 || (trade!=1 && trade!=2) ||
               when>result.tick_ ||
               (it->second.supervisor_npc_id || it->second.last_mentored_tick))
                throw std::runtime_error("snapshot: invalid workshop manager");
            it->second.supervisor_npc_id=worker;
            it->second.supervisor_appointment_id=appointment;
            it->second.supervisor_trade=static_cast<WorkshopTrade>(trade);
            it->second.last_mentored_tick=when;
        } else if(f[0]=="workshopjob" && result.snapshot_version_>=108){
            require_fields(f,9);
            const auto site=u64(f[1]),slot=u64(f[2]),id=u64(f[3]);
            WorkshopJobApproval job{};
            job.command_id=id;
            job.supervisor_appointment_id=u64(f[4]);
            job.supervisor_npc_id=u64(f[5]);
            job.approved_tick=u64(f[6]);
            job.baseline_batches=u64(f[7]);
            const auto target_batches=u64(f[8]);
            if(!target_batches || target_batches>50)
                throw std::runtime_error("snapshot: out-of-range owner-approved job quota");
            job.target_batches=static_cast<std::uint32_t>(target_batches);
            auto it=result.workshops_.find(site);
            if(it==result.workshops_.end() || slot<1 || slot>2 ||
               !id || !job.supervisor_appointment_id ||
               !job.supervisor_npc_id || !job.approved_tick ||
               job.approved_tick>result.tick_ || !job.target_batches ||
               job.target_batches>50 ||
               (slot==2 && !it->second.finisher_installed) ||
               it->second.approved_jobs[slot-1].command_id)
                throw std::runtime_error("snapshot: invalid workshop job");
            it->second.approved_jobs[slot-1]=job;
        } else if(f[0]=="storage" && result.snapshot_version_>=25){
            require_fields(f,result.snapshot_version_>=52?9:8);
            const auto site=u64(f[1]);OutpostStorage st{};
            if(result.snapshot_version_>=52){st.network_escrow_kg=i64(f[8]);
              if(st.network_escrow_kg<0||st.network_escrow_kg>1000000000)
                  throw std::runtime_error("snapshot: invalid production escrow");}
            st.source_ship=u64(f[2]);st.embodied_ore=i64(f[3]);
            static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
            for(int i=0;i<4;++i){auto qty=i64(f[4+i]);
                if(qty<0 || qty>4000)throw std::runtime_error("snapshot: invalid storage quantity");
                if(qty)st.goods_kg.emplace(names[i],qty);
            }
            inventory::StackStore cap(inventory::Catalogue::standard(),4000);
            for(const auto& [name,qty]:st.goods_kg)cap.load("good/"+name,qty);
            if(!site || !st.source_ship || st.embodied_ore!=2 ||
               !result.storages_.emplace(site,std::move(st)).second)
                throw std::runtime_error("snapshot: invalid storage facility");
        } else if(f[0]=="storage_node" && result.snapshot_version_>=26){
            require_fields(f,7);
            const auto site=u64(f[1]),node=u64(f[2]);
            if(!site || node<1 || node>static_cast<std::uint64_t>(std::numeric_limits<int>::max()/4) || !result.storages_.count(site))
                throw std::runtime_error("snapshot: orphaned extra storage");
            static const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
            std::map<std::string,std::int64_t> stacks;
            inventory::StackStore cap(inventory::Catalogue::standard(),4000);
            for(int i=0;i<4;++i){
                const auto q=i64(f[3+i]);
                if(q<0 || q>4000)throw std::runtime_error("snapshot: invalid extra storage quantity");
                if(q){stacks.emplace(names[i],q);cap.load(std::string("good/")+names[i],q);}
            }
            if(!result.storages_.at(site).extra_bins.emplace(static_cast<std::uint32_t>(node),
                                                             std::move(stacks)).second)
                throw std::runtime_error("snapshot: duplicate storage node");
        } else if(f[0]=="factory_placement" && result.snapshot_version_>=29){
            require_fields(f,5);
            const auto site=u64(f[1]),rawnode=u64(f[2]);
            const auto x=i64(f[3]),y=i64(f[4]);
            if(rawnode>kWorkshopConveyorNode || x < -kPlacementBound || x > kPlacementBound ||
               y < -kPlacementBound || y > kPlacementBound || !result.storages_.count(site))
                throw std::runtime_error("snapshot: invalid factory placement");
            const auto node=static_cast<std::uint32_t>(rawnode);
            auto& st=result.storages_.at(site);
            if((node==kWorkshopConveyorNode ?
                 (!result.workshops_.count(site) || result.workshops_.at(site).source_ship!=st.source_ship) :
                 (node!=0 && !st.extra_bins.count(node))) ||
               !st.equipment_positions.emplace(node,Vector2{x,y}).second)
                throw std::runtime_error("snapshot: placement references unbuilt equipment");
            try{validate_site_placements(st);}catch(const std::exception&){
                throw std::runtime_error("snapshot: overlapping factory equipment");
            }
        } else if(f[0]=="storage_link" && result.snapshot_version_>=27){
            require_fields(f,8);
            const auto site=u64(f[1]),id=u64(f[2]),src=u64(f[3]),dst=u64(f[4]);
            const auto item=u64(f[5]),moved=u64(f[7]);const auto rate=i64(f[6]);
            const auto it=result.storages_.find(site);
            const bool sw=src==kWorkshopConveyorNode,dw=dst==kWorkshopConveyorNode;
            const bool wm=sw||dw;
            const auto wi=result.workshops_.find(site);
            if(!site || !id || it==result.storages_.end() || src==dst ||
               (wm && (result.snapshot_version_<28 || sw==dw ||
                        wi==result.workshops_.end() ||
                        wi->second.source_ship!=it->second.source_ship ||
                        (sw ? (item==0 || (item==2 && !wi->second.finisher_installed)) : item!=0))) ||
               (!sw && src>static_cast<std::uint64_t>(std::numeric_limits<int>::max()/4)) ||
               (!dw && dst>static_cast<std::uint64_t>(std::numeric_limits<int>::max()/4)) ||
               (!sw && src && !it->second.extra_bins.count(static_cast<std::uint32_t>(src))) ||
               (!dw && dst && !it->second.extra_bins.count(static_cast<std::uint32_t>(dst))) ||
               item>3 || rate<1 || rate>1000 || moved/static_cast<std::uint64_t>(rate)>result.tick_ ||
               !it->second.links.emplace(id,OutpostStorageLink{id,
                     static_cast<std::uint32_t>(src),static_cast<std::uint32_t>(dst),
                     static_cast<std::uint8_t>(item),rate,moved,false,false,{}}).second)
                throw std::runtime_error("snapshot: invalid storage link");
        } else if(f[0]=="factory_conveyor" && result.snapshot_version_>=30){
            require_fields(f,5);
            const auto site=u64(f[1]),id=u64(f[2]);
            const auto active=u64(f[3]);
            auto st=result.storages_.find(site);
            if(st==result.storages_.end() || !id || !st->second.links.count(id) || active>1 ||
               st->second.links.at(id).route_required ||
               (active && f[4].empty()))
                throw std::runtime_error("snapshot: invalid installed conveyor receipt");
            auto& link=st->second.links.at(id);
            link.route_required=true;
            link.route_ready=active==1;
            if(!f[4].empty()){
                std::istringstream cells(f[4]);std::string cell;
                while(std::getline(cells,cell,';')){
                    const auto colon=cell.find(':');
                    if(colon==std::string::npos || cell.find(':',colon+1)!=std::string::npos ||
                       link.route_cells.size()>=4096)
                        throw std::runtime_error("snapshot: malformed conveyor route");
                    link.route_cells.push_back({i64(cell.substr(0,colon)),i64(cell.substr(colon+1))});
                }
            }
            try{validate_site_conveyors(st->second);}
            catch(const std::exception&){throw std::runtime_error("snapshot: invalid conveyor geometry");}
        } else if(f[0]=="geology" && result.snapshot_version_>=8){
            require_fields(f,6);
            const auto id=u64(f[1]),tax=u64(f[2]);
            const auto radius=i64(f[3]),density=i64(f[4]),initial=i64(f[5]);
            if(id==0 || tax>2 || initial!=asteroid_mass_tonnes(radius,density) ||
               !result.geology_.emplace(id,AsteroidGeology{
                   static_cast<AsteroidClass>(tax),radius,density,initial}).second)
                throw std::runtime_error("snapshot: invalid geological asteroid");
        } else if(result.snapshot_version_>=9 && (f[0]=="mineral" || f[0]=="good" || f[0]=="tailings")){
            if(f[0]=="tailings"){
                require_fields(f,3);
                const auto id=u64(f[1]); const auto kg=i64(f[2]);
                if(!id || kg<1 || kg>1000000000000LL ||
                   !result.colony_ships_.count(id) || result.colony_ships_.at(id).tailings_kg)
                    throw std::runtime_error("snapshot: invalid tailings");
                result.colony_ships_.at(id).tailings_kg=kg;
            }else{
                require_fields(f,4);
                const auto id=u64(f[1]); const auto kg=i64(f[3]);
                const bool mineral=f[0]=="mineral";
                if(!id || kg<1 || kg>1000000000000LL || !result.colony_ships_.count(id) ||
                   !(mineral?is_known_mineral(f[2]):is_known_good(f[2])) ||
                   (result.snapshot_version_<89 &&
                    (f[2]=="freight_water" || f[2]=="freight_food" ||
                     f[2]=="freight_nutrients")) ||
                   !(mineral?result.colony_ships_.at(id).minerals_kg:result.colony_ships_.at(id).goods_kg).emplace(f[2],kg).second)
                    throw std::runtime_error("snapshot: invalid mineral/product inventory");
            }
        } else if(f[0]=="lifesupport" && result.snapshot_version_>=10){
            require_fields(f,19);
            const auto id=u64(f[1]);
            if(!id || !result.colony_ships_.count(id) || result.life_support_.count(id))
                throw std::runtime_error("snapshot: orphaned or duplicate life support");
            ColonyLifeSupport l;
            l.food_kg=i64(f[2]);l.water_kg=i64(f[3]);l.oxygen_kg=i64(f[4]);
            l.nutrients_kg=i64(f[5]);l.co2_kg=i64(f[6]);l.crops_kg=i64(f[7]);
            l.compost_kg=i64(f[8]);l.wastewater_kg=i64(f[9]);l.organic_waste_kg=i64(f[10]);
            l.sludge_kg=i64(f[11]);l.hydrogen_kg=i64(f[12]);
            l.crops_ready_tick=u64(f[13]);l.cycles=u64(f[14]);
            l.consecutive_supplied_cycles=u64(f[15]);l.shortages=u64(f[16]);
            l.harvested_food_kg=i64(f[17]);l.recovered_water_kg=i64(f[18]);
            const auto valid_kg=[](std::int64_t value){return value>=0 && value<=100000000;};
            if(!valid_kg(l.food_kg) || !valid_kg(l.water_kg) || !valid_kg(l.oxygen_kg) ||
               !valid_kg(l.nutrients_kg) || !valid_kg(l.co2_kg) || !valid_kg(l.crops_kg) ||
               !valid_kg(l.compost_kg) || !valid_kg(l.wastewater_kg) ||
               !valid_kg(l.organic_waste_kg) || !valid_kg(l.sludge_kg) ||
               !valid_kg(l.hydrogen_kg))
                throw std::runtime_error("snapshot: invalid life-support quantity");
            const auto mass=l.food_kg+l.water_kg+l.oxygen_kg+l.nutrients_kg+l.co2_kg+
                l.crops_kg+l.compost_kg+l.wastewater_kg+l.organic_waste_kg+
                l.sludge_kg+l.hydrogen_kg;
            if(mass<1000 || mass>100000000 ||
               (result.snapshot_version_<11 && mass%1000) || l.crops_ready_tick>result.tick_+1000000 ||
               l.cycles>result.tick_/20 || l.consecutive_supplied_cycles>l.cycles ||
               l.shortages>l.cycles || l.harvested_food_kg<0 || l.recovered_water_kg<0 ||
               l.harvested_food_kg>100000000 || l.recovered_water_kg>100000000)
                throw std::runtime_error("snapshot: invalid life-support record");
            result.life_support_.emplace(id,l);
        } else if(f[0]=="farm" && result.snapshot_version_>=11){
            require_fields(f,27);
            const auto id=u64(f[1]),source=u64(f[2]);
            FarmFacility farm{};farm.source_ship=source;farm.embodied_ore=i64(f[3]);
            farm.workers=i64(f[4]);farm.production_cycles=u64(f[5]);
            farm.pending_food_kg=i64(f[6]);
            for(int i=0;i<3;++i)farm.recent_food_kg[i]=i64(f[7+i]);
            auto& l=farm.life;
            l.food_kg=i64(f[10]);l.water_kg=i64(f[11]);l.oxygen_kg=i64(f[12]);
            l.nutrients_kg=i64(f[13]);l.co2_kg=i64(f[14]);l.crops_kg=i64(f[15]);
            l.compost_kg=i64(f[16]);l.wastewater_kg=i64(f[17]);l.organic_waste_kg=i64(f[18]);
            l.sludge_kg=i64(f[19]);l.hydrogen_kg=i64(f[20]);
            l.crops_ready_tick=u64(f[21]);l.cycles=u64(f[22]);
            l.consecutive_supplied_cycles=u64(f[23]);l.shortages=u64(f[24]);
            l.harvested_food_kg=i64(f[25]);l.recovered_water_kg=i64(f[26]);
            const auto nonneg=[](std::int64_t x){return x>=0 && x<=100000000;};
            if(!id || !source || farm.embodied_ore!=4 || farm.workers<0 || farm.workers>8 ||
               farm.production_cycles>result.tick_ || !nonneg(farm.pending_food_kg) ||
               !nonneg(farm.recent_food_kg[0]) || !nonneg(farm.recent_food_kg[1]) ||
               !nonneg(farm.recent_food_kg[2]) ||
               !nonneg(l.food_kg)||!nonneg(l.water_kg)||!nonneg(l.oxygen_kg)||
               !nonneg(l.nutrients_kg)||!nonneg(l.co2_kg)||!nonneg(l.crops_kg)||
               !nonneg(l.compost_kg)||!nonneg(l.wastewater_kg)||!nonneg(l.organic_waste_kg)||
               !nonneg(l.sludge_kg)||!nonneg(l.hydrogen_kg)||
               (l.crops_kg && (l.crops_kg%3 || !l.crops_ready_tick ||
                 l.crops_ready_tick>result.tick_+5)) ||
               l.cycles>result.tick_/20 || l.consecutive_supplied_cycles>l.cycles ||
               l.shortages>l.cycles || !nonneg(l.harvested_food_kg) ||
               !nonneg(l.recovered_water_kg) ||
               !result.farms_.emplace(id,farm).second)
                throw std::runtime_error("snapshot: invalid farm facility");
        } else if(f[0]=="habitat" && result.snapshot_version_>=12){
            require_fields(f,25);
            const auto id=u64(f[1]),source=u64(f[2]);
            HabitatFacility h{};h.source_ship=source;h.embodied_ore=i64(f[3]);
            h.capacity=i64(f[4]);h.residents=i64(f[5]);
            h.cycles=u64(f[6]);h.consecutive_comfort_cycles=u64(f[7]);
            auto& l=h.life;
            l.food_kg=i64(f[8]);l.water_kg=i64(f[9]);l.oxygen_kg=i64(f[10]);
            l.nutrients_kg=i64(f[11]);l.co2_kg=i64(f[12]);l.crops_kg=i64(f[13]);
            l.compost_kg=i64(f[14]);l.wastewater_kg=i64(f[15]);l.organic_waste_kg=i64(f[16]);
            l.sludge_kg=i64(f[17]);l.hydrogen_kg=i64(f[18]);
            l.crops_ready_tick=u64(f[19]);l.cycles=u64(f[20]);
            l.consecutive_supplied_cycles=u64(f[21]);l.shortages=u64(f[22]);
            l.harvested_food_kg=i64(f[23]);l.recovered_water_kg=i64(f[24]);
            const auto nonneg=[](std::int64_t x){return x>=0 && x<=100000000;};
            if(!id || !source || h.embodied_ore!=4 || h.capacity!=8 ||
               h.residents<0 || h.residents>h.capacity || h.cycles>result.tick_/20 ||
               h.consecutive_comfort_cycles>h.cycles || l.cycles!=h.cycles ||
               l.consecutive_supplied_cycles>l.cycles || l.shortages>l.cycles ||
               !nonneg(l.food_kg)||!nonneg(l.water_kg)||!nonneg(l.oxygen_kg)||
               !nonneg(l.nutrients_kg)||!nonneg(l.co2_kg)||!nonneg(l.crops_kg)||
               !nonneg(l.compost_kg)||!nonneg(l.wastewater_kg)||!nonneg(l.organic_waste_kg)||
               !nonneg(l.sludge_kg)||!nonneg(l.hydrogen_kg)||
               l.crops_ready_tick || !nonneg(l.harvested_food_kg) || !nonneg(l.recovered_water_kg) ||
               !result.habitats_.emplace(id,h).second)
                throw std::runtime_error("snapshot: invalid habitat facility");
        } else if(f[0]=="community" && result.snapshot_version_>=15){
            require_fields(f,13);
            const auto id=u64(f[1]);CommunityFacility c{};
            c.source_ship=u64(f[2]);c.embodied_ore=i64(f[3]);
            c.food_kg=i64(f[4]);c.water_kg=i64(f[5]);
            c.organic_waste_kg=i64(f[6]);c.wastewater_kg=i64(f[7]);
            c.attendants=i64(f[8]);c.cycles=u64(f[9]);
            c.consecutive_service_cycles=u64(f[10]);c.shortages=u64(f[11]);
            c.served_visits=u64(f[12]);
            if(!id || !c.source_ship || c.embodied_ore!=4 ||
               c.food_kg<0 || c.food_kg>100000000 ||
               c.water_kg<0 || c.water_kg>100000000 ||
               c.organic_waste_kg<0 || c.organic_waste_kg>100000000 ||
               c.wastewater_kg<0 || c.wastewater_kg>100000000 ||
               c.attendants<0 || c.attendants>4 || c.cycles>result.tick_/20 ||
               c.consecutive_service_cycles>c.cycles || c.shortages>c.cycles ||
               c.served_visits>c.cycles*8 ||
               !result.communities_.emplace(id,c).second)
                throw std::runtime_error("snapshot: invalid community services");
        } else if(f[0]=="clinic" && result.snapshot_version_>=16){
            require_fields(f,result.snapshot_version_>=38?20:15);
            const auto id=u64(f[1]);ClinicFacility c{};
            c.source_ship=u64(f[2]);c.embodied_ore=i64(f[3]);
            c.water_kg=i64(f[4]);c.substrate_kg=i64(f[5]);
            c.wastewater_kg=i64(f[6]);c.medical_waste_kg=i64(f[7]);
            c.medics=i64(f[8]);c.active_cases=i64(f[9]);
            c.cycles=u64(f[10]);c.consecutive_care_cycles=u64(f[11]);
            c.shortages=u64(f[12]);c.treated_cases=u64(f[13]);c.recorded_cases=u64(f[14]);
            if(result.snapshot_version_>=38){
                c.training_units=u64(f[15]);c.ems_treatment_units=u64(f[16]);
                c.ems_treated_cases=u64(f[17]);c.last_ems_tick=u64(f[18]);
                c.ems_cases_this_tick=u64(f[19]);
            }
            const auto valid=[](std::int64_t v){return v>=0 && v<=100000000;};
            if(!id || !c.source_ship || c.embodied_ore!=4 ||
               !valid(c.water_kg)||!valid(c.substrate_kg)||!valid(c.wastewater_kg)||
               !valid(c.medical_waste_kg)||c.medics<0||c.medics>4||
               c.active_cases<0||c.active_cases>8||c.cycles>result.tick_/20||
               c.consecutive_care_cycles>c.cycles||c.shortages>c.cycles||
               c.recorded_cases>c.cycles||c.treated_cases>c.recorded_cases||
               c.active_cases!=static_cast<std::int64_t>(c.recorded_cases-c.treated_cases)||
               c.medical_waste_kg!=static_cast<std::int64_t>(c.treated_cases+c.training_units+c.ems_treatment_units) ||
               c.last_ems_tick>result.tick_ || c.ems_cases_this_tick>4 ||
               (c.ems_cases_this_tick && !c.last_ems_tick) ||
               c.ems_treated_cases>c.ems_treatment_units ||
               !result.clinics_.emplace(id,c).second)
                throw std::runtime_error("snapshot: invalid clinic");
        } else if(f[0]=="waterworks" && result.snapshot_version_>=18){
            require_fields(f,15);const auto id=u64(f[1]);WaterworksFacility p{};
            p.source_ship=u64(f[2]);p.embodied_ore=i64(f[3]);
            p.raw_feed_kg=i64(f[4]);p.brine_kg=i64(f[5]);p.purified_total_kg=i64(f[6]);
            p.cycles=u64(f[7]);p.consecutive_service_cycles=u64(f[8]);
            for(int j=0;j<3;++j){p.recent_output[j]=i64(f[9+j]);p.recent_demand[j]=i64(f[12+j]);}
            if(!id || !p.source_ship || p.embodied_ore!=4 ||
               p.raw_feed_kg<0 || p.raw_feed_kg>100000000 ||
               p.brine_kg<0 || p.brine_kg>100000000 ||
               p.purified_total_kg<0 || p.purified_total_kg>900000000 ||
               p.brine_kg*9!=p.purified_total_kg ||
               p.cycles>result.tick_/20 || p.consecutive_service_cycles>p.cycles ||
               std::any_of(p.recent_output.begin(),p.recent_output.end(),[](auto x){return x<0 || x>18 || x%9;}) ||
               std::any_of(p.recent_demand.begin(),p.recent_demand.end(),[](auto x){return x<0 || x>1000;}) ||
               !result.waterworks_.emplace(id,p).second)
                throw std::runtime_error("snapshot: invalid waterworks");
        } else if(f[0]=="reclaimer" && result.snapshot_version_>=17){
            require_fields(f,22);const auto id=u64(f[1]);ReclaimerFacility r{};
            r.source_ship=u64(f[2]);r.embodied_ore=i64(f[3]);
            r.sludge_kg=i64(f[4]);r.carbon_residue_kg=i64(f[5]);
            r.water_recovered_total_kg=i64(f[6]);r.oxygen_generated_total_kg=i64(f[7]);
            r.cycles=u64(f[8]);r.consecutive_powered_cycles=u64(f[9]);
            for(int j=0;j<3;++j){r.water_recent[j]=i64(f[10+j]);r.oxygen_recent[j]=i64(f[13+j]);
                r.water_demand_recent[j]=i64(f[16+j]);r.oxygen_demand_recent[j]=i64(f[19+j]);}
            auto valid=[](std::int64_t x){return x>=0 && x<=100000000;};
            if(!id || !r.source_ship || r.embodied_ore!=4 ||
               !valid(r.sludge_kg) || !valid(r.carbon_residue_kg) ||
               !valid(r.water_recovered_total_kg) || !valid(r.oxygen_generated_total_kg) ||
               r.cycles>result.tick_/20 || r.consecutive_powered_cycles>r.cycles ||
               r.sludge_kg*9!=r.water_recovered_total_kg ||
               r.carbon_residue_kg*7!=r.oxygen_generated_total_kg*3 ||
               !result.reclaimers_.emplace(id,r).second)
                throw std::runtime_error("snapshot: invalid reclaimer");
            for(int j=0;j<3;++j)if(!valid(r.water_recent[j]) || !valid(r.oxygen_recent[j]) ||
                    !valid(r.water_demand_recent[j]) || !valid(r.oxygen_demand_recent[j]) ||
                    r.water_recent[j]>r.water_recovered_total_kg ||
                    r.oxygen_recent[j]>r.oxygen_generated_total_kg ||
                    r.water_recent[j]>18 || r.oxygen_recent[j]>14 ||
                    r.water_demand_recent[j]>16 || r.oxygen_demand_recent[j]>16 ||
                    r.water_demand_recent[j]!=r.oxygen_demand_recent[j])
                throw std::runtime_error("snapshot: invalid reclaimer period");
        } else if(f[0]=="npcnext" && result.snapshot_version_>=20){
            require_fields(f,2);
            if(result.next_npc_id_!=1 || result.npc_next_seen_)
                throw std::runtime_error("snapshot: repeated NPC counter");
            result.next_npc_id_=u64(f[1]);
            result.npc_next_seen_=true;
            if(result.next_npc_id_==0 || result.next_npc_id_>100000000 ||
               (result.next_npc_id_==1 && (result.snapshot_version_==20 ||
                    (result.snapshot_version_<63 && !result.colony_ships_.empty()))))
                throw std::runtime_error("snapshot: invalid NPC counter");
        } else if(f[0]=="npc" && result.snapshot_version_>=20){
            require_fields(f,result.snapshot_version_>=86 ? 11 :
                result.snapshot_version_>=32 ? 9 : result.snapshot_version_>=31 ? 8 : 7);
            NpcRecord n{};n.id=u64(f[1]);n.home_ship=u64(f[2]);n.location=u64(f[3]);
            const auto role=u64(f[4]);n.farm_xp=u64(f[5]);n.mentor_id=u64(f[6]);
            if(result.snapshot_version_>=31)n.transport_id=u64(f[7]);
            n.assigned_site=result.snapshot_version_>=32 ? u64(f[8]) : n.location;
            if(result.snapshot_version_>=86){
                const auto kind=u64(f[9]);
                n.residence_id=u64(f[10]);
                if(kind<1 || kind>4 || !n.residence_id ||
                   (kind==1 && n.residence_id!=n.home_ship))
                    throw std::runtime_error("snapshot: invalid legal residence");
                n.residence_kind=static_cast<transport::LocationKind>(kind);
            }
            if(!n.id || role>(result.snapshot_version_>=78?3ULL:2ULL) || n.farm_xp>1000000 || n.mentor_id==n.id ||
               !result.npc_next_seen_ || n.id>=result.next_npc_id_ ||
               !result.npcs_.emplace(n.id,NpcRecord{n.id,n.home_ship,n.location,
                                            static_cast<NpcRole>(role),n.farm_xp,n.mentor_id,n.transport_id,n.assigned_site,
                                            n.residence_kind,n.residence_id}).second)
                throw std::runtime_error("snapshot: invalid NPC identity");
        } else if(f[0]=="npctraining" && result.snapshot_version_>=105){
            require_fields(f,result.snapshot_version_>=108?8:6);
            const auto id=u64(f[1]);
            const auto command=u64(f[2]),when=u64(f[3]),site=u64(f[4]);
            const auto trade=u64(f[5]);
            if(!id || !command || !when || when>result.tick_ ||
               !site || (trade!=1 && trade!=2))
                throw std::runtime_error("snapshot: invalid industrial training record");
            NpcRecord* person=nullptr;
            auto live=result.npcs_.find(id);
            if(live!=result.npcs_.end())person=&live->second;
            else{
                auto dead=result.casualties_.find(id);
                if(dead!=result.casualties_.end())person=&dead->second.identity;
            }
            if(!person || person->industrial_training.size()>=6)
                throw std::runtime_error("snapshot: orphan or excessive training proof");
            const auto mentor=result.snapshot_version_>=108?u64(f[6]):0;
            const auto appointment=result.snapshot_version_>=108?u64(f[7]):0;
            if((mentor==0)!=(appointment==0) || mentor>1000000 ||
               mentor==id)
                throw std::runtime_error("snapshot: invalid industrial mentor proof");
            person->industrial_training.push_back(
                NpcTradeTraining{command,when,site,
                                 static_cast<WorkshopTrade>(trade),mentor,appointment});
        } else if(f[0]=="teamxp" && result.snapshot_version_>=20){
            require_fields(f,3);
            const auto site=u64(f[1]),xp=u64(f[2]);
            if(!site || xp>1000000 || !result.farm_team_xp_.emplace(site,xp).second)
                throw std::runtime_error("snapshot: invalid team record");
        } else if(f[0]=="policy" && result.snapshot_version_>=19){
            require_fields(f,11);
            const auto id=u64(f[1]);
            PolicySite site{};site.source_ship=u64(f[2]);
            for(int i=0;i<4;++i){
                const auto level=u64(f[3+2*i]);
                const auto staff=i64(f[4+2*i]);
                if(level>3 || staff<0 || staff>3)
                    throw std::runtime_error("snapshot: invalid policy assurance");
                site.domains[i].declared=static_cast<AssuranceLevel>(level);
                site.domains[i].inspectors=staff;
            }
            if(!result.policy_sites_.emplace(id,site).second)
                throw std::runtime_error("snapshot: duplicate policy site");
        } else if(f[0]=="utility" && result.snapshot_version_>=13){
            require_fields(f,result.snapshot_version_>=14?19:14);
            const auto id=u64(f[1]);UtilityFacility u{};
            u.source_ship=u64(f[2]);u.embodied_ore=i64(f[3]);
            u.condition=i64(f[4]);u.stored_energy=i64(f[5]);
            const auto powered=u64(f[6]);u.powered=powered==1;
            u.cycles=u64(f[7]);u.consecutive_powered_cycles=u64(f[8]);
            u.outages=u64(f[9]);u.repairs=u64(f[10]);
            u.generated_energy=i64(f[11]);u.consumed_energy=i64(f[12]);
            u.curtailed_energy=i64(f[13]);
            if(result.snapshot_version_>=14){
                u.spare_modules=i64(f[14]);u.technicians=i64(f[15]);
                const auto enabled=u64(f[16]);
                if(enabled>1)throw std::runtime_error("snapshot: invalid maintenance policy");
                u.delegated_maintenance=enabled==1;
                u.delegated_repairs=u64(f[17]);u.blocked_maintenance_cycles=u64(f[18]);
            }
            if(!id || !u.source_ship || u.repairs>result.tick_ ||
               u.spare_modules<0 || u.spare_modules>1000 ||
               u.technicians<0 || u.technicians>8 ||
               u.delegated_repairs>u.repairs || u.blocked_maintenance_cycles>u.cycles ||
               u.embodied_ore!=4+2*static_cast<std::int64_t>(u.repairs) ||
               u.condition<0 || u.condition>100 || u.stored_energy<0 || u.stored_energy>120 ||
               powered>1 || u.cycles>result.tick_/20 ||
               u.consecutive_powered_cycles>u.cycles || u.outages>u.cycles ||
               u.generated_energy<0 || u.consumed_energy<0 || u.curtailed_energy<0 ||
               u.generated_energy+u.curtailed_energy>static_cast<std::int64_t>(result.tick_)*8 ||
               u.consumed_energy>u.generated_energy ||
               u.stored_energy!=u.generated_energy-u.consumed_energy ||
               !result.utilities_.emplace(id,u).second)
                throw std::runtime_error("snapshot: invalid utility operation");
        } else if(f[0]=="physicaldependent" && result.snapshot_version_>=59){
            if(f.size()!=7)throw std::runtime_error("snapshot: malformed physical dependent");
            const auto id=u64(f[1]),guardian=u64(f[2]),command=u64(f[3]),
                at=u64(f[4]),home=u64(f[5]),location=u64(f[6]);
            if(id<(1ULL<<62) || !guardian || !command || !at ||
               at>result.tick_ || !home || !location ||
               !result.physical_dependents_.emplace(id,PhysicalDependent{
                    id,guardian,command,at,home,location}).second)
                throw std::runtime_error("snapshot: duplicate or invalid dependent physical identity");
        } else if(f[0]=="colonydeparture" && result.snapshot_version_>=58){
            if(f.size()<7)throw std::runtime_error("snapshot: truncated colony departure");
            const auto source=u64(f[1]),receiver=u64(f[2]),cmd=u64(f[3]),tick=u64(f[4]);
            const auto count=u64(f[5]);
            if(!source||!receiver||source==receiver||!cmd||!tick||
               tick>result.tick_ || count==0 || count>16 ||
               f.size()<(result.snapshot_version_>=60 ? 7+count : 6+count))
                throw std::runtime_error("snapshot: malformed colony departure");
            ColonyDeparture d{source,receiver,cmd,tick,{}};
            for(std::size_t i=0;i<count;++i)d.relocated_npcs.push_back(u64(f[6+i]));
            if(result.snapshot_version_>=60){
                const auto children=u64(f[6+count]);
                if(children>32 || count+children>16 || f.size()!=7+count+children)
                    throw std::runtime_error("snapshot: malformed dependent departure manifest");
                for(std::size_t i=0;i<children;++i)
                    d.relocated_dependents.push_back(u64(f[7+count+i]));
                if(!std::is_sorted(d.relocated_dependents.begin(),d.relocated_dependents.end()) ||
                   std::adjacent_find(d.relocated_dependents.begin(),
                                      d.relocated_dependents.end())!=d.relocated_dependents.end() ||
                   (!d.relocated_dependents.empty() && d.relocated_dependents.front()<(1ULL<<62)))
                    throw std::runtime_error("snapshot: invalid dependent departure identities");
            }else if(f.size()!=6+count)
                throw std::runtime_error("snapshot: extra legacy departure fields");
            if(!std::is_sorted(d.relocated_npcs.begin(),d.relocated_npcs.end()) ||
               !d.relocated_npcs.front() ||
               std::adjacent_find(d.relocated_npcs.begin(),d.relocated_npcs.end())!=d.relocated_npcs.end() ||
               !result.colony_departures_.emplace(source,d).second)
                throw std::runtime_error("snapshot: duplicate or unordered colony departure");
        } else if(f[0]=="voyage" && result.snapshot_version_>=33){
            if(f.size()<7)throw std::runtime_error("snapshot: truncated voyage");
            const auto id=u64(f[1]),ferry=u64(f[2]),origin=u64(f[3]);
            const auto status=u64(f[4]),count=u64(f[5]);
            if(!id || !ferry || !origin || ferry==origin || (status!=1 && status!=2 && (result.snapshot_version_<35 || (status!=3 && status!=5 && (result.snapshot_version_<36 || status!=6)))) ||
               !count || count>10000 || f.size()!=(result.snapshot_version_>=48 ? 12+count : result.snapshot_version_>=35 ? 11+count : result.snapshot_version_>=34 ? 7+count : 6+count))
                throw std::runtime_error("snapshot: invalid voyage manifest");
            VoyageRecord v;v.ferry=ferry;v.origin=origin;v.status=static_cast<unsigned>(status);
            const std::size_t offset=result.snapshot_version_>=48?12:result.snapshot_version_>=35?11:result.snapshot_version_>=34?7:6;
            if(result.snapshot_version_>=34)v.destination=u64(f[6]);
            if(result.snapshot_version_>=35){
                v.last_incident_id=u64(f[7]);v.stranded_tick=u64(f[8]);
                const auto cause=u64(f[9]);v.rescue_transport_id=u64(f[10]);
                if(cause>5)throw std::runtime_error("snapshot: invalid ferry incident reason");
                v.stranded_reason=static_cast<unsigned>(cause);
            }
            if(result.snapshot_version_>=48)v.secondary_rescue_transport_id=u64(f[11]);
            if((v.secondary_rescue_transport_id &&
                (result.snapshot_version_<48 || v.status!=5 ||
                 v.secondary_rescue_transport_id==v.rescue_transport_id)) ||
               ((v.status==1 || v.status==3) && v.destination) ||
               ((v.status==2 || v.status==5) && result.snapshot_version_>=34 && !v.destination) ||
               (v.status==3 && (!v.stranded_tick || !v.stranded_reason || !v.last_incident_id ||
                 v.stranded_tick>result.tick_)) ||
               (v.status==5 && (!v.last_incident_id || !v.rescue_transport_id ||
                 v.stranded_tick || v.stranded_reason)) ||
               (v.status!=3 && (v.stranded_tick || v.stranded_reason)) ||
               (v.status!=5 && v.status!=6 && v.rescue_transport_id) ||
               (v.status==6 && (!v.last_incident_id || v.stranded_tick || v.stranded_reason ||
                 ((v.destination || v.rescue_transport_id) &&
                  (result.snapshot_version_<37 || !v.destination || !v.rescue_transport_id)))))
                throw std::runtime_error("snapshot: invalid voyage lifecycle");
            for(std::size_t i=0;i<count;++i){auto npc=u64(f[offset+i]);
                if(!npc || (!v.passengers.empty() && npc<=v.passengers.back()))
                    throw std::runtime_error("snapshot: duplicate/unsorted voyage passenger");
                v.passengers.push_back(npc);
            }
            if(!result.voyages_.emplace(id,std::move(v)).second)
                throw std::runtime_error("snapshot: duplicate voyage ID");
        } else if(f[0]=="deceased" && result.snapshot_version_>=36){
            require_fields(f,result.snapshot_version_>=86?14:12);
            NpcCasualty d{};auto& n=d.identity;n.id=u64(f[1]);
            n.home_ship=u64(f[2]);n.location=u64(f[3]);
            const auto role=u64(f[4]);n.role=static_cast<NpcRole>(role);
            n.farm_xp=u64(f[5]);n.mentor_id=u64(f[6]);
            n.transport_id=u64(f[7]);n.assigned_site=u64(f[8]);
            d.death_tick=u64(f[9]);d.command_id=u64(f[10]);d.ferry=u64(f[11]);
            if(result.snapshot_version_>=86){
                const auto kind=u64(f[12]);
                n.residence_id=u64(f[13]);
                if(kind<1 || kind>4 || !n.residence_id ||
                   (kind==1 && n.residence_id!=n.home_ship))
                    throw std::runtime_error("snapshot: invalid deceased legal residence");
                n.residence_kind=static_cast<transport::LocationKind>(kind);
            }
            if(!n.id || n.id>=result.next_npc_id_ || role!=static_cast<unsigned>(NpcRole::Ship) ||
               n.assigned_site!=n.home_ship || n.farm_xp>1000000 ||
               !n.home_ship || !n.location || !n.assigned_site || n.mentor_id==n.id ||
               !d.death_tick || d.death_tick>result.tick_ || !d.command_id ||
               !d.ferry || d.ferry!=n.location || result.npcs_.count(n.id) ||
               !result.casualties_.emplace(n.id,d).second)
                throw std::runtime_error("snapshot: invalid deceased NPC tombstone");
        } else if(f[0]=="medic" && result.snapshot_version_>=38){
            require_fields(f,4);
            const auto id=u64(f[1]),site=u64(f[2]),units=u64(f[3]);
            if(!id || !site || units<1 || units>1000 ||
               !result.named_medics_.emplace(id,MedicTraining{site,units}).second)
                throw std::runtime_error("snapshot: invalid named medical qualification");
        } else if((f[0]=="patient" || f[0]=="patienthistory") && result.snapshot_version_>=38){
            require_fields(f,result.snapshot_version_>=39?14:10);
            if(f[0]=="patienthistory" && result.snapshot_version_<39)
                throw std::runtime_error("snapshot: premature medical history");
            const auto id=u64(f[1]);MedicalCase m{};
            m.rescue_voyage=u64(f[2]);m.triage_command=u64(f[3]);m.triage_tick=u64(f[4]);
            const auto severity=u64(f[5]);m.severity=static_cast<unsigned>(severity);
            m.treatment_command=u64(f[6]);m.treatment_tick=u64(f[7]);
            m.clinic_site=u64(f[8]);m.medic_id=u64(f[9]);
            if(result.snapshot_version_>=39){
                m.stabilization_command=u64(f[10]);m.stabilized_tick=u64(f[11]);
                m.recovery_due_tick=u64(f[12]);m.recovered_tick=u64(f[13]);
            }
            if(!id || !m.rescue_voyage || !m.triage_command || !m.triage_tick ||
               m.triage_tick>result.tick_ || severity<1 || severity>3 ||
               (!!m.treatment_command != !!m.treatment_tick) ||
               (!!m.treatment_command != !!m.clinic_site) ||
               (!!m.treatment_command != !!m.medic_id) ||
               (!!m.stabilization_command != !!m.stabilized_tick) ||
               (m.stabilized_tick && (m.stabilized_tick<m.triage_tick ||
                   m.stabilized_tick>result.tick_ || (m.treatment_tick &&
                   m.stabilized_tick>m.treatment_tick))) ||
               (m.recovery_due_tick && (!m.treatment_command ||
                   m.recovery_due_tick != m.treatment_tick+3*m.severity)) ||
               (m.recovered_tick && (!m.recovery_due_tick ||
                   m.recovered_tick!=m.recovery_due_tick || m.recovered_tick>result.tick_)) ||
               (m.recovery_due_tick && m.recovery_due_tick<=result.tick_ && !m.recovered_tick &&
                !(result.casualties_.count(id) &&
                  result.casualties_.at(id).death_tick<=m.recovery_due_tick)) ||
               m.treatment_tick>result.tick_ ||
               (m.treatment_tick && m.treatment_tick<m.triage_tick) ||
               !(f[0]=="patient" ? result.medical_cases_.emplace(id,m).second :
                 result.medical_history_.emplace(std::make_pair(id,m.triage_command),m).second))
                throw std::runtime_error("snapshot: invalid patient case");
        } else if(f[0]=="medicshift" && result.snapshot_version_>=39){
            require_fields(f,6);const auto cmd=u64(f[1]);MedicShift sh{};
            sh.clinic_site=u64(f[2]);sh.medic_id=u64(f[3]);
            sh.start_tick=u64(f[4]);sh.end_tick=u64(f[5]);
            if(!cmd || !sh.clinic_site || !sh.medic_id || !sh.start_tick ||
               sh.start_tick>result.tick_ || sh.end_tick<=sh.start_tick ||
               sh.end_tick-sh.start_tick>10000 ||
               !result.medic_shifts_.emplace(cmd,sh).second)
                throw std::runtime_error("snapshot: invalid medic shift");
        } else if(f[0]=="medicalkit" && result.snapshot_version_>=39){
            require_fields(f,6);const auto id=u64(f[1]);MobileMedicalKit k{};
            k.water_kg=i64(f[2]);k.substrate_kg=i64(f[3]);
            k.wastewater_kg=i64(f[4]);k.medical_waste_kg=i64(f[5]);
            if(!id || k.water_kg<0 || k.substrate_kg<0 || k.wastewater_kg<0 ||
               k.medical_waste_kg<0 || k.water_kg>1000 || k.substrate_kg>1000 ||
               k.wastewater_kg>1000000 || k.medical_waste_kg>1000000 ||
               !result.mobile_medical_kits_.emplace(id,k).second)
                throw std::runtime_error("snapshot: invalid ship medical kit");
        } else if(f[0]=="crewrehome" && result.snapshot_version_>=38){
            require_fields(f,6);
            const auto id=u64(f[1]);CrewRehome h{};
            h.wreck=u64(f[2]);h.rescue_ship=u64(f[3]);
            h.incident_command=u64(f[4]);h.tick=u64(f[5]);
            if(!id || !h.wreck || !h.rescue_ship || !h.incident_command ||
               !h.tick || h.tick>result.tick_ || h.wreck==h.rescue_ship ||
               !result.crew_rehomes_.emplace(id,h).second)
                throw std::runtime_error("snapshot: invalid crew rehome");
        } else if (f[0] == "pending") {
            if (result.snapshot_version_ < 31) require_fields(f, 11);
            else if (f.size() < 13) throw std::runtime_error("snapshot: missing v31 pending manifest");
            Command c;
            c.id = u64(f[1]); c.execute_tick = u64(f[2]); c.sequence = u64(f[3]);
            c.issuer = u64(f[4]); c.actor = u64(f[5]); c.target = u64(f[6]);
            auto action = u64(f[7]);
            if (action > (result.snapshot_version_ >= 108 ? static_cast<unsigned>(Action::CancelWorkshopJob) : result.snapshot_version_ >= 107 ? static_cast<unsigned>(Action::RelieveWorkshopShift) : result.snapshot_version_ >= 106 ? static_cast<unsigned>(Action::CancelWorkshopShift) : result.snapshot_version_ >= 105 ? static_cast<unsigned>(Action::TrainWorkshopTrade) : result.snapshot_version_ >= 104 ? static_cast<unsigned>(Action::FireShipWeapon) : result.snapshot_version_ >= 101 ? static_cast<unsigned>(Action::HackConfuse) : result.snapshot_version_ >= 100 ? static_cast<unsigned>(Action::SetHardpointPower) : result.snapshot_version_ >= 69 ? static_cast<unsigned>(Action::CancelSurveyFocus) : result.snapshot_version_ >= 66 ? static_cast<unsigned>(Action::LaunchGeologicalDrone) : result.snapshot_version_ >= 65 ? static_cast<unsigned>(Action::RecoverSurveyProbe) : result.snapshot_version_ >= 64 ? static_cast<unsigned>(Action::ConfigureExplorationScanner) : result.snapshot_version_ >= 63 ? static_cast<unsigned>(Action::SurveyLocal) : result.snapshot_version_ >= 62 ? static_cast<unsigned>(Action::SalvageWreck) : result.snapshot_version_ >= 59 ? static_cast<unsigned>(Action::HostRegisterDependent) : result.snapshot_version_ >= 58 ? static_cast<unsigned>(Action::HostColonyDeparture) : result.snapshot_version_ >= 56 ? static_cast<unsigned>(Action::HostTransferConsignedStock) : result.snapshot_version_ >= 53 ? static_cast<unsigned>(Action::HostReturnConstructedStock) : result.snapshot_version_ >= 52 ? static_cast<unsigned>(Action::HostEscrowConstructedStock) : result.snapshot_version_ >= 48 ? static_cast<unsigned>(Action::HostSplitEvacuatePassengers) : result.snapshot_version_ >= 41 ? static_cast<unsigned>(Action::HostSettleMigrant) : result.snapshot_version_ >= 39 ? static_cast<unsigned>(Action::HostMobileStabilize) : result.snapshot_version_ >= 38 ? static_cast<unsigned>(Action::HostTreatInjuries) : result.snapshot_version_ >= 37 ? static_cast<unsigned>(Action::HostResolveMixedFerryLoss) : result.snapshot_version_ >= 36 ? static_cast<unsigned>(Action::HostDestroyPassengerFerry) : result.snapshot_version_ >= 35 ? static_cast<unsigned>(Action::HostEvacuatePassengers) : result.snapshot_version_ >= 32 ? static_cast<unsigned>(Action::HostDisembarkPassengers) : result.snapshot_version_ >= 30 ? static_cast<unsigned>(Action::CompleteConveyorRoute) : result.snapshot_version_ >= 29 ? static_cast<unsigned>(Action::PlaceFactoryEquipment) : result.snapshot_version_ >= 28 ? static_cast<unsigned>(Action::ConstructWorkshopLink) : result.snapshot_version_ >= 27 ? static_cast<unsigned>(Action::ConstructStorageLink) : result.snapshot_version_ >= 26 ? static_cast<unsigned>(Action::TransferStorageNodeStack) : result.snapshot_version_ >= 25 ? static_cast<unsigned>(Action::TransferStorageStack) : result.snapshot_version_ >= 24 ? static_cast<unsigned>(Action::TransferWorkshopStack) : result.snapshot_version_ >= 23 ? static_cast<unsigned>(Action::TransferWorkshopFinalOutput) : result.snapshot_version_ >= 22 ? static_cast<unsigned>(Action::TransferWorkshopOutput) : result.snapshot_version_ >= 21 ? static_cast<unsigned>(Action::HostCrewLoss) : result.snapshot_version_ >= 20 ? static_cast<unsigned>(Action::AssignFarmMentor) : result.snapshot_version_ >= 19 ? static_cast<unsigned>(Action::AssignPolicyInspectors) : result.snapshot_version_ >= 18 ? static_cast<unsigned>(Action::TransferWaterworksFeed) : result.snapshot_version_ >= 17 ? static_cast<unsigned>(Action::ConstructReclaimer) : result.snapshot_version_ >= 16 ? static_cast<unsigned>(Action::AssignClinicStaff) : result.snapshot_version_ >= 15 ? static_cast<unsigned>(Action::AssignCommunityStaff) : result.snapshot_version_ >= 14 ? static_cast<unsigned>(Action::SetUtilityMaintenance) : result.snapshot_version_ >= 13 ? static_cast<unsigned>(Action::RepairUtility) : result.snapshot_version_ >= 12 ? static_cast<unsigned>(Action::AssignHabitatResidents) : result.snapshot_version_ >= 11 ? static_cast<unsigned>(Action::AssignFarmWorkers) : result.snapshot_version_ >= 10 ? static_cast<unsigned>(Action::LoadLifeWater) : result.snapshot_version_ >= 9 ? static_cast<unsigned>(Action::FabricateMaterialModule) : result.snapshot_version_ >= 7 ? static_cast<unsigned>(Action::ConstructOutpost) : result.snapshot_version_ >= 6 ? static_cast<unsigned>(Action::DeliverGateSpares) : result.snapshot_version_ >= 5 ? static_cast<unsigned>(Action::SetGatePairOnline) : result.snapshot_version_ >= 4 ? static_cast<unsigned>(Action::SellOre) : static_cast<unsigned>(Action::UnloadOre))) throw std::runtime_error("snapshot: invalid action");
            c.action = static_cast<Action>(action);
            auto tx = i64(f[8]), ty = i64(f[9]);
            if (tx < std::numeric_limits<int>::min() || tx > std::numeric_limits<int>::max() ||
                ty < std::numeric_limits<int>::min() || ty > std::numeric_limits<int>::max())
                throw std::runtime_error("snapshot: invalid thrust");
            c.thrust_x = static_cast<int>(tx); c.thrust_y = static_cast<int>(ty);
            c.amount = i64(f[10]);
            if (result.snapshot_version_ >= 31) {
                c.transport_id = u64(f[11]);
                const auto count = u64(f[12]);
                if (count > 10000 || f.size() != 13 + count +
                    (c.action==Action::HostSplitEvacuatePassengers?2:0))
                    throw std::runtime_error("snapshot: invalid pending passenger count");
                std::set<std::uint64_t> unique;
                for (std::size_t i=0; i<count; ++i) {
                    auto npc=u64(f[13+i]);
                    if (!npc || !unique.insert(npc).second)
                        throw std::runtime_error("snapshot: duplicate pending passenger");
                    c.passenger_ids.push_back(npc);
                }
                if(c.action==Action::HostSplitEvacuatePassengers){
                    c.secondary_receiver=u64(f[13+count]);
                    c.secondary_transport=u64(f[14+count]);
                    if(result.snapshot_version_<48||!c.secondary_receiver||
                       !c.secondary_transport||c.thrust_x<=0||
                       static_cast<std::uint64_t>(c.thrust_x)>=count||
                       c.thrust_y<0||(c.thrust_y>0 && result.snapshot_version_<49)||
                       c.amount<=0)
                        throw std::runtime_error("snapshot: invalid pending split rescue");
                }
                const bool incident=c.action==Action::HostStrandFerry || c.action==Action::HostResumeFerry ||
                    c.action==Action::HostEvacuatePassengers ||
                    c.action==Action::HostDestroyPassengerFerry ||
                    c.action==Action::HostResolveMixedFerryLoss;
                const bool pending_migration=c.action==Action::HostSettleMigrant;
                const bool pending_split=c.action==Action::HostSplitEvacuatePassengers;
                const bool passenger_action=incident || c.action==Action::HostEmbarkPassengers ||
                    c.action==Action::HostDisembarkPassengers;
                const bool medical=c.action==Action::HostTrainMedic ||
                    c.action==Action::HostTriageRescued || c.action==Action::HostTreatInjuries ||
                    c.action==Action::HostScheduleMedicShift || c.action==Action::HostLoadMedicalKit ||
                    c.action==Action::HostMobileStabilize;
                if(passenger_action ? (result.snapshot_version_<(incident?35:32) || !c.transport_id || !count ||
                    c.issuer!=Simulation::kServerAuthority || !c.actor || !c.target ||
                    c.thrust_x || c.thrust_y ||
                    (incident ? (c.action==Action::HostStrandFerry ? (c.target!=c.actor || c.amount<1 || c.amount>5) :
                        c.action==Action::HostResumeFerry ? (c.target!=c.actor || c.amount!=0) :
                        c.action==Action::HostDestroyPassengerFerry ? (c.target!=c.actor || c.amount!=0 || result.snapshot_version_<36) :
                        c.action==Action::HostResolveMixedFerryLoss ?
                            (result.snapshot_version_<37 || c.target==c.actor || c.amount<=0 ||
                             static_cast<std::uint64_t>(c.amount)>=c.passenger_ids.size() ||
                             c.id==c.transport_id ||
                             !std::is_sorted(c.passenger_ids.begin(),c.passenger_ids.begin()+
                                 (c.amount>0 && static_cast<std::uint64_t>(c.amount)<=c.passenger_ids.size()?c.amount:0)) ||
                             (c.amount>0 && static_cast<std::uint64_t>(c.amount)<c.passenger_ids.size() &&
                              !std::is_sorted(c.passenger_ids.begin()+c.amount,c.passenger_ids.end()))) :
                        (c.target==c.actor || c.amount<=0)) : (c.actor==c.target || c.amount))) :
                    (!medical && !pending_migration && !pending_split && c.action!=Action::HostColonyDeparture && c.action!=Action::HostRegisterDependent && (c.transport_id || count)))
                    throw std::runtime_error("snapshot: invalid pending transport action");
                if(pending_migration && (result.snapshot_version_<41 ||
                   c.issuer!=Simulation::kServerAuthority || c.actor==c.target ||
                   !c.actor || !c.target || !c.transport_id ||
                   (count!=1 && !(result.snapshot_version_>=44 && count==2 &&
                     c.passenger_ids[0]<c.passenger_ids[1])) ||
                   c.amount || c.thrust_x || c.thrust_y))
                    throw std::runtime_error("snapshot: invalid pending migration");
                if(medical && (result.snapshot_version_<38 || c.issuer!=Simulation::kServerAuthority ||
                   !c.actor || !c.target || c.thrust_x || c.thrust_y ||
                   (c.action==Action::HostTrainMedic && (c.transport_id || count || c.amount<1 || c.amount>3)) ||
                   (c.action==Action::HostTriageRescued && (!count || c.transport_id==0 ||
                       c.actor!=c.target || c.amount<1 || c.amount>3)) ||
                   (c.action==Action::HostTreatInjuries && (!count || c.transport_id==0 || c.amount!=0)) ||
                   (c.action==Action::HostScheduleMedicShift &&
                    (result.snapshot_version_<39 || c.transport_id || count || c.amount<1 || c.amount>10000)) ||
                   (c.action==Action::HostLoadMedicalKit &&
                    (result.snapshot_version_<39 || c.transport_id || count || c.amount<1 || c.amount>100)) ||
                   (c.action==Action::HostMobileStabilize &&
                    (result.snapshot_version_<39 || !count || c.transport_id!=c.target || c.amount)) ))
                    throw std::runtime_error("snapshot: invalid pending medical action");
            }
            if((c.action==Action::AppointWorkshopSupervisor ||
                c.action==Action::MentorWorkshopTrade ||
                c.action==Action::ApproveWorkshopJob ||
                c.action==Action::CancelWorkshopJob) &&
               (result.snapshot_version_<108 || !c.actor || !c.target ||
                c.issuer==Simulation::kServerAuthority ||
                c.thrust_y || c.thrust_x<1 || c.thrust_x>2 ||
                c.transport_id || !c.passenger_ids.empty() ||
                c.secondary_receiver || c.secondary_transport ||
                (c.action==Action::AppointWorkshopSupervisor ?
                   (c.amount<0 || c.amount>1000000) :
                 c.action==Action::MentorWorkshopTrade ?
                   (c.amount<1 || c.amount>1000000) :
                 c.action==Action::ApproveWorkshopJob ?
                   (c.amount<1 || c.amount>50) : (c.amount<1))))
                throw std::runtime_error("snapshot: invalid pending workshop management order");
            if(c.action==Action::RelieveWorkshopShift &&
               (result.snapshot_version_<107 || !c.actor || !c.target ||
                c.issuer==Simulation::kServerAuthority ||
                c.amount<1 || c.thrust_y<1 || c.thrust_y>1000000 ||
                (c.thrust_x!=1 && c.thrust_x!=2) || c.transport_id ||
                !c.passenger_ids.empty() || c.secondary_receiver ||
                c.secondary_transport))
                throw std::runtime_error("snapshot: invalid queued workshop relief");
            if((c.action==Action::StartWorkshopShift ||
                c.action==Action::CancelWorkshopShift) &&
               (result.snapshot_version_<106 || !c.actor || !c.target ||
                c.issuer==Simulation::kServerAuthority ||
                (c.thrust_x!=1 && c.thrust_x!=2) ||
                (c.action==Action::StartWorkshopShift ?
                   (c.amount<1 || c.amount>1000000 ||
                    c.thrust_y<1 || c.thrust_y>10000 ||
                    c.execute_tick>std::numeric_limits<std::uint64_t>::max()-
                        static_cast<std::uint64_t>(c.thrust_y)) :
                   (c.amount<1 || c.thrust_y!=0)) ||
                c.transport_id || !c.passenger_ids.empty() ||
                c.secondary_receiver || c.secondary_transport))
                throw std::runtime_error("snapshot: invalid queued workshop shift");
            if(c.action==Action::TrainWorkshopTrade &&
               (result.snapshot_version_<105 || !c.actor || !c.target ||
                !c.issuer || c.issuer==Simulation::kServerAuthority ||
                c.amount<1 || c.amount>1000000 ||
                (c.thrust_x!=1 && c.thrust_x!=2) || c.thrust_y ||
                c.transport_id || !c.passenger_ids.empty() ||
                c.secondary_receiver || c.secondary_transport))
                throw std::runtime_error("snapshot: invalid queued industrial training");
            if(c.action==Action::HostRegisterDependent &&
               (result.snapshot_version_<59 || c.issuer!=Simulation::kServerAuthority ||
                c.actor!=c.target || c.transport_id<(1ULL<<62) ||
                c.passenger_ids.size()!=1 || c.amount || c.thrust_x || c.thrust_y))
                throw std::runtime_error("snapshot: malformed pending dependent registration");
            if(c.action==Action::HostColonyDeparture &&
               (result.snapshot_version_<58 || c.issuer!=Simulation::kServerAuthority ||
                !c.actor || !c.target || c.actor==c.target ||
                c.passenger_ids.empty() || c.passenger_ids.size()>16 || c.amount || c.thrust_x || c.thrust_y ||
                c.transport_id || c.secondary_receiver || c.secondary_transport ||
                !std::is_sorted(c.passenger_ids.begin(),c.passenger_ids.end()) ||
                std::adjacent_find(c.passenger_ids.begin(),c.passenger_ids.end())!=c.passenger_ids.end()))
                throw std::runtime_error("snapshot: invalid pending colony departure");
            if (c.id == 0 || c.issuer == 0 || c.execute_tick <= result.tick_ ||
                ((c.action==Action::HostDestroyShip || c.action==Action::HostCrewLoss) &&
                 (result.snapshot_version_<21 || c.issuer!=Simulation::kServerAuthority ||
                  c.actor==0 || c.target==0 || c.thrust_x || c.thrust_y ||
                  (c.action==Action::HostDestroyShip && (c.actor==c.target || c.amount!=0)) ||
                  (c.action==Action::HostCrewLoss && (c.actor!=c.target || c.amount<1 || c.amount>1000)))) ||
                ((c.action==Action::SetGatePairOnline || c.action==Action::SetGateStaff ||
                  c.action==Action::DeliverGateSpares) &&
                 (c.issuer!=Simulation::kServerAuthority || c.actor==c.target ||
                  c.actor==0 || c.target==0 ||
                  (c.action==Action::SetGatePairOnline && (c.amount!=0 && c.amount!=1)) ||
                  (c.action==Action::SetGateStaff && (c.amount<0 || c.amount>16)) ||
                  (c.action==Action::DeliverGateSpares && (c.amount<1 || c.amount>10000)) ||
                  c.thrust_x!=0 || c.thrust_y!=0)) ||
                (c.action!=Action::SetGatePairOnline && c.action!=Action::SetGateStaff &&
                 c.action!=Action::DeliverGateSpares && c.action!=Action::HostDestroyShip &&
                 c.action!=Action::HostCrewLoss && c.action!=Action::HostEmbarkPassengers &&
                 c.action!=Action::HostDisembarkPassengers && c.action!=Action::HostStrandFerry &&
                 c.action!=Action::HostResumeFerry && c.action!=Action::HostEvacuatePassengers &&
                 c.action!=Action::HostDestroyPassengerFerry &&
                 c.action!=Action::HostResolveMixedFerryLoss &&
                 c.action!=Action::HostTrainMedic &&
                 c.action!=Action::HostTriageRescued &&
                 c.action!=Action::HostTreatInjuries &&
                 c.action!=Action::HostScheduleMedicShift &&
                 c.action!=Action::HostLoadMedicalKit &&
                 c.action!=Action::HostMobileStabilize &&
                 c.action!=Action::HostSettleMigrant &&
                 c.action!=Action::HostSplitEvacuatePassengers &&
                 c.action!=Action::HostColonyDeparture &&
                 c.action!=Action::HostRegisterDependent &&
                 c.issuer==Simulation::kServerAuthority) ||
                ((c.action==Action::RefineOre || c.action==Action::FabricateModule ||
                  c.action==Action::ConstructOutpost) && (c.target!=0 || c.thrust_x || c.thrust_y ||
                  c.amount<1 || c.amount>1000 ||
                  (c.action==Action::ConstructOutpost && c.amount!=1))) ||
                ((c.action==Action::RefineMineral || c.action==Action::FabricateMaterialModule) &&
                 (result.snapshot_version_<9 || c.thrust_x || c.thrust_y ||
                   c.amount<1 || (c.action==Action::RefineMineral ?
                     (c.amount>100000 || !mineral_name(c.target) || c.target==9) :
                     (c.amount>1000 || (c.target!=1 && c.target!=2))))) ||
                ((c.action>=Action::PlantCrops && c.action<=Action::LoadLifeWater) &&
                   (result.snapshot_version_<10 || c.thrust_x || c.thrust_y || c.target ||
                    c.amount<1 || c.amount>(c.action==Action::LoadLifeWater?100:10000))) ||
                ((c.action>=Action::ConstructFarm && c.action<=Action::AssignFarmWorkers) &&
                    (result.snapshot_version_<11 || !c.target || c.thrust_y ||
                     (c.action==Action::ConstructFarm && (c.amount!=1 || c.thrust_x)) ||
                     (c.action==Action::AssignFarmWorkers && (c.thrust_x || !c.amount ||
                         c.amount < -8 || c.amount>8)) ||
                     (c.action==Action::TransferFarmStock &&
                        (c.thrust_x<1 || c.thrust_x>5 || !c.amount ||
                         c.amount < -10000 || c.amount>10000)))) ||
                ((c.action>=Action::ConstructHabitat && c.action<=Action::AssignHabitatResidents) &&
                    (result.snapshot_version_<12 || !c.target || c.thrust_y ||
                     (c.action==Action::ConstructHabitat && (c.amount!=1 || c.thrust_x)) ||
                     (c.action==Action::AssignHabitatResidents && (c.thrust_x || !c.amount ||
                         c.amount < -8 || c.amount>8)) ||
                     (c.action==Action::TransferHabitatStock &&
                        (c.thrust_x<1 || c.thrust_x>3 || !c.amount ||
                         c.amount < -10000 || c.amount>10000)))) ||
                ((c.action==Action::ConstructUtility || c.action==Action::RepairUtility) &&
                 (result.snapshot_version_<13 || !c.target || c.thrust_x || c.thrust_y || c.amount!=1)) ||
                ((c.action==Action::SetPolicyAssurance || c.action==Action::AssignPolicyInspectors) &&
                 (result.snapshot_version_<19 || !c.target || c.thrust_x<0 || c.thrust_x>3 ||
                  c.thrust_y || c.amount<0 || c.amount>3)) ||
                ((c.action==Action::ConstructWaterworks || c.action==Action::TransferWaterworksFeed) &&
                 (result.snapshot_version_<18 || !c.target || c.thrust_x || c.thrust_y ||
                  (c.action==Action::ConstructWaterworks && c.amount!=1) ||
                  (c.action==Action::TransferWaterworksFeed &&
                    (!c.amount || c.amount%1000 || c.amount<-10000 || c.amount>10000)))) ||
                ((c.action==Action::ConstructReclaimer) &&
                 (result.snapshot_version_<17 || !c.target || c.thrust_x || c.thrust_y || c.amount!=1)) ||
                ((c.action>=Action::ConstructClinic && c.action<=Action::AssignClinicStaff) &&
                 (result.snapshot_version_<16 || !c.target || c.thrust_y ||
                  (c.action==Action::ConstructClinic && (c.amount!=1 || c.thrust_x)) ||
                  (c.action==Action::TransferClinicStock && (c.thrust_x<1 || c.thrust_x>2 ||
                     !c.amount || c.amount<-10000 || c.amount>10000)) ||
                  (c.action==Action::AssignClinicStaff && (c.thrust_x || c.amount<0 || c.amount>4)))) ||
                ((c.action>=Action::ConstructCommunity && c.action<=Action::AssignCommunityStaff) &&
                 (result.snapshot_version_<15 || !c.target || c.thrust_y ||
                  (c.action==Action::ConstructCommunity && (c.amount!=1 || c.thrust_x)) ||
                  (c.action==Action::TransferCommunityStock && (c.thrust_x<1 || c.thrust_x>2 ||
                     !c.amount || c.amount<-10000 || c.amount>10000)) ||
                  (c.action==Action::AssignCommunityStaff && (c.thrust_x || c.amount<0 || c.amount>4)))) ||
                ((c.action>=Action::TransferUtilitySpares && c.action<=Action::SetUtilityMaintenance) &&
                 (result.snapshot_version_<14 || !c.target || c.thrust_x || c.thrust_y ||
                  (c.action==Action::TransferUtilitySpares && (!c.amount || c.amount<-32 || c.amount>32)) ||
                  (c.action==Action::AssignUtilityTechnicians && (c.amount<0 || c.amount>8)) ||
                  (c.action==Action::SetUtilityMaintenance && (c.amount<0 || c.amount>1)))) ||
                ((c.action>=Action::ConstructWorkshop && c.action<=Action::TransferWorkshopStack) &&
                 (result.snapshot_version_<22 || !c.target || c.thrust_y ||
                  (c.action!=Action::TransferWorkshopStack && c.thrust_x) ||
                  (c.action==Action::TransferWorkshopStack &&
                   (result.snapshot_version_<24 || c.thrust_x<0 || c.thrust_x>3 ||
                    !c.amount || c.amount<-1000 || c.amount>1000)) ||
                  (c.action>=Action::ConstructWorkshopFinisher && result.snapshot_version_<23) ||
                  ((c.action==Action::ConstructWorkshop || c.action==Action::ConstructWorkshopFinisher) && c.amount!=1) ||
                  ((c.action==Action::TransferWorkshopFeed) &&
                     (!c.amount || c.amount%1000 || c.amount<-10000 || c.amount>10000)) ||
                  ((c.action==Action::AssignWorkshopOperator || c.action==Action::AssignWorkshopFinisher) &&
                     (c.amount<0 || c.amount>1000000)) ||
                  ((c.action==Action::TransferWorkshopOutput || c.action==Action::TransferWorkshopFinalOutput) &&
                     (c.amount<50 || c.amount>500 || c.amount%50)))) ||
                ((c.action==Action::ConstructStorage || c.action==Action::TransferStorageStack) &&
                 (result.snapshot_version_<25 || !c.target ||
                  (c.action==Action::ConstructStorage ?
                    (c.amount!=1 || c.thrust_x || c.thrust_y) :
                    (c.amount<1 || c.amount>1000 || c.thrust_x<0 || c.thrust_x>3 ||
                     c.thrust_y<0 || c.thrust_y>3)))) ||
                ((c.action==Action::PlaceConveyorCell || c.action==Action::CompleteConveyorRoute) &&
                  (result.snapshot_version_<30 || !c.target || c.amount<1 ||
                   c.thrust_x< -kPlacementBound || c.thrust_x>kPlacementBound ||
                   c.thrust_y< -kPlacementBound || c.thrust_y>kPlacementBound ||
                   (c.action==Action::CompleteConveyorRoute && (c.thrust_x || c.thrust_y)))) ||
                ((c.action==Action::PlaceFactoryEquipment) &&
                  (result.snapshot_version_<29 || !c.target || c.amount< -1 ||
                   c.amount>std::numeric_limits<int>::max()/4 ||
                   c.thrust_x< -kPlacementBound || c.thrust_x>kPlacementBound ||
                   c.thrust_y< -kPlacementBound || c.thrust_y>kPlacementBound)) ||
                ((c.action==Action::ConstructStorageLink) &&
                 (result.snapshot_version_<27 || !c.target || c.thrust_x<0 ||
                  c.thrust_y<0 ||
                  c.thrust_x>std::numeric_limits<int>::max()/4 ||
                  c.thrust_y>std::numeric_limits<int>::max()/4 ||
                  c.thrust_x==c.thrust_y || c.amount<4 ||
                  c.amount>4003 || c.amount/4<1 || c.amount/4>1000)) ||
                ((c.action==Action::ConstructWorkshopLink) &&
                 (result.snapshot_version_<28 || !c.target || c.thrust_x<0 ||
                  c.thrust_x>std::numeric_limits<int>::max()/4 ||
                  (c.thrust_y!=0 && c.thrust_y!=1) || c.amount<4 || c.amount>4003 ||
                  c.amount/4<1 || c.amount/4>1000 ||
                  (c.thrust_y==0 && c.amount%4!=0) ||
                  (c.thrust_y==1 && c.amount%4==0))) ||
                ((c.action==Action::ConstructStorageNode || c.action==Action::TransferStorageNodeStack) &&
                 (result.snapshot_version_<26 || !c.target ||
                  (c.action==Action::ConstructStorageNode ?
                   (c.amount<1 || c.amount>std::numeric_limits<int>::max()/4 || c.thrust_x || c.thrust_y) :
                   (c.amount<1 || c.amount>1000 || c.thrust_x<4 ||
                    c.thrust_y<0 || c.thrust_y>3)))) ||
                !pending_ids.insert(c.id).second)
                throw std::runtime_error("snapshot: invalid pending command");
            result.commands_[c.execute_tick].push_back(c);
        } else if (f[0] == "accepted") {
            require_fields(f, 2);
            const auto id = u64(f[1]);
            if (id == 0 || !result.submitted_ids_.insert(id).second)
                throw std::runtime_error("snapshot: invalid accepted ID");
        } else if (f[0] == "event") {
            require_fields(f, 5);
            Event ev;
            ev.tick = u64(f[1]); ev.command_id = u64(f[2]);
            auto outcome = u64(f[3]);
            if (outcome > (result.snapshot_version_ >= 101 ? static_cast<std::uint64_t>(Outcome::AttackResisted) : result.snapshot_version_ >= 100 ? static_cast<std::uint64_t>(Outcome::ModuleOffline) : result.snapshot_version_ >= 7 ? static_cast<std::uint64_t>(Outcome::InsufficientCrew) : result.snapshot_version_ >= 5 ? static_cast<std::uint64_t>(Outcome::GateOffline) : result.snapshot_version_ >= 4 ? static_cast<std::uint64_t>(Outcome::InsufficientFunds) : static_cast<std::uint64_t>(Outcome::HangarFull)))
                throw std::runtime_error("snapshot: invalid outcome");
            ev.result = static_cast<Outcome>(outcome);
            ev.transferred = i64(f[4]);
            if (ev.command_id == 0 || ev.tick > result.tick_ || ev.transferred < 0 ||
                !event_ids.insert(ev.command_id).second)
                throw std::runtime_error("snapshot: invalid event");
            result.events_.push_back(ev);
        } else if (f[0] == "hangar" && result.snapshot_version_ >= 3) {
            require_fields(f, 5);
            const auto station = u64(f[1]), owner = u64(f[2]);
            const auto ore = i64(f[3]), capacity = i64(f[4]);
            if (station == 0 || owner == 0 || ore < 0 || capacity <= 0 ||
                capacity > 1000000000LL || ore > capacity ||
                !result.hangars_.emplace(std::make_pair(station, owner), StationHangar{ore, capacity}).second)
                throw std::runtime_error("snapshot: invalid station hangar");
        } else if (f[0] == "market" && result.snapshot_version_ >= 4) {
            require_fields(f,5);
            const auto id=u64(f[1]); const auto ore=i64(f[2]),credits=i64(f[3]),price=i64(f[4]);
            if (id==0 || ore<0 || credits<0 || price<=0 || price>1000000 ||
                !result.markets_.emplace(id,StationMarket{ore,credits,price}).second)
                throw std::runtime_error("snapshot: invalid market");
        } else if (f[0] == "wallet" && result.snapshot_version_ >= 4) {
            require_fields(f,3);
            const auto owner=u64(f[1]); const auto credits=i64(f[2]);
            if (owner==0 || credits<0 || !result.wallets_.emplace(owner,credits).second)
                throw std::runtime_error("snapshot: invalid wallet");
        } else if (f[0] == "depleted" && result.snapshot_version_ >= 2) {
            require_fields(f, 2);
            const auto id = u64(f[1]);
            if (id == 0 || id >= result.next_entity_id_ || !result.depleted_.insert(id).second ||
                result.entities_.count(id)) throw std::runtime_error("snapshot: invalid depleted tombstone");
        } else if (f[0] == "automine" && result.snapshot_version_ >= 2) {
            require_fields(f, 5);
            const auto actor = u64(f[1]), target = u64(f[2]), start_id = u64(f[4]);
            const auto amount = i64(f[3]);
            if (actor == 0 || target == 0 || start_id == 0 || amount <= 0 || amount > 1000000000LL ||
                !result.mining_.emplace(actor, AutoMining{target, amount, start_id}).second)
                throw std::runtime_error("snapshot: invalid active mining operation");
        } else if (f[0] == "activity" && result.snapshot_version_ >= 2) {
            require_fields(f, 8);
            ActivityEvent a;
            a.tick = u64(f[1]); const auto kind = u64(f[2]);
            a.start_command_id = u64(f[3]); a.actor = u64(f[4]); a.target = u64(f[5]);
            const auto outcome = u64(f[6]); a.transferred = i64(f[7]);
            if (kind > (result.snapshot_version_>=99 ? static_cast<unsigned>(ActivityKind::PlayerRescued) : result.snapshot_version_>=97 ? static_cast<unsigned>(ActivityKind::NpcRendezvous) : result.snapshot_version_>=27 ? static_cast<unsigned>(ActivityKind::AutoStorageTransfer) : result.snapshot_version_>=21 ? static_cast<unsigned>(ActivityKind::NpcLost) : result.snapshot_version_>=5 ? static_cast<unsigned>(ActivityKind::JumpCompleted) : static_cast<unsigned>(ActivityKind::Docked)) ||
                outcome > static_cast<std::uint64_t>(Outcome::HangarFull) ||
                a.tick == 0 || a.tick > result.tick_ || a.target == 0 || a.transferred < 0 ||
                (kind == 1 && (a.start_command_id == 0 || a.actor == 0 ||
                               outcome != static_cast<std::uint64_t>(Outcome::Applied) || a.transferred == 0)) ||
                (kind==static_cast<unsigned>(ActivityKind::AutoStorageTransfer) &&
                 (result.snapshot_version_<27 || !a.start_command_id || !a.actor ||
                  a.target!=a.start_command_id || a.transferred<=0 ||
                  outcome!=static_cast<unsigned>(Outcome::Applied))) ||
                (kind != 1 && kind!=static_cast<unsigned>(ActivityKind::AutoStorageTransfer) && a.transferred != 0) ||
                ((kind==static_cast<unsigned>(ActivityKind::ShipDestroyed) ||
                  kind==static_cast<unsigned>(ActivityKind::NpcLost)) &&
                  (!a.start_command_id || !a.actor || outcome!=static_cast<std::uint64_t>(Outcome::Applied))))
                throw std::runtime_error("snapshot: invalid activity event");
            a.kind = static_cast<ActivityKind>(kind);
            a.result = static_cast<Outcome>(outcome);
            result.activity_events_.push_back(a);
        } else throw std::runtime_error("snapshot: unknown record");
    }
    // WO2-H: each industrial certificate derives from three distinct Applied
    // original-world commands, never invented qualifications or source totals.
    if(result.snapshot_version_>=105){
        std::set<std::uint64_t> credited_commands;
        std::set<std::pair<EntityId,std::uint64_t>> mentored_site_ticks;
        const auto verify=[&](const NpcRecord& n){
            std::array<unsigned,2> per_trade{{0,0}};
            std::uint64_t previous_practice_tick=0;
            for(const auto& t:n.industrial_training){
                const auto code=static_cast<unsigned>(t.trade);
                // One named NPC cannot practice twice in one committed tick.
                // Persisted credits also retain chronological command order.
                if(code<1 || code>2 || !t.command_id || !t.tick ||
                   t.tick<=previous_practice_tick ||
                   t.tick>result.tick_ || !t.workshop_site ||
                   !credited_commands.insert(t.command_id).second ||
                   ++per_trade[code-1]>3 ||
                   !result.submitted_ids_.count(t.command_id) ||
                   !std::any_of(result.events_.begin(),result.events_.end(),
                     [&](const Event& e){return e.command_id==t.command_id &&
                         e.tick==t.tick && e.result==Outcome::Applied &&
                         e.transferred==1;}))
                    throw std::runtime_error("snapshot: invalid industrial training provenance");
                if(result.snapshot_version_>=108 && t.mentor_id){
                    const auto manager=result.npcs_.find(t.mentor_id);
                    const auto deceased=result.casualties_.find(t.mentor_id);
                    if(!t.supervisor_appointment_id || t.mentor_id==n.id ||
                       (manager==result.npcs_.end() &&
                        deceased==result.casualties_.end()) ||
                       !result.submitted_ids_.count(t.supervisor_appointment_id) ||
                       !std::any_of(result.events_.begin(),result.events_.end(),
                          [&](const Event& ev){
                              return ev.command_id==t.supervisor_appointment_id &&
                                     ev.tick<=t.tick &&
                                     ev.result==Outcome::Applied;}) ||
                       !mentored_site_ticks.insert({t.workshop_site,t.tick}).second)
                        throw std::runtime_error("snapshot: invalid original mentor identity or appointment receipt");
                }
                previous_practice_tick=t.tick;
            }
        };
        for(const auto& [id,n]:result.npcs_){(void)id;verify(n);}
        for(const auto& [id,dead]:result.casualties_){(void)id;verify(dead.identity);}
        if(result.snapshot_version_>=108)
            for(const auto& [site,shop]:result.workshops_){
                std::uint64_t latest=0;
                for(const auto& key:mentored_site_ticks)
                    if(key.first==site)latest=std::max(latest,key.second);
                if(shop.last_mentored_tick!=latest)
                    throw std::runtime_error("snapshot: workshop mentored-tick evidence mismatch");
            }
    }
    // v32 could persist active custody without recording the original manifest.
    // Never synthesize a voyage from incomplete evidence: its origin and
    // passenger membership cannot be proven. Explicitly refuse this legacy
    // state until an authenticated external migration manifest is available.
    if(result.snapshot_version_==32){
        for(const auto& [id,n]:result.npcs_) {
            (void)id;
            if(n.transport_id)
                throw std::runtime_error("snapshot: legacy v32 active voyage requires verified manifest migration");
        }
    }
    if(result.snapshot_version_>=33){
        for(const auto& [id,v]:result.voyages_){
            const auto ferry=result.entities_.find(v.ferry),origin=result.entities_.find(v.origin);
            if(ferry==result.entities_.end() || origin==result.entities_.end() ||
               !result.colony_ships_.count(v.ferry) || !result.colony_ships_.count(v.origin) ||
               (v.destination && !result.colony_ships_.count(v.destination)))
                throw std::runtime_error("snapshot: voyage ship missing");
            if(v.status==6 && ferry->second.type!=EntityType::Wreck)
                throw std::runtime_error("snapshot: lost ferry not a wreck");
            if(v.status==3 && (ferry->second.velocity.x || ferry->second.velocity.y ||
               ferry->second.jump_state!=ShipJumpState::Ready ||
               ferry->second.dock_state!=DockState::InSpace || ferry->second.type!=EntityType::Ship))
                throw std::runtime_error("snapshot: stranded ferry not immobilized");
            for(auto npc:v.passengers){
                auto n=result.npcs_.find(npc);
                if(n==result.npcs_.end() && v.status!=6)throw std::runtime_error("snapshot: voyage passenger missing");
                if(v.status==6){
                    const auto d=result.casualties_.find(npc);
                    const auto rescue=result.voyages_.find(v.rescue_transport_id);
                    const bool is_rescued=v.rescue_transport_id &&
                        rescue!=result.voyages_.end() &&
                        std::binary_search(rescue->second.passengers.begin(),
                            rescue->second.passengers.end(),npc);
                    if(is_rescued){
                        if(d!=result.casualties_.end() && d->second.command_id==v.last_incident_id)
                            throw std::runtime_error("snapshot: passenger both rescued and killed");
                    }else if(d==result.casualties_.end() ||
                       d->second.identity.transport_id!=id ||
                       d->second.command_id!=v.last_incident_id || d->second.ferry!=v.ferry)
                        throw std::runtime_error("snapshot: incomplete fatal passenger manifest");
                }
                if((v.status==1 || v.status==3) && (n->second.transport_id!=id || n->second.location!=v.ferry))
                    throw std::runtime_error("snapshot: active voyage custody mismatch");
            }
        }
        for(const auto& [id,v]:result.voyages_){
            if(v.last_incident_id){
                const auto receipt=std::find_if(result.events_.begin(),result.events_.end(),
                    [&](const Event& e){return e.command_id==v.last_incident_id && e.result==Outcome::Applied;});
                if(receipt==result.events_.end() ||
                   (v.status==3 && receipt->tick!=v.stranded_tick))
                    throw std::runtime_error("snapshot: ferry incident missing committed receipt");
            }
            if(v.status==6 && v.rescue_transport_id){
                const auto linked=result.voyages_.find(v.rescue_transport_id);
                if(result.snapshot_version_<37 || linked==result.voyages_.end() ||
                   linked->second.origin!=v.ferry || linked->second.ferry!=v.destination ||
                   linked->second.passengers.empty() || v.rescue_transport_id!=v.last_incident_id ||
                   v.rescue_transport_id==id ||
                   ![&](){for(auto npc:linked->second.passengers){
                       if(std::binary_search(v.passengers.begin(),v.passengers.end(),npc))continue;
                       const auto h=result.crew_rehomes_.find(npc);
                       if(result.snapshot_version_<38 || h==result.crew_rehomes_.end() ||
                          h->second.wreck!=v.ferry || h->second.rescue_ship!=v.destination ||
                          h->second.incident_command!=v.last_incident_id)return false;
                   }return true;}())
                    throw std::runtime_error("snapshot: invalid mixed-incident rescue provenance");
            }
            if(v.status==5 && v.secondary_rescue_transport_id){
                const auto a=result.voyages_.find(v.rescue_transport_id);
                const auto b=result.voyages_.find(v.secondary_rescue_transport_id);
                if(result.snapshot_version_<48||a==result.voyages_.end()||
                   b==result.voyages_.end()||a==b||
                   a->second.origin!=v.ferry||b->second.origin!=v.ferry||
                   a->second.ferry!=v.destination||
                   !a->second.passengers.size()||!b->second.passengers.size())
                    throw std::runtime_error("snapshot: incomplete split-ferry rescue provenance");
                std::vector<std::uint64_t> union_ids=a->second.passengers;
                union_ids.insert(union_ids.end(),b->second.passengers.begin(),
                                 b->second.passengers.end());
                std::sort(union_ids.begin(),union_ids.end());
                // v49 may additionally evacuate the source's physically
                // operating crew. They keep source home/assignment, yet each
                // must be on exactly one resulting receiving voyage.
                const auto ordinary=std::includes(union_ids.begin(),union_ids.end(),
                                                   v.passengers.begin(),v.passengers.end());
                std::vector<std::uint64_t> extras;
                std::set_difference(union_ids.begin(),union_ids.end(),
                                    v.passengers.begin(),v.passengers.end(),
                                    std::back_inserter(extras));
                if(!ordinary||std::adjacent_find(union_ids.begin(),union_ids.end())!=union_ids.end()||
                   (!extras.empty()&&result.snapshot_version_<49))
                    throw std::runtime_error("snapshot: split-ferry passenger conservation failure");
                if(!extras.empty()){
                    std::vector<std::uint64_t> actual_crew;
                    for(const auto& [person,n]:result.npcs_)
                        if(n.home_ship==v.ferry&&n.assigned_site==v.ferry&&
                           n.role==NpcRole::Ship)actual_crew.push_back(person);
                    if(extras!=actual_crew||
                       extras.size()!=static_cast<std::size_t>(result.colony_ships_.at(v.ferry).crew))
                        throw std::runtime_error("snapshot: missing/extra source ferry operating crew");
                    for(const auto crew:extras){
                        const auto& n=result.npcs_.at(crew);
                        const bool primary_current=n.location==a->second.ferry&&
                            n.transport_id==v.rescue_transport_id;
                        const bool secondary_current=n.location==b->second.ferry&&
                            n.transport_id==v.secondary_rescue_transport_id;
                        // On a completed voyage crew may already be ashore;
                        // immutable historical voyage/crew receipts prove the rescue.
                        const bool safely_arrived=!n.transport_id&&
                            ((a->second.status==2&&n.location==a->second.destination)||
                             (b->second.status==2&&n.location==b->second.destination));
                        if(!primary_current&&!secondary_current&&!safely_arrived)
                            throw std::runtime_error("snapshot: split crew custody disagrees with voyage");
                    }
                }
            }else if(v.status==5){
                const auto linked=result.voyages_.find(v.rescue_transport_id);
                if(linked==result.voyages_.end() || linked->second.origin!=v.ferry ||
                   linked->second.ferry!=v.destination || linked->second.passengers!=v.passengers ||
                   v.rescue_transport_id==id)
                    throw std::runtime_error("snapshot: broken rescue voyage provenance");
            }
        }
        for(const auto& [id,d]:result.casualties_){
            if(result.npcs_.count(id) || !result.colony_ships_.count(d.identity.home_ship) ||
               !result.colony_ships_.count(d.ferry) ||
               !result.submitted_ids_.count(d.command_id))
                throw std::runtime_error("snapshot: deceased identity or receipt missing");
            const auto v=std::find_if(result.voyages_.begin(),result.voyages_.end(),
                [&](const auto& entry){return entry.second.ferry==d.ferry &&
                    entry.second.status==6 && entry.second.last_incident_id==d.command_id;});
            if(v==result.voyages_.end())throw std::runtime_error("snapshot: orphan ferry casualty");
            if(d.identity.transport_id &&
               (d.identity.transport_id!=v->first ||
                !std::binary_search(v->second.passengers.begin(),
                                    v->second.passengers.end(),id)))
                throw std::runtime_error("snapshot: casualty not in original voyage manifest");
            const auto a=std::find_if(result.activity_events_.begin(),result.activity_events_.end(),
                [&](const ActivityEvent& e){return e.kind==ActivityKind::NpcLost &&
                    e.start_command_id==d.command_id && e.target==id &&
                    e.tick==d.death_tick && e.actor==d.ferry;});
            if(a==result.activity_events_.end())throw std::runtime_error("snapshot: missing casualty activity");
        }
        if(result.snapshot_version_>=36){
            for(const auto& a:result.activity_events_){
                if(a.kind!=ActivityKind::NpcLost)continue;
                bool fatal=false;
                for(const auto& [voyage_id,v]:result.voyages_){(void)voyage_id;
                    if(v.status==6 && v.last_incident_id==a.start_command_id &&
                       v.ferry==a.actor){fatal=true;break;}
                }
                if(fatal){
                    const auto dead=result.casualties_.find(a.target);
                    if(dead==result.casualties_.end() || dead->second.command_id!=a.start_command_id ||
                       dead->second.death_tick!=a.tick || dead->second.ferry!=a.actor)
                        throw std::runtime_error("snapshot: missing fatal crew tombstone");
                }
            }
        }
        // A corrupted snapshot must not bypass the same ferry berth limit.
        std::map<EntityId,std::size_t> occupied_berths;
        for(const auto& [npc_id,n]:result.npcs_){(void)npc_id;
            if(n.transport_id && ++occupied_berths[n.location]>16)
                throw std::runtime_error("snapshot: passenger ferry over capacity");
        }
        for(const auto& [id,n]:result.npcs_)if(n.transport_id){
            const auto v=result.voyages_.find(n.transport_id);
            if(v==result.voyages_.end() || (v->second.status!=1 && v->second.status!=3) ||
               !std::binary_search(v->second.passengers.begin(),v->second.passengers.end(),id))
                throw std::runtime_error("snapshot: orphan passenger custody");
        }
    }
    if(result.snapshot_version_>=38){
        for(const auto& [id,h]:result.crew_rehomes_){
            const auto n=result.npcs_.find(id);
            const auto death=result.casualties_.find(id);
            const NpcRecord* identity=n!=result.npcs_.end()?&n->second:
                death!=result.casualties_.end()?&death->second.identity:nullptr;
            if(!identity || identity->home_ship!=h.rescue_ship ||
               identity->assigned_site!=h.rescue_ship || !result.colony_ships_.count(h.wreck) ||
               !result.colony_ships_.count(h.rescue_ship) ||
               result.entities_.at(h.wreck).type!=EntityType::Wreck ||
               !result.submitted_ids_.count(h.incident_command))
                throw std::runtime_error("snapshot: crew rescue lacks live lineage");
            bool found=false;
            for(const auto& [vid,v]:result.voyages_){(void)vid;
                if(v.ferry==h.wreck && v.status==6 && v.last_incident_id==h.incident_command &&
                   v.rescue_transport_id && result.voyages_.count(v.rescue_transport_id) &&
                   std::binary_search(result.voyages_.at(v.rescue_transport_id).passengers.begin(),
                                      result.voyages_.at(v.rescue_transport_id).passengers.end(),id))found=true;
            }
            if(!found)throw std::runtime_error("snapshot: crew rehome missing rescue voyage");
        }
        for(const auto& [id,q]:result.named_medics_){
            if((!result.npcs_.count(id) && !result.casualties_.count(id)) ||
               !result.clinics_.count(q.clinic_site) || q.units<1)
                throw std::runtime_error("snapshot: medic qualification missing person/clinic");
        }
        const auto check_case=[&](std::uint64_t id,const MedicalCase& m){
            if((!result.npcs_.count(id) && !result.casualties_.count(id)) ||
               !result.voyages_.count(m.rescue_voyage) ||
               !result.submitted_ids_.count(m.triage_command) ||
               (m.stabilization_command && !result.submitted_ids_.count(m.stabilization_command)) ||
               (m.treatment_command && (!result.submitted_ids_.count(m.treatment_command) ||
                  !result.clinics_.count(m.clinic_site) ||
                  !result.named_medics_.count(m.medic_id) ||
                  result.named_medics_.at(m.medic_id).units<3)))
                throw std::runtime_error("snapshot: invalid medical evidence link");
            const auto has_receipt=[&](std::uint64_t cmd,std::uint64_t tick){
                return std::any_of(result.events_.begin(),result.events_.end(),
                    [&](const Event& e){return e.command_id==cmd && e.tick==tick &&
                        e.result==Outcome::Applied;});
            };
            if(result.snapshot_version_>=39 &&
               (!has_receipt(m.triage_command,m.triage_tick) ||
                (m.stabilization_command && !has_receipt(m.stabilization_command,m.stabilized_tick)) ||
                (m.treatment_command && !has_receipt(m.treatment_command,m.treatment_tick))))
                throw std::runtime_error("snapshot: missing medical episode receipt");
            if(m.recovery_due_tick){
                bool served=false;
                for(const auto& [cmd,shift]:result.medic_shifts_){(void)cmd;
                    if(shift.clinic_site==m.clinic_site && shift.medic_id==m.medic_id &&
                       shift.start_tick<=m.treatment_tick && m.treatment_tick<shift.end_tick)
                       served=true;
                }
                if(!served)throw std::runtime_error("snapshot: treated without qualified staff shift");
            }
        };
        for(const auto& [id,m]:result.medical_cases_)check_case(id,m);
        if(result.snapshot_version_>=39){
            for(const auto& [key,m]:result.medical_history_){
                if(key.second!=m.triage_command || (!m.recovered_tick && !m.treatment_command) ||
                   (result.medical_cases_.count(key.first) &&
                    result.medical_cases_.at(key.first).triage_tick<=m.triage_tick))
                    throw std::runtime_error("snapshot: invalid historical medical episode");
                check_case(key.first,m);
            }
            for(const auto& [cmd,shift]:result.medic_shifts_){
                if(!result.clinics_.count(shift.clinic_site) ||
                   !result.named_medics_.count(shift.medic_id) ||
                   result.named_medics_.at(shift.medic_id).clinic_site!=shift.clinic_site ||
                   !result.submitted_ids_.count(cmd) ||
                   !std::any_of(result.events_.begin(),result.events_.end(),
                       [&](const Event& e){return e.command_id==cmd && e.tick==shift.start_tick &&
                           e.result==Outcome::Applied;}))
                    throw std::runtime_error("snapshot: invalid medic shift receipt");
                for(const auto& [other_id,other]:result.medic_shifts_){
                    if(other_id>=cmd)break;
                    if(shift.start_tick<other.end_tick && other.start_tick<shift.end_tick &&
                       shift.medic_id==other.medic_id)
                        throw std::runtime_error("snapshot: overlapping shifts for one medic");
                }
            }
            for(const auto& [site,cl]:result.clinics_){
                std::vector<std::pair<std::uint64_t,int>> edges;
                for(const auto& [cmd,shift]:result.medic_shifts_){(void)cmd;
                    if(shift.clinic_site==site){edges.emplace_back(shift.start_tick,1);
                        edges.emplace_back(shift.end_tick,-1);}
                }
                std::sort(edges.begin(),edges.end()); // ending shifts release capacity first
                std::int64_t concurrent=0;
                for(const auto& [tick,delta]:edges){(void)tick;
                    concurrent+=delta;
                    if(concurrent<0 || concurrent>cl.medics)
                        throw std::runtime_error("snapshot: exceeded clinic shift staffing slots");
                }
            }
            for(const auto& [ship,kit]:result.mobile_medical_kits_){
                if(!result.entities_.count(ship) || !result.colony_ships_.count(ship) ||
                   (kit.water_kg+kit.substrate_kg+kit.wastewater_kg+kit.medical_waste_kg)<1)
                    throw std::runtime_error("snapshot: orphaned mobile medical stock");
            }
        }
        for(const auto& [site,c]:result.clinics_){
            std::uint64_t training=0;
            for(const auto& [id,q]:result.named_medics_){(void)id;
                if(q.clinic_site==site)training+=q.units;
            }
            if(c.training_units!=training)
                throw std::runtime_error("snapshot: training units and medical stocks diverge");
            std::uint64_t units=0,cases=0;
            for(const auto& [id,m]:result.medical_cases_){(void)id;
                if(m.clinic_site==site){units+=m.severity;++cases;}
            }
            for(const auto& [key,m]:result.medical_history_){(void)key;
                if(m.clinic_site==site){units+=m.severity;++cases;}
            }
            if(c.ems_treatment_units!=units || c.ems_treated_cases!=cases)
                throw std::runtime_error("snapshot: clinic EMS treatment totals mismatch");
        }
    }
    if (pending_ids.size() + event_ids.size() != result.submitted_ids_.size())
        throw std::runtime_error("snapshot: missing or extra command receipts");
    for (const auto id : pending_ids) if (!result.submitted_ids_.count(id))
        throw std::runtime_error("snapshot: pending command receipt missing");
    for (const auto id : event_ids) if (!result.submitted_ids_.count(id))
        throw std::runtime_error("snapshot: event receipt missing");
    if(result.snapshot_version_>=5){
        if(parsed_systems!=result.systems_)
            throw std::runtime_error("snapshot: missing registered system");
        for(const auto& [id,e]:result.entities_){
            if(!result.systems_.count(e.system_id))throw std::runtime_error("snapshot: unregistered entity system");
            if(e.type==EntityType::Stargate && !result.systems_.count(e.gate_destination))
                throw std::runtime_error("snapshot: invalid gate destination system");
            if(e.type==EntityType::Ship && e.jump_state==ShipJumpState::Jumping){
                const auto* gate=result.find(e.pending_gate);
                if(!gate || gate->type!=EntityType::Stargate || gate->system_id!=e.system_id)
                    throw std::runtime_error("snapshot: jumping ship has no valid origin gate");
            }
        }
        for(const auto& a:result.activity_events_)if(a.kind==ActivityKind::JumpCompleted){
            const auto* ship=result.find(a.actor);const auto* gate=result.find(a.target);
            if(!ship || (ship->type!=EntityType::Ship && ship->type!=EntityType::Wreck) ||
               !gate || gate->type!=EntityType::Stargate ||
               ship->system_id!=gate->gate_destination) {
               // Future return travel could legitimately move a ship again; each
               // historical jump is validated by durable per-commit snapshots.
               if(!ship || !gate || (ship->type!=EntityType::Ship && ship->type!=EntityType::Wreck) ||
                  gate->type!=EntityType::Stargate)
                   throw std::runtime_error("snapshot: bad historic transfer references");
            }
        }
    }
    for(const auto& [pair,op]:result.gate_ops_){
        const auto* a=result.find(pair.first);const auto* b=result.find(pair.second);
        if(!a || !b || a->type!=EntityType::Stargate || b->type!=EntityType::Stargate ||
           a->system_id!=b->gate_destination || b->system_id!=a->gate_destination ||
           (op.condition==0 && (a->gate_online || b->gate_online)))
            throw std::runtime_error("snapshot: gate operations topology/condition invalid");
    }
    if(result.snapshot_version_>=19)for(const auto& [id,h]:result.habitats_){
        const auto techs=result.utilities_.count(id)?result.utilities_.at(id).technicians:0;
        const auto medics=result.clinics_.count(id)?result.clinics_.at(id).medics:0;
        const auto attendants=result.communities_.count(id)?result.communities_.at(id).attendants:0;
        if(techs+medics+attendants+result.assigned_policy_inspectors(id)>h.residents)
            throw std::runtime_error("snapshot: policy labor overcommitted");
    }
    for(const auto& [id,g]:result.geology_){
        const auto* rock=result.find(id);
        if(!rock || rock->type!=EntityType::Asteroid || rock->ore<=0 ||
           rock->ore>g.original_tonnes || g.original_tonnes>1000000000LL)
            throw std::runtime_error("snapshot: geological mass/identity invalid");
    }
    for(const auto& [id,c]:result.colony_ships_){
        const auto* ship=result.find(id);
        if(!ship || (ship->type!=EntityType::Ship &&
           !(result.snapshot_version_>=36 && ship->type==EntityType::Wreck && c.crew==0 &&
             [&](){for(const auto& [vid,v]:result.voyages_){(void)vid;
                if(v.ferry==id && v.status==6)return true;
             }return false;}())) || !ship->owner_id ||
           result.loaded_cargo_liters(*ship)>ship->cargo_capacity*1000)
            throw std::runtime_error("snapshot: invalid colony ship or overloaded inventory");
    }
    for(const auto& [id,site]:result.built_outposts_){
        const auto* station=result.find(id);
        const auto* ship=result.find(site.source_colony_ship);
        if(!station || station->type!=EntityType::Station || !station->owner_id ||
           !ship || ship->type!=EntityType::Ship || !result.colony_ships_.count(ship->id) ||
           ship->owner_id!=station->owner_id || !result.hangar(id,station->owner_id))
            throw std::runtime_error("snapshot: invalid player-built outpost");
    }
    for(const auto& [id,st]:result.storages_){
        if(result.snapshot_version_>=30){
            try{validate_site_conveyors(st);}catch(const std::exception&){
                throw std::runtime_error("snapshot: invalid installed conveyor route");
            }
        }
        auto i=result.built_outposts_.find(id);
        if(i==result.built_outposts_.end() || i->second.source_colony_ship!=st.source_ship)
            throw std::runtime_error("snapshot: orphaned storage");
    }
    for(const auto& [id,w]:result.workshops_){
        const auto outpost=result.built_outposts_.find(id);
        if(outpost==result.built_outposts_.end() ||
           outpost->second.source_colony_ship!=w.source_ship)
            throw std::runtime_error("snapshot: workshop lacks real outpost");
        if(w.finisher_npc_id){
            const auto n=result.npcs_.find(w.finisher_npc_id);
            if(n==result.npcs_.end() || n->second.home_ship!=w.source_ship ||
               n->second.location!=id || n->second.role!=NpcRole::Habitat)
                throw std::runtime_error("snapshot: invalid finisher operator");
        }
        if(w.operator_npc_id){
            const auto n=result.npcs_.find(w.operator_npc_id);
            if(n==result.npcs_.end() || n->second.home_ship!=w.source_ship ||
               n->second.location!=id || n->second.role!=NpcRole::Habitat)
                throw std::runtime_error("snapshot: invalid workshop operator");
        }
        if(result.snapshot_version_>=108){
            if((w.supervisor_npc_id==0)!=(w.supervisor_appointment_id==0))
                throw std::runtime_error("snapshot: partial workshop manager appointment");
            if(w.supervisor_npc_id){
                const auto n=result.npcs_.find(w.supervisor_npc_id);
                const auto* home=result.find(w.source_ship);
                if(n==result.npcs_.end() ||
                   n->second.home_ship!=w.source_ship ||
                   n->second.location!=id || n->second.assigned_site!=id ||
                   n->second.role!=NpcRole::Habitat ||
                   n->second.transport_id ||
                   !home || !result.workshop_trade_certified(
                       w.supervisor_npc_id,w.supervisor_trade) ||
                   w.operator_npc_id==w.supervisor_npc_id ||
                   w.finisher_npc_id==w.supervisor_npc_id ||
                   !result.submitted_ids_.count(w.supervisor_appointment_id) ||
                   !std::any_of(result.events_.begin(),result.events_.end(),
                      [&](const Event& e){
                          return e.command_id==w.supervisor_appointment_id &&
                                 e.result==Outcome::Applied;}))
                    throw std::runtime_error("snapshot: manager lacks real NPC or appointment");
                for(const auto& [other_site,other_shop]:result.workshops_)
                    if(other_site!=id &&
                       other_shop.supervisor_npc_id==w.supervisor_npc_id)
                        throw std::runtime_error("snapshot: manager assigned to multiple original shops");
            }
            for(unsigned slot=0;slot<2;++slot){
                const auto& job=w.approved_jobs[slot];
                if(!job.command_id)continue;
                const auto actual=slot==0?w.completed_batches:w.finisher_batches;
                if(!job.supervisor_npc_id ||
                   !job.supervisor_appointment_id ||
                   (w.supervisor_npc_id &&
                    (job.supervisor_npc_id!=w.supervisor_npc_id ||
                     job.supervisor_appointment_id!=w.supervisor_appointment_id ||
                     w.supervisor_trade!=static_cast<WorkshopTrade>(slot+1))) ||
                   job.baseline_batches>actual ||
                   !result.submitted_ids_.count(job.command_id) ||
                   !std::any_of(result.events_.begin(),result.events_.end(),
                       [&](const Event& e){return e.command_id==job.command_id &&
                          e.tick==job.approved_tick && e.result==Outcome::Applied &&
                          e.transferred==0;}))
                    throw std::runtime_error("snapshot: original workshop job lacked owner approval");
            }
        }
        if(result.snapshot_version_>=106){
            for(const auto cancelled:w.cancelled_pending_shifts){
                bool pending_found=false;
                for(const auto& [due,orders]:result.commands_){
                    if(due<=result.tick_)continue;
                    for(const auto& pending:orders)
                        if(pending.id==cancelled &&
                           pending.action==Action::StartWorkshopShift &&
                           pending.target==id &&
                           pending.actor==w.source_ship)
                            pending_found=true;
                }
                if(!pending_found)
                    throw std::runtime_error("snapshot: cancelled shift has no original pending order");
            }
            for(unsigned slot=0;slot<2;++slot){
                const auto& sh=w.shifts[slot];
                if(!sh.command_id)continue;
                if(!result.submitted_ids_.count(sh.command_id) ||
                   !std::any_of(result.events_.begin(),result.events_.end(),
                     [&](const Event& ev){return ev.command_id==sh.command_id &&
                         ev.tick==sh.start_tick && ev.result==Outcome::Applied &&
                         ev.transferred==0;}))
                    throw std::runtime_error("snapshot: shift missing original command proof");
                if(sh.relief_command_id &&
                   (result.snapshot_version_<107 ||
                    sh.relief_command_id==sh.command_id ||
                    !result.submitted_ids_.count(sh.relief_command_id) ||
                    !std::any_of(result.events_.begin(),result.events_.end(),
                       [&](const Event& ev){return ev.command_id==sh.relief_command_id &&
                           ev.tick>sh.start_tick && ev.tick<=result.tick_ &&
                           ev.tick<sh.end_tick && ev.result==Outcome::Applied &&
                           ev.transferred==0;})))
                    throw std::runtime_error("snapshot: relief lacks original applied command");
            }
        }
    }
    for(const auto& [id,f]:result.farms_){
        const auto* site=result.find(id);const auto* ship=result.find(f.source_ship);
        if(!site || site->type!=EntityType::Station ||
           !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=f.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           !result.colony_ships_.count(f.source_ship))
            throw std::runtime_error("snapshot: orphaned or foreign farm");
    }
    for(const auto& [id,policy]:result.policy_sites_){
        const auto it=result.built_outposts_.find(id);
        const auto h=result.habitats_.find(id);
        const auto* site=result.find(id);
        if(it==result.built_outposts_.end() || h==result.habitats_.end() ||
           it->second.source_colony_ship!=policy.source_ship ||
           !site || !result.find(policy.source_ship) ||
           site->owner_id!=result.find(policy.source_ship)->owner_id)
            throw std::runtime_error("snapshot: orphaned policy");
    }
    for(const auto& [id,p]:result.waterworks_){
        const auto* site=result.find(id);const auto* ship=result.find(p.source_ship);
        if(!site || site->type!=EntityType::Station || !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=p.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           !result.habitats_.count(id) || !result.utilities_.count(id))
            throw std::runtime_error("snapshot: orphaned waterworks");
    }
    for(const auto& [id,r]:result.reclaimers_){
        const auto* site=result.find(id);const auto* ship=result.find(r.source_ship);
        if(!site || site->type!=EntityType::Station || !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=r.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           !result.habitats_.count(id) || !result.utilities_.count(id))
            throw std::runtime_error("snapshot: orphaned reclaimer");
    }
    for(const auto& [id,u]:result.utilities_){
        const auto* site=result.find(id);const auto* ship=result.find(u.source_ship);
        if(!site || site->type!=EntityType::Station ||
           !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=u.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           !result.colony_ships_.count(u.source_ship) ||
           (u.technicians && (!result.habitats_.count(id) ||
              u.technicians>result.habitats_.at(id).residents)))
            throw std::runtime_error("snapshot: orphaned or foreign utility");
    }
    for(const auto& [id,c]:result.communities_){
        const auto* site=result.find(id);const auto* ship=result.find(c.source_ship);
        const auto hi=result.habitats_.find(id);const auto ui=result.utilities_.find(id);
        if(!site || site->type!=EntityType::Station || !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=c.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           hi==result.habitats_.end() ||
           c.attendants+(ui==result.utilities_.end()?0:ui->second.technicians) > hi->second.residents)
            throw std::runtime_error("snapshot: orphaned or oversubscribed community hall");
    }
    for(const auto& [id,c]:result.clinics_){
        const auto* site=result.find(id);const auto* ship=result.find(c.source_ship);
        const auto hi=result.habitats_.find(id);const auto ui=result.utilities_.find(id);
        const auto ci=result.communities_.find(id);
        const auto attendants=ci==result.communities_.end()?0:ci->second.attendants;
        const auto techs=ui==result.utilities_.end()?0:ui->second.technicians;
        if(!site || site->type!=EntityType::Station || !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=c.source_ship ||
           !ship || ship->owner_id!=site->owner_id || hi==result.habitats_.end() ||
           c.medics+attendants+techs>hi->second.residents ||
           c.active_cases>hi->second.residents+(result.farms_.count(id)?result.farms_.at(id).workers:0))
            throw std::runtime_error("snapshot: orphaned or overstaffed clinic");
    }
    for(const auto& [id,h]:result.habitats_){
        const auto* site=result.find(id);const auto* ship=result.find(h.source_ship);
        if(!site || site->type!=EntityType::Station ||
           !result.built_outposts_.count(id) ||
           result.built_outposts_.at(id).source_colony_ship!=h.source_ship ||
           !ship || ship->owner_id!=site->owner_id ||
           !result.colony_ships_.count(h.source_ship) ||
           (result.farms_.count(id) && result.farms_.at(id).workers+h.residents>h.capacity))
            throw std::runtime_error("snapshot: orphaned or over-capacity habitat");
    }
    if(result.snapshot_version_>=27){
        std::map<std::uint64_t,std::uint64_t> moved_totals;
        for(const auto& a:result.activity_events_){
            if(a.kind!=ActivityKind::AutoStorageTransfer)continue;
            const auto* r=result.storage_link(a.actor,a.target);
            if(!r || a.start_command_id!=r->id || a.transferred>r->units_per_tick ||
               a.transferred<=0 || !result.submitted_ids_.count(r->id))
                throw std::runtime_error("snapshot: invalid automated storage effect");
            auto& sum=moved_totals[r->id];
            if(static_cast<std::uint64_t>(a.transferred)>
               std::numeric_limits<std::uint64_t>::max()-sum)
                throw std::runtime_error("snapshot: storage effect overflow");
            sum+=static_cast<std::uint64_t>(a.transferred);
        }
        for(const auto& [site,st]:result.storages_){(void)site;
            std::map<std::uint32_t,std::vector<std::uint32_t>> routes;
            for(const auto& [id,link]:st.links){
                if(moved_totals[id]!=link.units_moved ||
                   !result.submitted_ids_.count(id))
                    throw std::runtime_error("snapshot: incomplete storage transfer history");
                if(link.source!=kWorkshopConveyorNode && link.destination!=kWorkshopConveyorNode)
                    routes[link.source].push_back(link.destination);
            }
            // Defensive cycle check even for malformed manually edited saves.
            for(const auto& [id,link]:st.links){(void)id;
                if(link.source==kWorkshopConveyorNode || link.destination==kWorkshopConveyorNode)continue;
                std::set<std::uint32_t> seen;std::vector<std::uint32_t> todo{link.destination};
                while(!todo.empty()){
                    auto node=todo.back();todo.pop_back();
                    if(!seen.insert(node).second)continue;
                    if(node==link.source)throw std::runtime_error("snapshot: cyclic storage links");
                    for(auto to:routes[node])todo.push_back(to);
                }
            }
        }
    }
    if(result.snapshot_version_>=21) for(const auto& a:result.activity_events_){
        if(a.kind==ActivityKind::NpcLost && (a.target>=result.next_npc_id_ || !a.target ||
              result.npcs_.count(a.target) || !result.colony_ships_.count(a.actor)))
            throw std::runtime_error("snapshot: invalid deceased NPC identity");
        if(a.kind==ActivityKind::ShipDestroyed){
            const auto* wreck=result.find(a.target);
            if(!wreck || wreck->type!=EntityType::Wreck)
                throw std::runtime_error("snapshot: missing destroyed ship wreck");
        }
    }
    if(result.snapshot_version_>=20){
        if(!result.npc_next_seen_)throw std::runtime_error("snapshot: missing NPC counter");
        std::map<std::pair<EntityId,int>,std::int64_t> counts;
        std::map<std::uint64_t,int> mentor_occupancy;
        for(const auto& [id,n]:result.npcs_){
            // A 4C.3 pilot remains a single existing NPC, with its only
            // physical custody linked to a versioned shuttle flight state.
            const bool shuttle_custody=result.snapshot_version_>=81 && [&](){
                for(const auto& [key,sh]:result.planetary_shuttles_) {
                    if(sh.pilot_npc!=id &&
                       !std::binary_search(sh.passenger_ids.begin(),
                                           sh.passenger_ids.end(),id) &&
                       !std::binary_search(sh.disembarked_ids.begin(),
                                           sh.disembarked_ids.end(),id))continue;
                    const auto site=result.planetary_sites_.find(key);
                    if(site==result.planetary_sites_.end())return false;
                    const bool visitor=std::binary_search(
                        sh.disembarked_ids.begin(),sh.disembarked_ids.end(),id);
                    const auto expected=visitor?site->second.surface_location:
                        ((sh.flight_phase==1 || sh.flight_phase==3)?
                        shuttle_transit_custody_id(key):
                        (sh.flight_phase==2?site->second.surface_location:sh.source_ship));
                    return n.location==expected && n.home_ship==sh.source_ship &&
                           n.role==NpcRole::Ship && n.assigned_site==sh.source_ship &&
                           !n.transport_id;
                }
                return false;
            }();
            if(n.transport_id && (result.snapshot_version_<32 || !result.colony_ships_.count(n.location) ||
                (n.location==n.assigned_site && !result.crew_rehomes_.count(n.id))))
                throw std::runtime_error("snapshot: invalid active passenger custody");
            if(result.snapshot_version_>=32 && !n.assigned_site)throw std::runtime_error("snapshot: missing assignment");
            if(!result.colony_ships_.count(n.home_ship) ||
                (n.role==NpcRole::Ship && n.assigned_site!=n.home_ship) ||
                (n.role==NpcRole::Farm && (!result.farms_.count(n.assigned_site) ||
                  result.farms_.at(n.assigned_site).source_ship!=n.home_ship)) ||
                (n.role==NpcRole::Habitat && (!result.habitats_.count(n.assigned_site) ||
                  result.habitats_.at(n.assigned_site).source_ship!=n.home_ship)) ||
                (n.role==NpcRole::PlanetarySurface && ![&](){
                    if(result.snapshot_version_<78)return false;
                    for(const auto& [key,facility]:result.planetary_sites_){
                        (void)key;
                        if(facility.surface_location==n.assigned_site &&
                           facility.source_ship==n.home_ship &&
                           (facility.delegated_npcs[0]==id ||
                            facility.delegated_npcs[1]==id))return true;
                    }
                    return false;
                }()) ||
                (!n.transport_id && n.location!=n.assigned_site &&
                 !shuttle_custody &&
                 ![&](){for(const auto& [voyage_id,v]:result.voyages_){(void)voyage_id;
                     if((v.status==2 || v.status==5) && v.destination==n.location &&
                        std::binary_search(v.passengers.begin(),v.passengers.end(),id))return true;}
                     return false;}()))
                throw std::runtime_error("snapshot: NPC outside assigned colony");
            counts[{n.assigned_site ? n.assigned_site : n.location,static_cast<int>(n.role)}]++;
            if(n.mentor_id){
                const auto mentor=result.npcs_.find(n.mentor_id);
                if(mentor==result.npcs_.end() || mentor->second.home_ship!=n.home_ship ||
                   mentor->second.assigned_site!=n.assigned_site || n.role!=NpcRole::Farm ||
                   mentor->second.role!=NpcRole::Farm || ++mentor_occupancy[n.mentor_id]>1)
                    throw std::runtime_error("snapshot: invalid mentor relationship");
            }
        }
        for(const auto& [site,xp]:result.farm_team_xp_){(void)xp;
            if(!result.farms_.count(site) || xp>result.farms_.at(site).production_cycles)
                throw std::runtime_error("snapshot: impossible team experience");
        }
        for(const auto& [id,c]:result.colony_ships_)
            if(counts[{id,static_cast<int>(NpcRole::Ship)}]!=c.crew)
                throw std::runtime_error("snapshot: ship NPC count mismatch");
        for(const auto& [id,f]:result.farms_)
            if(counts[{id,static_cast<int>(NpcRole::Farm)}]!=f.workers)
                throw std::runtime_error("snapshot: farm NPC count mismatch");
        for(const auto& [id,h]:result.habitats_)
            if(counts[{id,static_cast<int>(NpcRole::Habitat)}]!=h.residents)
                throw std::runtime_error("snapshot: habitat NPC count mismatch");
        if(result.snapshot_version_>=78)for(const auto& [key,facility]:result.planetary_sites_){
            (void)key;
            if(facility.delegated_npcs[0]) {
                if(counts[{facility.surface_location,
                           static_cast<int>(NpcRole::PlanetarySurface)}]!=
                           facility.assigned_workers)
                    throw std::runtime_error("snapshot: planetary NPC count mismatch");
            } else if(!result.npcs_.empty() && facility.assigned_workers)
                throw std::runtime_error("snapshot: named planetary NPC missing");
        }
    }
    // All v116 approvals, including CLEARED historical family flights,
    // remain forever disjoint from ordinary commands, work and transit.
    if(result.snapshot_version_>=116)
        for(const auto approval:result.planetary_family_booking_ids_) {
            if(result.submitted_ids_.count(approval) ||
               result.shuttle_flight_request_ids_.count(approval) ||
               result.local_delivery_orders_.count(approval) ||
               result.voyages_.count(approval) ||
               result.pending_celestial_transits_.count(approval) ||
               result.completed_celestial_transits_.count(approval) ||
               (result.legacy_planetary_residence_import_ &&
                result.legacy_planetary_residence_import_->host_request_id==
                    approval))
                throw std::runtime_error("snapshot: family request ID crossed authorities");
            for(const auto& [key,site]:result.planetary_sites_) {
                (void)key;
                if(site.housing_order_id==approval ||
                   site.utility_order_id==approval)
                    throw std::runtime_error("snapshot: family ID reused as building");
                for(const auto request:site.founder_consent_requests)
                    if(request==approval)
                        throw std::runtime_error("snapshot: family ID reused as founder");
                for(const auto& [person,proof]:site.visitor_residency) {
                    (void)person;
                    if(proof[1]==approval || proof[2]==approval)
                        throw std::runtime_error("snapshot: family ID reused as settler");
                }
                for(const auto& p:site.production_projects)
                    if(p.order_id==approval)
                        throw std::runtime_error("snapshot: family ID reused as production");
                for(const auto& p:site.service_projects)
                    if(p.order_id==approval)
                        throw std::runtime_error("snapshot: family ID reused as service");
                for(const auto& r:site.utility_handovers)
                    if(r.request_id==approval)
                        throw std::runtime_error("snapshot: family ID reused as utility relief");
                for(const auto& h:site.production_handovers)
                    for(const auto& r:h)
                        if(r.request_id==approval)
                            throw std::runtime_error("snapshot: family ID reused as production relief");
                for(const auto& h:site.service_handovers)
                    for(const auto& r:h)
                        if(r.request_id==approval)
                            throw std::runtime_error("snapshot: family ID reused as service relief");
            }
        }
    // v116: bind each family seat to one original physical dependent and
    // its same-site named guardian. Never synthesize a second NPC roster.
    std::set<std::uint64_t> planetary_child_berths;
    if(result.snapshot_version_>=116)
        for(const auto& [key,sh]:result.planetary_shuttles_) {
            const auto& family=sh.family;
            if(!family.stage)continue;
            const auto site=result.planetary_sites_.find(key);
            if(site==result.planetary_sites_.end() ||
               sh.passenger_ids.size()+family.child_ids.size()>3 ||
               !std::binary_search(sh.passenger_ids.begin(),
                                   sh.passenger_ids.end(),family.guardian_id) ||
               (family.co_parent_id &&
                !std::binary_search(sh.passenger_ids.begin(),
                                    sh.passenger_ids.end(),family.co_parent_id)) ||
               !result.planetary_family_booking_ids_.count(family.booking_request) ||
               result.submitted_ids_.count(family.booking_request) ||
               result.shuttle_flight_request_ids_.count(family.booking_request) ||
               result.voyages_.count(family.booking_request) ||
               result.local_delivery_orders_.count(family.booking_request))
                throw std::runtime_error("snapshot: orphan or reused family booking");
            const auto custody=sh.flight_phase==1 || sh.flight_phase==3?
                shuttle_transit_custody_id(key):
                sh.flight_phase==2?site->second.surface_location:sh.source_ship;
            for(const auto adult:{family.guardian_id,family.co_parent_id}) {
                if(!adult)continue;
                const auto n=result.npcs_.find(adult);
                if(n==result.npcs_.end() || n->second.home_ship!=sh.source_ship ||
                   n->second.location!=custody || n->second.role!=NpcRole::Ship ||
                   n->second.assigned_site!=sh.source_ship)
                    throw std::runtime_error("snapshot: separated family adult");
            }
            for(const auto child:family.child_ids) {
                const auto dep=result.physical_dependents_.find(child);
                if(!planetary_child_berths.insert(child).second ||
                   dep==result.physical_dependents_.end() ||
                   dep->second.home_ship!=sh.source_ship ||
                   dep->second.location!=custody ||
                   (dep->second.guardian_id!=family.guardian_id &&
                    dep->second.guardian_id!=family.co_parent_id))
                    throw std::runtime_error("snapshot: separated or repeated child berth");
            }
            for(const auto& [other_key,other]:result.planetary_shuttles_)
                if(other_key!=key &&
                   other.family.booking_request==family.booking_request)
                    throw std::runtime_error("snapshot: reused family approval");
        }
    // v118 permanently records admitted / departed ACTUAL households.
    // A residing child is in real planetary custody even after the shuttle
    // family manifest is released; never require an imaginary NPC clone.
    std::set<std::uint64_t> planetary_household_children,household_receipts;
    if(result.snapshot_version_>=118)
        for(const auto& [key,site]:result.planetary_sites_) {
            const auto sh=result.planetary_shuttles_.find(key);
            std::int64_t current_children=0;
            for(const auto& [guardian,house]:site.households) {
                if(sh==result.planetary_shuttles_.end() ||
                   site.housing_phase!=2 ||
                   site.service_projects[0].phase!=2 ||
                   house.consent_mask!=3 || !house.co_parent_id ||
                   guardian==house.co_parent_id ||
                   house.child_ids.size()!=1 ||
                   !result.planetary_family_booking_ids_.count(
                       house.booking_request) ||
                   !result.shuttle_flight_request_ids_.count(
                       house.arrival_receipt) ||
                   !household_receipts.insert(house.admission_request).second ||
                   (house.return_request &&
                    !household_receipts.insert(house.return_request).second))
                    throw std::runtime_error("snapshot: unapproved household residence");
                const auto child=house.child_ids.front();
                const auto dep=result.physical_dependents_.find(child);
                if(dep==result.physical_dependents_.end() ||
                   dep->second.home_ship!=site.source_ship ||
                   (dep->second.guardian_id!=guardian &&
                    dep->second.guardian_id!=house.co_parent_id) ||
                   !planetary_household_children.insert(child).second)
                    throw std::runtime_error("snapshot: repeated/unowned household child");
                const auto expected_legal=house.return_request?
                    transport::Endpoint{transport::LocationKind::ColonyShip,
                                        site.source_ship}:
                    transport::Endpoint{transport::LocationKind::PlanetarySite,
                                        site.surface_location};
                for(const auto adult:{guardian,house.co_parent_id}) {
                    const auto n=result.npcs_.find(adult);
                    if(n==result.npcs_.end() ||
                       n->second.home_ship!=site.source_ship ||
                       n->second.legal_residence()!=expected_legal)
                        throw std::runtime_error("snapshot: household parent legal address mismatch");
                }
                if(!house.return_request) {
                    ++current_children;
                    const auto bound=sh->second.family.stage>=5 &&
                        sh->second.family.guardian_id==guardian &&
                        sh->second.family.co_parent_id==house.co_parent_id &&
                        sh->second.family.child_ids==house.child_ids;
                    if(dep->second.location!=site.surface_location &&
                       !bound)
                        throw std::runtime_error("snapshot: active child missing from planet");
                    planetary_child_berths.insert(child);
                }else if(dep->second.location!=site.source_ship)
                    throw std::runtime_error("snapshot: returned child missing home custody");
            }
            if(current_children>6)
                throw std::runtime_error("snapshot: unsupported planetary housing density");
        }
    if(result.snapshot_version_>=59)for(const auto& [id,d]:result.physical_dependents_){
        const auto g=result.npcs_.find(d.guardian_id);
        if(g==result.npcs_.end() || result.npcs_.count(id) || result.casualties_.count(id) ||
           !result.colony_ships_.count(d.home_ship) ||
           (!result.colony_ships_.count(d.location) &&
            !planetary_child_berths.count(id)) ||
           g->second.home_ship!=d.home_ship || g->second.location!=d.location ||
           (d.location!=d.home_ship && !planetary_child_berths.count(id)))
            throw std::runtime_error("snapshot: child orphaned or without verified physical guardian");
        const auto receipt=std::find_if(result.events_.begin(),result.events_.end(),
            [&](const Event& e){return e.command_id==d.registration_command &&
                e.tick==d.registered_tick && e.result==Outcome::Applied &&
                e.transferred==0;});
        if(receipt==result.events_.end())
            throw std::runtime_error("snapshot: dependent registration lacks applied host event");
    }
    // Validate the immutable full-roster handoff against the final physical
    // simulation, not just against a promising event label.
    if(result.snapshot_version_>=58)for(const auto& [ship_id,d]:result.colony_departures_){
        const auto from=result.colony_ships_.find(ship_id);
        const auto to=result.colony_ships_.find(d.receiver);
        if(from==result.colony_ships_.end()||to==result.colony_ships_.end()||
           from->second.crew!=0||to->second.crew<static_cast<std::int64_t>(d.relocated_npcs.size())||
           !result.entities_.count(ship_id) ||
           result.entities_.at(ship_id).type!=EntityType::Ship ||
           !result.entities_.count(d.receiver) ||
           result.entities_.at(d.receiver).type!=EntityType::Ship)
            throw std::runtime_error("snapshot: invalid evacuated intact colony hull");
        for(const auto& [id,n]:result.npcs_)
            if(n.home_ship==ship_id)
                throw std::runtime_error("snapshot: colony still owns living people after departure");
        for(const auto& [site,fr]:result.farms_)
            if(fr.source_ship==ship_id && fr.workers)
                throw std::runtime_error("snapshot: stranded farming workforce");
        for(const auto& [site,ha]:result.habitats_)
            if(ha.source_ship==ship_id && ha.residents)
                throw std::runtime_error("snapshot: stranded resident workforce");
        for(auto id:d.relocated_npcs){
            const auto n=result.npcs_.find(id);
            if(n==result.npcs_.end() || n->second.home_ship!=d.receiver ||
               n->second.location!=d.receiver || n->second.assigned_site!=d.receiver ||
               n->second.role!=NpcRole::Ship || n->second.transport_id)
                throw std::runtime_error("snapshot: departed person not under receiving crew custody");
        }
        for(const auto& [id,dependent]:result.physical_dependents_){
            (void)id;
            if(dependent.home_ship==ship_id || dependent.location==ship_id)
                throw std::runtime_error("snapshot: evacuated colony abandoned a dependent");
        }
        for(auto id:d.relocated_dependents){
            const auto child=result.physical_dependents_.find(id);
            if(child==result.physical_dependents_.end() ||
               child->second.home_ship!=d.receiver || child->second.location!=d.receiver ||
               !std::binary_search(d.relocated_npcs.begin(),d.relocated_npcs.end(),
                                   child->second.guardian_id))
                throw std::runtime_error("snapshot: dependent separated from evacuated guardian");
        }
        const auto receipt=std::find_if(result.events_.begin(),result.events_.end(),
            [&](const Event& e){return e.command_id==d.command_id &&
                e.tick==d.tick && e.result==Outcome::Applied &&
                e.transferred==0;});
        if(receipt==result.events_.end())
            throw std::runtime_error("snapshot: departed colony lacks applied world event");
    }
    for(const auto& [ship_id,c]:result.colony_ships_){
        std::int64_t crew=c.crew;
        for(const auto& [id,f]:result.farms_){(void)id;
            if(f.source_ship==ship_id)crew+=f.workers;
        }
        for(const auto& [id,h]:result.habitats_){(void)id;
            if(h.source_ship==ship_id)crew+=h.residents;
        }
        if((crew<1 && !(result.snapshot_version_>=58 && !crew && result.colony_departures_.count(ship_id) && result.entities_.at(ship_id).type==EntityType::Ship) && !(result.snapshot_version_>=36 && !crew &&
            result.entities_.count(ship_id) && result.entities_.at(ship_id).type==EntityType::Wreck)) ||
           crew>1000)throw std::runtime_error("snapshot: invalid total crew");
    }
    for(const auto& [id,e]:result.entities_){
        if(e.type==EntityType::Station && e.owner_id && !result.built_outposts_.count(id))
            throw std::runtime_error("snapshot: player station lacks construction origin");
    }
    for (const auto& [key, hangar] : result.hangars_) {
        (void)hangar;
        const auto it = result.entities_.find(key.first);
        if (it == result.entities_.end() || it->second.type != EntityType::Station)
            throw std::runtime_error("snapshot: hangar references nonexistent station");
    }
    for (const auto& [id, entity] : result.entities_) {
        (void)id;
        if (entity.type != EntityType::Ship || entity.dock_state == DockState::InSpace) continue;
        const auto it = result.entities_.find(entity.dock_station);
        if (it == result.entities_.end() || it->second.type != EntityType::Station ||
            it->second.system_id!=entity.system_id ||
            !result.hangar(entity.dock_station, entity.owner_id))
            throw std::runtime_error("snapshot: dock state references missing station or private hangar");
        if ((entity.dock_state == DockState::Docked || entity.dock_state == DockState::Undocking) &&
            (entity.position.x != it->second.position.x || entity.position.y != it->second.position.y))
            throw std::runtime_error("snapshot: docked ship position contradicts station");
    }
    for (const auto& [id,m] : result.markets_) {
        const auto station=result.find(id);
        if (!station || station->type!=EntityType::Station || m.ore<0 || m.credits<0)
            throw std::runtime_error("snapshot: market missing station");
    }
    for (const auto& [owner,balance] : result.wallets_) {
        if (owner==0 || balance<0) throw std::runtime_error("snapshot: invalid wallet");
    }
    for (const auto& [actor, job] : result.mining_) {
        const auto it = result.entities_.find(actor);
        if (it == result.entities_.end() || it->second.type != EntityType::Ship ||
            it->second.dock_state != DockState::InSpace ||
            !result.entities_.count(job.target) ||
            result.entities_.at(job.target).type != EntityType::Asteroid ||
            result.depleted_.count(job.target) || !result.submitted_ids_.count(job.start_command_id))
            throw std::runtime_error("snapshot: orphaned auto-mining job");
    }
    for (const auto& a : result.activity_events_) {
        if (a.start_command_id != 0 && !result.submitted_ids_.count(a.start_command_id))
            throw std::runtime_error("snapshot: activity refers to unknown command");
        if (a.kind == ActivityKind::AsteroidDespawned && !result.depleted_.count(a.target))
            throw std::runtime_error("snapshot: missing depleted tombstone");
    }
    if(result.snapshot_version_>=77) {
        for(const auto& [key,site]:result.planetary_sites_) {
            if(key!=PlanetarySiteKey{site.region.body,site.region.latitude,
                                    site.region.longitude,site.region.revision})
                throw std::runtime_error("snapshot: planetary cell key mismatch");
            const auto* body=body_for_local_environment(
                result.physical_celestial_systems_,site.region.body);
            const auto* source_ship=result.find(site.source_ship);
            if(!body || (body->kind!=CelestialKind::Planet &&
                         body->kind!=CelestialKind::Moon) ||
               !site.owner || !site.source_ship ||
               (source_ship && source_ship->owner_id!=site.owner))
                throw std::runtime_error("snapshot: orphan planetary facility");
            if(!site.farm && site.harvest_cycles>0)
                throw std::runtime_error("snapshot: unbuilt farm produced food");
            // v109–v113 retain housing, utilities and physical production
            // WIP as conserved on-site structural modules. A completed
            // production project sets the original farm/extractor flag, but
            // it must not debit the same two modules twice.
            const auto expected_installed=4+4*(
                std::int64_t(site.farm ||
                             site.production_projects[0].phase!=0)+
                std::int64_t(site.extractor ||
                             site.production_projects[1].phase!=0)+
                std::int64_t(site.housing_phase!=0)+
                std::int64_t(site.utility_phase!=0));
            if(site.embodied_ore!=expected_installed)
                throw std::runtime_error("snapshot: invalid regional module custody");
            if(site.extractor && !result.oasis_reserves_.count(site.region.body))
                throw std::runtime_error("snapshot: extractor has no physical source");
            if(result.snapshot_version_>=78) {
                if(site.surface_location!=surface_custody_id(key) ||
                   (site.assigned_workers!=2 &&
                    !(site.assigned_workers==0 && site.spaceport_phase==3 &&
                      !site.delegated_npcs[0] && !site.delegated_npcs[1])) ||
                   site.spaceport_phase<1 || site.spaceport_phase>3 ||
                   site.construction_required!=240 ||
                   site.construction_progress>240 ||
                   (site.spaceport_phase==3 && site.construction_progress!=240) ||
                   (site.spaceport_phase!=3 && (site.farm || site.extractor)))
                    throw std::runtime_error("snapshot: invalid surface port custody");
            }
        }
    }
    if(result.snapshot_version_>=76) {
        for(const auto& [ship,rig]:result.oasis_extractors_) {
            const auto* hull=result.find(ship);
            if(!hull || hull->type!=EntityType::Ship ||
               !result.colony_ships_.count(ship) ||
               !result.life_support_.count(ship) ||
               !result.oasis_reserves_.count(rig.body) ||
               (rig.body>>32)!=hull->system_id ||
               !result.celestial_oasis_prospect(rig.body).has_value() ||
               !result.celestial_oasis_prospect(rig.body)->candidate)
                throw std::runtime_error("snapshot: invalid oasis extractor custody");
        }
    }
    if(result.snapshot_version_>=75) {
        for(const auto& [body,r]:result.oasis_reserves_) {
            (void)r;
            const auto p=result.celestial_oasis_prospect(body);
            if(!p || !p->candidate)
                throw std::runtime_error("snapshot: oasis source outside real catalogue");
        }
    }
    if(result.snapshot_version_>=74) {
        if(result.local_environments_.size()!=result.generated_local_regions_.size())
            throw std::runtime_error("snapshot: local environment count mismatch");
        for(const auto& [body,e]:result.local_environments_) {
            if(!result.generated_local_regions_.count(body))
                throw std::runtime_error("snapshot: orphaned local environment");
            const auto* astronomical_body=
                body_for_local_environment(result.physical_celestial_systems_,body);
            if(!astronomical_body ||
               !same_local_profile(e,local_profile_for(*astronomical_body)))
                throw std::runtime_error("snapshot: altered local environment");
        }
    }
    if(result.snapshot_version_>=73) {
        std::set<EntityId> all_generated_ids;
        for(const auto& [body,manifest]:result.generated_local_regions_) {
            const auto system=static_cast<std::uint32_t>(body>>32);
            const auto it=result.physical_celestial_systems_.find(system);
            if(it==result.physical_celestial_systems_.end() ||
               std::none_of(it->second.begin(),it->second.end(),
                   [body](const PhysicalCelestialBody& e){return e.id==body;}))
                throw std::runtime_error("snapshot: unrecognized generated region");
            for(const auto entity:manifest) {
                if(!entity)continue;
                if(!all_generated_ids.insert(entity).second)
                    throw std::runtime_error("snapshot: reused generated local entity");
                // Each originally allocated deposit is either still in the
                // correct local frame or exists as a durable depletion tombstone.
                const auto live=result.local_entity_anchors_.find(entity);
                if(live==result.local_entity_anchors_.end()) {
                    if(!result.depleted_.count(entity))
                        throw std::runtime_error("snapshot: generated deposit disappeared");
                }else if(live->second!=body)
                    throw std::runtime_error("snapshot: region deposit anchor changed");
            }
        }
        for(const auto& [entity,body]:result.local_entity_anchors_) {
            const auto it=result.entities_.find(entity);
            if(!result.generated_local_regions_.count(body) ||
               !all_generated_ids.count(entity) ||
               it==result.entities_.end() ||
               it->second.type!=EntityType::Asteroid ||
               it->second.system_id!=static_cast<std::uint32_t>(body>>32))
                throw std::runtime_error("snapshot: invalid local region entity custody");
        }
    }
    if(result.snapshot_version_>=72) {
        std::set<EntityId> pending_ships;
        for(const auto& [id,p]:result.pending_celestial_transits_) {
            if(result.completed_celestial_transits_.count(id) ||
               !pending_ships.insert(p.ship).second)
                throw std::runtime_error("snapshot: duplicate active frame transit");
            const auto ship=result.entities_.find(p.ship);
            if(ship==result.entities_.end()||
               ship->second.type!=EntityType::Ship||
               ship->second.owner_id!=p.owner||
               ship->second.system_id!=p.system||
               result.ship_frames_.at(p.ship).anchor_body!=p.source_anchor ||
               ship->second.dock_state!=DockState::InSpace ||
               ship->second.jump_state!=ShipJumpState::Ready)
                throw std::runtime_error("snapshot: invalid pending transit custody");
            const auto system=result.physical_celestial_systems_.find(p.system);
            if(system==result.physical_celestial_systems_.end() ||
               std::none_of(system->second.begin(),system->second.end(),
                    [&p](const PhysicalCelestialBody& b){return b.id==p.destination_body;}))
                throw std::runtime_error("snapshot: nonexistent pending transit target");
        }
        for(const auto& [id,p]:result.completed_celestial_transits_) {
            const auto ship=result.entities_.find(p.ship);
            if(ship==result.entities_.end()||
               ship->second.owner_id!=p.owner ||
               p.system!=ship->second.system_id)
                throw std::runtime_error("snapshot: completed transit lost ship identity");
        }
    }
    if(result.snapshot_version_>=71) {
        std::size_t actual_ships=0;
        for(const auto& [id,e]:result.entities_) {
            if(e.type!=EntityType::Ship)continue;
            ++actual_ships;
            const auto frame=result.ship_frames_.find(id);
            if(frame==result.ship_frames_.end() || frame->second.system!=e.system_id)
                throw std::runtime_error("snapshot: missing or wrong ship frame custody");
            const auto& entry=frame->second;
            if(entry.anchor_body) {
                const auto systems=result.physical_celestial_systems_.find(entry.system);
                if(systems==result.physical_celestial_systems_.end())
                    throw std::runtime_error("snapshot: missing frame body system");
                const auto& bodies=systems->second;
                const auto body=std::find_if(bodies.begin(),bodies.end(),
                    [&entry](const PhysicalCelestialBody& b){
                        return b.id==entry.anchor_body;
                    });
                if(body==bodies.end() || body->physical_radius_mm >
                       std::uint64_t(std::numeric_limits<std::int64_t>::max()-100000000LL))
                    throw std::runtime_error("snapshot: invalid frame anchor");
                const auto expected_x=static_cast<long double>(body->position.x_mm)+
                    static_cast<long double>(body->physical_radius_mm)+100000000.L;
                if(expected_x!=static_cast<long double>(entry.origin.x_mm) ||
                   entry.origin.y_mm!=body->position.y_mm ||
                   entry.origin.z_mm!=body->position.z_mm)
                    throw std::runtime_error("snapshot: altered frame origin");
            }
        }
        if(actual_ships!=result.ship_frames_.size())
            throw std::runtime_error("snapshot: orphaned ship frame custody");
    }
    if(result.snapshot_version_>=70){
        if(result.physical_celestial_systems_.size()!=result.systems_.size())
            throw std::runtime_error("snapshot: missing physical celestial systems");
        for(const auto& system:result.systems_){
            const auto expected=physical_bodies_for_system(system);
            const auto actual=result.physical_celestial_systems_.find(system);
            if(actual==result.physical_celestial_systems_.end()||
               actual->second.size()!=expected.size())
                throw std::runtime_error("snapshot: incomplete celestial catalogue");
            for(std::size_t i=0;i<expected.size();++i){
                const auto& x=expected[i];const auto& y=actual->second[i];
                if(x.id!=y.id||x.parent_id!=y.parent_id||x.kind!=y.kind||
                   x.orbital_radius_milli_au!=y.orbital_radius_milli_au||
                   x.position.x_mm!=y.position.x_mm||
                   x.position.y_mm!=y.position.y_mm||
                   x.position.z_mm!=y.position.z_mm||
                   x.physical_radius_mm!=y.physical_radius_mm)
                    throw std::runtime_error("snapshot: celestial geometry tampering");
            }
        }
    }
    if(result.snapshot_version_>=68){
        for(const auto& [key,p]:result.pending_probe_discoveries_){
            const auto carrier=result.entities_.find(p.carrier);
            if(key.first!=p.report.owner||key.second!=p.report.site||
               carrier==result.entities_.end()||
               (carrier->second.type!=EntityType::Ship&&
                carrier->second.type!=EntityType::Wreck)||
               carrier->second.owner_id!=p.report.owner)
                throw std::runtime_error("snapshot: invalid delayed probe custody");
            const auto probe=result.survey_probes_.find(p.probe_id);
            if(probe!=result.survey_probes_.end()&&
               (probe->second.owner!=p.report.owner||
                probe->second.carrier!=p.carrier||
                probe->second.system!=p.report.system))
                throw std::runtime_error("snapshot: delayed probe ownership conflict");
        }
    }
    if(result.snapshot_version_>=65){
        for(const auto& [id,p]:result.survey_probes_){
            const auto it=result.entities_.find(p.carrier);
            if(id!=p.id || it==result.entities_.end() ||
               (it->second.type!=EntityType::Ship && it->second.type!=EntityType::Wreck) ||
               it->second.owner_id!=p.owner || it->second.system_id!=p.system ||
               !it->second.fitting.probe_launcher() ||
               !result.submitted_ids_.count(p.id))
                throw std::runtime_error("snapshot: survey probe has no valid physical carrier");
        }
    }
    if(result.snapshot_version_>=88) {
        std::set<PlanetarySiteKey> active_jobs;
        for(const auto& [id,job]:result.local_delivery_orders_) {
            const auto site=result.planetary_sites_.find(job.site);
            const auto shuttle=result.planetary_shuttles_.find(job.site);
            const auto ship=result.entities_.find(job.board_ship);
            if(site==result.planetary_sites_.end() ||
               shuttle==result.planetary_shuttles_.end() ||
               ship==result.entities_.end() ||
               ship->second.owner_id!=job.owner ||
               site->second.owner!=job.owner ||
               site->second.source_ship!=job.board_ship ||
               site->second.spaceport_phase!=3 ||
               shuttle->second.owner!=job.owner ||
               shuttle->second.source_ship!=job.board_ship ||
               !result.colony_ships_.count(job.board_ship))
                throw std::runtime_error("snapshot: orphaned local delivery contract");
            if(job.stage==LocalDeliveryStage::Complete ||
               job.stage==LocalDeliveryStage::Cancelled)continue;
            if(job.stage==LocalDeliveryStage::Posted) {
                if(job.assigned_pilot)
                    throw std::runtime_error("snapshot: premature courier assignment");
                continue; // any number of unclaimed, non-custodial orders
            }
            if(!active_jobs.insert(job.site).second)
                throw std::runtime_error("snapshot: competing active local deliveries");
            const auto pilot=result.npcs_.find(job.assigned_pilot);
            if(!job.assigned_pilot || pilot==result.npcs_.end() ||
               pilot->second.home_ship!=job.board_ship ||
               shuttle->second.pilot_npc!=job.assigned_pilot)
                throw std::runtime_error("snapshot: missing assigned courier NPC");
            const auto flight_id=[id](bool return_leg) {
                return 0x10000000000000ULL | (id<<1) |
                       std::uint64_t(return_leg?1:0);
            };
            if((job.stage==LocalDeliveryStage::Descending &&
                (shuttle->second.descent_receipt!=flight_id(false) ||
                 (shuttle->second.flight_phase!=1 &&
                  shuttle->second.flight_phase!=2))) ||
               ((job.stage==LocalDeliveryStage::Loading ||
                 job.stage==LocalDeliveryStage::Ascending) &&
                (shuttle->second.flight_phase!=2 ||
                 shuttle->second.descent_receipt!=flight_id(false))) ||
               (job.stage==LocalDeliveryStage::Delivering &&
                (shuttle->second.ascent_receipt!=flight_id(true) ||
                 (shuttle->second.flight_phase!=3 &&
                  shuttle->second.flight_phase!=0))))
                throw std::runtime_error("snapshot: invalid courier flight phase");
        }
    }
    if(result.snapshot_version_>=98){
        for(const auto& [owner,p]:result.player_people_){
            const auto ship=result.entities_.find(p.physical_ship);
            const auto home=result.entities_.find(p.home_ship);
            if(owner!=p.owner || home==result.entities_.end() ||
               home->second.owner_id!=owner ||
               ship==result.entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               (p.state==PlayerPersonState::AboardOwnShip &&
                 (p.home_ship!=p.physical_ship ||
                  ship->second.owner_id!=owner)))
                throw std::runtime_error("snapshot: forged player persona location");
            if(p.state==PlayerPersonState::Rescued){
                const auto flight=result.npc_flight_orders_.find(p.rescue_flight);
                if(ship->second.owner_id!=p.custodian ||
                   flight==result.npc_flight_orders_.end() ||
                   flight->second.mode!=NpcFlightMode::LocalRendezvous ||
                   flight->second.purpose!=NpcFlightPurpose::Rescue ||
                   flight->second.phase!=NpcFlightPhase::Arrived ||
                   flight->second.ship!=p.physical_ship ||
                   flight->second.station!=p.home_ship ||
                   flight->second.owner!=p.custodian)
                    throw std::runtime_error("snapshot: unsupported player rescue custody");
                bool receipt=false;
                for(const auto& event:result.activity_events_)
                    if(event.kind==ActivityKind::PlayerRescued &&
                       event.start_command_id==p.last_receipt &&
                       event.actor==p.physical_ship && event.target==p.home_ship &&
                       event.tick==p.last_update_tick &&
                       event.result==Outcome::Applied &&
                       event.transferred==0)receipt=true;
                if(!receipt)throw std::runtime_error("snapshot: player rescue missing original receipt");
            }
        }
    }
    if(result.snapshot_version_>=95){
        std::set<std::uint64_t> distinct_requests,active_ships,active_pilots;
        for(const auto& [id,flight]:result.npc_flight_orders_){
            (void)id;
            const auto ship=result.entities_.find(flight.ship);
            if(ship==result.entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               ship->second.owner_id!=flight.owner ||
               !result.colony_ships_.count(flight.ship) ||
               !result.life_support_.count(flight.ship))
                throw std::runtime_error("snapshot: forged NPC flight ship owner");
            if(flight.mode==NpcFlightMode::Celestial){
                // Existing FLIGHT-01 physical v72 receipts remain unchanged.
                if(!distinct_requests.insert(flight.transit_request).second)
                    throw std::runtime_error("snapshot: duplicate celestial flight receipt");
                const auto pending=result.pending_celestial_transits_.find(
                    flight.transit_request);
                const auto done=result.completed_celestial_transits_.find(
                    flight.transit_request);
                if(flight.phase==NpcFlightPhase::Planned ||
                   flight.phase==NpcFlightPhase::Cancelled){
                    if(pending!=result.pending_celestial_transits_.end() ||
                       done!=result.completed_celestial_transits_.end())
                        throw std::runtime_error("snapshot: unstarted NPC flight has physical travel receipt");
                }else if(flight.phase==NpcFlightPhase::InTransit){
                    if(pending==result.pending_celestial_transits_.end() ||
                       done!=result.completed_celestial_transits_.end() ||
                       pending->second.owner!=flight.owner ||
                       pending->second.ship!=flight.ship ||
                       pending->second.destination_body!=flight.destination_body ||
                       pending->second.started_tick!=flight.started_tick)
                        throw std::runtime_error("snapshot: NPC flight has no original pending voyage");
                }else{
                    if(pending!=result.pending_celestial_transits_.end() ||
                       done==result.completed_celestial_transits_.end() ||
                       done->second.owner!=flight.owner ||
                       done->second.ship!=flight.ship ||
                       done->second.destination_body!=flight.destination_body ||
                       done->second.completed_tick!=flight.resolved_tick)
                        throw std::runtime_error("snapshot: NPC flight arrived without physical receipt");
                }
            }else{
                // FLIGHT-02 uses the SAME existing real ship/dock control.
                // A queued command alone never proves station arrival.
                const auto target=result.entities_.find(flight.station);
                const bool rendezvous=flight.mode==NpcFlightMode::LocalRendezvous;
                if(result.snapshot_version_<96 ||
                   (rendezvous && result.snapshot_version_<97) ||
                   flight.station==flight.ship ||
                   ((!rendezvous ||
                     flight.phase==NpcFlightPhase::Planned ||
                     flight.phase==NpcFlightPhase::InTransit) &&
                    (target==result.entities_.end() ||
                     target->second.type!=(rendezvous?EntityType::Ship:EntityType::Station) ||
                     target->second.system_id!=ship->second.system_id ||
                     !result.same_local_frame(flight.ship,flight.station))))
                    throw std::runtime_error("snapshot: impossible NPC docking target");
                if(flight.local_commands){
                    if(!result.submitted_ids_.count(flight.last_local_command))
                        throw std::runtime_error("snapshot: missing original NPC ship command");
                }
                if(flight.phase==NpcFlightPhase::Arrived){
                    const bool docked=ship->second.dock_state==DockState::Docked &&
                        ship->second.dock_station==flight.station;
                    bool applied=false,dock_receipt=false;
                    for(const auto& ev:result.events_)
                        if(ev.command_id==flight.last_local_command &&
                           ev.result==Outcome::Applied)applied=true;
                    for(const auto& event:result.activity_events_)
                        if(event.kind==(rendezvous?ActivityKind::NpcRendezvous:ActivityKind::Docked) &&
                           (!rendezvous || (event.start_command_id==flight.last_local_command &&
                                            event.tick==flight.resolved_tick &&
                                            event.result==Outcome::Applied)) &&
                           event.actor==flight.ship &&
                           event.target==flight.station &&
                           event.tick>=flight.started_tick &&
                           event.tick<=flight.resolved_tick)
                            dock_receipt=true;
                    if(!applied || !dock_receipt || (!rendezvous && !docked))
                        throw std::runtime_error("snapshot: false NPC local arrival");
                }
                if(!rendezvous && flight.phase==NpcFlightPhase::InTransit &&
                   ship->second.dock_state==DockState::Docked &&
                   ship->second.dock_station==flight.station){
                    // Docked transition and coordinator receipt can be one
                    // tick apart; this phase is still recoverable.
                }
            }
            if(flight.phase==NpcFlightPhase::Planned ||
               flight.phase==NpcFlightPhase::InTransit){
                if(!active_ships.insert(flight.ship).second ||
                   !active_pilots.insert(flight.pilot_npc).second)
                    throw std::runtime_error("snapshot: NPC pilot or ship assigned to two flights");
                // Real casualties retain canonical tombstones; the native
                // worker reconciler cancels an unstarted stale pilot order.
                if(!result.npcs_.count(flight.pilot_npc) &&
                   !result.casualties_.count(flight.pilot_npc))
                    throw std::runtime_error("snapshot: flight pilot has no canonical identity");
            }
        }
    }
    if(result.snapshot_version_>=94) {
        std::set<std::pair<WorkTaskKind,std::uint64_t>> exclusive_active;
        for(const auto& [id,directive]:result.work_directives_){
            const auto ship=result.entities_.find(directive.colony_ship);
            if(ship==result.entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               ship->second.owner_id!=directive.owner ||
               !result.colony_ships_.count(directive.colony_ship) ||
               !result.life_support_.count(directive.colony_ship))
                throw std::runtime_error("snapshot: delegation has no original owner");
            if(directive.status!=WorkDirectiveStatus::Cancelled &&
               !exclusive_active.insert({directive.kind,directive.source_order}).second)
                throw std::runtime_error("snapshot: duplicate active delegated work");
            if(directive.requires_directive){
                const auto parent=result.work_directives_.find(
                    directive.requires_directive);
                if(parent==result.work_directives_.end() ||
                   parent->second.id>=id ||
                   parent->second.owner!=directive.owner ||
                   parent->second.colony_ship!=directive.colony_ship ||
                   (directive.status==WorkDirectiveStatus::Fulfilled &&
                    parent->second.status!=WorkDirectiveStatus::Fulfilled))
                    throw std::runtime_error("snapshot: work dependency/cycle forged");
            }
            if(directive.kind==WorkTaskKind::LocalDelivery ||
               directive.kind==WorkTaskKind::IntercolonyFreight){
                // Freight and its prerequisite site pickup legitimately
                // share one source ID; both retain separate native receipts.
                const auto pickup=result.local_delivery_orders_.find(
                    directive.source_order);
                if(pickup==result.local_delivery_orders_.end() ||
                   pickup->second.owner!=directive.owner ||
                   pickup->second.board_ship!=directive.colony_ship)
                    throw std::runtime_error("snapshot: forged transport delegation origin");
                if(directive.kind==WorkTaskKind::LocalDelivery){
                    if(directive.status==WorkDirectiveStatus::Fulfilled &&
                       pickup->second.stage!=LocalDeliveryStage::Complete)
                        throw std::runtime_error("snapshot: unproven shuttle completion");
                }else{
                    const auto freight=result.intercolony_freight_.find(
                        directive.source_order);
                    if((freight==result.intercolony_freight_.end() &&
                        directive.status!=WorkDirectiveStatus::Cancelled) ||
                       (freight!=result.intercolony_freight_.end() &&
                        (freight->second.owner!=directive.owner ||
                         freight->second.source_ship!=directive.colony_ship ||
                         freight->second.cargo!=pickup->second.cargo ||
                         freight->second.kilograms!=pickup->second.kilograms)))
                        throw std::runtime_error("snapshot: forged freight delegation");
                    if(directive.status==WorkDirectiveStatus::Fulfilled &&
                       (freight==result.intercolony_freight_.end() ||
                        freight->second.stage!=IntercolonyFreightStage::Delivered ||
                        !freight->second.arrival_receipt))
                        throw std::runtime_error("snapshot: unproven native freight arrival");
                }
                continue;
            }
            const auto contract=result.shipyard_customer_orders_.find(
                directive.source_order);
            if(contract==result.shipyard_customer_orders_.end() ||
               contract->second.customer_owner!=directive.owner ||
               contract->second.customer_ship!=directive.colony_ship)
                throw std::runtime_error("snapshot: delegation has no original customer");
            if(directive.status==WorkDirectiveStatus::Fulfilled){
                if(contract->second.stage!=ShipyardCustomerStage::Delivered)
                    throw std::runtime_error("snapshot: work completed without hull");
                if(directive.commissioning_crew[0]){
                    const auto build=result.freighter_build_orders_.find(
                        contract->second.build_order);
                    if(build==result.freighter_build_orders_.end() ||
                       !build->second.commissioned() ||
                       build->second.commissioning_crew!=
                            directive.commissioning_crew ||
                       build->second.provisions_kg_each!=
                            directive.provisions_kg_each)
                        throw std::runtime_error("snapshot: unproven NPC commissioning");
                }
            }
        }
    }
    if(result.snapshot_version_>=93) {
        std::set<std::uint64_t> contract_builds;
        for(const auto& [id,contract]:result.shipyard_customer_orders_) {
            (void)id;
            const auto yard=result.shipyard_constructions_.find(contract.yard_order);
            const auto buyer=result.entities_.find(contract.customer_ship);
            const auto supplier=result.entities_.find(contract.supplier_ship);
            if(yard==result.shipyard_constructions_.end() ||
               !yard->second.operational() ||
               yard->second.owner!=contract.supplier_owner ||
               yard->second.colony_ship!=contract.supplier_ship ||
               buyer==result.entities_.end() ||
               supplier==result.entities_.end() ||
               buyer->second.type!=EntityType::Ship ||
               supplier->second.type!=EntityType::Ship ||
               buyer->second.owner_id!=contract.customer_owner ||
               supplier->second.owner_id!=contract.supplier_owner ||
               !result.colony_ships_.count(contract.customer_ship) ||
               !result.colony_ships_.count(contract.supplier_ship) ||
               !contract_builds.insert(contract.build_order).second)
                throw std::runtime_error("snapshot: invalid customer/supplier custody");
            const auto build=result.freighter_build_orders_.find(contract.build_order);
            if(contract.stage==ShipyardCustomerStage::Requested ||
               contract.stage==ShipyardCustomerStage::Accepted) {
                if(build!=result.freighter_build_orders_.end())
                    throw std::runtime_error("snapshot: unfunded customer hull");
            }else{
                if(build==result.freighter_build_orders_.end() ||
                   build->second.owner!=contract.supplier_owner ||
                   build->second.source_ship!=contract.supplier_ship ||
                   build->second.yard_order!=contract.yard_order ||
                   build->second.posted_tick<contract.posted_tick)
                    throw std::runtime_error("snapshot: missing paid physical build");
                if(contract.stage==ShipyardCustomerStage::Delivered) {
                    const auto made=result.entities_.find(contract.delivered_ship);
                    if(!build->second.finished() ||
                       contract.delivered_ship!=build->second.launched_ship ||
                       contract.delivered_tick<build->second.launched_tick ||
                       made==result.entities_.end() ||
                       made->second.owner_id!=contract.customer_owner)
                        throw std::runtime_error("snapshot: customer handover not proven");
                }else if(build->second.finished()) {
                    const auto made=result.entities_.find(build->second.launched_ship);
                    if(made==result.entities_.end() ||
                       made->second.owner_id!=contract.supplier_owner)
                        throw std::runtime_error("snapshot: unclaimed hull ownership forged");
                }
            }
        }
    }
    if(result.snapshot_version_>=91) {
        std::set<std::uint64_t> active_slips;
        std::set<EntityId> manufactured_hulls;
        for(const auto& [id,job]:result.freighter_build_orders_){
            (void)id;
            const auto yard=result.shipyard_constructions_.find(job.yard_order);
            const auto source=result.entities_.find(job.source_ship);
            const auto source_frame=result.ship_frames_.find(job.source_ship);
            if(yard==result.shipyard_constructions_.end() ||
               !yard->second.operational() ||
               yard->second.owner!=job.owner ||
               yard->second.colony_ship!=job.source_ship ||
               source==result.entities_.end() ||
               source->second.type!=EntityType::Ship ||
               source->second.owner_id!=job.owner ||
               source_frame==result.ship_frames_.end() ||
               source_frame->second.system!=source->second.system_id ||
               // A source vessel can physically depart after posting: the
               // build must stall, not make the saved world unrecoverable.
               // Immutable original frame is retained as build provenance.
               (!job.finished() &&
                 !active_slips.insert(job.yard_order).second))
                throw std::runtime_error("snapshot: invalid freighter yard custody");
            if(job.finished()){
                const auto made=result.entities_.find(job.launched_ship);
                const auto frame=result.ship_frames_.find(job.launched_ship);
                if(!manufactured_hulls.insert(job.launched_ship).second ||
                   made==result.entities_.end() ||
                   (made->second.type!=EntityType::Ship &&
                    made->second.type!=EntityType::Wreck) ||
                   !made->second.owner_id ||
                   made->second.cargo_capacity!=100 ||
                   made->second.manufactured_order_id!=job.order_id ||
                   (made->second.type==EntityType::Ship &&
                    (frame==result.ship_frames_.end() ||
                     frame->second.system!=made->second.system_id)) ||
                   (made->second.type==EntityType::Wreck &&
                    frame!=result.ship_frames_.end()) ||
                   (result.snapshot_version_>=92 &&
                    made->second.type==EntityType::Ship &&
                    (job.commissioned() !=
                     (result.colony_ships_.count(job.launched_ship)!=0) ||
                     job.commissioned() !=
                     (result.life_support_.count(job.launched_ship)!=0) ||
                     job.commissioned() !=
                     made->second.fitting.exploration_scanner())))
                    throw std::runtime_error("snapshot: forged manufactured hull");
            }
        }
    }
    if(result.snapshot_version_>=91)
        for(const auto& [ship_id,ship]:result.entities_) {
            if(!ship.manufactured_order_id)continue;
            const auto order=result.freighter_build_orders_.find(
                ship.manufactured_order_id);
            if(order==result.freighter_build_orders_.end() ||
               !order->second.finished() ||
               order->second.launched_ship!=ship_id)
                throw std::runtime_error("snapshot: unclaimed manufactured vessel");
        }
    if(result.snapshot_version_>=90) {
        std::set<EntityId> yard_colonies;
        std::set<std::uint64_t> assigned_builders;
        for(const auto& [id,build]:result.shipyard_constructions_) {
            (void)id;
            const auto entity=result.entities_.find(build.colony_ship);
            if(entity==result.entities_.end() ||
               entity->second.type!=EntityType::Ship ||
               entity->second.owner_id!=build.owner ||
               !result.colony_ships_.count(build.colony_ship) ||
               !result.life_support_.count(build.colony_ship) ||
               !yard_colonies.insert(build.colony_ship).second)
                throw std::runtime_error("snapshot: orphaned or duplicated shipyard");
            for(const auto worker:build.workers) {
                // Worker IDs are immutable historical contributors. Later
                // death, migration, or rehome must stall an unfinished yard,
                // not make the entire SQLite world impossible to reload.
                const auto alive=result.npcs_.find(worker);
                const auto deceased=result.casualties_.find(worker);
                if(alive==result.npcs_.end() &&
                   deceased==result.casualties_.end())
                    throw std::runtime_error("snapshot: shipyard builder never existed");
                if(!build.operational() &&
                   !assigned_builders.insert(worker).second)
                    throw std::runtime_error("snapshot: worker double-booked");
            }
        }
    }
    if(result.snapshot_version_>=89) {
        std::set<EntityId> loaded_carriers;
        for(const auto& [id,leg]:result.intercolony_freight_) {
            const auto order=result.local_delivery_orders_.find(id);
            const auto origin=result.entities_.find(leg.source_ship);
            const auto freighter=result.entities_.find(leg.carrier_ship);
            const auto destination=result.entities_.find(leg.destination_ship);
            const auto frame=result.ship_frames_.find(leg.carrier_ship);
            const auto from=result.ship_frames_.find(leg.source_ship);
            if(order==result.local_delivery_orders_.end() ||
               order->second.owner!=leg.owner ||
               order->second.board_ship!=leg.source_ship ||
               (leg.stage==IntercolonyFreightStage::AwaitingPickup ?
                    order->second.stage==LocalDeliveryStage::Cancelled :
                    order->second.stage!=LocalDeliveryStage::Complete) ||
               order->second.kilograms!=leg.kilograms ||
               order->second.cargo!=leg.cargo ||
               origin==result.entities_.end() ||
               freighter==result.entities_.end() ||
               destination==result.entities_.end() ||
               origin->second.owner_id!=leg.owner ||
               freighter->second.owner_id!=leg.owner ||
               destination->second.owner_id!=leg.owner ||
               !result.colony_ships_.count(leg.source_ship) ||
               !result.colony_ships_.count(leg.carrier_ship) ||
               !result.colony_ships_.count(leg.destination_ship) ||
               !result.life_support_.count(leg.source_ship) ||
               !result.life_support_.count(leg.carrier_ship) ||
               !result.life_support_.count(leg.destination_ship) ||
               from==result.ship_frames_.end() ||
               frame==result.ship_frames_.end() ||
               from->second.system!=frame->second.system ||
               leg.origin_body!=std::get<0>(order->second.site))
                throw std::runtime_error("snapshot: orphaned freight and origin");
            const auto code=sealed_freight_code(leg.cargo);
            if(!code)throw std::runtime_error("snapshot: unknown sealed goods");
            const auto& goods=result.colony_ships_.at(leg.carrier_ship).goods_kg;
            const auto found=goods.find(code);
            if(leg.stage==IntercolonyFreightStage::AwaitingPickup) {
                if(!loaded_carriers.insert(leg.carrier_ship).second ||
                   found!=goods.end())
                    throw std::runtime_error("snapshot: planned carrier already holds freight");
            }else if(leg.stage==IntercolonyFreightStage::Loaded) {
                if(!loaded_carriers.insert(leg.carrier_ship).second ||
                   found==goods.end() || found->second!=leg.kilograms)
                    throw std::runtime_error("snapshot: physical freight missing or duplicate");
            }else{
                const auto receipt=result.completed_celestial_transits_.find(
                    leg.arrival_receipt);
                if(receipt==result.completed_celestial_transits_.end() ||
                   receipt->second.owner!=leg.owner ||
                   receipt->second.ship!=leg.carrier_ship ||
                   receipt->second.completed_tick<leg.loaded_tick ||
                   receipt->second.destination_body==leg.origin_body)
                    throw std::runtime_error("snapshot: freight without proven arrival");
            }
        }
        // Every sealed freight stack must be justified by exactly one
        // active physical carrier assignment. Historical Delivered jobs
        // cannot silently leave unclaimed sealed cargo in a ship hold.
        for(const auto& [ship,colony]:result.colony_ships_)
            for(const auto& [code,kg]:colony.goods_kg) {
                if(code!="freight_water" && code!="freight_food" &&
                   code!="freight_nutrients")continue;
                std::size_t witnesses=0;
                for(const auto& [id,leg]:result.intercolony_freight_) {
                    (void)id;
                    if(leg.stage==IntercolonyFreightStage::Loaded &&
                       leg.carrier_ship==ship &&
                       code==sealed_freight_code(leg.cargo) &&
                       leg.kilograms==kg)++witnesses;
                }
                if(witnesses!=1)
                    throw std::runtime_error("snapshot: unclaimed sealed intercolony goods");
            }
    }
    if(result.snapshot_version_>=86) {
        if(result.snapshot_version_>=115 &&
           result.legacy_planetary_residence_import_) {
            const auto& migration=*result.legacy_planetary_residence_import_;
            const auto request=migration.host_request_id;
            if(result.submitted_ids_.count(request) ||
               result.shuttle_flight_request_ids_.count(request) ||
               result.voyages_.count(request) ||
               result.local_delivery_orders_.count(request) ||
               result.pending_celestial_transits_.count(request) ||
               result.completed_celestial_transits_.count(request) ||
               result.npcs_.count(request) ||
               result.casualties_.count(request) ||
               result.physical_dependents_.count(request))
                throw std::runtime_error("snapshot: legacy import request reused");
            for(const auto& [key,site]:result.planetary_sites_) {
                (void)key;
                if(site.housing_order_id==request ||
                   site.utility_order_id==request)
                    throw std::runtime_error("snapshot: legacy import collides with building");
                for(const auto approved:site.founder_consent_requests)
                    if(approved==request)
                        throw std::runtime_error("snapshot: legacy import collides with founder");
                for(const auto& [id,proof]:site.visitor_residency) {
                    (void)id;
                    if(proof[1]==request || proof[2]==request)
                        throw std::runtime_error("snapshot: legacy import collides with settler");
                }
                for(const auto& h:site.utility_handovers)
                    if(h.request_id==request)
                        throw std::runtime_error("snapshot: legacy import collides with relief");
                for(const auto& p:site.production_projects)
                    if(p.order_id==request)
                        throw std::runtime_error("snapshot: legacy import collides with production");
                for(const auto& history:site.production_handovers)
                    for(const auto& h:history)
                        if(h.request_id==request)
                            throw std::runtime_error("snapshot: legacy import collides with succession");
            }
            for(const auto& [id,address]:migration.legal_addresses) {
                const auto live=result.npcs_.find(id);
                const auto dead=result.casualties_.find(id);
                if((live==result.npcs_.end())==(dead==result.casualties_.end()))
                    throw std::runtime_error("snapshot: ungrounded historical resident");
                const auto& person=live!=result.npcs_.end()?
                    live->second:dead->second.identity;
                if(person.legal_residence()!=transport::Endpoint{
                       transport::LocationKind::PlanetarySite,address})
                    throw std::runtime_error("snapshot: altered legacy legal address");
                std::size_t sites=0;
                for(const auto& [key,site]:result.planetary_sites_) {
                    (void)key;
                    if(site.surface_location!=address)continue;
                    const auto ship=result.entities_.find(site.source_ship);
                    if(site.spaceport_phase!=3 || !site.owner ||
                       person.home_ship!=site.source_ship ||
                       ship==result.entities_.end() ||
                       ship->second.owner_id!=site.owner)
                        throw std::runtime_error("snapshot: orphan legacy authority");
                    ++sites;
                }
                if(sites!=1)
                    throw std::runtime_error("snapshot: ambiguous historical planetary address");
            }
        }
        const auto legal_address_is_registered=[&](const NpcRecord& n) {
            const auto residence=n.legal_residence();
            if(!residence.id)return false;
            switch(residence.kind) {
                case transport::LocationKind::ColonyShip:
                    return residence.id==n.home_ship &&
                           result.colony_ships_.count(residence.id)!=0;
                case transport::LocationKind::SpaceColony:
                    return result.built_outposts_.count(residence.id)!=0 &&
                           result.habitats_.count(residence.id)!=0;
                case transport::LocationKind::PlanetarySite:
                    for(const auto& [key,site]:result.planetary_sites_) {
                        (void)key;
                        if(site.surface_location!=residence.id ||
                           site.spaceport_phase!=3)continue;
                        if(result.snapshot_version_<109)return true;
                        // A verified historical address is *legal only*.
                        // It does not certify a modern bed, consent or flight.
                        if(result.snapshot_version_>=115 &&
                           result.legacy_planetary_residence_import_) {
                            const auto& earlier=
                                result.legacy_planetary_residence_import_->legal_addresses;
                            const auto recorded=earlier.find(n.id);
                            if(recorded!=earlier.end() &&
                               recorded->second==residence.id)return true;
                        }
                        if(site.housing_phase!=2)return false;
                        for(const auto founded:site.founder_residents)
                            if(founded==n.id)return true;
                        if(result.snapshot_version_>=111) {
                            const auto visitor=site.visitor_residency.find(n.id);
                            if(visitor!=site.visitor_residency.end() &&
                               visitor->second[2]==0)return true;
                        }
                        if(result.snapshot_version_>=118) {
                            const auto household=site.households.find(n.id);
                            if(household!=site.households.end() &&
                               !household->second.return_request)return true;
                            for(const auto& [guardian,record]:site.households) {
                                (void)guardian;
                                if(record.co_parent_id==n.id &&
                                   !record.return_request)return true;
                            }
                        }
                        return false;
                    }
                    return false;
                case transport::LocationKind::OtherNpcLocation:
                    return false; // no registered housing authority yet
            }
            return false;
        };
        for(const auto& [id,n]:result.npcs_) {
            (void)id;
            if(!legal_address_is_registered(n))
                throw std::runtime_error("snapshot: orphaned NPC legal residence");
        }
        for(const auto& [id,d]:result.casualties_) {
            (void)id;
            if(!legal_address_is_registered(d.identity))
                throw std::runtime_error("snapshot: orphaned deceased legal residence");
        }
        if(result.snapshot_version_>=109) {
            std::set<std::uint64_t> founded_npcs,request_ids;
            for(const auto& [key,site]:result.planetary_sites_) {
                (void)key;
                if(result.snapshot_version_>=113) {
                    std::size_t active_builds=0;
                    for(std::size_t i=0;i<site.production_projects.size();++i) {
                        const auto& p=site.production_projects[i];
                        if(!p.phase)continue;
                        const bool installed=i==0?site.farm:site.extractor;
                        if(site.housing_phase!=2 || site.utility_phase!=2 ||
                           (p.phase==1 && installed) ||
                           (p.phase==2 && !installed) ||
                           (p.phase==1 && ++active_builds>1) ||
                           !p.order_id || !p.supervisor ||
                           !request_ids.insert(p.order_id).second ||
                           result.submitted_ids_.count(p.order_id) ||
                           result.shuttle_flight_request_ids_.count(p.order_id) ||
                           result.local_delivery_orders_.count(p.order_id) ||
                           result.voyages_.count(p.order_id) ||
                           result.pending_celestial_transits_.count(p.order_id) ||
                           result.completed_celestial_transits_.count(p.order_id) ||
                           p.order_id==site.housing_order_id ||
                           p.order_id==site.utility_order_id)
                            throw std::runtime_error("snapshot: invalid production project admission");
                        const auto worker=result.npcs_.find(p.supervisor);
                        const auto dead=result.casualties_.find(p.supervisor);
                        if(worker==result.npcs_.end() &&
                           dead==result.casualties_.end())
                            throw std::runtime_error("snapshot: missing original production builder");
                        const auto& named=worker!=result.npcs_.end()?
                            worker->second:dead->second.identity;
                        if(named.home_ship!=site.source_ship ||
                           std::count_if(named.industrial_training.begin(),
                               named.industrial_training.end(),
                               [](const NpcTradeTraining& credit){
                                   return credit.trade==WorkshopTrade::Shape;
                               })<3)
                            throw std::runtime_error("snapshot: unqualified physical production builder");
                        // A completed visiting supervisor can legitimately
                        // leave; an unfinished job must remain recoverable
                        // even if its current supervisor is temporarily absent.
                        // The original site/flight records own physical custody,
                        // not a fabricated second employment ledger.
                        if(result.snapshot_version_>=114) {
                            const auto& history=site.production_handovers[i];
                            if(!history.empty()) {
                                auto incumbent=history.front().previous;
                                std::uint64_t last_tick=0;
                                const auto certified_at=[&](std::uint64_t id,
                                                            std::uint64_t at) {
                                    const auto alive=result.npcs_.find(id);
                                    const auto lost=result.casualties_.find(id);
                                    if(alive==result.npcs_.end() &&
                                       lost==result.casualties_.end())return false;
                                    const auto& n=alive!=result.npcs_.end()?
                                        alive->second:lost->second.identity;
                                    return n.home_ship==site.source_ship &&
                                        std::count_if(
                                            n.industrial_training.begin(),
                                            n.industrial_training.end(),
                                            [at](const NpcTradeTraining& receipt){
                                                return receipt.trade==WorkshopTrade::Shape &&
                                                       receipt.tick<=at;
                                            })>=3;
                                };
                                for(const auto& h:history) {
                                    if(!h.request_id || !h.tick ||
                                       h.tick>result.tick_ || h.tick<=last_tick ||
                                       h.previous!=incumbent ||
                                       h.replacement==incumbent ||
                                       !certified_at(h.previous,h.tick) ||
                                       !certified_at(h.replacement,h.tick) ||
                                       !request_ids.insert(h.request_id).second ||
                                       result.submitted_ids_.count(h.request_id) ||
                                       result.shuttle_flight_request_ids_.count(
                                           h.request_id) ||
                                       result.local_delivery_orders_.count(h.request_id) ||
                                       result.voyages_.count(h.request_id) ||
                                       h.request_id==site.housing_order_id ||
                                       h.request_id==site.utility_order_id)
                                        throw std::runtime_error(
                                            "snapshot: invalid production supervisor succession");
                                    incumbent=h.replacement;
                                    last_tick=h.tick;
                                }
                                if(incumbent!=p.supervisor)
                                    throw std::runtime_error(
                                        "snapshot: interrupted production supervisor handover");
                            }
                        }
                    }
                }
                if(result.snapshot_version_>=117) {
                    const auto built_count=[](bool present) {
                        return present?std::int64_t{1}:std::int64_t{0};
                    };
                    const auto exact_mass=std::int64_t{4}+4*(
                        built_count(site.housing_phase!=0)+
                        built_count(site.utility_phase!=0)+
                        built_count(site.farm || site.production_projects[0].phase!=0)+
                        built_count(site.extractor || site.production_projects[1].phase!=0)+
                        built_count(site.service_projects[0].phase!=0)+
                        built_count(site.service_projects[1].phase!=0));
                    if(site.embodied_ore!=exact_mass)
                        throw std::runtime_error("snapshot: unbacked planetary building modules");
                    for(std::size_t i=0;i<site.service_projects.size();++i) {
                        const auto& p=site.service_projects[i];
                        const auto& history=site.service_handovers[i];
                        if(!p.phase) {
                            if(!history.empty())
                                throw std::runtime_error("snapshot: owner relief without physical service");
                            continue;
                        }
                        if(site.housing_phase!=2 || site.utility_phase!=2 ||
                           !p.order_id || !p.supervisor ||
                           !request_ids.insert(p.order_id).second ||
                           result.submitted_ids_.count(p.order_id) ||
                           result.shuttle_flight_request_ids_.count(p.order_id) ||
                           result.planetary_family_booking_ids_.count(p.order_id) ||
                           result.local_delivery_orders_.count(p.order_id) ||
                           result.voyages_.count(p.order_id) ||
                           result.pending_celestial_transits_.count(p.order_id) ||
                           result.completed_celestial_transits_.count(p.order_id) ||
                           p.order_id==site.housing_order_id ||
                           p.order_id==site.utility_order_id ||
                           (result.legacy_planetary_residence_import_ &&
                            p.order_id==result.legacy_planetary_residence_import_->
                                host_request_id) ||
                           p.powered_cycles>(result.tick_-p.completed_tick))
                            throw std::runtime_error("snapshot: unqualified service admission");
                        const auto certified_at=[&](std::uint64_t id,std::uint64_t at) {
                            const auto live=result.npcs_.find(id);
                            const auto dead=result.casualties_.find(id);
                            if(live==result.npcs_.end() &&
                               dead==result.casualties_.end())return false;
                            const auto& named=live!=result.npcs_.end()?
                                live->second:dead->second.identity;
                            return named.home_ship==site.source_ship &&
                                std::count_if(named.industrial_training.begin(),
                                              named.industrial_training.end(),
                                              [at](const NpcTradeTraining& receipt){
                                    return receipt.trade==WorkshopTrade::Shape &&
                                           receipt.tick<=at;})>=3;
                        };
                        if(!certified_at(p.supervisor,result.tick_))
                            throw std::runtime_error("snapshot: uncertified named service builder");
                        if(history.size()>16)
                            throw std::runtime_error("snapshot: excessive service supervisor changes");
                        if(!history.empty()) {
                            std::uint64_t incumbent=history.front().previous;
                            std::uint64_t previous_tick=0;
                            for(const auto& h:history) {
                                if(!h.request_id || !h.tick ||
                                   h.tick<=previous_tick || h.tick>result.tick_ ||
                                   h.previous!=incumbent ||
                                   h.replacement==incumbent ||
                                   !certified_at(h.previous,h.tick) ||
                                   !certified_at(h.replacement,h.tick) ||
                                   !request_ids.insert(h.request_id).second ||
                                   result.submitted_ids_.count(h.request_id) ||
                                   result.shuttle_flight_request_ids_.count(
                                       h.request_id) ||
                                   result.planetary_family_booking_ids_.count(
                                       h.request_id) ||
                                   result.local_delivery_orders_.count(h.request_id) ||
                                   result.voyages_.count(h.request_id) ||
                                   h.request_id==site.housing_order_id ||
                                   h.request_id==site.utility_order_id)
                                    throw std::runtime_error("snapshot: forged service handover");
                                previous_tick=h.tick;incumbent=h.replacement;
                            }
                            if(incumbent!=p.supervisor)
                                throw std::runtime_error("snapshot: interrupted service succession");
                        }
                    }
                }
                if(result.snapshot_version_>=110 && site.utility_phase) {
                    if(!site.utility_order_id ||
                       !request_ids.insert(site.utility_order_id).second ||
                       result.submitted_ids_.count(site.utility_order_id) ||
                       result.shuttle_flight_request_ids_.count(site.utility_order_id) ||
                       result.local_delivery_orders_.count(site.utility_order_id) ||
                       result.voyages_.count(site.utility_order_id))
                        throw std::runtime_error("snapshot: duplicate surface utility order");
                    const auto worker=result.npcs_.find(site.utility_supervisor);
                    const auto dead=result.casualties_.find(site.utility_supervisor);
                    if(worker==result.npcs_.end() &&
                       dead==result.casualties_.end())
                        throw std::runtime_error("snapshot: missing utility supervisor identity");
                    const auto& person=worker!=result.npcs_.end()?
                        worker->second:dead->second.identity;
                    const auto shape_credits=std::count_if(
                        person.industrial_training.begin(),
                        person.industrial_training.end(),
                        [](const NpcTradeTraining& receipt){
                            return receipt.trade==WorkshopTrade::Shape;
                        });
                    if(person.home_ship!=site.source_ship || shape_credits<3)
                        throw std::runtime_error("snapshot: unqualified surface supervisor");
                    if(result.snapshot_version_>=112 &&
                       !site.utility_handovers.empty()) {
                        auto current=site.utility_handovers.front().previous;
                        auto previous_tick=std::uint64_t{0};
                        const auto qualified_at=[&](std::uint64_t npc,
                                                    std::uint64_t tick) {
                            const auto live=result.npcs_.find(npc);
                            const auto deceased=result.casualties_.find(npc);
                            if(live==result.npcs_.end() &&
                               deceased==result.casualties_.end())return false;
                            const auto& n=live!=result.npcs_.end()?
                                live->second:deceased->second.identity;
                            if(n.home_ship!=site.source_ship)return false;
                            return std::count_if(n.industrial_training.begin(),
                                n.industrial_training.end(),[tick](
                                   const NpcTradeTraining& training) {
                                    return training.trade==WorkshopTrade::Shape &&
                                           training.tick<=tick;
                                })>=3;
                        };
                        for(const auto& handover:site.utility_handovers) {
                            if(!handover.request_id || !handover.tick ||
                               handover.tick>result.tick_ ||
                               handover.tick<=previous_tick ||
                               handover.previous!=current ||
                               handover.replacement==current ||
                               !qualified_at(current,handover.tick) ||
                               !qualified_at(handover.replacement,handover.tick) ||
                               !request_ids.insert(handover.request_id).second ||
                               result.submitted_ids_.count(handover.request_id) ||
                               result.shuttle_flight_request_ids_.count(
                                   handover.request_id) ||
                               result.local_delivery_orders_.count(
                                   handover.request_id) ||
                               result.voyages_.count(handover.request_id) ||
                               handover.request_id==site.housing_order_id ||
                               handover.request_id==site.utility_order_id)
                                throw std::runtime_error(
                                    "snapshot: invalid surface supervisor handover proof");
                            previous_tick=handover.tick;
                            current=handover.replacement;
                        }
                        if(current!=site.utility_supervisor)
                            throw std::runtime_error(
                                "snapshot: interrupted surface utility supervisor chain");
                    }
                }
                for(std::size_t i=0;i<2;++i) {
                    const auto id=site.founder_residents[i];
                    const auto request=site.founder_consent_requests[i];
                    if(!id)continue;
                    const auto living=result.npcs_.find(id);
                    const NpcRecord* person=living!=result.npcs_.end()?
                        &living->second:nullptr;
                    if(!person) {
                        const auto dead=result.casualties_.find(id);
                        if(dead!=result.casualties_.end())
                            person=&dead->second.identity;
                    }
                    if(!person || id!=site.delegated_npcs[i] ||
                       !founded_npcs.insert(id).second ||
                       !request_ids.insert(request).second ||
                       !request || request==site.housing_order_id ||
                       result.submitted_ids_.count(request) ||
                       result.shuttle_flight_request_ids_.count(request) ||
                       result.local_delivery_orders_.count(request) ||
                       person->home_ship!=site.source_ship ||
                       person->assigned_site!=site.surface_location ||
                       person->role!=NpcRole::PlanetarySurface ||
                       person->legal_residence()!=transport::Endpoint{
                           transport::LocationKind::PlanetarySite,
                           site.surface_location})
                        throw std::runtime_error("snapshot: invalid original founder admission");
                }
                if(result.snapshot_version_>=111) {
                    std::size_t active_visitors=0;
                    const auto shuttle=result.planetary_shuttles_.find(key);
                    for(const auto& [id,proof]:site.visitor_residency) {
                        const auto live=result.npcs_.find(id);
                        const auto dead=result.casualties_.find(id);
                        if((live==result.npcs_.end() &&
                            dead==result.casualties_.end()) ||
                           shuttle==result.planetary_shuttles_.end() ||
                           !result.shuttle_flight_request_ids_.count(proof[0]) ||
                           !proof[1] || !request_ids.insert(proof[1]).second ||
                           result.submitted_ids_.count(proof[1]) ||
                           result.shuttle_flight_request_ids_.count(proof[1]) ||
                           result.local_delivery_orders_.count(proof[1]) ||
                           result.voyages_.count(proof[1]) ||
                           proof[1]==site.housing_order_id ||
                           proof[1]==site.utility_order_id ||
                           (proof[2] &&
                            (!request_ids.insert(proof[2]).second ||
                             result.submitted_ids_.count(proof[2]) ||
                             result.shuttle_flight_request_ids_.count(proof[2]) ||
                             result.local_delivery_orders_.count(proof[2]) ||
                             result.voyages_.count(proof[2]) ||
                             proof[2]==site.housing_order_id ||
                             proof[2]==site.utility_order_id)))
                            throw std::runtime_error("snapshot: invalid visitor admission proof");
                        const NpcRecord& person=live!=result.npcs_.end()?
                            live->second:dead->second.identity;
                        if(person.home_ship!=site.source_ship ||
                           person.role!=NpcRole::Ship ||
                           person.assigned_site!=site.source_ship ||
                           id==site.delegated_npcs[0] ||
                           id==site.delegated_npcs[1] ||
                           !founded_npcs.insert(id).second)
                            throw std::runtime_error("snapshot: forged visitor identity");
                        if(!proof[2]) {
                            ++active_visitors;
                            if(person.legal_residence()!=transport::Endpoint{
                                  transport::LocationKind::PlanetarySite,
                                  site.surface_location})
                                throw std::runtime_error("snapshot: missing actual visitor residence");
                            if(live!=result.npcs_.end()) {
                                const auto arrival=shuttle->second.visitor_arrival_receipts.find(id);
                                if(person.location!=site.surface_location ||
                                   arrival==shuttle->second.visitor_arrival_receipts.end() ||
                                   arrival->second!=proof[0] ||
                                   !std::binary_search(
                                       shuttle->second.disembarked_ids.begin(),
                                       shuttle->second.disembarked_ids.end(),id) ||
                                   !result.npc_transport_arrival(proof[0],id))
                                    throw std::runtime_error("snapshot: unproven resident arrival");
                            }
                        }
                    }
                    if(active_visitors>2)
                        throw std::runtime_error("snapshot: planetary beds over capacity");
                }
            }
        }
    }
    if(result.snapshot_version_>=100){
        result.ship_status_=ship_status::Engine::decode(parsed_status.str());
        for(const auto& effect:result.ship_status_.records()){
            const auto source=result.entities_.find(effect.source_ship);
            const auto target=result.entities_.find(effect.target_ship);
            if(source==result.entities_.end() || target==result.entities_.end() ||
               (source->second.type!=EntityType::Ship &&
                source->second.type!=EntityType::Wreck) ||
               (target->second.type!=EntityType::Ship &&
                target->second.type!=EntityType::Wreck) ||
               effect.started_tick>result.tick_ ||
               result.submitted_ids_.count(effect.id) ||
               source->second.owner_id==target->second.owner_id)
                throw std::runtime_error("snapshot: invalid ship status source or target");
            // Historical records outlive removable hardware. An ACTIVE
            // effect must still address the genuine original fitting; after
            // expiration it may describe safely removed equipment.
            if(effect.expires_tick>result.tick_)
                for(const auto& key:effect.targets)
                    if(key.rack!=0 || key.slot>=2 ||
                       target->second.fitting.hardpoints[key.slot]!=key.hardware)
                        throw std::runtime_error("snapshot: active status module mismatch");
            if(result.snapshot_version_>=101 &&
               (effect.id & (std::uint64_t{1}<<63))) {
                const auto native_command=effect.id & ~(std::uint64_t{1}<<63);
                const auto proven=std::find_if(result.events_.begin(),
                    result.events_.end(),[&](const Event& event){
                        return event.command_id==native_command &&
                               event.result==Outcome::Applied &&
                               event.tick==effect.started_tick;
                    });
                if(!native_command ||
                   native_command>=(std::uint64_t{1}<<62) ||
                   proven==result.events_.end() ||
                   effect.expires_tick-effect.started_tick!=
                     (effect.resistance==60?kHackDurationTicks/2:
                      kHackDurationTicks) ||
                   effect.pulse_ticks!=2 || effect.intensity!=100 ||
                   (effect.resistance!=0 && effect.resistance!=60) ||
                   source->second.last_hack_tick<effect.started_tick)
                    throw std::runtime_error("snapshot: forged physical hacking receipt");
            }
        }
    }
    if(result.snapshot_version_>=102)
        for(const auto& [command_id,order]:result.npc_cyber_operators_){
            const auto source=result.entities_.find(order.attacking_ship);
            const auto target=result.entities_.find(order.target_ship);
            if(command_id!=order.command_id ||
               !result.submitted_ids_.count(command_id) ||
               source==result.entities_.end() ||
               target==result.entities_.end() ||
               (source->second.type!=EntityType::Ship &&
                source->second.type!=EntityType::Wreck) ||
               (target->second.type!=EntityType::Ship &&
                target->second.type!=EntityType::Wreck) ||
               source->second.owner_id!=order.owner ||
               target->second.owner_id==order.owner)
                throw std::runtime_error("snapshot: orphan NPC cyber attribution");
            bool original_command=false;
            for(const auto& [due,commands]:result.commands_){
                (void)due;
                for(const auto& command:commands)
                    if(command.id==command_id &&
                       command.action==Action::HackConfuse &&
                       command.issuer==order.owner &&
                       command.actor==order.attacking_ship &&
                       command.target==order.target_ship)
                        original_command=true;
            }
            for(const auto& event:result.events_)
                if(event.command_id==command_id)
                    original_command=true; // immutable original execution receipt
            if(!original_command)
                throw std::runtime_error("snapshot: NPC cyber order lacks original command");
        }
    if (result.canonical_state() != bytes)
        throw std::runtime_error("snapshot: noncanonical or out-of-order records");
    return result;
}

void Simulation::swap(Simulation& other) noexcept {
    using std::swap;
    swap(system_id_, other.system_id_);
    systems_.swap(other.systems_);
    swap(tick_, other.tick_);
    swap(next_entity_id_, other.next_entity_id_);
    swap(rng_state_, other.rng_state_);
    entities_.swap(other.entities_);
    swap(ship_status_,other.ship_status_);
    npc_cyber_operators_.swap(other.npc_cyber_operators_);
    ship_frames_.swap(other.ship_frames_);
    generated_local_regions_.swap(other.generated_local_regions_);
    local_entity_anchors_.swap(other.local_entity_anchors_);
    local_environments_.swap(other.local_environments_);
    oasis_reserves_.swap(other.oasis_reserves_);
    oasis_extractors_.swap(other.oasis_extractors_);
    planetary_sites_.swap(other.planetary_sites_);
    planetary_shuttles_.swap(other.planetary_shuttles_);
    local_delivery_orders_.swap(other.local_delivery_orders_);
    intercolony_freight_.swap(other.intercolony_freight_);
    shipyard_constructions_.swap(other.shipyard_constructions_);
    freighter_build_orders_.swap(other.freighter_build_orders_);
    shipyard_customer_orders_.swap(other.shipyard_customer_orders_);
    work_directives_.swap(other.work_directives_);
    npc_flight_orders_.swap(other.npc_flight_orders_);
    shuttle_flight_request_ids_.swap(other.shuttle_flight_request_ids_);
    planetary_family_booking_ids_.swap(other.planetary_family_booking_ids_);
    pending_celestial_transits_.swap(other.pending_celestial_transits_);
    completed_celestial_transits_.swap(other.completed_celestial_transits_);
    physical_celestial_systems_.swap(other.physical_celestial_systems_);
    depleted_.swap(other.depleted_);
    mining_.swap(other.mining_);
    discoveries_.swap(other.discoveries_);
    pending_probe_discoveries_.swap(other.pending_probe_discoveries_);
    swap(probe_report_delay_ticks_,other.probe_report_delay_ticks_);
    survey_probes_.swap(other.survey_probes_);
    hangars_.swap(other.hangars_);
    markets_.swap(other.markets_);
    wallets_.swap(other.wallets_);
    gate_ops_.swap(other.gate_ops_);
    colony_ships_.swap(other.colony_ships_);
    life_support_.swap(other.life_support_);
    reclaimers_.swap(other.reclaimers_);
    waterworks_.swap(other.waterworks_);
    policy_sites_.swap(other.policy_sites_);
    npcs_.swap(other.npcs_);
    casualties_.swap(other.casualties_);
    named_medics_.swap(other.named_medics_);
    medical_cases_.swap(other.medical_cases_);
    medical_history_.swap(other.medical_history_);
    medic_shifts_.swap(other.medic_shifts_);
    mobile_medical_kits_.swap(other.mobile_medical_kits_);
    crew_rehomes_.swap(other.crew_rehomes_);
    colony_departures_.swap(other.colony_departures_);
    physical_dependents_.swap(other.physical_dependents_);
    voyages_.swap(other.voyages_);
    farm_team_xp_.swap(other.farm_team_xp_);
    swap(next_npc_id_,other.next_npc_id_);
    swap(npc_next_seen_,other.npc_next_seen_);
    farms_.swap(other.farms_);
    habitats_.swap(other.habitats_);
    utilities_.swap(other.utilities_);
    communities_.swap(other.communities_);
    clinics_.swap(other.clinics_);
    built_outposts_.swap(other.built_outposts_);
    workshops_.swap(other.workshops_);
    storages_.swap(other.storages_);
    geology_.swap(other.geology_);
    activity_events_.swap(other.activity_events_);
    swap(snapshot_version_, other.snapshot_version_);
    commands_.swap(other.commands_);
    submitted_ids_.swap(other.submitted_ids_);
    events_.swap(other.events_);
}

std::int64_t Simulation::total_ore() const {
    std::int64_t total = 0;
    for (const auto& [id, e] : entities_) {
        (void)id;
        if (e.ore < 0 || e.cargo < 0 || e.ore > std::numeric_limits<std::int64_t>::max() - total)
            throw std::overflow_error("invalid ore total");
        total += e.ore;
        if (e.cargo > std::numeric_limits<std::int64_t>::max() - total)
            throw std::overflow_error("invalid cargo total");
        total += e.cargo;
    }
    std::int64_t colony_cargo_kg=0;
    for(const auto& [id,c]:colony_ships_){
        (void)id;
        std::int64_t physical_kg=c.tailings_kg;
        for(const auto& [name,kg]:c.minerals_kg){(void)name;physical_kg+=kg;}
        for(const auto& [name,kg]:c.goods_kg){(void)name;physical_kg+=kg;}
        if(physical_kg<0 || physical_kg>1000000000000LL-colony_cargo_kg)
            throw std::overflow_error("invalid colony material stacks");
        colony_cargo_kg+=physical_kg;
        const auto& eq=entities_.at(id);
        const auto installed=(eq.fitting.exploration_scanner()?1:0)+
            (eq.fitting.probe_launcher()?1:0)+
            (eq.fitting.intrusion_emitter()?1:0)+
            (eq.fitting.cyber_firewall()?1:0);
        const auto deployed=std::count_if(survey_probes_.begin(),survey_probes_.end(),
            [id](const auto& row){return row.second.carrier==id;});
        const auto mass=c.refined+2*(c.modules+installed+eq.survey_probes_carried+
            static_cast<std::int64_t>(deployed));
        if(mass<0 || mass>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid colony ore equivalent");
        total+=mass;
    }
    // SHIP-01A.1: material already removed from the colony's four-module
    // inventory remains in the physically built yard at 2 ore/module.
    for(const auto& [id,yard]:shipyard_constructions_) {
        (void)id;
        if(yard.embodied_ore!=8 ||
           total>std::numeric_limits<std::int64_t>::max()-yard.embodied_ore)
            throw std::overflow_error("invalid shipyard embodied mass");
        total+=yard.embodied_ore;
    }
    // Six manufactured modules are already debited; the 12 units stay
    // embodied in the real assembly job and later in its minted hull.
    for(const auto& [id,order]:freighter_build_orders_){
        (void)id;
        if(order.embodied_ore!=12 ||
           total>std::numeric_limits<std::int64_t>::max()-order.embodied_ore)
            throw std::overflow_error("invalid fabricated freighter mass");
        total+=order.embodied_ore;
    }
    std::int64_t life_mass_kg=0;
    const auto collect=[&](const ColonyLifeSupport& l){
        const auto kg=l.food_kg+l.water_kg+l.oxygen_kg+l.nutrients_kg+l.co2_kg+
          l.crops_kg+l.compost_kg+l.wastewater_kg+l.organic_waste_kg+
          l.sludge_kg+l.hydrogen_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid aggregate life-support mass");
        life_mass_kg+=kg;
    };
    for(const auto& [id,l]:life_support_){(void)id;collect(l);}
    for(const auto& [id,f]:farms_){(void)id;
        collect(f.life);
        if(f.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid embodied greenhouse mass");
        total+=f.embodied_ore;
    }
    for(const auto& [id,u]:utilities_){(void)id;
        if(u.embodied_ore<4 || u.embodied_ore>1000000 ||
           total>std::numeric_limits<std::int64_t>::max()-u.embodied_ore)
            throw std::overflow_error("invalid utility embodied mass");
        if(u.spare_modules<0 || u.spare_modules>1000 ||
           total>std::numeric_limits<std::int64_t>::max()-u.embodied_ore-2*u.spare_modules)
            throw std::overflow_error("invalid utility spare parts mass");
        total+=u.embodied_ore+2*u.spare_modules;
    }
    for(const auto& [id,c]:communities_){(void)id;
        if(c.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid community hall mass");
        total+=c.embodied_ore;
        const auto kg=c.food_kg+c.water_kg+c.organic_waste_kg+c.wastewater_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid community stock mass");
        life_mass_kg+=kg;
    }
    for(const auto& [id,p]:waterworks_){(void)id;
        if(p.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid waterworks construction mass");
        total+=4;
        const auto kg=p.raw_feed_kg+p.brine_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid waterworks mass");
        life_mass_kg+=kg;
    }
    for(const auto& [id,r]:reclaimers_){(void)id;
        if(r.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid reclaimer mass");
        total+=4;
        const auto kg=r.sludge_kg+r.carbon_residue_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid reclaimer residue");
        life_mass_kg+=kg;
    }
    for(const auto& [id,c]:clinics_){(void)id;
        if(c.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid embodied clinic mass");
        total+=4;
        const auto kg=c.water_kg+c.substrate_kg+c.wastewater_kg+c.medical_waste_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid clinic mass");
        life_mass_kg+=kg;
    }
    for(const auto& [id,k]:mobile_medical_kits_){(void)id;
        const auto kg=k.water_kg+k.substrate_kg+k.wastewater_kg+k.medical_waste_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid mobile medical inventory");
        life_mass_kg+=kg;
    }
    for(const auto& [id,h]:habitats_){(void)id;
        collect(h.life);
        if(h.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid embodied habitat mass");
        total+=h.embodied_ore;
    }
    for(const auto& [id,w]:workshops_){(void)id;
        if(w.embodied_ore!=(w.finisher_installed?4:2) ||
           total>std::numeric_limits<std::int64_t>::max()-w.embodied_ore)
            throw std::overflow_error("invalid workshop embodied mass");
        total+=w.embodied_ore;
        const auto kg=w.aluminum_kg+w.panels_kg+w.scrap_kg+w.reserved_aluminum_kg+
                      w.reserved_panels_kg+w.frames_kg;
        if(kg<0 || kg>6000 || kg>std::numeric_limits<std::int64_t>::max()-life_mass_kg)
            throw std::overflow_error("invalid workshop inventories");
        life_mass_kg+=kg;
    }
    for(const auto& [site,st]:storages_){(void)site;
        if(st.embodied_ore!=2 || total>std::numeric_limits<std::int64_t>::max()-2)
            throw std::overflow_error("invalid storage construction");
        total+=2;
        if(st.extra_bins.size()>static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max()-total)/2))
            throw std::overflow_error("invalid extra storage structures");
        total+=static_cast<std::int64_t>(2*st.extra_bins.size());
        if(st.links.size()>static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max()-total)/2))
            throw std::overflow_error("invalid storage links");
        total+=static_cast<std::int64_t>(2*st.links.size());
        for(const auto& [linkid,link]:st.links){(void)linkid;
            if(link.route_cells.size()>static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max()-total)/2))
                throw std::overflow_error("invalid conveyor construction mass");
            total+=static_cast<std::int64_t>(2*link.route_cells.size());
        }
        if(st.network_escrow_kg<0||st.network_escrow_kg>1000000000)
            throw std::overflow_error("invalid constructed storage network escrow");
        std::int64_t qty=st.network_escrow_kg;
        auto cap=storage_inventory_snapshot(site);
        for(const auto& [name,value]:st.goods_kg){(void)name;
            if(value<0 || value>4000-(qty-st.network_escrow_kg))throw std::overflow_error("invalid storage stack");
            qty+=value;
        }
        for(const auto& [node,contents]:st.extra_bins){
            if(node<1 || node>static_cast<std::uint32_t>(std::numeric_limits<int>::max()/4))
                throw std::overflow_error("invalid extra storage identity");
            auto extra=storage_node_inventory_snapshot(site,node);
            std::int64_t bin_qty=0;
            for(const auto& [name,value]:contents){(void)name;
                if(value<0 || value>4000-bin_qty)throw std::overflow_error("invalid extra storage stack");
                bin_qty+=value;
            }
            if(bin_qty>std::numeric_limits<std::int64_t>::max()-qty)
                throw std::overflow_error("extra storage total overflow");
            qty+=bin_qty;
            (void)extra;
        }
        if(qty>std::numeric_limits<std::int64_t>::max()-life_mass_kg)
            throw std::overflow_error("invalid storage material total");
        life_mass_kg+=qty;
    }
    // PLAY-13D.3: installed regional modules and each kilogram of site
    // stocks remain counted exactly once in world total matter.
    for(const auto& [key,site]:planetary_sites_) {
        (void)key;
        if(site.housing_modules>2 || site.housing_phase>2 ||
            site.embodied_ore!=4+4*(
                                   std::int64_t(site.farm ||
                                     site.production_projects[0].phase!=0)+
                                   std::int64_t(site.extractor ||
                                     site.production_projects[1].phase!=0)+
                                   std::int64_t(site.housing_phase!=0)+
                                   std::int64_t(site.utility_phase!=0)) ||
           site.embodied_ore>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid planetary installation mass");
        total+=site.embodied_ore+2*site.housing_modules;
        const auto kg=site.water_kg+site.nutrients_kg+site.food_kg+
                      site.wastewater_kg+site.oxygen_kg+site.co2_kg+
                      site.organic_waste_kg+site.brine_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid regional supplies mass");
        life_mass_kg+=kg;
    }

    // PLAY-13D.4C.2: source modules became the shuttle's physical dry mass;
    // manifest kilograms move out of ship storage exactly once.
    std::set<std::uint64_t> assigned_shuttle_pilots;
    for(const auto& [key,v]:planetary_shuttles_) {
        const auto site=planetary_sites_.find(key);
        if(site==planetary_sites_.end() || v.owner!=site->second.owner ||
           v.source_ship!=site->second.source_ship || v.dry_mass_ore!=4 ||
           v.water_kg<0 || v.food_kg<0 || v.nutrients_kg<0 || v.water_kg>1000 ||
           v.food_kg>1000-v.water_kg ||
           v.nutrients_kg>1000-v.water_kg-v.food_kg ||
           v.oxygen_kg<0 ||
           v.oxygen_kg>1000-v.water_kg-v.food_kg-v.nutrients_kg ||
           v.housing_module>1 ||
           !colony_ships_.count(v.source_ship) ||
           total>std::numeric_limits<std::int64_t>::max()-v.dry_mass_ore)
            throw std::overflow_error("invalid source-backed shuttle");
        if(snapshot_version_>=81 &&
           ((v.descent_receipt && !shuttle_flight_request_ids_.count(v.descent_receipt)) ||
            (v.ascent_receipt && !shuttle_flight_request_ids_.count(v.ascent_receipt)) ||
            (v.flight_request && !shuttle_flight_request_ids_.count(v.flight_request))))
            throw std::overflow_error("shuttle references missing flight receipt");
        if(v.duty_ids.size()>v.disembarked_ids.size() ||
           !std::is_sorted(v.duty_ids.begin(),v.duty_ids.end()))
            throw std::overflow_error("invalid temporary visitor duties");
        for(const auto worker:v.duty_ids)
            if(!std::binary_search(v.disembarked_ids.begin(),
                                   v.disembarked_ids.end(),worker))
                throw std::overflow_error("temporary worker not on surface");
        if(v.disembarked_ids.size()>64 ||
           !std::is_sorted(v.disembarked_ids.begin(),v.disembarked_ids.end()))
            throw std::overflow_error("noncanonical surface visitor ledger");
        if(v.visitor_arrival_receipts.size()>v.disembarked_ids.size())
            throw std::overflow_error("excess individual transport receipts");
        for(const auto& [visitor,receipt]:v.visitor_arrival_receipts)
            if(!visitor || !receipt ||
               !std::binary_search(v.disembarked_ids.begin(),v.disembarked_ids.end(),visitor) ||
               !shuttle_flight_request_ids_.count(receipt))
                throw std::overflow_error("unbacked individual visitor arrival");
        for(const auto visitor_id:v.disembarked_ids) {
            if(!visitor_id || visitor_id==v.pilot_npc ||
               !assigned_shuttle_pilots.insert(visitor_id).second)
                throw std::overflow_error("duplicate or conflicting shuttle visitor");
            const auto person=npcs_.find(visitor_id);
            if(person==npcs_.end() ||
               person->second.location!=site->second.surface_location ||
               person->second.home_ship!=v.source_ship ||
               person->second.assigned_site!=v.source_ship ||
               person->second.role!=NpcRole::Ship ||
               person->second.transport_id)
                throw std::overflow_error("surface visitor custody divergence");
        }
        for(const auto passenger_id:v.passenger_ids) {
            if(!passenger_id || passenger_id==v.pilot_npc ||
               !assigned_shuttle_pilots.insert(passenger_id).second)
                throw std::overflow_error("duplicate or pilot shuttle passenger");
            const auto passenger=npcs_.find(passenger_id);
            const auto where=(v.flight_phase==1 || v.flight_phase==3)?
                shuttle_transit_custody_id(key):
                (v.flight_phase==2?site->second.surface_location:v.source_ship);
            if(passenger==npcs_.end() || passenger->second.location!=where ||
               passenger->second.home_ship!=v.source_ship ||
               passenger->second.assigned_site!=v.source_ship ||
               passenger->second.role!=NpcRole::Ship ||
               passenger->second.transport_id)
                throw std::overflow_error("invalid named shuttle passenger custody");
        }
        if(v.passenger_ids.size()>3 ||
           !std::is_sorted(v.passenger_ids.begin(),v.passenger_ids.end()))
            throw std::overflow_error("noncanonical shuttle passenger manifest");
        if(v.pilot_npc) {
            if(!assigned_shuttle_pilots.insert(v.pilot_npc).second)
                throw std::overflow_error("pilot assigned to multiple shuttles");
            const auto npc=npcs_.find(v.pilot_npc);
            const auto facility=planetary_sites_.find(key);
            const auto destination=facility==planetary_sites_.end()?EntityId{0}:
                facility->second.surface_location;
            const auto expected=(v.flight_phase==1 || v.flight_phase==3)?
                shuttle_transit_custody_id(key):
                (v.flight_phase==2?destination:v.source_ship);
            if(npc==npcs_.end() || npc->second.home_ship!=v.source_ship ||
               npc->second.assigned_site!=v.source_ship ||
               npc->second.location!=expected || npc->second.transport_id ||
               npc->second.role!=NpcRole::Ship ||
               ((v.flight_phase==1 || v.flight_phase==3) &&
                (!v.flight_request || !v.arrival_tick)) ||
               (v.flight_phase>3))
                throw std::overflow_error("invalid shuttle pilot custody");
        }
        total+=v.dry_mass_ore+2*v.housing_module;
        const auto held=v.water_kg+v.food_kg+v.nutrients_kg+v.oxygen_kg;
        if(held>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid shuttle held stock");
        life_mass_kg+=held;
    }
    // PLAY-13C.6D constructed extraction equipment retains its source modules.
    for(const auto& [ship,rig]:oasis_extractors_) {
        (void)ship;
        if(rig.embodied_ore!=4 || total>std::numeric_limits<std::int64_t>::max()-4)
            throw std::overflow_error("invalid extractor embodied module mass");
        total+=4;
    }
    // PLAY-13C.6C explicit genesis-only finite environmental reservoirs.
    for(const auto& [body,reserve]:oasis_reserves_) {
        (void)body;
        const auto kg=reserve.water_kg+reserve.nutrients_kg;
        if(kg<0 || kg>1000000000LL-life_mass_kg)
            throw std::overflow_error("invalid physical oasis reserve mass");
        life_mass_kg+=kg;
    }
    // Only the WHOLE world must conserve complete ore-equivalent mass;
    // individual inventories can now hold a single kilogram stack.
    if(colony_cargo_kg>std::numeric_limits<std::int64_t>::max()-life_mass_kg)
        throw std::overflow_error("invalid world material total");
    const auto combined_kg=life_mass_kg+colony_cargo_kg;
    if(combined_kg%1000 || combined_kg/1000>std::numeric_limits<std::int64_t>::max()-total)
        throw std::overflow_error("invalid conserved material total");
    total+=combined_kg/1000;
    for(const auto& [id,site]:built_outposts_){
        (void)id;
        if(site.embodied_ore<0 || site.embodied_ore>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid outpost material equivalent");
        total+=site.embodied_ore;
    }
    for (const auto& [key, h] : hangars_) {
        (void)key;
        if (h.ore < 0 || h.ore > std::numeric_limits<std::int64_t>::max() - total)
            throw std::overflow_error("invalid hangar ore total");
        total += h.ore;
    }
    for (const auto& [id,m] : markets_) {
        (void)id;
        if (m.ore<0 || m.ore>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid market ore total");
        total+=m.ore;
    }
    return total;
}
std::int64_t Simulation::total_material_kg() const {
    const auto tonnes=total_ore();
    if(tonnes>std::numeric_limits<std::int64_t>::max()/1000)
        throw std::overflow_error("world mass exceeds kilogram precision");
    return tonnes*1000;
}
std::int64_t Simulation::total_credits() const {
    std::int64_t total=0;
    for (const auto& [id,m] : markets_) {
        (void)id;
        if (m.credits<0 || m.credits>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid market credits");
        total+=m.credits;
    }
    for (const auto& [id,balance] : wallets_) {
        (void)id;
        if (balance<0 || balance>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid wallet credits");
        total+=balance;
    }
    return total;
}
} // namespace galaxy
