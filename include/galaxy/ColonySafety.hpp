#pragma once
// COLONY-AUTO-04A — read-only live-world assessment and host safety admission.
// This module never creates cargo, moves people or edits Simulation inventory.
// The host must journal every returned decision with the canonical world writer.
#include "galaxy/MacroNeeds.hpp"
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <string>

struct sqlite3;
namespace galaxy::colony_safety {
enum class Risk : int { Safe=0, ForecastShortage=1, Critical=2, Emergency=3 };
enum class Order : int {
    SetLocalProduction=1, SealOccupiedExit=2, DisableDistress=3,
    DumpOccupiedAtmosphere=4, PreventEmergencyRepair=5
};
struct Assessment {
    EntityId site=0, home_ship=0;
    PlayerId owner=0;
    std::uint64_t population=0, world_tick=0;
    macro_needs::Runway remaining{};
    Risk risk=Risk::Safe;
    bool emergency_override=false;
    bool should_prepare_departure=false;
};
struct Decision {
    Assessment assessment;
    Order order=Order::SetLocalProduction;
    PlayerId requesting_player=0;
    bool allowed=false;
    bool deliberate_hazard_attempt=false;
    std::string reason;
};
// AUTO-04D.1: authoritative, READ-ONLY response intent. An intent is never
// evidence that a repair, shipment, or evacuation actually happened.
// No social tolerance, player mood, forecast import, or inferred transport
// capacity can weaken life-preserving priority.
enum class ResponseLevel : int {
    None=0, MonitorAndRequestSupply=1, UrgentRepairAndDepartureSearch=2,
    EmergencySurvivalAndDistress=3
};
struct ResponsePlan {
    EntityId site=0, home_ship=0;
    std::uint64_t world_tick=0, population=0;
    Risk risk=Risk::Safe;
    ResponseLevel level=ResponseLevel::None;
    bool request_supply=false;
    bool seek_physical_repairs=false;
    bool search_verified_departure=false;
    bool activate_distress=false;
    bool bypass_player_production_preferences=false;
    // Rescue remains false until committed capacity, route and person-custody
    // transfers have been verified elsewhere by the world owner.
    bool rescue_completed=false;
};
ResponsePlan plan_response(const Assessment& current);
// In particular, this rejects an obsolete assessment after advancing ticks;
// the host must re-assess after every committed world mutation.
ResponsePlan plan_response(const Simulation&,EntityId home_ship,EntityId site);
struct ResponseTransition {
    ResponsePlan previous,current;
    bool risk_changed=false,escalated=false,requires_new_notice=false;
};
ResponseTransition response_transition(const ResponsePlan& previous,
                                       const ResponsePlan& current);
// Only the selected physical compartment is assessed. No double counting of
// crew, resident or farm staff compartments, and NO speculative shipments.
Assessment assess(const Simulation&, EntityId home_ship, EntityId site,
                  std::uint64_t forecast_warning_periods=4,
                  std::uint64_t critical_periods=1);
// Preflight, NOT production execution. The actual policy mutation must be a
// separate canonical world action with exact tick and authorized replay.
Decision preflight(const Simulation&, EntityId home_ship, EntityId site,
                   PlayerId issuer,Order order,
                   std::uint64_t forecast_warning_periods=4,
                   std::uint64_t critical_periods=1);
// Append-only source facts for later GRSM observation. Caller must supply the
// canonical PersistentWorld sqlite3* handle inside its existing transaction;
// this class never opens a second world database or attributes omniscient belief.
void create_evidence_table(sqlite3* world_writer);
void record_decision(sqlite3* world_writer, std::uint64_t unique_event_id,
                     const Decision& decision);
std::uint64_t evidence_count(sqlite3* world_writer);
} // namespace galaxy::colony_safety
