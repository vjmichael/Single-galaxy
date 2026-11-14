#pragma once
// Stage 7 POP-03: independently authorized named labor in the same original
// Simulation / PersistentWorld. This is NOT another labor ledger, staffing
// service, or permission to execute without an authenticated owner decision.
#include "galaxy/Stage07PopulationHouseholds.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace galaxy::stage7 {
struct LaborOffer {
    std::uint64_t source_generation=0,source_tick=0,source_hash=0;
    PlayerId owner=0;
    EntityId home_ship=0,site=0;
    std::uint64_t worker=0;
    bool finisher=false;
    Action action=Action::AssignWorkshopOperator;
};
// A deliberately finite original skill-domain admission: the industrial
// kernel currently implements panel cutting and frame shaping, not all 29
// proposed professions. No action for missing machines, stock, skills,
// present personnel or owner authority can be offered.
inline std::optional<LaborOffer> offer_original_workshop_labor(
        const PersistentWorld& world,PlayerId authenticated_owner,
        EntityId home_ship,EntityId site,bool finisher=false) {
    const auto& sim=world.world();
    const auto census=project(sim); // reject duplicate original identities
    (void)census;
    const auto slots=workforce02::plan_original_workshop(
        sim,authenticated_owner,home_ship,site);
    const auto& slot=slots.at(finisher?1:0);
    if(!slot.can_offer_assignment() || !slot.suggested_npc ||
       sim.npc_records().count(slot.suggested_npc)!=1 ||
       !sim.npc_available_for_duty(slot.suggested_npc) ||
       world.generation()==0)return std::nullopt;
    return LaborOffer{world.generation(),sim.tick(),sim.state_hash(),
        authenticated_owner,home_ship,site,slot.suggested_npc,finisher,
        finisher?Action::AssignWorkshopFinisher:Action::AssignWorkshopOperator};
}

// The offer is NOT the approval. It must be explicitly accepted by the
// independent authenticated owner using the exact generation/worker/action
// last observed; the original native submit+execute is separately required.
// A stale offer or a replayed command never passes the original gate.
inline std::optional<Command> authorize_original_workshop_labor(
        const PersistentWorld& world,const LaborOffer& offer,
        std::uint64_t command_id,bool independently_approved) {
    if(!independently_approved || !command_id ||
       world.generation()!=offer.source_generation ||
       world.world().tick()!=offer.source_tick ||
       world.world().state_hash()!=offer.source_hash ||
       world.world().has_command_id(command_id) ||
       offer.owner==0 || offer.owner==Simulation::kServerAuthority ||
       offer.action!=(offer.finisher?
           Action::AssignWorkshopFinisher:Action::AssignWorkshopOperator))
        return std::nullopt;
    const auto current=offer_original_workshop_labor(
        world,offer.owner,offer.home_ship,offer.site,offer.finisher);
    if(!current || current->worker!=offer.worker ||
       current->source_generation!=offer.source_generation)
        return std::nullopt;
    return workforce02::approved_original_assignment(
        world.world(),offer.owner,offer.home_ship,offer.site,
        offer.finisher,offer.worker,command_id,true);
}

// Count only an original Applied command at the actual site. An approved
// proposal does not claim any work occurred. Original assembly, energy,
// material reservation and durable SQLite receipts remain independently
// enforced by the kernel; this check cannot create output or grant wages.
inline bool original_labor_applied(const PersistentWorld& world,
        const LaborOffer& offer,std::uint64_t command_id) {
    if(!command_id || world.generation()<=offer.source_generation ||
       !world.world().has_command_id(command_id))return false;
    const auto* site=world.world().workshop(offer.site);
    if(!site || site->source_ship!=offer.home_ship ||
       (offer.finisher?site->finisher_npc_id:site->operator_npc_id)!=offer.worker)
        return false;
    const auto& events=world.world().events();
    return std::count_if(events.begin(),events.end(),[&](const Event& e){
        return e.command_id==command_id && e.result==Outcome::Applied &&
               e.tick>offer.source_tick;
    })==1;
}
} // namespace galaxy::stage7
