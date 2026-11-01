#include "galaxy/Simulation.hpp"
#include "galaxy/QuantifiedProduction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace galaxy {
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
    return name=="structural_alloy" || name=="aluminum" || name=="aluminum_panels" || name=="aluminum_scrap" || name=="aluminum_frames" || name=="ceramic" ||
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
    return add_entity(entity);
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
            npcs_.emplace(id,NpcRecord{id,ship,ship,NpcRole::Ship,0,0});
        }
        for(const auto& [site,f]:farms_)if(f.source_ship==ship)
            for(std::int64_t n=0;n<f.workers;++n){
                auto id=next_npc_id_++;
                npcs_.emplace(id,NpcRecord{id,ship,site,NpcRole::Farm,0,0});
            }
        for(const auto& [site,h]:habitats_)if(h.source_ship==ship)
            for(std::int64_t n=0;n<h.residents;++n){
                auto id=next_npc_id_++;
                npcs_.emplace(id,NpcRecord{id,ship,site,NpcRole::Habitat,0,0});
            }
    }
    if(npcs_.empty())throw std::invalid_argument("no colony population to materialize");
    snapshot_version_=20;
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
        // A resident reassigned away from this outpost cannot remain its
        // machine operator. Reserved material remains owned by the workshop.
        if(source!=ship){
            auto wi=workshops_.find(source);
            if(wi!=workshops_.end()){
                if(wi->second.operator_npc_id==id)wi->second.operator_npc_id=0;
                if(wi->second.finisher_npc_id==id)wi->second.finisher_npc_id=0;
            }
        }
        n.location=target;n.role=target_role;
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
        if(n.role!=NpcRole::Farm || n.location!=site)continue;
        std::uint64_t bonus=0;
        if(n.mentor_id){
            auto mentor=npcs_.find(n.mentor_id);
            if(mentor!=npcs_.end() && mentor->second.location==site &&
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
    if(f->second.workers>=2)farm_team_xp_[site]=std::min<std::uint64_t>(1000000,farm_team_xp_[site]+1);
}
FarmWorkforceAssessment Simulation::assess_farm_workforce(EntityId site) const {
    FarmWorkforceAssessment r{};
    const auto f=farms_.find(site);
    if(f==farms_.end())return r;
    r.workers=f->second.workers;
    if(auto it=farm_team_xp_.find(site);it!=farm_team_xp_.end())r.cooperative_cycles=it->second;
    r.coordination=static_cast<std::int64_t>(std::min<std::uint64_t>(100,r.cooperative_cycles/10));
    for(const auto& [id,n]:npcs_){(void)id;
        if(n.location==site && n.role==NpcRole::Farm)r.experience_total+=n.farm_xp;
    }
    return r;
}
const ColonyShip* Simulation::colony_ship(EntityId id) const {
    const auto it=colony_ships_.find(id);
    return it==colony_ships_.end()?nullptr:&it->second;
}
std::int64_t Simulation::loaded_cargo_liters(const Entity& ship) const {
    const auto* c=colony_ship(ship.id);
    // Fixed gameplay volumes: each legacy ore/refined unit 1 m3, construction
    // module 2 m3; current kilogram-quantity material stack 1 litre per unit.
    // These are balancing coefficients, NOT density / payload calculations.
    const std::int64_t bulk=ship.cargo+(c ? c->refined+2*c->modules : 0);
    if(bulk<0 || bulk>1000000000000LL)throw std::overflow_error("cargo overflow");
    std::int64_t volume=bulk*1000;
    if(c){
        const auto add=[&](std::int64_t v){
            if(v<0 || v>std::numeric_limits<std::int64_t>::max()-volume)
                throw std::overflow_error("invalid cargo stack volume");
            volume+=v;
        };
        add(c->tailings_kg);
        for(const auto& [name,units]:c->minerals_kg){(void)name;add(units);}
        for(const auto& [name,units]:c->goods_kg){(void)name;add(units);}
    }
    return volume;
}
std::int64_t Simulation::ship_cargo_volume_liters(EntityId ship_id) const {
    const auto* ship=find(ship_id);
    if(!ship || ship->type!=EntityType::Ship)
        throw std::invalid_argument("not a ship");
    return loaded_cargo_liters(*ship);
}
void Simulation::transfer_geological_minerals(EntityId ship, EntityId rock, std::int64_t tonnes){
    auto c=colony_ships_.find(ship);
    const auto g=geology_.find(rock);
    if(c==colony_ships_.end() || g==geology_.end())throw std::runtime_error("mineral transfer requires colony ship and geological asteroid");
    for(const auto phase:mineral_profile(g->second.taxonomy))
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

bool Simulation::submit(const Command& c) {
    if (c.id == 0 || c.execute_tick <= tick_ || c.issuer == 0 || submitted_ids_.count(c.id)) return false;
    if (c.action == Action::HostDestroyShip || c.action == Action::HostCrewLoss) {
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
    // GRSM-01b: host-authorized deterministic physical incidents. These are
    // simulation state transitions, NOT user-invokable reputation events.
    // Combat/damage resolution will eventually authorize these outcomes.
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
        case Action::Thrust:
            if (actor.dock_state != DockState::InSpace) {
                result.result = Outcome::NotInSpace; return result;
            }
            if (actor.jump_state==ShipJumpState::Jumping) {
                result.result=Outcome::InTransition;return result;
            }
            if (actor.jump_state==ShipJumpState::ArrivalCloak && (c.thrust_x || c.thrust_y)) {
                actor.jump_state=ShipJumpState::Ready;
                actor.jump_ticks=0;
            }
            if (c.thrust_x < -1 || c.thrust_x > 1 || c.thrust_y < -1 || c.thrust_y > 1) {
                result.result = Outcome::InvalidArgument;
                return result;
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
            if(n>0 && colony_ships_.count(c.actor) && geology_.count(c.target))
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
        case Action::ConstructWorkshop:
        case Action::TransferWorkshopFeed:
        case Action::AssignWorkshopOperator:
        case Action::TransferWorkshopOutput:
        case Action::ConstructWorkshopFinisher:
        case Action::AssignWorkshopFinisher:
        case Action::TransferWorkshopFinalOutput:
        case Action::TransferWorkshopStack:
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
            if(c.action==Action::TransferWorkshopFeed ||
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
                    if(c.amount){
                        auto npc=npcs_.find(static_cast<std::uint64_t>(c.amount));
                        if(npc==npcs_.end() || npc->second.home_ship!=c.actor ||
                           npc->second.location!=c.target || npc->second.role!=NpcRole::Habitat ||
                           !habitats_.count(c.target)){
                            result.result=Outcome::InvalidTarget;return result;
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
                // ECON-02B1c: exact, independent item stacks. Signed quantity:
                // positive ship -> site, negative site -> ship. Selector identifies
                // the item, NOT an arbitrary server-side string supplied by caller.
                if(c.action==Action::TransferWorkshopStack){
                    if(c.thrust_x==2 && !w.finisher_installed){
                        result.result=Outcome::InvalidTarget;return result;
                    }
                    const auto qty=std::abs(c.amount);
                    const char* names[]={"aluminum","aluminum_panels","aluminum_frames","aluminum_scrap"};
                    std::int64_t* stock[]={&w.aluminum_kg,&w.panels_kg,&w.frames_kg,&w.scrap_kg};
                    auto& local=*stock[c.thrust_x];
                    const std::string name=names[c.thrust_x];
                    if(c.amount>0){
                        auto it=cship.goods_kg.find(name);
                        if(it==cship.goods_kg.end() || it->second<qty || local>1000-qty){
                            result.result=Outcome::NoResources;return result;
                        }
                        it->second-=qty;
                        if(it->second==0)cship.goods_kg.erase(it);
                        local+=qty;
                    }else{
                        if(local<qty){result.result=Outcome::NoResources;return result;}
                        if(loaded_cargo_liters(actor)>actor.cargo_capacity*1000-qty){
                            result.result=Outcome::CargoFull;return result;
                        }
                        local-=qty;
                        cship.goods_kg[name]+=qty;
                    }
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
        if(n>0 && colony_ships_.count(actor_id) && geology_.count(job.target))
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
        if(!f.workers)continue;
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
        const auto batches=std::min({f.workers,std::int64_t{8},l.water_kg/2,
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
            if(u.delegated_maintenance && u.condition<=40){
                const bool staffed=u.technicians>0 && h!=habitats_.end() &&
                    h->second.residents>=u.technicians &&
                    h->second.life.consecutive_supplied_cycles>0;
                if(staffed && u.spare_modules>0){
                    --u.spare_modules;u.embodied_ore+=2;
                    u.condition=std::min<std::int64_t>(100,u.condition+70);
                    ++u.repairs;++u.delegated_repairs;
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
        const auto crew=colony_ships_.at(id).crew;
        const auto food=std::min(crew,l.food_kg);
        const auto water=std::min(crew,l.water_kg);
        const auto oxygen=std::min(crew,l.oxygen_kg);
        const bool supplied=food==crew && water==crew && oxygen==crew;
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
        const auto u=utilities_.find(site);
        const auto n=npcs_.find(w.operator_npc_id);
        const bool staff=w.operator_npc_id && n!=npcs_.end() &&
            n->second.home_ship==w.source_ship &&
            n->second.location==site && n->second.role==NpcRole::Habitat &&
            habitats_.count(site) && habitats_.at(site).residents>0;
        const bool power=u!=utilities_.end() && u->second.powered;
        const bool working=u!=utilities_.end() && u->second.condition>0;
        industry::Recipe recipe{"aluminum_panel","cut",{{"aluminum",20000}},
            {"aluminum_panels",19000},{"aluminum_scrap",1000}};
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
            const bool fn_staff=w.finisher_npc_id && fn!=npcs_.end() &&
                fn->second.home_ship==w.source_ship && fn->second.location==site &&
                fn->second.role==NpcRole::Habitat &&
                habitats_.count(site) && habitats_.at(site).residents>0 &&
                w.finisher_npc_id!=w.operator_npc_id;
            industry::Recipe fr{"aluminum_frame","shape",{{"aluminum_panels",20000}},
                {"aluminum_frames",19000},{"aluminum_scrap",1000}};
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
void Simulation::step() {
    if (tick_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("tick counter exhausted");
    ++tick_;
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
    process_gate_maintenance();
    process_utilities();
    process_workshops();
    process_waterworks();
    process_colony_needs();
    process_farms();
    process_habitats();
    process_communities();
    process_clinics();
    process_reclaimers();
    for (auto& pair : entities_) {
        Entity& entity = pair.second;
        if (entity.type != EntityType::Ship || entity.dock_state != DockState::InSpace || entity.jump_state != ShipJumpState::Ready) continue;
        entity.position.x = std::clamp(entity.position.x + entity.velocity.x, -kWorldBoundMm, kWorldBoundMm);
        entity.position.y = std::clamp(entity.position.y + entity.velocity.y, -kWorldBoundMm, kWorldBoundMm);
        if (entity.position.x == kWorldBoundMm && entity.velocity.x > 0) entity.velocity.x = 0;
        if (entity.position.x == -kWorldBoundMm && entity.velocity.x < 0) entity.velocity.x = 0;
        if (entity.position.y == kWorldBoundMm && entity.velocity.y > 0) entity.velocity.y = 0;
        if (entity.position.y == -kWorldBoundMm && entity.velocity.y < 0) entity.velocity.y = 0;
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
    out << (snapshot_version_ >= 24 ? "galaxy-kernel-v24|" : snapshot_version_ >= 23 ? "galaxy-kernel-v23|" : snapshot_version_ >= 22 ? "galaxy-kernel-v22|" : snapshot_version_ >= 21 ? "galaxy-kernel-v21|" : snapshot_version_ >= 20 ? "galaxy-kernel-v20|" : snapshot_version_ >= 19 ? "galaxy-kernel-v19|" : snapshot_version_ >= 18 ? "galaxy-kernel-v18|" : snapshot_version_ >= 17 ? "galaxy-kernel-v17|" : snapshot_version_ >= 16 ? "galaxy-kernel-v16|" : snapshot_version_ >= 15 ? "galaxy-kernel-v15|" : snapshot_version_ >= 14 ? "galaxy-kernel-v14|" : snapshot_version_ >= 13 ? "galaxy-kernel-v13|" : snapshot_version_ >= 12 ? "galaxy-kernel-v12|" : snapshot_version_ >= 11 ? "galaxy-kernel-v11|" : snapshot_version_ >= 10 ? "galaxy-kernel-v10|" : snapshot_version_ >= 9 ? "galaxy-kernel-v9|" : snapshot_version_ >= 8 ? "galaxy-kernel-v8|" : snapshot_version_ >= 7 ? "galaxy-kernel-v7|" : snapshot_version_ >= 6 ? "galaxy-kernel-v6|" : snapshot_version_ >= 5 ? "galaxy-kernel-v5|" : snapshot_version_ >= 4 ? "galaxy-kernel-v4|" : snapshot_version_ == 3 ? "galaxy-kernel-v3|" : snapshot_version_ == 2 ? "galaxy-kernel-v2|" : "galaxy-kernel-v1|") << system_id_ << '|' << tick_ << '|' << next_entity_id_
        << '|' << rng_state_ << '\n';
    if (snapshot_version_>=5) for(const auto system:systems_)out<<"system|"<<system<<'\n';
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
        out << '\n';
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
           <<c.treated_cases<<'|'<<c.recorded_cases<<'\n';
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
        for(const auto& [id,n]:npcs_)
            out<<"npc|"<<id<<'|'<<n.home_ship<<'|'<<n.location<<'|'
               <<static_cast<int>(n.role)<<'|'<<n.farm_xp<<'|'<<n.mentor_id<<'\n';
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
                << '|' << c.amount << '\n';
        }
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
    if (h[0] != "galaxy-kernel-v1" && h[0] != "galaxy-kernel-v2" && h[0] != "galaxy-kernel-v3" && h[0] != "galaxy-kernel-v4" && h[0] != "galaxy-kernel-v5" && h[0] != "galaxy-kernel-v6" && h[0] != "galaxy-kernel-v7" && h[0] != "galaxy-kernel-v8" && h[0] != "galaxy-kernel-v9" && h[0] != "galaxy-kernel-v10" && h[0] != "galaxy-kernel-v11" && h[0] != "galaxy-kernel-v12" && h[0] != "galaxy-kernel-v13" && h[0] != "galaxy-kernel-v14" && h[0] != "galaxy-kernel-v15" && h[0] != "galaxy-kernel-v16" && h[0] != "galaxy-kernel-v17" && h[0] != "galaxy-kernel-v18" && h[0] != "galaxy-kernel-v19" && h[0] != "galaxy-kernel-v20" && h[0] != "galaxy-kernel-v21" && h[0] != "galaxy-kernel-v22" && h[0] != "galaxy-kernel-v23" && h[0] != "galaxy-kernel-v24") throw std::runtime_error("snapshot: unsupported version");
    const auto sys = u64(h[1]);
    if (sys == 0 || sys > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("snapshot: invalid system");
    Simulation result(u64(h[4]), static_cast<std::uint32_t>(sys));
    result.snapshot_version_ = h[0] == "galaxy-kernel-v24" ? 24 : h[0] == "galaxy-kernel-v23" ? 23 : h[0] == "galaxy-kernel-v22" ? 22 : h[0] == "galaxy-kernel-v21" ? 21 : h[0] == "galaxy-kernel-v20" ? 20 : h[0] == "galaxy-kernel-v19" ? 19 : h[0] == "galaxy-kernel-v18" ? 18 : h[0] == "galaxy-kernel-v17" ? 17 : h[0] == "galaxy-kernel-v16" ? 16 : h[0] == "galaxy-kernel-v15" ? 15 : h[0] == "galaxy-kernel-v14" ? 14 : h[0] == "galaxy-kernel-v13" ? 13 : h[0] == "galaxy-kernel-v12" ? 12 : h[0] == "galaxy-kernel-v11" ? 11 : h[0] == "galaxy-kernel-v10" ? 10 : h[0] == "galaxy-kernel-v9" ? 9 : h[0] == "galaxy-kernel-v8" ? 8 : h[0] == "galaxy-kernel-v7" ? 7 : h[0] == "galaxy-kernel-v6" ? 6 : h[0] == "galaxy-kernel-v5" ? 5 : h[0] == "galaxy-kernel-v4" ? 4 : h[0] == "galaxy-kernel-v3" ? 3 : h[0] == "galaxy-kernel-v2" ? 2 : 1;
    result.tick_ = u64(h[2]);
    result.next_entity_id_ = u64(h[3]);
    if (result.next_entity_id_ == 0) throw std::runtime_error("snapshot: invalid next entity ID");
    std::set<std::uint64_t> pending_ids, event_ids;
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
        } else if (f[0] == "entity") {
            require_fields(f, result.snapshot_version_>=5 ? 22 : result.snapshot_version_ >= 3 ? 15 : 12);
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
            if (!result.valid_position(e.position) ||
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
            if(!id || crew<1 || crew>1000 || refined<0 || modules<0 ||
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
            require_fields(f,15);const auto id=u64(f[1]);ClinicFacility c{};
            c.source_ship=u64(f[2]);c.embodied_ore=i64(f[3]);
            c.water_kg=i64(f[4]);c.substrate_kg=i64(f[5]);
            c.wastewater_kg=i64(f[6]);c.medical_waste_kg=i64(f[7]);
            c.medics=i64(f[8]);c.active_cases=i64(f[9]);
            c.cycles=u64(f[10]);c.consecutive_care_cycles=u64(f[11]);
            c.shortages=u64(f[12]);c.treated_cases=u64(f[13]);c.recorded_cases=u64(f[14]);
            const auto valid=[](std::int64_t v){return v>=0 && v<=100000000;};
            if(!id || !c.source_ship || c.embodied_ore!=4 ||
               !valid(c.water_kg)||!valid(c.substrate_kg)||!valid(c.wastewater_kg)||
               !valid(c.medical_waste_kg)||c.medics<0||c.medics>4||
               c.active_cases<0||c.active_cases>8||c.cycles>result.tick_/20||
               c.consecutive_care_cycles>c.cycles||c.shortages>c.cycles||
               c.recorded_cases>c.cycles||c.treated_cases>c.recorded_cases||
               c.active_cases!=static_cast<std::int64_t>(c.recorded_cases-c.treated_cases)||
               c.medical_waste_kg!=static_cast<std::int64_t>(c.treated_cases) ||
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
               (result.next_npc_id_==1 && (result.snapshot_version_==20 || !result.colony_ships_.empty())))
                throw std::runtime_error("snapshot: invalid NPC counter");
        } else if(f[0]=="npc" && result.snapshot_version_>=20){
            require_fields(f,7);
            NpcRecord n{};n.id=u64(f[1]);n.home_ship=u64(f[2]);n.location=u64(f[3]);
            const auto role=u64(f[4]);n.farm_xp=u64(f[5]);n.mentor_id=u64(f[6]);
            if(!n.id || role>2 || n.farm_xp>1000000 || n.mentor_id==n.id ||
               !result.npc_next_seen_ || n.id>=result.next_npc_id_ ||
               !result.npcs_.emplace(n.id,NpcRecord{n.id,n.home_ship,n.location,
                                            static_cast<NpcRole>(role),n.farm_xp,n.mentor_id}).second)
                throw std::runtime_error("snapshot: invalid NPC identity");
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
        } else if (f[0] == "pending") {
            require_fields(f, 11);
            Command c;
            c.id = u64(f[1]); c.execute_tick = u64(f[2]); c.sequence = u64(f[3]);
            c.issuer = u64(f[4]); c.actor = u64(f[5]); c.target = u64(f[6]);
            auto action = u64(f[7]);
            if (action > (result.snapshot_version_ >= 24 ? static_cast<unsigned>(Action::TransferWorkshopStack) : result.snapshot_version_ >= 23 ? static_cast<unsigned>(Action::TransferWorkshopFinalOutput) : result.snapshot_version_ >= 22 ? static_cast<unsigned>(Action::TransferWorkshopOutput) : result.snapshot_version_ >= 21 ? static_cast<unsigned>(Action::HostCrewLoss) : result.snapshot_version_ >= 20 ? static_cast<unsigned>(Action::AssignFarmMentor) : result.snapshot_version_ >= 19 ? static_cast<unsigned>(Action::AssignPolicyInspectors) : result.snapshot_version_ >= 18 ? static_cast<unsigned>(Action::TransferWaterworksFeed) : result.snapshot_version_ >= 17 ? static_cast<unsigned>(Action::ConstructReclaimer) : result.snapshot_version_ >= 16 ? static_cast<unsigned>(Action::AssignClinicStaff) : result.snapshot_version_ >= 15 ? static_cast<unsigned>(Action::AssignCommunityStaff) : result.snapshot_version_ >= 14 ? static_cast<unsigned>(Action::SetUtilityMaintenance) : result.snapshot_version_ >= 13 ? static_cast<unsigned>(Action::RepairUtility) : result.snapshot_version_ >= 12 ? static_cast<unsigned>(Action::AssignHabitatResidents) : result.snapshot_version_ >= 11 ? static_cast<unsigned>(Action::AssignFarmWorkers) : result.snapshot_version_ >= 10 ? static_cast<unsigned>(Action::LoadLifeWater) : result.snapshot_version_ >= 9 ? static_cast<unsigned>(Action::FabricateMaterialModule) : result.snapshot_version_ >= 7 ? static_cast<unsigned>(Action::ConstructOutpost) : result.snapshot_version_ >= 6 ? static_cast<unsigned>(Action::DeliverGateSpares) : result.snapshot_version_ >= 5 ? static_cast<unsigned>(Action::SetGatePairOnline) : result.snapshot_version_ >= 4 ? static_cast<unsigned>(Action::SellOre) : static_cast<unsigned>(Action::UnloadOre))) throw std::runtime_error("snapshot: invalid action");
            c.action = static_cast<Action>(action);
            auto tx = i64(f[8]), ty = i64(f[9]);
            if (tx < std::numeric_limits<int>::min() || tx > std::numeric_limits<int>::max() ||
                ty < std::numeric_limits<int>::min() || ty > std::numeric_limits<int>::max())
                throw std::runtime_error("snapshot: invalid thrust");
            c.thrust_x = static_cast<int>(tx); c.thrust_y = static_cast<int>(ty);
            c.amount = i64(f[10]);
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
                 c.action!=Action::HostCrewLoss && c.issuer==Simulation::kServerAuthority) ||
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
            if (outcome > (result.snapshot_version_ >= 7 ? static_cast<std::uint64_t>(Outcome::InsufficientCrew) : result.snapshot_version_ >= 5 ? static_cast<std::uint64_t>(Outcome::GateOffline) : result.snapshot_version_ >= 4 ? static_cast<std::uint64_t>(Outcome::InsufficientFunds) : static_cast<std::uint64_t>(Outcome::HangarFull)))
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
            if (kind > (result.snapshot_version_>=21 ? static_cast<unsigned>(ActivityKind::NpcLost) : result.snapshot_version_>=5 ? static_cast<unsigned>(ActivityKind::JumpCompleted) : static_cast<unsigned>(ActivityKind::Docked)) ||
                outcome > static_cast<std::uint64_t>(Outcome::HangarFull) ||
                a.tick == 0 || a.tick > result.tick_ || a.target == 0 || a.transferred < 0 ||
                (kind == 1 && (a.start_command_id == 0 || a.actor == 0 ||
                               outcome != static_cast<std::uint64_t>(Outcome::Applied) || a.transferred == 0)) ||
                (kind != 1 && a.transferred != 0) ||
                ((kind==static_cast<unsigned>(ActivityKind::ShipDestroyed) ||
                  kind==static_cast<unsigned>(ActivityKind::NpcLost)) &&
                  (!a.start_command_id || !a.actor || outcome!=static_cast<std::uint64_t>(Outcome::Applied))))
                throw std::runtime_error("snapshot: invalid activity event");
            a.kind = static_cast<ActivityKind>(kind);
            a.result = static_cast<Outcome>(outcome);
            result.activity_events_.push_back(a);
        } else throw std::runtime_error("snapshot: unknown record");
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
        if(!ship || ship->type!=EntityType::Ship || !ship->owner_id ||
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
            if(!result.colony_ships_.count(n.home_ship) ||
                (n.role==NpcRole::Ship && n.location!=n.home_ship) ||
                (n.role==NpcRole::Farm && (!result.farms_.count(n.location) ||
                  result.farms_.at(n.location).source_ship!=n.home_ship)) ||
                (n.role==NpcRole::Habitat && (!result.habitats_.count(n.location) ||
                  result.habitats_.at(n.location).source_ship!=n.home_ship)))
                throw std::runtime_error("snapshot: NPC outside assigned colony");
            counts[{n.location,static_cast<int>(n.role)}]++;
            if(n.mentor_id){
                const auto mentor=result.npcs_.find(n.mentor_id);
                if(mentor==result.npcs_.end() || mentor->second.home_ship!=n.home_ship ||
                   mentor->second.location!=n.location || n.role!=NpcRole::Farm ||
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
    }
    for(const auto& [ship_id,c]:result.colony_ships_){
        std::int64_t crew=c.crew;
        for(const auto& [id,f]:result.farms_){(void)id;
            if(f.source_ship==ship_id)crew+=f.workers;
        }
        for(const auto& [id,h]:result.habitats_){(void)id;
            if(h.source_ship==ship_id)crew+=h.residents;
        }
        if(crew<1 || crew>1000)throw std::runtime_error("snapshot: invalid total crew");
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
    depleted_.swap(other.depleted_);
    mining_.swap(other.mining_);
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
        const auto mass=c.refined+2*c.modules;
        if(mass<0 || mass>std::numeric_limits<std::int64_t>::max()-total)
            throw std::overflow_error("invalid colony ore equivalent");
        total+=mass;
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
