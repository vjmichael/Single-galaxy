#pragma once
// PLAY-STATION-03A: pure bounded *station-local* geological batch transition.
// This is not yet a Simulation::Action or a durable PersistentWorld transaction.
// The enclosing original-world broker MUST independently authorize and journal
// source UnloadOre receipt, equipment construction, labor and power provenance.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace galaxy::station_refining {
// Every candidate lot is identified by its original rock and committed unload.
// The underlying StationHangar remains the one authoritative physical ore stock.
struct OreLot {
    EntityId station=0, rock=0;
    PlayerId owner=0;
    std::uint64_t unload_receipt=0;
    AsteroidClass taxonomy=AsteroidClass::Carbonaceous;
    std::int64_t remaining_tonnes=0;
};
// No default refinery: the host must debit two genuine pre-existing modules,
// assign an existing worker, and supply finite plant power before registering it.
struct Facility {
    EntityId station=0, source_ship=0;
    PlayerId owner=0;
    std::int64_t installed_modules=0, assigned_workers=0, power_units=0;
    std::int64_t processed_tonnes=0;
    std::uint64_t ready_tick=0, last_receipt=0;
    std::map<std::string,std::int64_t> mineral_kg;
};
enum class Status {
    Applied, WrongOwner, InvalidLot, NoFacility, NoWorker, NoPower,
    NoOre, CoolingDown, Replay, StockFull
};
struct Result { Status status=Status::InvalidLot; std::int64_t processed_kg=0; };

// A complete one-tonne batch is atomic: no partial ore or phantom concentrate.
// Prevalidate all stock bounds *before* touching any physical inventory.
inline Result refine_one_tonne(StationHangar& hangar, OreLot& lot,
                              Facility& plant, PlayerId requesting_owner,
                              EntityId actual_station,
                              std::uint64_t tick, std::uint64_t receipt) {
    if(!requesting_owner || !actual_station || plant.owner!=requesting_owner ||
       lot.owner!=requesting_owner || plant.station!=actual_station ||
       lot.station!=actual_station)
        return {Status::WrongOwner,0};
    if(!lot.rock || !lot.unload_receipt || lot.unload_receipt>=receipt ||
       lot.remaining_tonnes<0 || lot.remaining_tonnes>1000000000 ||
       hangar.ore<0 || hangar.ore>hangar.capacity || hangar.capacity<=0)
        return {Status::InvalidLot,0};
    if(!plant.source_ship || plant.installed_modules!=2)
        return {Status::NoFacility,0};
    if(plant.assigned_workers<=0)
        return {Status::NoWorker,0};
    if(plant.power_units<=0)
        return {Status::NoPower,0};
    if(!receipt || receipt<=plant.last_receipt)
        return {Status::Replay,0};
    if(tick<plant.ready_tick)
        return {Status::CoolingDown,0};
    if(lot.remaining_tonnes<1 || hangar.ore<1)
        return {Status::NoOre,0};
    const auto profile=mineral_profile(lot.taxonomy); // same original kernel table
    std::int64_t sum=0;
    for(const auto& phase:profile) {
        if(!phase.name || phase.permille<=0 || phase.permille>1000 ||
           sum>1000-phase.permille)
            return {Status::InvalidLot,0};
        sum+=phase.permille;
        const auto it=plant.mineral_kg.find(phase.name);
        if(it!=plant.mineral_kg.end() &&
           (it->second<0 || it->second>1000000000-phase.permille))
            return {Status::StockFull,0};
    }
    if(sum!=1000 || plant.processed_tonnes>=1000000000 ||
       tick>std::numeric_limits<std::uint64_t>::max()-4)
        return {Status::InvalidLot,0};
    // Prepare a complete output inventory before touching the source; map
    // allocation failures must never leave ore debited but minerals absent.
    auto proposed=plant.mineral_kg;
    for(const auto& phase:profile)
        proposed[phase.name]+=phase.permille;
    // All checks passed. Commit ore -> named kilograms exactly once.
    --lot.remaining_tonnes;
    --hangar.ore;
    --plant.power_units;
    ++plant.processed_tonnes;
    plant.ready_tick=tick+4;
    plant.last_receipt=receipt;
    plant.mineral_kg.swap(proposed);
    return {Status::Applied,1000};
}
} // namespace galaxy::station_refining
