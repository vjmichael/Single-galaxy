#pragma once

#include "galaxy/Inventory.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace galaxy {

using EntityId = std::uint64_t;
using PlayerId = std::uint64_t;

// Units are integer millimeters and integer millimeters/tick.
// A tick is a logical simulation step; real-time scheduling is adapter-owned.
struct Vector2 {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

enum class EntityType { Ship, Asteroid, Station, Stargate, Wreck };
enum class Action { Thrust, Mine, StartMining, StopMining, RequestUndock, RequestDock, UnloadOre, LoadOre, SellOre, RequestJump, SetGatePairOnline, SetGateStaff, DeliverGateSpares, RefineOre, FabricateModule, ConstructOutpost, RefineMineral, FabricateMaterialModule, PlantCrops, HarvestCrops, RecycleWater, RecycleCompost, ProduceOxygen, LoadLifeWater, ConstructFarm, TransferFarmStock, AssignFarmWorkers, ConstructHabitat, TransferHabitatStock, AssignHabitatResidents, ConstructUtility, RepairUtility, TransferUtilitySpares, AssignUtilityTechnicians, SetUtilityMaintenance, ConstructCommunity, TransferCommunityStock, AssignCommunityStaff, ConstructClinic, TransferClinicStock, AssignClinicStaff, ConstructReclaimer, ConstructWaterworks, TransferWaterworksFeed, SetPolicyAssurance, AssignPolicyInspectors, AssignFarmMentor, HostDestroyShip, HostCrewLoss, ConstructWorkshop, TransferWorkshopFeed, AssignWorkshopOperator, TransferWorkshopOutput, ConstructWorkshopFinisher, AssignWorkshopFinisher, TransferWorkshopFinalOutput, TransferWorkshopStack, ConstructStorage, TransferStorageStack };
enum class Outcome { Applied, InvalidActor, Unauthorized, InvalidTarget, OutOfRange, InvalidArgument, NoResources, CargoFull, NotMining, NotInSpace, NotDocked, InTransition, ShipMoving, HangarFull, InsufficientFunds, WrongSystem, GateOffline, NotColonyShip, InsufficientCrew };
enum class ActivityKind { AsteroidDespawned, AutoMiningTransfer, MiningStopped, Undocked, Docked, JumpCompleted, ShipDestroyed, NpcLost };
enum class DockState { InSpace, Docked, Undocking, Docking };
enum class ShipJumpState { Ready, Jumping, ArrivalCloak };
struct StationHangar { std::int64_t ore = 0; std::int64_t capacity = 0; };
struct StationMarket { std::int64_t ore = 0; std::int64_t credits = 0; std::int64_t price_per_ore = 0; };
// B6.6: one durable record for both directional endpoints of a physical gate link.
// Counts are intentionally not individual NPCs yet; labor recruitment and logistics
// must eventually feed these amounts through ordinary authorized game actions.
struct GateOperations {
    std::int64_t condition = 1000; // 0 broken, 1000 nominal
    std::int64_t spare_parts = 0; // input inventory allocated to maintenance
    std::int64_t technicians = 0; // assigned, equivalent trained workers
    std::uint64_t cycles = 0;
    std::uint64_t consumed_parts = 0;
};

// B7.0a: onboard bootstrap inventory; counts are physical ore-equivalent mass.
// In this initial vertical slice NPCs are a finite headcount, not individuals yet.
struct ColonyShip {
    std::int64_t crew = 0;
    std::int64_t refined = 0; // 1 raw ore -> 1 unit refined material
    std::int64_t modules = 0; // 2 refined material -> 1 construction module
    // B7.0c: individually tracked phase concentrates and usable refined goods.
    // Integer kilograms allow mineral fractions of a mined metric tonne.
    std::map<std::string,std::int64_t> minerals_kg;
    std::map<std::string,std::int64_t> goods_kg;
    std::int64_t tailings_kg = 0; // process residue stays in the physical hold
};
// B7.0d: modest onboard hydroponics and life-support stockrooms. All compartments
// are kilograms and their sum is conserved through every recipe and crew cycle.
// These are aggregates; individual NPC needs are a later milestone.
struct ColonyLifeSupport {
    std::int64_t food_kg=0, water_kg=0, oxygen_kg=0;
    std::int64_t nutrients_kg=0, co2_kg=0, crops_kg=0;
    std::int64_t compost_kg=0, wastewater_kg=0, organic_waste_kg=0;
    std::int64_t sludge_kg=0, hydrogen_kg=0;
    std::uint64_t crops_ready_tick=0;
    std::uint64_t cycles=0, consecutive_supplied_cycles=0, shortages=0;
    std::int64_t harvested_food_kg=0, recovered_water_kg=0;
};
// B7.0e: physically stationed NPC labor and a player-built agricultural facility.
// All food/water/gas is stored AT the outpost. Nothing teleports to the ship.
struct FarmFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4; // two modules -> four tonnes in the structure
    std::int64_t workers=0; // transferred from ship, never created
    ColonyLifeSupport life{};
    std::uint64_t production_cycles=0;
    std::int64_t pending_food_kg=0; // produced in the current 20-tick window
    std::array<std::int64_t,3> recent_food_kg{{0,0,0}};
};
// B7.0f: residential compartments at a specific player-built outpost.
// A habitat can house local farm staff plus additional residents transferred
// from the colony ship. Farm staff consume their existing local farm provisions;
// residents consume only this habitat's separate physical stocks.
struct HabitatFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4;  // two 2-tonne structural modules
    std::int64_t capacity=8;
    std::int64_t residents=0; // non-farming colonists, moved from ship
    ColonyLifeSupport life{};
    std::uint64_t cycles=0, consecutive_comfort_cycles=0;
};
// B7.0g: solar service facility; energy is finite stored kWh-equivalent game units.
// The initial solar input is bounded by installed collector capacity; no created
// mineral mass. Repairs embed real manufactured modules into the equipment.
struct UtilityFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4; // two structural modules; +2 per repair
    std::int64_t condition=100;  // 0 failed; players must physically repair
    std::int64_t stored_energy=0; // 0..120 game power units
    bool powered=false;         // last tick actually met local demand
    std::uint64_t cycles=0, consecutive_powered_cycles=0, outages=0;
    std::uint64_t repairs=0;
    std::int64_t generated_energy=0, consumed_energy=0, curtailed_energy=0;
    // B7.0h: spares are real player-manufactured modules delivered at close range.
    // Technicians are assignments from pre-existing housed residents, not spawned people.
    std::int64_t spare_modules=0, technicians=0;
    bool delegated_maintenance=false; // explicit player-owned standing policy
    std::uint64_t delegated_repairs=0, blocked_maintenance_cycles=0;
};
// B7.0j: one physically built community hall per outpost. Its meals and
// drinking water are OPTIONAL additional social provision, not baseline life
// support. Actual consumption becomes local organic waste/wastewater.
// The staffed hall offers restorative leisure and social time; it is NOT a
// clinical healthcare system or proof of individual happiness.
struct CommunityFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4; // two manufactured structural modules
    std::int64_t food_kg=0,water_kg=0,organic_waste_kg=0,wastewater_kg=0;
    std::int64_t attendants=0; // assigned FROM existing habitat residents
    std::uint64_t cycles=0,consecutive_service_cycles=0,shortages=0,served_visits=0;
};
// B7.0k: basic, physical first-aid facility. Each treatment consumes one kg
// purified onboard-delivered water and one kg local nutrient/medical substrate.
// The 2 kg remain in medical wastewater/waste; no free cure or medicine creation.
// Aggregate clinical episodes are diagnosed from a real local needs shortage,
// bounded by the site's existing people. Individual diagnoses remain future work.
struct ClinicFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4;
    std::int64_t water_kg=0,substrate_kg=0,wastewater_kg=0,medical_waste_kg=0;
    std::int64_t medics=0,active_cases=0;
    std::uint64_t cycles=0,consecutive_care_cycles=0,shortages=0,treated_cases=0,recorded_cases=0;
};
// B7.0k-2: a physically constructed local water/oxygen reclamation unit.
// Processing at most 20kg of each feedstream per 20-tick needs period.
// Wastewater: 10kg -> 9kg water + 1kg sludge.
// CO2 stream: 10kg -> 7kg oxygen + 3kg carbon-rich residue.
// These are GAME mass-flow recipes, not laboratory chemical kinetics.
// No new water/oxygen matter is created, and losses block perpetual-autonomy claims.
struct ReclaimerFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4;
    std::int64_t sludge_kg=0, carbon_residue_kg=0;
    std::int64_t water_recovered_total_kg=0, oxygen_generated_total_kg=0;
    std::uint64_t cycles=0, consecutive_powered_cycles=0;
    std::array<std::int64_t,3> water_recent{{0,0,0}}, oxygen_recent{{0,0,0}};
    std::array<std::int64_t,3> water_demand_recent{{0,0,0}}, oxygen_demand_recent{{0,0,0}};
};
// B7.0l: water-bearing geological mineral concentrate is separated into raw
// water goods. Only an authorized nearby ship can deliver that raw feed to a
// constructed, powered local waterworks. 10kg input -> 9kg potable + 1kg brine.
// This does not make the mineral deposit renewable or create site-wide piping.
struct WaterworksFacility {
    EntityId source_ship=0;
    std::int64_t embodied_ore=4;
    std::int64_t raw_feed_kg=0, brine_kg=0;
    std::int64_t purified_total_kg=0;
    std::uint64_t cycles=0, consecutive_service_cycles=0;
    std::array<std::int64_t,3> recent_output{{0,0,0}};
    std::array<std::int64_t,3> recent_demand{{0,0,0}};
};
// B7.0n-A/B: bounded, persistent individuals. These are identity/skill records,
// not 24 expensive autonomous decision processes. Existing headcount and mass
// accounting remain authoritative until the migration is completed.
enum class NpcRole : int { Ship=0, Farm=1, Habitat=2 };
struct NpcRecord {
    std::uint64_t id=0;
    EntityId home_ship=0, location=0;
    NpcRole role=NpcRole::Ship;
    std::uint64_t farm_xp=0, mentor_id=0;
};
struct FarmWorkforceAssessment {
    std::uint64_t cooperative_cycles=0;
    std::uint64_t experience_total=0;
    std::int64_t workers=0;
    std::int64_t coordination=0; // 0..100, learned through work, not a manager stat
};
// B7.0m: standing, site-local governance. An assurance mandate is distinct
// from real operational performance and from actual compliance enforcement.
// The assigned inspectors are EXISTING housed residents, reserved from other jobs.
// A policy does not create staff, resources, or verified outcomes.
enum class PolicyDomain : int { Maintenance=0, Healthcare=1, Community=2, WaterSupply=3 };
enum class AssuranceLevel : int { None=0, Lax=1, Medium=2, Strict=3 };
struct PolicyAssurance {
    AssuranceLevel declared=AssuranceLevel::None;
    std::int64_t inspectors=0;
};
struct PolicySite {
    EntityId source_ship=0;
    std::array<PolicyAssurance,4> domains{};
};
struct PolicyAudit {
    EntityId site=0;
    PolicyDomain domain=PolicyDomain::Maintenance;
    AssuranceLevel declared=AssuranceLevel::None;
    std::int64_t assigned_inspectors=0;
    std::int64_t required_inspectors=0;
    bool operational_evidence=false;
    bool oversight_staffed=false;
    bool assurance_supported=false;
};
const char* policy_domain_name(PolicyDomain);
struct ColonyWaterSupply {
    std::int64_t occupied_sites=0, equipped_sites=0, measured_sites=0;
    std::int64_t potable_output_last3_kg=0, water_demand_last3_kg=0;
    bool short_window_coverage=false;
    bool indefinitely_renewable=false;
};
struct ColonyLifeRecovery {
    std::int64_t occupied_sites=0, equipped_sites=0, measured_sites=0;
    std::int64_t water_recovered_last3_kg=0, oxygen_generated_last3_kg=0;
    std::int64_t water_demand_last3_kg=0, oxygen_demand_last3_kg=0;
    std::int64_t sludge_kg=0, carbon_residue_kg=0;
    bool local_recycling_coverage=false;  // throughput, NOT verified self-sufficiency
    bool independent_renewal_proven=false; // no external replenishment modeled yet
};
struct ColonyViability {
    enum class Stage { Survival, Stable, Livable, SelfSufficient, Prosperous };
    Stage stage=Stage::Survival;
    std::int64_t reserve_cycles=0;
    bool recent_shortage=false;
    bool renewable_food_proven=false;
    bool renewable_water_proven=false;
    bool sufficient_quality_of_life=false;
    bool autonomous_growth_ready=false;
    std::int64_t crew_total=0;
    std::int64_t farm_workers=0;
    std::int64_t recent_food_production_kg=0;
    std::int64_t recent_food_demand_kg=0;
    bool sustained_food_surplus=false;
    bool has_operational_farm=false;
    std::int64_t housed_colonists=0, housing_capacity=0, homeless_outpost_workers=0;
    std::int64_t habitat_residents=0;
    bool housing_provisioned=false;
    bool utilities_sustained=false; // 3 consecutive powered periods at serviced occupied sites
    std::int64_t powered_outposts=0, unpowered_outposts=0;
    std::int64_t maintenance_spare_modules=0, assigned_technicians=0;
    std::int64_t maintenance_blocked_sites=0;
    bool delegated_maintenance_ready=false; // diagnostic only, NOT autonomy certification
};
// B7.0i conservative, entirely read-only diagnostic. Unlike the historical
// "Livable" proxy, readiness requires each occupied site's physical stock,
// repeatable food throughput and staffed, supplied, non-overcommitted utility
// service. Unsupported institutions/renewal cannot certify autonomous growth.
// These stable bit positions are part of the local telemetry contract.
enum ColonyResilienceBlock : std::uint32_t {
    NoOccupiedSettlement   = 1u << 0,
    CrewReservesLow        = 1u << 1,
    SiteReservesLow        = 1u << 2,
    RecentShortage         = 1u << 3,
    UnreliablePower        = 1u << 4,
    MaintenanceUnsupported = 1u << 5,
    StaffOvercommitted     = 1u << 6,
    FoodBufferUnproven     = 1u << 7,
    WaterRenewalUnverified = 1u << 8,
    HealthRestUnmodeled    = 1u << 9,
    GrowthPolicyUnmodeled  = 1u << 10,
    CommunityServicesMissing = 1u << 11
};
struct ColonyResilience {
    std::int64_t onboard_reserve_cycles=0; // 20-tick needs periods at present stock
    std::int64_t lowest_site_reserve_cycles=0; // minimum of occupied local stores
    std::int64_t occupied_sites=0, sites_with_reserves=0;
    std::int64_t serviced_sites=0, maintained_sites=0;
    std::int64_t spare_modules=0, nonmaintenance_residents=0;
    std::int64_t community_supported_sites=0;
    bool restorative_leisure_proven=false;
    std::int64_t clinic_supported_sites=0, active_clinical_cases=0, treated_clinical_cases=0;
    bool basic_clinic_access_proven=false; // diagnostic only, not full healthcare
    bool food_buffer_proven=false;
    bool operational_delegation_candidate=false; // NO offline catch-up or NPC governance
    bool autonomous_growth_ready=false;          // explicitly unsupported
    std::uint32_t blocker_mask=NoOccupiedSettlement | WaterRenewalUnverified |
        HealthRestUnmodeled | GrowthPolicyUnmodeled | CommunityServicesMissing;
};
const char* viability_stage_name(ColonyViability::Stage stage);

// B7.0b: initial bulk geological deposit profiles. Class labels are based on
// real asteroid mineral families, but exact fractions are GAME HYPOTHESES.
// One bulk ore unit is one metric tonne for these geology-tagged objects.
enum class AsteroidClass { Carbonaceous=0, Silicate=1, Metallic=2 };
struct AsteroidGeology {
    AsteroidClass taxonomy = AsteroidClass::Carbonaceous;
    std::int64_t radius_m = 0;
    std::int64_t density_kg_m3 = 0;
    std::int64_t original_tonnes = 0;
};
// Fixed illustrative per-thousand mass shares; derived deterministically from
// geological class, not claimed as lab measurements of a specific asteroid.
struct MineralFraction { const char* name; int permille; };
std::vector<MineralFraction> mineral_profile(AsteroidClass taxonomy);
// Stable command codes: target=phase ID on RefineMineral, target=1 for
// structural-alloy or target=2 for aluminum on FabricateMaterialModule.
const char* mineral_name(std::uint64_t code);
bool is_known_mineral(const std::string& name);
bool is_known_good(const std::string& name);

std::int64_t asteroid_mass_tonnes(std::int64_t radius_m, std::int64_t density_kg_m3);

// ECON-02B1a: one installed Tier 1 processing machine at a player-built
// outpost. Inventories are real site-local kilograms, not cloned ship stock.
// 20kg aluminum -> 19kg panels + 1kg recoverable scrap in five working ticks.
struct OutpostWorkshop {
    EntityId source_ship=0;
    std::int64_t embodied_ore=2; // consumes one real construction module
    std::int64_t aluminum_kg=0, panels_kg=0, scrap_kg=0;
    std::int64_t reserved_aluminum_kg=0; // WIP, always exactly 20kg or 0
    std::uint32_t ticks_remaining=0;
    std::uint64_t completed_batches=0, elapsed_ticks=0;
    std::uint64_t operator_npc_id=0; // real local habitat resident
    // ECON-02B1b: second independently staffed processor consumes 20kg of
    // site-local panel blanks -> 19kg shaped frames + 1kg physical scrap.
    // Existing panel stock is the bounded (1000kg) intermediate buffer.
    bool finisher_installed=false;
    std::uint64_t finisher_npc_id=0;
    std::int64_t reserved_panels_kg=0; // 20kg when finisher has WIP
    std::uint32_t finisher_ticks_remaining=0;
    std::int64_t frames_kg=0;         // finite 1000kg output stock
    std::uint64_t finisher_batches=0, finisher_elapsed_ticks=0;
};
// ECON-02B2b: player-built, site-bound 4 m3 mixed-stack container.
// For this first real-world gate only one can be built at each outpost.
struct OutpostStorage {
    EntityId source_ship=0;
    std::int64_t embodied_ore=2; // one real module, no free storage
    std::map<std::string,std::int64_t> goods_kg;
};
struct BuiltOutpost {
    EntityId source_colony_ship = 0;
    std::int64_t embodied_ore = 0; // four modules, eight ore-equivalent units
};

struct Entity {
    EntityId id = 0;
    std::uint32_t system_id = 1;
    EntityType type = EntityType::Ship;
    PlayerId owner_id = 0; // 0 denotes environment-owned object
    Vector2 position{};
    Vector2 velocity{};
    std::int64_t ore = 0;
    std::int64_t cargo = 0;
    std::int64_t cargo_capacity = 0; // m3; fixed gameplay item volumes, NO mass limit
    DockState dock_state = DockState::InSpace;
    EntityId dock_station = 0;
    std::uint32_t transition_ticks = 0;
    // v5: A single durable world may contain multiple star systems; only the
    // ship changes partition when an authorized jump finishes.
    ShipJumpState jump_state = ShipJumpState::Ready;
    EntityId pending_gate = 0;
    std::uint32_t jump_ticks = 0;
    // Stargate-only fields (zero for ordinary entities).
    std::uint32_t gate_destination = 0;
    Vector2 gate_arrival{};
    bool gate_online = false;
};

struct Command {
    std::uint64_t id = 0;          // globally unique, for idempotency
    std::uint64_t execute_tick = 0;
    std::uint64_t sequence = 0;    // determines order within tick
    PlayerId issuer = 0;
    EntityId actor = 0;
    EntityId target = 0;
    Action action = Action::Thrust;
    int thrust_x = 0;              // each axis -1, 0, +1
    int thrust_y = 0;
    std::int64_t amount = 0;       // ore units for Mine
};

struct ActivityEvent {
    std::uint64_t tick = 0;
    ActivityKind kind = ActivityKind::AsteroidDespawned;
    std::uint64_t start_command_id = 0; // 0 for one-off mining
    EntityId actor = 0;
    EntityId target = 0;
    Outcome result = Outcome::Applied;
    std::int64_t transferred = 0;
};

struct AutoMining {
    EntityId target = 0;
    std::int64_t amount_per_tick = 0;
    std::uint64_t start_command_id = 0;
};

struct Event {
    std::uint64_t tick = 0;
    std::uint64_t command_id = 0;
    Outcome result = Outcome::InvalidArgument;
    std::int64_t transferred = 0;
};

class Simulation {
public:
    // Reserved server-side journal identity, not a player account.
    static constexpr PlayerId kServerAuthority = std::numeric_limits<PlayerId>::max();
    static constexpr std::int64_t kWorldBoundMm = 1000000000LL;
    static constexpr std::int64_t kMaxVelocityMmPerTick = 20000;
    static constexpr std::int64_t kAccelerationMmPerTick2 = 250;
    static constexpr std::int64_t kMiningRangeMm = 5000;
    static constexpr std::int64_t kDockingRangeMm = 2500;
    static constexpr std::int64_t kUndockOffsetMm = 1500;
    static constexpr std::uint32_t kDockTransitionTicks = 2;
    static constexpr std::int64_t kDefaultHangarCapacity = 1000;
    static constexpr std::int64_t kJumpActivationMm = 2500;
    static constexpr std::uint32_t kJumpTransitTicks = 2;
    static constexpr std::uint32_t kArrivalCloakTicks = 5;

    explicit Simulation(std::uint64_t seed, std::uint32_t system_id = 1);
    EntityId add_ship(PlayerId owner, Vector2 position, std::int64_t cargo_capacity);
    // Genesis-only colony start: one real ship with a finite founding crew.
    EntityId add_colony_ship(PlayerId owner, Vector2 position,
                             std::int64_t cargo_capacity, std::int64_t founding_crew);
    // Host-controlled, explicit migration of aggregate crew to persistent IDs.
    // Existing live-world upgrades require a separate journaled migration gate;
    // this entry point runs at genesis only to preserve authoritative recovery.
    // No NPC is added: counts at all sites must match the existing ledger.
    void enable_individual_workforce();
    const std::map<std::uint64_t,NpcRecord>& npc_records() const {return npcs_;}
    FarmWorkforceAssessment assess_farm_workforce(EntityId site) const;
    const ColonyShip* colony_ship(EntityId ship) const;
    // Authoritative cargo capacity uses game volume, NOT payload mass.
    // Milliscale: 1 m3 = 1000 litres; legacy ore units occupy 1 m3 each.
    std::int64_t ship_cargo_volume_liters(EntityId ship) const;
    // Read-only copies of authoritative per-item inventories for cargo UI/logistics.
    inventory::StackStore ship_inventory_snapshot(EntityId ship) const;
    inventory::StackStore workshop_bin_inventory_snapshot(EntityId site, int item) const;
    inventory::StackStore storage_inventory_snapshot(EntityId site) const;
    const OutpostStorage* storage(EntityId site) const;

    // Initial-world only: finite packaged food, water, oxygen and nutrient stocks.
    // No free resource regeneration; this explicitly adds starter mass at genesis.
    void provision_colony_genesis(EntityId ship, std::int64_t food_kg,
                                  std::int64_t water_kg, std::int64_t oxygen_kg,
                                  std::int64_t nutrient_kg, std::int64_t co2_kg);
    const ColonyLifeSupport* life_support(EntityId ship) const;
    ColonyViability assess_colony(EntityId ship) const;
    ColonyResilience assess_resilience(EntityId ship) const;
    ColonyLifeRecovery assess_life_recovery(EntityId ship) const;
    ColonyWaterSupply assess_water_supply(EntityId ship) const;
    std::vector<PolicyAudit> assess_policies(EntityId ship) const;
    std::int64_t assigned_policy_inspectors(EntityId site) const;
    const std::map<EntityId,PolicySite>& policy_sites() const {return policy_sites_;}
    const std::map<EntityId,ColonyLifeSupport>& life_support_records() const {return life_support_;}
    const FarmFacility* farm(EntityId outpost) const;
    const std::map<EntityId,FarmFacility>& farms() const {return farms_;}
    const HabitatFacility* habitat(EntityId outpost) const;
    const std::map<EntityId,HabitatFacility>& habitats() const {return habitats_;}
    const UtilityFacility* utility(EntityId site) const;
    const std::map<EntityId,UtilityFacility>& utilities() const {return utilities_;}
    const CommunityFacility* community(EntityId site) const;
    const std::map<EntityId,CommunityFacility>& communities() const {return communities_;}
    const ClinicFacility* clinic(EntityId site) const;
    const ReclaimerFacility* reclaimer(EntityId site) const;
    const std::map<EntityId,ReclaimerFacility>& reclaimers() const {return reclaimers_;}
    const WaterworksFacility* waterworks(EntityId site) const;
    const std::map<EntityId,WaterworksFacility>& waterworks_sites() const {return waterworks_;}
    const std::map<EntityId,ClinicFacility>& clinics() const {return clinics_;}
    const std::map<EntityId, ColonyShip>& colony_ships() const {return colony_ships_;}
    const std::map<EntityId, BuiltOutpost>& built_outposts() const {return built_outposts_;}
    const OutpostWorkshop* workshop(EntityId site) const;
    const std::map<EntityId,OutpostWorkshop>& workshops() const {return workshops_;}
    EntityId add_asteroid(Vector2 position, std::int64_t ore);
    // Genesis-only, massive realistic-scale asteroid; respects world conservation.
    EntityId add_geological_asteroid(Vector2 position, AsteroidClass taxonomy,
                                     std::int64_t radius_m, std::int64_t density_kg_m3);
    const AsteroidGeology* geology(EntityId asteroid) const;
    const std::map<EntityId,AsteroidGeology>& geological_asteroids() const {return geology_;}
    EntityId add_station(Vector2 position);
    // v5 initial-world only: register geographically distinct partitions.
    void add_system(std::uint32_t system);
    EntityId add_station_in_system(std::uint32_t system, Vector2 position);
    EntityId add_asteroid_in_system(std::uint32_t system, Vector2 position, std::int64_t ore);
    EntityId add_gate(std::uint32_t origin, Vector2 position,
                      std::uint32_t destination, Vector2 arrival);
    // B6.5/B6.6 physical gate state and operating condition are authoritative
    // and persisted with the same checkpoint/operation-journal boundary.
    void configure_gate_operations(EntityId first, EntityId second,
                                   std::int64_t condition, std::int64_t spare_parts,
                                   std::int64_t technicians);
    const GateOperations* gate_operations(EntityId first, EntityId second) const;
    const std::map<std::pair<EntityId,EntityId>,GateOperations>& all_gate_operations() const noexcept {return gate_ops_;}
    bool protected_from_attack(EntityId ship) const;
    const std::set<std::uint32_t>& systems() const noexcept { return systems_; }
    // Explicit game-start factory. Owner receives a bay ONLY at this specific station.
    // World setup must finish before a PersistentWorld is created.
    EntityId spawn_ship_docked(PlayerId owner, EntityId station_id,
                               std::int64_t ship_capacity, std::int64_t hangar_capacity);
    // Initial-world only; station purchases ore at a fixed bid from finite reserves.
    void configure_market(EntityId station_id, std::int64_t bid, std::int64_t reserve);
    const StationHangar* hangar(EntityId station_id, PlayerId owner) const;
    const StationMarket* market(EntityId station_id) const;
    std::int64_t wallet(PlayerId owner) const;
    const std::map<EntityId, StationMarket>& markets() const { return markets_; }
    const std::map<PlayerId, std::int64_t>& wallets() const { return wallets_; }
    const std::map<std::pair<EntityId, PlayerId>, StationHangar>& hangars() const { return hangars_; }
    std::vector<EntityId> generate_asteroids(std::size_t count, std::int64_t max_coordinate_mm = 100000);

    // Rejects duplicate or past-tick commands; does not execute immediately.
    bool submit(const Command& command);
    void step();
    void steps(std::uint64_t count);

    std::uint64_t tick() const { return tick_; }
    std::uint32_t system_id() const { return system_id_; }
    const std::map<EntityId, Entity>& entities() const { return entities_; }
    const std::vector<Event>& events() const { return events_; }
    const std::vector<ActivityEvent>& activity_events() const { return activity_events_; }
    const std::map<EntityId, AutoMining>& active_mining() const { return mining_; }
    bool was_depleted(EntityId id) const { return depleted_.count(id) != 0; }
    const Entity* find(EntityId id) const;
    // Server-side partition query. Callers must not broadcast `entities()` to
    // a player: B6.2c will build per-system, per-player SGVIEW projections.
    std::vector<EntityId> entity_ids_in_system(std::uint32_t system) const;
    std::string canonical_state() const;
    std::uint64_t state_hash() const;
    // B2: strictly validated, versioned restore from canonical numeric snapshot.
    static Simulation from_snapshot(const std::string& encoded);
    void swap(Simulation& other) noexcept;
    std::int64_t total_ore() const;
    std::int64_t total_material_kg() const; // exact metric mass, including tailings
    std::int64_t total_credits() const;
    bool has_command_id(std::uint64_t id) const { return submitted_ids_.count(id) != 0; }
    std::size_t accepted_command_count() const { return submitted_ids_.size(); }


private:
    friend class PersistentWorld;
    std::uint64_t random_u64();
    EntityId add_entity(Entity entity);
    Event execute(const Command& command);
    bool valid_position(Vector2 position) const;
    Outcome mine_status(const Entity& actor, EntityId target) const;
    std::int64_t loaded_cargo_liters(const Entity& ship) const;
    void transfer_geological_minerals(EntityId ship, EntityId rock, std::int64_t tonnes);
    void despawn(EntityId asteroid, EntityId actor, std::uint64_t start_command_id);
    void stop_mining(EntityId actor, Outcome reason);
    void process_automatic_mining();
    void process_station_transitions();
    void process_jump_transitions();
    void process_gate_maintenance();
    void process_colony_needs();
    void process_farms();
    void move_npc_records(EntityId ship, EntityId site, NpcRole role, std::int64_t delta);
    void award_farm_experience(EntityId site);
    void process_habitats();
    void process_workshops();
    void process_utilities();
    void process_communities();
    void process_clinics();
    void process_reclaimers();
    void process_waterworks();

    std::uint32_t system_id_;
    std::set<std::uint32_t> systems_;
    std::uint64_t tick_ = 0;
    std::uint64_t next_entity_id_ = 1;
    std::uint64_t rng_state_;
    std::map<EntityId, Entity> entities_; // stable deterministic iteration
    std::set<EntityId> depleted_;
    std::map<EntityId, AutoMining> mining_;
    std::map<std::pair<EntityId, PlayerId>, StationHangar> hangars_;
    std::map<EntityId, StationMarket> markets_;
    std::map<PlayerId, std::int64_t> wallets_;
    std::map<std::pair<EntityId,EntityId>,GateOperations> gate_ops_;
    std::map<EntityId,ColonyShip> colony_ships_;
    std::map<EntityId, ColonyLifeSupport> life_support_;
    std::map<EntityId, FarmFacility> farms_;
    std::map<EntityId, HabitatFacility> habitats_;
    std::map<EntityId, UtilityFacility> utilities_;
    std::map<EntityId, CommunityFacility> communities_;
    std::map<EntityId, ClinicFacility> clinics_;
    std::map<EntityId, ReclaimerFacility> reclaimers_;
    std::map<EntityId, WaterworksFacility> waterworks_;
    std::map<EntityId,PolicySite> policy_sites_;
    std::map<std::uint64_t,NpcRecord> npcs_;
    std::uint64_t next_npc_id_=1;
    bool npc_next_seen_=false;
    std::map<EntityId,std::uint64_t> farm_team_xp_;
    std::map<EntityId,BuiltOutpost> built_outposts_;
    std::map<EntityId,OutpostWorkshop> workshops_;
    std::map<EntityId,OutpostStorage> storages_;
    std::map<EntityId, AsteroidGeology> geology_;
    std::vector<ActivityEvent> activity_events_;
    int snapshot_version_ = 1; // v25 constructed storage and item stacks; old saves readable
    std::map<std::uint64_t, std::vector<Command>> commands_;
    std::set<std::uint64_t> submitted_ids_;
    std::vector<Event> events_;
};

std::string to_string(Outcome outcome);
} // namespace galaxy
