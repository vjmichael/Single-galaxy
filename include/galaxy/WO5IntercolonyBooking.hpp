#pragma once
// WO5-B: grounded bridge from a WO4 planetary SURPLUS and owner ship shortage
// to the original two-leg local pickup + intercolony freight scheduler.
// Existing Simulation/PersistentWorld own the finite shuttle, carrier,
// pilot, route, freight stock and SQLite receipts. This bridge never
// creates goods, cargo capacity, remote arrivals, worker or transit IDs.
#include "galaxy/WO4RegionalDistribution.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::logistics05 {

struct IntercolonyBookingOffer {
    PlayerId owner=0;
    economy04::CarrierRequest demand{};
    economy04::Policy reserves{};
    EntityId origin_ship=0,carrier_ship=0,destination_ship=0;
    PlanetarySiteKey pickup_site{};
    ShuttleCargo cargo=ShuttleCargo::Food;
    std::int64_t kilograms=0;
    std::uint64_t native_local_order_id=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
};
enum class BookingState : std::uint8_t {
    Refused=0, OriginalRejected=1, OriginalWorldPosted=2
};
struct BookingResult {
    BookingState status=BookingState::Refused;
    std::uint64_t original_order_id=0;
    std::uint64_t original_generation=0;
    std::uint64_t world_state_hash=0;
};

inline std::optional<IntercolonyBookingOffer> inspect_intercolony_booking(
    const Simulation& world,PlayerId owner,
    const economy04::CarrierRequest& request,EntityId carrier,
    std::uint64_t native_order_id,std::int64_t approved_kg,
    economy04::Policy reserves={}) {
    using namespace economy04;
    // The original sealed intercolony freighter only carries water, food
    // and nutrients. WO4 vital oxygen lacks that second-leg adapter;
    // refuse rather than label it a transportable sealed package.
    if(!owner || owner==Simulation::kServerAuthority ||
       !native_order_id || native_order_id>=(std::uint64_t{1}<<50) ||
       approved_kg<=0 || approved_kg>1000 ||
       approved_kg>request.proposed_kg ||
       request.observed_tick!=world.tick() ||
       request.observed_state_hash!=world.state_hash() ||
       request.source.kind!=CompartmentKind::PlanetarySurface ||
       request.destination.kind!=CompartmentKind::ColonyShip ||
       request.destination.physical_location!=
           request.destination.source_ship ||
       request.source.source_ship==request.destination.source_ship ||
       (request.priority!=Priority::VitalCurrentWindow &&
        request.priority!=Priority::VitalReserve) ||
       (request.vital!=Vital::Food && request.vital!=Vital::Water))
        return std::nullopt;
    const auto origin=request.source.source_ship;
    const auto destination=request.destination.source_ship;
    if(!carrier || carrier==origin || carrier==destination ||
       !colony03::original_owner(world,owner,origin) ||
       !colony03::original_owner(world,owner,carrier) ||
       !colony03::original_owner(world,owner,destination))
        return std::nullopt;
    const auto site=world.planetary_sites().find(request.source.planetary_key);
    const auto shuttle=world.planetary_shuttles().find(request.source.planetary_key);
    if(site==world.planetary_sites().end() ||
       shuttle==world.planetary_shuttles().end() ||
       site->second.surface_location!=request.source.physical_location ||
       site->second.owner!=owner || site->second.source_ship!=origin ||
       site->second.spaceport_phase!=3 ||
       shuttle->second.owner!=owner || shuttle->second.source_ship!=origin ||
       shuttle->second.flight_phase!=0 ||
       !shuttle->second.passenger_ids.empty() ||
       shuttle->second.family.stage!=0 ||
       shuttle->second.water_kg<2)
        return std::nullopt;
    const auto board=observe_regional_distribution(world,owner,reserves);
    if(!board || !board->still_current(world))return std::nullopt;
    const auto match=[&](const CarrierRequest& candidate) {
        return candidate.source.kind==request.source.kind &&
               candidate.source.source_ship==request.source.source_ship &&
               candidate.source.physical_location==request.source.physical_location &&
               candidate.source.planetary_key==request.source.planetary_key &&
               candidate.destination.kind==request.destination.kind &&
               candidate.destination.source_ship==request.destination.source_ship &&
               candidate.destination.physical_location==request.destination.physical_location &&
               candidate.vital==request.vital &&
               candidate.priority==request.priority &&
               candidate.proposed_kg==request.proposed_kg &&
               candidate.observed_tick==request.observed_tick &&
               candidate.observed_state_hash==request.observed_state_hash;
    };
    if(std::none_of(board->unbooked_carrier_requests.begin(),
                    board->unbooked_carrier_requests.end(),match))
        return std::nullopt;
    // The ORIGINAL site must really retain its vital reserve after export:
    // no social-planning source buffer may override local life support.
    const auto original=observe_owner_world(world,owner,origin,reserves);
    if(!original)return std::nullopt;
    const auto row=std::find_if(original->compartments.begin(),
        original->compartments.end(),[&](const auto& c) {
            return c.kind==CompartmentKind::PlanetarySurface &&
                   c.planetary_key==request.source.planetary_key;
        });
    const auto index=request.vital==Vital::Food?0u:1u;
    if(row==original->compartments.end() ||
       row->needs[index].buffer_above_target_kg<approved_kg)
        return std::nullopt;
    const auto cargo=request.vital==Vital::Water?
        ShuttleCargo::Water:ShuttleCargo::Food;
    // Exercise BOTH original native posting and actual intercolony route
    // admissions on a throwaway Simulation. No new host-only shortcut.
    auto preview=world;
    if(!preview.post_local_delivery_order(owner,origin,
          request.source.planetary_key,cargo,approved_kg,native_order_id) ||
       !preview.plan_intercolony_freight(
          owner,native_order_id,carrier,destination))
        return std::nullopt;
    IntercolonyBookingOffer offer{};
    offer.owner=owner;offer.demand=request;offer.reserves=reserves;
    offer.origin_ship=origin;offer.carrier_ship=carrier;
    offer.destination_ship=destination;offer.pickup_site=request.source.planetary_key;
    offer.cargo=cargo;offer.kilograms=approved_kg;
    offer.native_local_order_id=native_order_id;
    offer.observed_tick=world.tick();offer.observed_state_hash=world.state_hash();
    return offer;
}

// The only mutation here calls the ORIGINAL one-transaction pair: posting
// a real local pickup and the carrier's AwaitingPickup freight leg together.
// No cargo is debited until the actual original shuttle travels and unloads;
// no destination is credited until the carrier's REAL celestial arrival.
inline BookingResult book_approved_intercolony_pickup(
    PersistentWorld& persistent,const IntercolonyBookingOffer& offer,
    bool trusted_host_authenticated_owner_and_explicit_approval,
    FailPoint fail=FailPoint::None) {
    BookingResult result{};result.original_order_id=offer.native_local_order_id;
    if(!trusted_host_authenticated_owner_and_explicit_approval ||
       offer.observed_tick!=persistent.world().tick() ||
       offer.observed_state_hash!=persistent.world().state_hash())
        return result;
    const auto verified=inspect_intercolony_booking(
        persistent.world(),offer.owner,offer.demand,offer.carrier_ship,
        offer.native_local_order_id,offer.kilograms,offer.reserves);
    if(!verified || verified->origin_ship!=offer.origin_ship ||
       verified->destination_ship!=offer.destination_ship ||
       verified->pickup_site!=offer.pickup_site ||
       verified->cargo!=offer.cargo)return result;
    if(!persistent.post_intercolony_work_order(
        offer.owner,offer.origin_ship,offer.pickup_site,offer.cargo,
        offer.kilograms,offer.native_local_order_id,
        offer.carrier_ship,offer.destination_ship,fail)) {
        result.status=BookingState::OriginalRejected;
        return result;
    }
    const auto& board=persistent.world().local_delivery_board();
    const auto& freight=persistent.world().intercolony_freight();
    const auto pickup=board.find(offer.native_local_order_id);
    const auto leg=freight.find(offer.native_local_order_id);
    if(pickup==board.end() || leg==freight.end() ||
       pickup->second.stage!=LocalDeliveryStage::Posted ||
       leg->second.stage!=IntercolonyFreightStage::AwaitingPickup)
        throw std::logic_error("original WO5 posted freight receipt mismatch");
    result.status=BookingState::OriginalWorldPosted;
    result.original_generation=persistent.generation();
    result.world_state_hash=persistent.world().state_hash();
    return result;
}
} // namespace galaxy::logistics05
