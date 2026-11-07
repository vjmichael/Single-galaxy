#pragma once
// WO4-A: a READ-ONLY economic observation of actual, owner-controlled
// life-support compartments in the original Simulation. This is not stock,
// inventory authority, a reservation, an NPC work order, a price, a shipment,
// or a second world. WO5 must route any real deliveries through existing
// authenticated original-world transport and SQLite journal operations.
#include "galaxy/WO3ColonyOperations.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace galaxy::economy04 {

enum class CompartmentKind : std::uint8_t {
    ColonyShip=0, OutpostHabitat=1, OutpostFarm=2, PlanetarySurface=3
};
enum class Vital : std::uint8_t {Food=0, Water=1, Oxygen=2};
enum class Pressure : std::uint8_t {
    NoCurrentConsumers=0, CriticalCurrentWindow=1, BelowReserve=2,
    MeetsReserve=3
};
// "Window" is an owner-selected PLANNING BASKET in kg per current person,
// NOT a promise that each existing simulation subsystem consumes per tick.
// Until WO4-B defines an event/clock-aware policy, no tick conversion,
// production, demand event, or resource reservation is implied.
struct Policy {
    std::uint64_t reserve_windows=4;
    std::uint64_t kg_per_person_per_window=1;
};
struct Need {
    Vital vital=Vital::Food;
    Pressure pressure=Pressure::NoCurrentConsumers;
    std::int64_t physically_present_kg=0;
    std::int64_t one_window_kg=0;
    std::int64_t reserve_target_kg=0;
    std::int64_t reserve_deficit_kg=0;
    std::int64_t buffer_above_target_kg=0; // NOT approved transferable surplus
    std::uint64_t complete_windows_covered=0;
};
struct Compartment {
    CompartmentKind kind=CompartmentKind::ColonyShip;
    PlayerId owner=0;
    EntityId source_ship=0;
    EntityId physical_location=0;
    PlanetarySiteKey planetary_key{}; // meaningful only for a surface site
    std::uint64_t current_consumers=0;
    std::array<Need,3> needs{};
};
struct Snapshot {
    PlayerId owner=0;
    EntityId source_ship=0;
    std::uint64_t observed_tick=0;
    std::uint64_t observed_state_hash=0;
    Policy policy{};
    std::vector<Compartment> compartments; // stable source -> site order
    bool still_current(const Simulation& world) const {
        return world.tick()==observed_tick &&
               world.state_hash()==observed_state_hash;
    }
};
inline std::optional<std::array<Need,3>> calculate(
    std::uint64_t current_people, const std::array<std::int64_t,3>& physical_stock,
    Policy policy={}) {
    // Never clamp impossible populations or overflow target calculations:
    // a partial shortage report would disguise an unbounded missing need.
    constexpr auto limit=static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if(!policy.reserve_windows || policy.reserve_windows>32 ||
       !policy.kg_per_person_per_window ||
       policy.kg_per_person_per_window>1000000 ||
       current_people>limit/policy.kg_per_person_per_window)
        return std::nullopt;
    const auto one=current_people*policy.kg_per_person_per_window;
    if(one>limit/policy.reserve_windows)return std::nullopt;
    const auto target=one*policy.reserve_windows;
    for(const auto amount:physical_stock)if(amount<0)return std::nullopt;
    std::array<Need,3> result{};
    for(std::size_t i=0;i<result.size();++i){
        auto& n=result[i];
        n.vital=static_cast<Vital>(i);
        n.physically_present_kg=physical_stock[i];
        n.one_window_kg=static_cast<std::int64_t>(one);
        n.reserve_target_kg=static_cast<std::int64_t>(target);
        n.reserve_deficit_kg=n.physically_present_kg<n.reserve_target_kg?
            n.reserve_target_kg-n.physically_present_kg:0;
        n.buffer_above_target_kg=n.physically_present_kg>n.reserve_target_kg?
            n.physically_present_kg-n.reserve_target_kg:0;
        n.complete_windows_covered=one?
            static_cast<std::uint64_t>(n.physically_present_kg) / one:0;
        n.pressure=!one?Pressure::NoCurrentConsumers:
            n.physically_present_kg<n.one_window_kg?
                Pressure::CriticalCurrentWindow:
            n.physically_present_kg<n.reserve_target_kg?
                Pressure::BelowReserve:Pressure::MeetsReserve;
        // An empty worksite does not have an approved transferable surplus.
        if(!one)n.buffer_above_target_kg=0;
    }
    return result;
}
inline std::optional<Snapshot> observe_owner_world(
    const Simulation& world,PlayerId owner,EntityId source_ship,
    Policy policy={}) {
    if(!colony03::original_owner(world,owner,source_ship))return std::nullopt;
    const auto* people=world.colony_ship(source_ship);
    const auto* life=world.life_support(source_ship);
    if(!people || !life || people->crew<0)return std::nullopt;
    Snapshot snapshot{};
    snapshot.owner=owner;
    snapshot.source_ship=source_ship;
    snapshot.observed_tick=world.tick();
    snapshot.observed_state_hash=world.state_hash();
    snapshot.policy=policy;
    const auto add=[&](CompartmentKind kind,EntityId physical_location,
                       const PlanetarySiteKey& key,std::uint64_t count,
                       std::int64_t food,std::int64_t water,
                       std::int64_t oxygen)->bool {
        const auto needs=calculate(count,{food,water,oxygen},policy);
        if(!needs)return false;
        Compartment row{};
        row.kind=kind;row.owner=owner;row.source_ship=source_ship;
        row.physical_location=physical_location;
        row.planetary_key=key;row.current_consumers=count;row.needs=*needs;
        snapshot.compartments.push_back(row);
        return true;
    };
    if(!add(CompartmentKind::ColonyShip,source_ship,PlanetarySiteKey{},
            static_cast<std::uint64_t>(people->crew),
            life->food_kg,life->water_kg,life->oxygen_kg))
        return std::nullopt;
    // These are separate REAL bins. An outpost farm's provisions must
    // never be added to habitat stock without a genuine transport receipt.
    for(const auto& [site_id,built]:world.built_outposts()){
        if(built.source_colony_ship!=source_ship)continue;
        const auto* entity=world.find(site_id);
        if(!entity || entity->owner_id!=owner ||
           !colony03::original_site(world,source_ship,site_id))
            return std::nullopt;
        if(const auto* habitat=world.habitat(site_id)){
            if(habitat->source_ship!=source_ship || habitat->residents<0 ||
               !add(CompartmentKind::OutpostHabitat,site_id,PlanetarySiteKey{},
                    static_cast<std::uint64_t>(habitat->residents),
                    habitat->life.food_kg,habitat->life.water_kg,
                    habitat->life.oxygen_kg))return std::nullopt;
        }
        if(const auto* farm=world.farm(site_id)){
            if(farm->source_ship!=source_ship || farm->workers<0 ||
               !add(CompartmentKind::OutpostFarm,site_id,PlanetarySiteKey{},
                    static_cast<std::uint64_t>(farm->workers),
                    farm->life.food_kg,farm->life.water_kg,
                    farm->life.oxygen_kg))return std::nullopt;
        }
    }
    // Planets are not orbital stations, and the actual physical
    // passengers/delegates/dependents already have one native WO3 census.
    for(const auto& [key,site]:world.planetary_sites()){
        if(site.source_ship!=source_ship)continue;
        if(site.owner!=owner)return std::nullopt;
        const auto census=colony03::audit_planetary_surface(
            world,owner,source_ship,key);
        if(!census || census->current_surface_consumers<0 ||
           !add(CompartmentKind::PlanetarySurface,site.surface_location,key,
                static_cast<std::uint64_t>(census->current_surface_consumers),
                site.food_kg,site.water_kg,site.oxygen_kg))
            return std::nullopt;
    }
    return snapshot; // read-only; no hidden pending stock, movement, or receipts
}
} // namespace galaxy::economy04
