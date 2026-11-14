#pragma once
// WO5-A: connect WO4 owner-visible scarcity to the EXISTING physically built
// orbital-ship -> planetary-shuttle -> landed-surface cargo path.
// This module owns NO inventory, contract registry, route, worker or flight.
// Original PersistentWorld performs every kilogram transfer and journals it.
#include "galaxy/WO4RegionalDistribution.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::logistics05 {

struct ShuttleResupplyOffer {
    PlayerId owner=0;
    economy04::CarrierRequest shortage{};
    economy04::Policy reserve_policy{};
    EntityId source_ship=0;
    PlanetarySiteKey site{};
    ShuttleCargo cargo=ShuttleCargo::Water;
    std::int64_t kilograms=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
};

enum class PhysicalResult : std::uint8_t {
    Refused=0, NativeRejected=1, PhysicalTransferCommitted=2
};

struct PhysicalTransfer {
    PhysicalResult result=PhysicalResult::Refused;
    std::int64_t kilograms=0;
    std::uint64_t original_generation=0;
    std::uint64_t world_state_hash=0;
};

inline std::optional<ShuttleCargo> original_shuttle_vital(
    economy04::Vital vital) {
    switch(vital) {
    case economy04::Vital::Food:return ShuttleCargo::Food;
    case economy04::Vital::Water:return ShuttleCargo::Water;
    case economy04::Vital::Oxygen:return ShuttleCargo::Oxygen;
    }
    return std::nullopt;
}

// Accept only a STILL-LIVE candidate from the owner-scoped original physical
// WO4 demand board. A candidate is not an entitlement and this function does
// not queue stock, allocate a vehicle or write a SQLite snapshot.
inline std::optional<ShuttleResupplyOffer> inspect_planetary_resupply(
    const Simulation& world,PlayerId owner,
    const economy04::CarrierRequest& candidate,std::int64_t kilograms,
    economy04::Policy reserve_policy={}) {
    using namespace economy04;
    if(!owner || owner==Simulation::kServerAuthority ||
       kilograms<=0 || kilograms>1000 ||
       candidate.proposed_kg<kilograms ||
       candidate.observed_tick!=world.tick() ||
       candidate.observed_state_hash!=world.state_hash() ||
       candidate.source.kind!=CompartmentKind::ColonyShip ||
       candidate.destination.kind!=CompartmentKind::PlanetarySurface ||
       candidate.source.source_ship!=candidate.source.physical_location ||
       candidate.destination.source_ship!=candidate.source.source_ship ||
       candidate.destination.physical_location==candidate.source.physical_location ||
       (candidate.priority!=Priority::VitalCurrentWindow &&
        candidate.priority!=Priority::VitalReserve))
        return std::nullopt;
    const auto cargo=original_shuttle_vital(candidate.vital);
    if(!cargo)return std::nullopt;
    const auto ship=candidate.source.source_ship;
    if(!colony03::original_owner(world,owner,ship))return std::nullopt;
    const auto site=world.planetary_sites().find(candidate.destination.planetary_key);
    const auto shuttle=world.planetary_shuttles().find(candidate.destination.planetary_key);
    if(site==world.planetary_sites().end() ||
       shuttle==world.planetary_shuttles().end() ||
       site->second.surface_location!=candidate.destination.physical_location ||
       site->second.owner!=owner || site->second.source_ship!=ship ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=ship ||
       !shuttle->second.pilot_npc || shuttle->second.flight_phase!=0 ||
       !shuttle->second.passenger_ids.empty() ||
       shuttle->second.family.stage!=0)
        return std::nullopt;
    // An actual two-way shuttle trip needs at least two kg water propellant:
    // water is already held on the vehicle or physically loaded now.
    const auto water_after=shuttle->second.water_kg+
        (*cargo==ShuttleCargo::Water?kilograms:0);
    if(water_after<2)return std::nullopt;
    const auto board=observe_regional_distribution(world,owner,reserve_policy);
    if(!board || !board->still_current(world))return std::nullopt;
    const auto same=[&](const CarrierRequest& request) {
        const auto& a=request.source;
        const auto& b=request.destination;
        return a.source_ship==candidate.source.source_ship &&
               a.physical_location==candidate.source.physical_location &&
               a.kind==candidate.source.kind &&
               a.planetary_key==candidate.source.planetary_key &&
               b.source_ship==candidate.destination.source_ship &&
               b.physical_location==candidate.destination.physical_location &&
               b.kind==candidate.destination.kind &&
               b.planetary_key==candidate.destination.planetary_key &&
               request.vital==candidate.vital &&
               request.priority==candidate.priority &&
               request.proposed_kg==candidate.proposed_kg &&
               request.observed_state_hash==candidate.observed_state_hash &&
               request.observed_tick==candidate.observed_tick;
    };
    if(std::none_of(board->unbooked_carrier_requests.begin(),
                    board->unbooked_carrier_requests.end(),same))
        return std::nullopt; // cannot forge a larger/impossible shortage
    // Exercise the EXACT installed original shuttle load preflight on a
    // throwaway copy, not a reimplementation that could miss its cargo/berth
    // constraints. Original live world and SQLite are untouched.
    auto preview=world;
    if(!preview.load_planetary_shuttle(
        owner,ship,candidate.destination.planetary_key,*cargo,kilograms))
        return std::nullopt;
    ShuttleResupplyOffer offer{};
    offer.owner=owner;offer.shortage=candidate;offer.reserve_policy=reserve_policy;
    offer.source_ship=ship;offer.site=candidate.destination.planetary_key;
    offer.cargo=*cargo;offer.kilograms=kilograms;
    offer.observed_tick=world.tick();
    offer.observed_state_hash=world.state_hash();
    return offer;
}

// Trusted host must authenticate the owner and collect explicit owner
// approval. Actual load is one original SQLite-atomic operation; the goods
// remain ON THE SHUTTLE, not at the destination, until a physical flight.
inline PhysicalTransfer commit_approved_planetary_load(
    PersistentWorld& persistent,const ShuttleResupplyOffer& offer,
    bool host_authenticated_owner_approval,FailPoint fail=FailPoint::None) {
    PhysicalTransfer result{};
    if(!host_authenticated_owner_approval ||
       offer.observed_tick!=persistent.world().tick() ||
       offer.observed_state_hash!=persistent.world().state_hash())
        return result;
    const auto fresh=inspect_planetary_resupply(
        persistent.world(),offer.owner,offer.shortage,offer.kilograms,
        offer.reserve_policy);
    if(!fresh || fresh->source_ship!=offer.source_ship ||
       fresh->site!=offer.site || fresh->cargo!=offer.cargo)
        return result;
    if(!persistent.load_planetary_shuttle(
        offer.owner,offer.source_ship,offer.site,offer.cargo,offer.kilograms,fail)){
        result.result=PhysicalResult::NativeRejected;return result;
    }
    result.result=PhysicalResult::PhysicalTransferCommitted;
    result.kilograms=offer.kilograms;
    result.original_generation=persistent.generation();
    result.world_state_hash=persistent.world().state_hash();
    return result;
}

// The original launch/arrival receipt and original landed hold, not a WO4
// demand forecast, authorize eventual release. No instant remote delivery.
// This call cannot invent a pilot, flight or passenger clearance.
inline PhysicalTransfer commit_approved_landed_unload(
    PersistentWorld& persistent,PlayerId owner,EntityId ship,
    const PlanetarySiteKey& site,ShuttleCargo cargo,std::int64_t kilograms,
    std::uint64_t authentic_descent_receipt,
    bool host_authenticated_owner_approval,FailPoint fail=FailPoint::None) {
    PhysicalTransfer result{};
    if(!host_authenticated_owner_approval || !owner ||
       owner==Simulation::kServerAuthority ||
       !ship || kilograms<=0 || kilograms>1000 ||
       cargo>ShuttleCargo::Oxygen || !authentic_descent_receipt)
        return result;
    const auto& world=persistent.world();
    const auto sh=world.planetary_shuttles().find(site);
    const auto land=world.planetary_sites().find(site);
    if(sh==world.planetary_shuttles().end() ||
       land==world.planetary_sites().end() ||
       sh->second.owner!=owner || sh->second.source_ship!=ship ||
       land->second.owner!=owner || land->second.source_ship!=ship ||
       sh->second.flight_phase!=2 ||
       sh->second.descent_receipt!=authentic_descent_receipt)
        return result;
    auto preview=world;
    if(!preview.unload_planetary_shuttle(owner,ship,site,cargo,kilograms))
        return result;
    if(!persistent.unload_planetary_shuttle(
        owner,ship,site,cargo,kilograms,fail)){
        result.result=PhysicalResult::NativeRejected;return result;
    }
    result.result=PhysicalResult::PhysicalTransferCommitted;
    result.kilograms=kilograms;
    result.original_generation=persistent.generation();
    result.world_state_hash=persistent.world().state_hash();
    return result;
}
} // namespace galaxy::logistics05
