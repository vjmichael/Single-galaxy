#pragma once
// Stage 7 POP-04 (first integrated slice): one original persistent society,
// independently owned economy and effective legal governance. The original
// colony's society, production and immigration APIs remain the ONLY writers.
#include "galaxy/Stage07DelegatedLabor.hpp"
#include "galaxy/Stage07PopulationHouseholds.hpp"
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace galaxy::stage7 {
struct EconomicFacts {
    EntityId colony=0,worksite=0;
    std::int64_t ship_food_kg=0,ship_water_kg=0,ship_oxygen_kg=0;
    std::int64_t workshop_aluminum_kg=0,panels_kg=0,frames_kg=0,scrap_kg=0;
    std::uint64_t cut_batches=0,shape_batches=0;
    std::uint64_t operator_npc=0,finisher_npc=0;
    bool actual_workshop=false;
};
struct SocietyFacts {
    std::uint64_t generation=0,tick=0;
    PlayerId authenticated_owner=0;
    EntityId source_colony=0;
    Census population{};
    colony_conflict::GovernanceEpoch effective_governance{};
    EconomicFacts economy{};
};
// READ ONLY. Does not infer public welfare, labor expertise, a legal verdict,
// a new work order or immigration permission. Fixed original census and finite
// physical inventory are attached to the currently committed original tick.
inline SocietyFacts assess_original_society(const PersistentWorld& world,
        PlayerId owner,EntityId source,EntityId worksite=0) {
    const auto& sim=world.world();
    const auto* ship=sim.find(source);
    const auto* colony=sim.colony_ship(source);
    const auto* supplies=sim.life_support(source);
    if(!owner || owner==Simulation::kServerAuthority ||
       !ship || ship->owner_id!=owner || !colony || !supplies)
        throw std::invalid_argument("Stage 7: no authenticated original colony");
    SocietyFacts facts{};
    facts.generation=world.generation();
    facts.tick=sim.tick();
    facts.authenticated_owner=owner;
    facts.source_colony=source;
    facts.population=project(sim);
    facts.effective_governance=world.colony_governance(source);
    if(!facts.effective_governance.leader_id ||
       sim.npc_records().count(facts.effective_governance.leader_id)!=1 ||
       sim.npc_casualties().count(facts.effective_governance.leader_id))
        throw std::runtime_error("Stage 7: original legal leadership unverified");
    facts.economy.colony=source;
    facts.economy.ship_food_kg=supplies->food_kg;
    facts.economy.ship_water_kg=supplies->water_kg;
    facts.economy.ship_oxygen_kg=supplies->oxygen_kg;
    if(worksite) {
        const auto* shop=sim.workshop(worksite);
        if(!shop || shop->source_ship!=source)
            throw std::invalid_argument("Stage 7: unowned or nonexistent physical worksite");
        facts.economy.actual_workshop=true;
        facts.economy.worksite=worksite;
        facts.economy.workshop_aluminum_kg=shop->aluminum_kg;
        facts.economy.panels_kg=shop->panels_kg;
        facts.economy.frames_kg=shop->frames_kg;
        facts.economy.scrap_kg=shop->scrap_kg;
        facts.economy.cut_batches=shop->completed_batches;
        facts.economy.shape_batches=shop->finisher_batches;
        facts.economy.operator_npc=shop->operator_npc_id;
        facts.economy.finisher_npc=shop->finisher_npc_id;
    }
    return facts;
}
// Real AUTONOMOUS personal-observation transition. This delegates to the
// existing original one-writer EXIT-01D scheduler, with its 8-tick cadence,
// actual social history, stable named NPC identities and crash/replay journal.
// The host supplies no invented receiving-colony consent, ferry or passenger.
// Any eventual migration still requires separate original physical receipt,
// receiving owner approval and ART02/ECON03 family/census transaction.
struct SocietyStep {
    SocietyFacts before{};
    PersistentWorld::HostVoluntaryDepartureReport observed{};
    SocietyFacts after{};
};
inline SocietyStep observe_original_society(PersistentWorld& world,
        PlayerId owner,EntityId source,std::uint32_t scan_budget=8) {
    const auto before=assess_original_society(world,owner,source);
    if(!scan_budget || scan_budget>32)
        throw std::invalid_argument("Stage 7: bounded society admission required");
    PersistentWorld::HostVoluntaryDepartureSchedule plan{};
    plan.origin=source;
    plan.scan_budget=scan_budget;
    const auto receipt=world.supervise_voluntary_migration(plan);
    if(receipt.origin!=source || receipt.admission_event_id || receipt.selected_npc)
        throw std::runtime_error("Stage 7: unapproved autonomous migration intent");
    const auto after=assess_original_society(world,owner,source);
    if(after.population.named_living!=before.population.named_living ||
       after.population.registered_dependents!=
           before.population.registered_dependents ||
       after.economy.ship_food_kg!=before.economy.ship_food_kg ||
       after.economy.ship_water_kg!=before.economy.ship_water_kg ||
       after.economy.ship_oxygen_kg!=before.economy.ship_oxygen_kg)
        throw std::runtime_error("Stage 7: observation fabricated material changes");
    return {before,receipt,after};
}
} // namespace galaxy::stage7
