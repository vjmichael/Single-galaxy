#pragma once
// WO4-D: native cadence-backed life-support NEED profiles plus the actual
// INDUSTRY-01 goods/services taxonomy. Observation only: these profiles
// never debit inventory or issue NPC work. The original Simulation ticks
// remain the sole native consumer/producer; dynamic supply and recovery
// can change outcomes before each due tick.
#include "galaxy/WO4ResourceDemand.hpp"
#include "galaxy/Industry01Catalog.hpp"
#include "galaxy/MacroNeeds.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace galaxy::economy04 {
enum class GoodsRole : std::uint8_t {
    Feedstock=0, Intermediate=1, Consumable=2, Durable=3,
    Waste=4, Unknown=5
};
struct GoodsProfile {
    std::string code,group,unit;
    GoodsRole role=GoodsRole::Unknown;
    std::uint64_t mass_g_per_unit=0,cargo_mm3_per_unit=0;
    std::vector<std::string> need_tags;
    // True means the catalogue contains a possible recipe, never an
    // installed machine, available stock or authorized production.
    bool has_recipe=false,has_native_recipe=false;
    bool automatically_consumed_by_original_world=false;
};
inline std::optional<GoodsProfile> describe_good(const std::string& code) {
    const auto* item=industry01::find_item(code);
    if(!item || !item->mass_g_per_unit || !item->cargo_mm3_per_unit)
        return std::nullopt;
    GoodsProfile out{};
    out.code=item->code;out.group=item->group;out.unit=item->unit;
    out.mass_g_per_unit=item->mass_g_per_unit;
    out.cargo_mm3_per_unit=item->cargo_mm3_per_unit;
    out.need_tags=item->need_tags;
    out.role=item->lifecycle=="feedstock"?GoodsRole::Feedstock:
        item->lifecycle=="intermediate"?GoodsRole::Intermediate:
        item->lifecycle=="consumable"?GoodsRole::Consumable:
        item->lifecycle=="durable"?GoodsRole::Durable:
        item->lifecycle=="waste"?GoodsRole::Waste:GoodsRole::Unknown;
    for(const auto& recipe:industry01::recipes())
        for(const auto& product:recipe.outputs)
            if(product.code==code){
                out.has_recipe=true;
                if(industry01::is_native_workshop_recipe(recipe.code))
                    out.has_native_recipe=true;
            }
    // Item category alone cannot cause a real stock debit. Life-support
    // supplies live in original ColonyLifeSupport kg, not in this catalogue.
    out.automatically_consumed_by_original_world=false;
    return out;
}
struct ServiceProfile {
    std::string code,result;
    std::vector<std::string> staff,site_capabilities,consumables,equipment;
    // Do NOT mistake presence in a design catalogue for an implemented clinic,
    // school, lawyer or entertainment service.
    bool native_executable=false;
};
inline std::optional<ServiceProfile> describe_service(const std::string& code){
    const auto* svc=industry01::find_service(code);
    if(!svc)return std::nullopt;
    ServiceProfile result{};
    result.code=svc->code;result.result=svc->result;
    result.staff=svc->staff;result.site_capabilities=svc->site_capabilities;
    result.consumables=svc->consumables;result.equipment=svc->equipment;
    for(const auto& item:result.consumables)
        if(!industry01::find_item(item))return std::nullopt;
    for(const auto& item:result.equipment)
        if(!industry01::find_item(item))return std::nullopt;
    // No unimplemented autonomous service action. Site and staff also
    // require physical native authority in the original Simulation.
    result.native_executable=false;
    return result;
}
struct NativeNeedCycle {
    CompartmentKind compartment=CompartmentKind::ColonyShip;
    EntityId physical_location=0;
    PlanetarySiteKey planetary_key{};
    std::uint64_t persons=0;
    std::uint64_t cadence_ticks=20,next_due_tick=0;
    std::array<std::int64_t,3> physically_present_kg{};
    // Conditional surface worker/visitor withdrawals may be blocked by
    // production power/stock/staff preflights; a forecast is not a debit.
    bool conditional=false;
    std::uint64_t per_due_kg_each_vital=0;
};
struct ConsumptionView {
    std::uint64_t observed_tick=0,observed_state_hash=0;
    std::vector<NativeNeedCycle> cycles;
    bool still_current(const Simulation& world) const {
        return observed_tick==world.tick() &&
               observed_state_hash==world.state_hash();
    }
};
inline std::optional<ConsumptionView> observe_native_need_cycles(
    const Simulation& world,const Snapshot& snapshot) {
    if(!snapshot.still_current(world) ||
       !colony03::original_owner(world,snapshot.owner,snapshot.source_ship))
        return std::nullopt;
    ConsumptionView result{};
    result.observed_tick=snapshot.observed_tick;
    result.observed_state_hash=snapshot.observed_state_hash;
    const auto append=[&](const Compartment& compartment,
        std::uint64_t people,std::uint64_t cadence,bool conditional)->bool {
        if(people>static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max()))
            return false;
        NativeNeedCycle row{};
        row.compartment=compartment.kind;
        row.physical_location=compartment.physical_location;
        row.planetary_key=compartment.planetary_key;
        row.persons=people;row.cadence_ticks=cadence;
        row.conditional=conditional;
        row.per_due_kg_each_vital=people;
        for(std::size_t i=0;i<3;++i)
            row.physically_present_kg[i]=
                compartment.needs[i].physically_present_kg;
        try {row.next_due_tick=macro_needs::next_due(world.tick(),cadence);}
        catch(const std::exception&) {return false;}
        result.cycles.push_back(row);
        return true;
    };
    for(const auto& compartment:snapshot.compartments){
        if(compartment.owner!=snapshot.owner ||
           compartment.source_ship!=snapshot.source_ship)return std::nullopt;
        if(compartment.kind!=CompartmentKind::PlanetarySurface){
            if(!append(compartment,compartment.current_consumers,20,false))
                return std::nullopt;
            continue;
        }
        // Existing original planetary engine charges two physically
        // deployed workers every 10 ticks IF the worksite is provisioned.
        // Visitors and independently registered children are charged every
        // 20 ticks in the original shuttle operation, not every 10.
        const auto it=world.planetary_sites().find(compartment.planetary_key);
        if(it==world.planetary_sites().end() ||
           it->second.surface_location!=compartment.physical_location)
            return std::nullopt;
        const std::uint64_t workers=
            it->second.spaceport_phase>0 &&
            it->second.assigned_workers==2?2:0;
        if(workers && !append(compartment,workers,10,true))
            return std::nullopt;
        const auto surface_visitors=compartment.current_consumers>=workers?
            compartment.current_consumers-workers:0;
        if(surface_visitors &&
           !append(compartment,surface_visitors,20,true))
            return std::nullopt;
    }
    return result;
}
} // namespace galaxy::economy04
