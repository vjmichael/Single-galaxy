#pragma once

#include "galaxy/Inventory.hpp"
#include "galaxy/Astronomy.hpp"
#include "galaxy/PlanetaryGeography.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
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

// Persistent astronomical world-space coordinates in real millimeters,
// separate from short-range physics simulation coordinates (±1000 km).
struct CosmicPosition {std::int64_t x_mm=0,y_mm=0,z_mm=0;};
// v71: authoritative local-space frame custody per ship. An anchor of zero
// identifies the preexisting unanchored legacy-local world with origin (0,0,0).
struct ShipFrameCustody {
    std::uint32_t system=0;
    std::uint64_t anchor_body=0;
    CosmicPosition origin{};
};
// v72: host-registered transit request; IDs are single-use and journaled.
// The player chooses a destination, but the host validates ownership and
// computes final placement from authoritative celestial geometry.
struct PendingCelestialTransit {
    PlayerId owner=0;
    EntityId ship=0;
    std::uint32_t system=0;
    std::uint64_t source_anchor=0;
    std::uint64_t destination_body=0;
    std::uint64_t started_tick=0;
    std::uint64_t due_tick=0;
};
struct CompletedCelestialTransit {
    PlayerId owner=0;
    EntityId ship=0;
    std::uint32_t system=0;
    std::uint64_t destination_body=0;
    std::uint64_t completed_tick=0;
};
enum class CelestialKind {Star=0,Planet=1,Moon=2,DebrisRegion=3};
struct PhysicalCelestialBody {
    std::uint64_t id=0,parent_id=0;
    std::uint32_t system=0;
    CelestialKind kind=CelestialKind::Star;
    std::uint64_t orbital_radius_milli_au=0;
    CosmicPosition position{};
    std::uint64_t physical_radius_mm=0;
};

enum class EntityType { Ship, Asteroid, Station, Stargate, Wreck };
enum class Action { Thrust, Mine, StartMining, StopMining, RequestUndock, RequestDock, UnloadOre, LoadOre, SellOre, RequestJump, SetGatePairOnline, SetGateStaff, DeliverGateSpares, RefineOre, FabricateModule, ConstructOutpost, RefineMineral, FabricateMaterialModule, PlantCrops, HarvestCrops, RecycleWater, RecycleCompost, ProduceOxygen, LoadLifeWater, ConstructFarm, TransferFarmStock, AssignFarmWorkers, ConstructHabitat, TransferHabitatStock, AssignHabitatResidents, ConstructUtility, RepairUtility, TransferUtilitySpares, AssignUtilityTechnicians, SetUtilityMaintenance, ConstructCommunity, TransferCommunityStock, AssignCommunityStaff, ConstructClinic, TransferClinicStock, AssignClinicStaff, ConstructReclaimer, ConstructWaterworks, TransferWaterworksFeed, SetPolicyAssurance, AssignPolicyInspectors, AssignFarmMentor, HostDestroyShip, HostCrewLoss, ConstructWorkshop, TransferWorkshopFeed, AssignWorkshopOperator, TransferWorkshopOutput, ConstructWorkshopFinisher, AssignWorkshopFinisher, TransferWorkshopFinalOutput, TransferWorkshopStack, ConstructStorage, TransferStorageStack, ConstructStorageNode, TransferStorageNodeStack, ConstructStorageLink, ConstructWorkshopLink, PlaceFactoryEquipment, PlaceConveyorCell, CompleteConveyorRoute, HostEmbarkPassengers, HostDisembarkPassengers, HostStrandFerry, HostResumeFerry, HostEvacuatePassengers, HostDestroyPassengerFerry, HostResolveMixedFerryLoss, HostTrainMedic, HostTriageRescued, HostTreatInjuries, HostScheduleMedicShift, HostLoadMedicalKit, HostMobileStabilize, HostSettleMigrant, HostSplitEvacuatePassengers, HostEscrowConstructedStock, HostReturnConstructedStock, HostTransferConsignedStock, HostColonyDeparture, HostRegisterDependent, FireTrainingWeapon, SalvageWreck, SurveyLocal, ConfigureExplorationScanner, DeploySurveyProbe, ScanSurveyProbe, RecoverSurveyProbe, LaunchGeologicalDrone, FocusSurveyProbe, CancelSurveyFocus };
enum class Outcome { Applied, InvalidActor, Unauthorized, InvalidTarget, OutOfRange, InvalidArgument, NoResources, CargoFull, NotMining, NotInSpace, NotDocked, InTransition, ShipMoving, HangarFull, InsufficientFunds, WrongSystem, GateOffline, NotColonyShip, InsufficientCrew };
enum class ActivityKind { AsteroidDespawned, AutoMiningTransfer, MiningStopped, Undocked, Docked, JumpCompleted, ShipDestroyed, NpcLost, AutoStorageTransfer };
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
    // v38: audited medical input budgets separate from aggregate clinic episodes.
    std::uint64_t training_units=0,ems_treatment_units=0,ems_treated_cases=0;
    std::uint64_t last_ems_tick=0,ems_cases_this_tick=0;
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
    // Zero for legacy assigned custody; future passenger transfers require a voyage.
    std::uint64_t transport_id=0;
    EntityId assigned_site=0; // v32: permanent assignment, independent of physical location
};
// Distinguish permanent workforce assignment from physical custody.
// This view is read-only until the world-owned transfer journal is implemented.
// v36: immutable tombstone with the full NPC identity/assignment snapshot.
// The original live record is removed only after committing an explicit death.
struct NpcCasualty {
    NpcRecord identity{};
    std::uint64_t death_tick=0, command_id=0;
    EntityId ferry=0;
};
// A host-authorized skill assessment uses real clinic materials. Three units
// establish basic medic qualification; XP is attached to the actual named NPC.
struct MedicTraining { EntityId clinic_site=0; std::uint64_t units=0; };
// Named patient status is independent of fatal tombstones. Never die on restart.
struct MedicalCase {
    std::uint64_t rescue_voyage=0,triage_command=0,triage_tick=0;
    unsigned severity=0; // 1-3; status injured until an explicit treatment receipt
    std::uint64_t treatment_command=0,treatment_tick=0;
    EntityId clinic_site=0;
    std::uint64_t medic_id=0;
    // v39: stabilization is a bounded first-aid step, not definitive treatment.
    std::uint64_t stabilization_command=0, stabilized_tick=0;
    std::uint64_t recovery_due_tick=0, recovered_tick=0;
};
struct MedicShift {
    EntityId clinic_site=0;
    std::uint64_t medic_id=0, start_tick=0, end_tick=0;
};
struct MobileMedicalKit {
    std::int64_t water_kg=0,substrate_kg=0,wastewater_kg=0,medical_waste_kg=0;
};
enum class NpcHealthStatus { Healthy, Injured, Treated, Deceased, Unknown, Stabilized, Recovering, Recovered };
struct ColonyMedicalAudit {
    struct Counts { std::int64_t living=0,deceased=0,injured=0,stabilized=0,recovering=0,fit=0;
                    std::int64_t on_duty=0,unavailable=0,in_transit=0; };
    std::map<EntityId,Counts> by_home_ship;
    bool reconciled=false;
};
// v58: a non-destroyed evacuated ship remains an inert physical asset.
// All rescued personnel receive a single new home and exact custody witness.
// v59: a reserved high-ID ART02 child is a physical person, not a worker.
// Its immutable birth authority lives in ART02; this is physical custody.
struct PhysicalDependent {
    std::uint64_t child_id=0, guardian_id=0, registration_command=0, registered_tick=0;
    EntityId home_ship=0, location=0;
};
struct ColonyDeparture {
    EntityId source=0, receiver=0;
    std::uint64_t command_id=0, tick=0;
    std::vector<std::uint64_t> relocated_npcs;
    // v60: registered children evacuated with this complete adult roster.
    std::vector<std::uint64_t> relocated_dependents;
};
struct CrewRehome {
    EntityId wreck=0,rescue_ship=0;
    std::uint64_t incident_command=0,tick=0;
};
struct NpcCustodyView {
    std::uint64_t npc_id=0;
    EntityId home_ship=0, physical_location=0;
    NpcRole assigned_role=NpcRole::Ship;
    std::uint64_t active_transport_id=0;
    bool passenger=false;
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
// PLAY-13C.5: descriptive environment; hazards are not yet physics damage.
// PLAY-13C.6B: read-only prospect, NOT resource inventories, shelter or safety.
// Deployed *from* an existing ship: consumes two real manufactured modules.
// No extra NPCs are spawned and no free water/nutrient sources are created.
struct OasisExtractor {
    EntityId ship=0;
    std::uint64_t body=0;
    std::int64_t embodied_ore=4; // tonnes of installed modules
    std::uint8_t assigned_workers=2; // actual aboard colony crew, not new residents
    std::uint8_t condition=100;
    std::uint16_t energy_units=0; // bounded harvested local solar energy
    std::uint64_t cycles=0;
};
// PLAY-13D.3: one persistent facility per physical body+surface cell.
// All module tonnage is retained in embodied_ore. All stored resources are kg.
// The ship remains the physical logistics origin; no free settlement population.
struct PlanetarySiteFacility {
    planetary::PlanetaryRegionKey region{};
    PlayerId owner=0;
    EntityId source_ship=0;
    std::int64_t embodied_ore=4;
    bool farm=false, extractor=false;
    std::int64_t water_kg=0, nutrients_kg=0, food_kg=0, wastewater_kg=0;
    std::uint64_t harvest_cycles=0;
};
using PlanetarySiteKey = std::tuple<std::uint64_t,std::uint16_t,std::uint16_t,std::uint32_t>;
struct PhysicalOasisReserve {
    std::int64_t water_kg=0,nutrients_kg=0;
    std::int64_t initial_water_kg=0,initial_nutrients_kg=0;
};
struct CelestialOasisProspect {
    std::uint64_t body=0;
    std::uint8_t water=0,energy=0,nutrients=0,shelter=0;
    bool candidate=false;
};
struct LocalCelestialEnvironment {
    std::uint8_t zone=0; // 1 stellar, 2 debris, 3 rocky, 4 icy, 5 lunar
    std::uint8_t hazard=0; // observational severity 0..3, not applied damage
    AsteroidClass first=AsteroidClass::Carbonaceous;
    AsteroidClass second=AsteroidClass::Silicate;
};
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
// ECON-02B2c3: a module-backed, host-authorized one-way transport route.
// item_index: aluminum=0, panels=1, frames=2, scrap=3.
// ECON-02B2d2: reserved site-local endpoint for real workshop buffers.
// Never use this value for a player-constructed storage node.
constexpr std::uint32_t kWorkshopConveyorNode=std::numeric_limits<std::uint32_t>::max();
struct OutpostStorageLink {
    std::uint64_t id=0;  // command ID of construction, durable and never reused
    std::uint32_t source=0, destination=0;
    std::uint8_t item_index=0;
    std::int64_t units_per_tick=0;
    std::uint64_t units_moved=0;
    // ECON-02B2d3b: a durable path on the site grid. A route requiring
    // installation cannot transfer while its path is incomplete.
    bool route_required=false, route_ready=false;
    std::vector<Vector2> route_cells;
};
struct OutpostStorage {
    EntityId source_ship=0;
    std::int64_t embodied_ore=2; // one real module, no free storage
    std::map<std::string,std::int64_t> goods_kg;
    // v52: world-owned physical mass reserved by a verified production network.
    // Not available to ordinary storage transfers; its detailed contents are
    // conserved in the matching network checkpoint.
    std::int64_t network_escrow_kg=0;
    // ECON-02B2c2: additional real module-built 4m3 bins, keyed by positive site-local ID.
    // Legacy container remains node 0. All bins belong to the same constructed site.
    std::map<std::uint32_t,std::map<std::string,std::int64_t>> extra_bins;
    std::map<std::uint64_t,OutpostStorageLink> links;
    // ECON-02B2d3a: positions belong to installed, material-backed equipment.
    // Storage node 0 is the original bin; UINT32_MAX is the actual workshop.
    // A position alone never creates an inventory or machine.
    std::map<std::uint32_t,Vector2> equipment_positions;
};
struct BuiltOutpost {
    EntityId source_colony_ship = 0;
    std::int64_t embodied_ore = 0; // four modules, eight ore-equivalent units
};

// PLAY-11: physical fitting racks. IDs: 0=empty, 1=exploration survey scanner.
// Fixed introductory hull layout: 2 hardpoints / 2 support / 2 subsystem / 1 rig.
// A separate always-on proximity safety sensor is intrinsic to the hull.
struct ShipFitting {
    std::array<std::uint8_t,2> hardpoints{{0,0}};
    std::array<std::uint8_t,2> support{{0,0}};
    std::array<std::uint8_t,2> subsystems{{0,0}};
    std::array<std::uint8_t,1> rigging{{0}};
    bool probe_launcher() const { return hardpoints[0]==2 || hardpoints[1]==2; }
    bool exploration_scanner() const {
        return hardpoints[0]==1 || hardpoints[1]==1;
    }
};
struct Entity {
    EntityId id = 0;
    std::uint32_t system_id = 1;
    EntityType type = EntityType::Ship;
    PlayerId owner_id = 0; // 0 denotes environment-owned object
    Vector2 position{};
    Vector2 velocity{};
    std::uint16_t heading_deg = 0; // PLAY-04: yaw, clockwise from +X; authoritative and persistent
    std::int64_t hull = 0; // PLAY-08: v62 0 for non-ships, 100 for new player ships
    std::int64_t weapon_charges = 0; // finite starter training-weapon shots
    std::uint64_t last_weapon_tick = 0; // cooldown persists through restart
    bool training_drone = false; // explicit non-crew practice target; never a colony ship
    ShipFitting fitting{};
    std::uint8_t survey_probes_carried=0; // physically onboard probes, 2t-equivalent each
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

// PLAY-10: first observation is a durable owner-scoped fact; the source entity
// may later be depleted. Positions are copied only from real native entities.
struct DeployedSurveyProbe {
    std::uint64_t id=0; // launching command ID, unique and persistent
    EntityId carrier=0;
    PlayerId owner=0;
    std::uint32_t system=0;
    Vector2 position{};
    std::uint64_t launched_tick=0;
    // v66: 0 stationary probe; 1 outbound drill drone; 2 returning;
    // 3 returned to launch position, awaiting physical carrier recovery.
    std::uint8_t drone_phase=0;
    EntityId drone_target=0;
    Vector2 drone_home{};
    std::int64_t sample_ore=0; // retired v66 field; MUST remain zero (data-only survey)
    std::uint8_t taxonomy=0; // 0 unknown; 1..3 recorded mineral class, not physical ore
    // v69: mission target is coarse (5 km quantization), NOT oracle coordinates.
    EntityId focus_site=0;
    Vector2 focus_waypoint{};
};
struct DiscoveryRecord {
    PlayerId owner=0;
    EntityId site=0;
    std::uint32_t system=0;
    Vector2 location{};
    std::uint64_t first_tick=0;
    bool geological=false;
    // v67: non-destructive geological survey classification, 0 unknown;
    // 1 carbonaceous, 2 silicate, 3 metallic. Persists after drone recovery.
    std::uint8_t survey_taxonomy=0;
};
// v68: host-origin observation withheld from every player projection until
// its scheduled report time and a valid carrier/probe link. Persisted in world.
struct PendingProbeDiscovery {
    DiscoveryRecord report{};
    EntityId carrier=0;
    std::uint64_t probe_id=0;
    std::uint64_t ready_tick=0;
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
    std::int64_t amount = 0;       // ore units for Mine; v37 mixed-ferry rescue count
    // Durable transport envelope: original voyage ID and authenticated passenger manifest.
    std::uint64_t transport_id = 0;
    // v37 HostResolveMixedFerryLoss: complete onboard manifest in two sorted
    // segments [survivors (amount entries), fatalities (remaining entries)].
    // The immutable rescue voyage ID is the globally unique command ID.
    std::vector<std::uint64_t> passenger_ids;
    // v48 one source voyage, two disjoint receiving voyages, one applied command.
    EntityId secondary_receiver=0;
    std::uint64_t secondary_transport=0;
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
    static constexpr std::int64_t kTrainingWeaponRangeMm = 12000;
    static constexpr std::int64_t kSalvageRangeMm = 2500;
    static constexpr std::int64_t kLocalSurveyRangeMm = 50000;
    static constexpr std::int64_t kProbeScanRangeMm = 50000;
    static constexpr std::int64_t kProbeRecoveryRangeMm = 2500;
    // Max carrier-to-geological-drone command/telemetry link: 10 km.
    static constexpr std::int64_t kDroneControlRangeMm = 10000000;
    // 0.5 AU long-range probe uplink, reserved for galaxy-scale coordinate work.
    static constexpr std::int64_t kDeepSpaceProbeCommsRangeMm = 74798935350000LL;
    static bool within_deep_probe_link(std::int64_t dx_mm,std::int64_t dy_mm);
    static constexpr std::int64_t kProximitySafetyRangeMm = 5000;
    static constexpr std::int64_t kTrainingWeaponDamage = 15;
    static constexpr std::uint64_t kTrainingWeaponCooldownTicks = 2;
    static constexpr std::int64_t kDockingRangeMm = 2500;
    static constexpr std::int64_t kUndockOffsetMm = 1500;
    static constexpr std::uint32_t kDockTransitionTicks = 2;
    static constexpr std::int64_t kDefaultHangarCapacity = 1000;
    static constexpr std::int64_t kJumpActivationMm = 2500;
    static constexpr std::uint32_t kJumpTransitTicks = 2;
    static constexpr std::uint32_t kArrivalCloakTicks = 5;

    explicit Simulation(std::uint64_t seed, std::uint32_t system_id = 1);
    EntityId add_ship(PlayerId owner, Vector2 position, std::int64_t cargo_capacity);
    // Genesis-only uncrewed training target; does not appear in production worlds by migration.
    EntityId add_training_drone(Vector2 position);
    // Starting vessel receives one real pre-fitted scanner, represented in
    // resource-equivalent conservation. Does not migrate pre-existing saves.
    void provision_starter_exploration_scanner(EntityId ship);
    void provision_starter_probe_kit(EntityId ship);
    // Genesis-only colony start: one real ship with a finite founding crew.
    EntityId add_colony_ship(PlayerId owner, Vector2 position,
                             std::int64_t cargo_capacity, std::int64_t founding_crew);
    // Host-controlled, explicit migration of aggregate crew to persistent IDs.
    // Existing live-world upgrades require a separate journaled migration gate;
    // this entry point runs at genesis only to preserve authoritative recovery.
    // No NPC is added: counts at all sites must match the existing ledger.
    void enable_individual_workforce();
    const std::map<std::uint64_t,NpcRecord>& npc_records() const {return npcs_;}
    const std::map<std::uint64_t,NpcCasualty>& npc_casualties() const {return casualties_;}
    const std::map<std::uint64_t,MedicalCase>& medical_cases() const {return medical_cases_;}
    const std::map<std::uint64_t,MedicTraining>& named_medics() const {return named_medics_;}
    const std::map<std::pair<std::uint64_t,std::uint64_t>,MedicalCase>& medical_history() const {return medical_history_;}
    const std::map<std::uint64_t,MedicShift>& medic_shifts() const {return medic_shifts_;}
    const std::map<EntityId,MobileMedicalKit>& mobile_medical_kits() const {return mobile_medical_kits_;}
    ColonyMedicalAudit assess_medical_population() const;
    // A canonical, read-only job dispatch check. The caller remains responsible
    // for role qualification, schedules and facility-specific requirements.
    bool npc_available_for_duty(std::uint64_t npc_id) const;
    const std::map<std::uint64_t,CrewRehome>& crew_rehomes() const {return crew_rehomes_;}
    NpcHealthStatus npc_health_status(std::uint64_t npc_id) const;
    // Returns an immutable projection, never a second NPC authority.
    struct LegacyVoyageManifest {
        std::uint64_t transport_id=0;
        EntityId ferry=0, origin=0;
        std::vector<std::uint64_t> passenger_ids;
    };
    // Offline migration only: caller must independently verify supplied manifests.
    // Does not rewrite an existing PersistentWorld journal.
    static Simulation migrate_legacy_active_voyages(
        const std::string& v32_snapshot,
        const std::vector<LegacyVoyageManifest>& verified_manifests);
    void upgrade_passenger_snapshot_format();
    void upgrade_assignment_snapshot_format();
    // AUTO-04D.4/v57: genesis-only opt-in for physically accountable onboard
    // passengers. Preexisting worlds keep their historical needs semantics
    // until a separately journaled and verified migration is implemented.
    void enable_passenger_life_support();
    bool passenger_needs_enabled() const {return snapshot_version_>=57;}

    // v40 opt-in, genesis-only rule change. Old v39 farms and workshops retain
    // their original behavior unless an explicit journaled migration is built.
    void enable_medical_workforce_rules();
    NpcCustodyView npc_custody(std::uint64_t npc_id) const;
    std::size_t passengers_aboard(EntityId ship) const;
    // Physical compartment occupancy, including native crew and real travelers.
    // Required by v57; no double-charge for crew who boarded another ship.
    std::size_t people_physically_aboard(EntityId ship) const;
    const std::map<EntityId,ColonyDeparture>& colony_departures() const {return colony_departures_;}
    const std::map<std::uint64_t,PhysicalDependent>& physical_dependents() const {return physical_dependents_;}
    std::size_t children_physically_aboard(EntityId ship) const;
    // 1 active, 2 completed, 3 stranded, 5 rescued, 6 lost; zero unknown.
    unsigned voyage_status(std::uint64_t transport_id) const;
    // Native, read-only physical manifest audit. Does not create another
    // passenger registry or infer arrival from an external travel narrative.
    bool completed_voyage_matches(
        std::uint64_t transport_id,EntityId ferry,EntityId origin,
        EntityId destination,const std::vector<std::uint64_t>& passengers) const;
    // Derived, read-only incident/EMS dispatch report from canonical voyage,
    // casualty and receipt state. Never maintains a shadow NPC authority.
    struct FerryEmergencyReport {
        bool recorded=false;
        std::uint64_t incident_command_id=0, rescue_voyage_id=0;
        EntityId wreck=0, rescue_ship=0;
        std::vector<std::uint64_t> survivors, deceased;
    };
    FerryEmergencyReport ferry_emergency_report(std::uint64_t source_voyage_id) const;

    FarmWorkforceAssessment assess_farm_workforce(EntityId site) const;
    const ColonyShip* colony_ship(EntityId ship) const;
    // Authoritative cargo capacity uses game volume, NOT payload mass.
    // Milliscale: 1 m3 = 1000 litres; legacy ore units occupy 1 m3 each.
    std::int64_t ship_cargo_volume_liters(EntityId ship) const;
    // Read-only copies of authoritative per-item inventories for cargo UI/logistics.
    inventory::StackStore ship_inventory_snapshot(EntityId ship) const;
    inventory::StackStore workshop_bin_inventory_snapshot(EntityId site, int item) const;
    inventory::StackStore storage_inventory_snapshot(EntityId site) const;
    inventory::StackStore storage_node_inventory_snapshot(EntityId site, std::uint32_t node) const;
    const OutpostStorage* storage(EntityId site) const;
    const Vector2* factory_equipment_position(EntityId site,std::uint32_t node) const;
    const OutpostStorageLink* storage_link(EntityId site,std::uint64_t id) const;

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
    // Native host converts 10 gameplay seconds to its configured tick period.
    // The requested period is durable for future queued reports.
    void configure_probe_report_tick_ms(std::uint64_t tick_ms);
    // v70: deterministic physical celestial bodies with stable world IDs.
    // Init never consumes ship resources, and cannot run twice with new geometry.
    void ensure_physical_celestial_systems();
    // Explicit atomic migration, NOT a travel command.
    void ensure_ship_frame_registry();
    const std::map<EntityId,ShipFrameCustody>& ship_frames() const {return ship_frames_;}
    const std::map<std::uint64_t,PendingCelestialTransit>& pending_celestial_transits() const {
        return pending_celestial_transits_;
    }
    const std::map<std::uint64_t,CompletedCelestialTransit>& completed_celestial_transits() const {
        return completed_celestial_transits_;
    }
    // Different local frames never share ordinary spatial interactions.
    bool same_local_frame(EntityId a,EntityId b) const;
    // PLAY-13C.4: finite existing mineral stock may be redistributed into
    // native local deposits; this never creates material from nothing.
    void seed_celestial_local_region(std::uint64_t body_id);
    const std::map<std::uint64_t,std::array<EntityId,2>>& generated_local_regions() const {
        return generated_local_regions_;
    }
    const std::map<EntityId,std::uint64_t>& local_entity_anchors() const {
        return local_entity_anchors_;
    }
    // Deterministic profiles derived from persistent astronomical body IDs;
    // v74 snapshots store and independently cross-validate these records.
    void ensure_local_environment_profiles();
    const std::map<std::uint64_t,LocalCelestialEnvironment>& local_environments() const {
        return local_environments_;
    }
    std::optional<AsteroidClass> local_deposit_taxonomy(EntityId rock) const;
    // Derived solely from the saved physical catalogue and fixed galaxy seed.
    // Rare prospects require all four ecological prerequisites.
    // PLAY-13C.6C: finite explicitly supplied world-genesis material.
    // No oasis generates resources when discovered or visited.
    void provision_oasis_reserve_genesis(std::uint64_t body,
        std::int64_t water_kg,std::int64_t nutrients_kg);
    const std::map<std::uint64_t,PhysicalOasisReserve>& oasis_reserves() const {
        return oasis_reserves_;
    }
    // A deliberately bounded manual collection action; uninstalled automatic
    // production, planetary mining rigs and farming remain future gates.
    bool collect_oasis_resource(PlayerId owner,EntityId ship,
        std::uint64_t body,bool water,std::int64_t kilograms);
    bool install_oasis_extractor(PlayerId owner,EntityId ship,std::uint64_t body);
    const std::map<EntityId,OasisExtractor>& oasis_extractors() const {
        return oasis_extractors_;
    }
    std::optional<CelestialOasisProspect> celestial_oasis_prospect(
        std::uint64_t body_id) const;
    std::vector<CelestialOasisProspect> survey_oasis_prospects(
        PlayerId authenticated_owner,EntityId ship) const;
    // PLAY-13C.6A: observation only. No production, travel, or free supplies.
    struct ExpeditionProspect {
        std::uint64_t body=0;
        std::uint64_t ore_tonnes=0;
        std::uint64_t nickel_iron_kg=0;
        std::uint64_t plagioclase_kg=0;
        std::uint64_t food_periods=0,water_periods=0,oxygen_periods=0;
        std::uint8_t hazard=0;
        bool local_supply_site=false;
    };
    std::optional<ExpeditionProspect> assess_expedition(
        PlayerId authenticated_owner,EntityId ship) const;
    std::uint64_t probe_report_delay_ticks() const {return probe_report_delay_ticks_;}

    std::uint64_t tick() const { return tick_; }
    std::uint32_t system_id() const { return system_id_; }
    const std::map<EntityId, Entity>& entities() const { return entities_; }
    const std::vector<Event>& events() const { return events_; }
    const std::vector<ActivityEvent>& activity_events() const { return activity_events_; }
    const std::map<EntityId, AutoMining>& active_mining() const { return mining_; }
    const std::map<std::pair<PlayerId,EntityId>,DiscoveryRecord>& discoveries() const { return discoveries_; }
    std::size_t pending_probe_report_count() const {return pending_probe_discoveries_.size();}
    // Coarse, scan-range-limited contacts for the authenticated player only.
    struct SignatureHint { EntityId id=0;Vector2 coarse{};std::int64_t uncertainty_mm=5000000; };
    std::vector<SignatureHint> coarse_probe_signatures(PlayerId owner,EntityId ship,
        std::int64_t range_mm=500000000) const;
    // Read-only survey of deterministic system metadata; no fabricated physical bodies.
    std::optional<AstronomicalProfile> scan_celestial_profile(PlayerId owner,EntityId ship) const;
    std::vector<PhysicalCelestialBody> scan_physical_celestials(PlayerId owner,EntityId ship) const;
    // PGF-01B.2: authenticated, read-only planetary geography survey. The
    // returned site and surface projection originate from one watershed map.
    // Body IDs must already exist in the authoritative physical catalogue.
    const std::map<PlanetarySiteKey,PlanetarySiteFacility>& planetary_sites() const {
        return planetary_sites_;
    }
    bool build_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude);
    // Install only with more real manufactured modules and appropriate ground.
    bool equip_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude,bool farm);
    // Ship and site remain in identical anchored celestial frame.
    bool supply_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude,bool water,std::int64_t kilograms);
    bool cultivate_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude,std::int64_t batches);
    // Finite, explicitly genesis-funded oasis matter moves to a SITE stock.
    bool extract_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude,bool water,std::int64_t kilograms);
    bool retrieve_planetary_site(PlayerId owner,EntityId ship,std::uint64_t body,
        std::uint16_t latitude,std::uint16_t longitude,bool food,std::int64_t kilograms);
    std::optional<planetary::GeographicSite> scan_planetary_geography(
        PlayerId owner, EntityId ship, std::uint64_t body_id,
        std::uint16_t latitude, std::uint16_t longitude) const;
    // A persistent celestial body can be addressed by ID as a future navigation
    // anchor; this does not authorize warp, construction or landing.
    std::optional<PhysicalCelestialBody> celestial_navigation_anchor(
        PlayerId owner,EntityId ship,std::uint64_t body_id) const;
    const std::map<std::uint32_t,std::vector<PhysicalCelestialBody>>& physical_celestial_systems() const {
        return physical_celestial_systems_;
    }
    struct EnvironmentSurvey {
        std::uint32_t system=0;
        bool magnetized_region=false;
        bool dust_region=false;
        std::int64_t ship_speed_mm_per_tick=0;
    };
    std::optional<EnvironmentSurvey> scan_environment(PlayerId owner,EntityId ship) const;
    const std::map<std::uint64_t,DeployedSurveyProbe>& survey_probes() const {return survey_probes_;}
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
    bool can_depart_colony(const Command& command) const;
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
    void process_oasis_extractors();
    void process_farms();
    void move_npc_records(EntityId ship, EntityId site, NpcRole role, std::int64_t delta);
    void award_farm_experience(EntityId site);
    void process_habitats();
    void process_storage_links();
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
    std::map<EntityId,ShipFrameCustody> ship_frames_; // durable v71 local-space custody
    std::map<std::uint64_t,std::array<EntityId,2>> generated_local_regions_; // v73 immutable site manifest
    std::map<EntityId,std::uint64_t> local_entity_anchors_; // v73 nonship frame custody
    std::map<std::uint64_t,LocalCelestialEnvironment> local_environments_; // v74
    std::map<std::uint64_t,PhysicalOasisReserve> oasis_reserves_; // v75 explicit finite sources
    std::map<EntityId,OasisExtractor> oasis_extractors_;
    std::map<PlanetarySiteKey,PlanetarySiteFacility> planetary_sites_; // v76 real installed machinery
    std::map<std::uint64_t,PendingCelestialTransit> pending_celestial_transits_;
    std::map<std::uint64_t,CompletedCelestialTransit> completed_celestial_transits_;
    std::map<std::uint32_t,std::vector<PhysicalCelestialBody>> physical_celestial_systems_;
    std::set<EntityId> depleted_;
    std::map<EntityId, AutoMining> mining_;
    std::map<std::pair<PlayerId,EntityId>,DiscoveryRecord> discoveries_;
    std::map<std::pair<PlayerId,EntityId>,PendingProbeDiscovery> pending_probe_discoveries_;
    std::uint64_t probe_report_delay_ticks_=10;
    std::map<std::uint64_t,DeployedSurveyProbe> survey_probes_;
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
    // Permanently retained voyage receipts: v33/v34 1=active, 2=completed;
    // v35 adds 3=stranded and 5=rescued; v36 adds 6=lost.
    // v37 allows lost ferries with an atomic, linked partial rescue leg.
    // Passenger IDs are canonical sorted; historical voyage IDs never recycle.
    struct VoyageRecord {
        EntityId ferry=0, origin=0;
        std::vector<std::uint64_t> passengers;
        unsigned status=0;
        EntityId destination=0;
        // v35: immutable command ID for last incident; no random wall-clock actions.
        std::uint64_t last_incident_id=0, stranded_tick=0, rescue_transport_id=0;
        std::uint64_t secondary_rescue_transport_id=0;
        unsigned stranded_reason=0;
    };
    std::map<std::uint64_t,VoyageRecord> voyages_;
    std::map<std::uint64_t,NpcRecord> npcs_;
    std::map<std::uint64_t,NpcCasualty> casualties_;
    std::map<std::uint64_t,MedicTraining> named_medics_;
    std::map<std::uint64_t,MedicalCase> medical_cases_;
    // Immutable completed episodes; the existing medical_cases_ holds latest episodes.
    std::map<std::pair<std::uint64_t,std::uint64_t>,MedicalCase> medical_history_;
    std::map<std::uint64_t,MedicShift> medic_shifts_; // keyed by immutable host command
    std::map<EntityId,MobileMedicalKit> mobile_medical_kits_;
    std::map<std::uint64_t,CrewRehome> crew_rehomes_;
    std::map<EntityId,ColonyDeparture> colony_departures_;
    std::map<std::uint64_t,PhysicalDependent> physical_dependents_;
    std::uint64_t next_npc_id_=1;
    bool npc_next_seen_=false;
    std::map<EntityId,std::uint64_t> farm_team_xp_;
    std::map<EntityId,BuiltOutpost> built_outposts_;
    std::map<EntityId,OutpostWorkshop> workshops_;
    std::map<EntityId,OutpostStorage> storages_;
    std::map<EntityId, AsteroidGeology> geology_;
    std::vector<ActivityEvent> activity_events_;
    int snapshot_version_ = 1; // v30 adds durable route cells; v1-v29 remain readable
    std::map<std::uint64_t, std::vector<Command>> commands_;
    std::set<std::uint64_t> submitted_ids_;
    std::vector<Event> events_;
};

std::string to_string(Outcome outcome);
} // namespace galaxy
