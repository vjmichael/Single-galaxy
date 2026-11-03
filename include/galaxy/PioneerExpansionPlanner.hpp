#pragma once
// DEMOG-03: deterministic, read-only pioneer expedition advice. No authority,
// births, population changes, ship creation, inventory debit or destination
// settlement creation. Those require a separate host-validated, journaled gate.
#include <cstdint>
#include <vector>

namespace galaxy::pioneers {
enum Pressure : std::uint32_t {
    None=0, HousingForecast=1u<<0, SoftPlanningGoal=1u<<1,
    FoodProjection=1u<<2, WaterProjection=1u<<3,
    OxygenProjection=1u<<4, MineralDepletion=1u<<5
};
enum class Advice : std::uint8_t {
    NoExpansionNeeded, NeedCrew, NeedSurvey, NeedShip,
    NeedSupplies, PioneerProposal
};
struct Policy {
    std::uint64_t soft_settlement_goal=800; // advisory only, NEVER population cap
    std::uint64_t minimum_pioneers=80;
    std::uint64_t maximum_pioneers=150;
    std::uint64_t keep_population=200;
    std::uint64_t keep_adults=100;
    std::uint64_t forecast_years=5;
    std::uint64_t origin_emergency_reserve_years=1;
    std::uint32_t pioneer_population_share_permille=200; // <=20% on one mission
    std::uint32_t adult_fraction_permille=700; // manifest must have >=70% adults
    std::uint64_t food_per_person_voyage_and_landing=2;
    std::uint64_t water_per_person_voyage_and_landing=2;
    std::uint64_t oxygen_per_person_voyage_and_landing=2;
    std::uint64_t kit_base_material=350;
    std::uint64_t kit_material_per_person=3;
};
struct Source {
    std::uint64_t settlement_id=0;
    std::uint64_t population=0, adults=0, projected_population=0;
    std::uint64_t housing_capacity=0;
    // Arbitrary common units of physical stock/production/consumption supplied
    // by the authoritative economy. Caller must use consistent units.
    std::uint64_t food_stock=0, water_stock=0, oxygen_stock=0;
    std::uint64_t food_produced_per_year=0, water_produced_per_year=0,
                  oxygen_produced_per_year=0;
    std::uint64_t food_needed_per_year=0, water_needed_per_year=0,
                  oxygen_needed_per_year=0;
    std::uint64_t mineable_ore=0, ore_needed_per_year=0;
    std::uint64_t unreserved_construction_material=0;
    std::uint64_t available_ship_capacity=0; // must refer to EXISTING vessel
};
struct Site {
    std::uint64_t site_id=0;
    bool surveyed=false, reachable=false, unclaimed=false;
    // Measured sustained local support for settlers, not free stocked inventory.
    std::uint64_t food_support=0, water_support=0;
    std::uint64_t landing_capacity=0;
    std::uint64_t mineral_opportunity=0; // surveyed abstract quality 0..1000
    std::uint64_t travel_cost=0; // common unit, score penalty
    std::uint32_t danger_permille=0; // 0..1000; risk screened, not waived
};
struct Result {
    Advice advice=Advice::NoExpansionNeeded;
    std::uint32_t pressure_mask=None;
    std::uint64_t origin=0, destination=0;
    std::uint64_t pioneers=0, required_adults=0;
    std::uint64_t food_to_reserve=0, water_to_reserve=0,
                  oxygen_to_reserve=0, material_to_reserve=0;
    // True means only that constraints from this immutable input passed.
    // It is NOT an authorization or a durable reservation.
    bool requires_host_authorization=true;
};
Result advise(const Source& source, const std::vector<Site>& sites,
              const Policy& policy=Policy{});
} // namespace galaxy::pioneers
