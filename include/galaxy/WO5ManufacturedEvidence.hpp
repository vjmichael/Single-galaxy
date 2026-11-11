#pragma once
// WO5-C immutable presentation of original v56/v57 shipment proofs. No
// WO5 status table, goods balance, reservation or client-side claim of
// delivery. Canonical ShipmentStatusView reads real SQLite world receipts;
// original Simulation corroborates the actual carrier/site inventory.
#include "galaxy/ShipmentStatusView.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace galaxy::logistics05 {

enum class ManufacturedPhase : std::uint8_t {
    AwaitingConsent=0, Dispatched=1, PhysicallyLoaded=2,
    PhysicallyDelivered=3, Settled=4
};
struct ManufacturedEvidence {
    std::string id;
    ManufacturedPhase phase=ManufacturedPhase::AwaitingConsent;
    PlayerId viewing_principal=0;
    PlayerId source_owner=0,carrier_owner=0,receiver_owner=0;
    EntityId source_site=0,destination_site=0,carrier_ship=0;
    std::int64_t kilograms=0;
    bool loaded_receipt=false,unload_receipt=false,settlement_receipt=false;
    std::int64_t original_carrier_held_kg=0;
    std::int64_t original_destination_stock_kg=0;
};

// This presentation deliberately excludes return/redirect: those have
// their own original v57/v58 receipts and may not be shown as delivery to
// the original requested recipient.
inline std::optional<ManufacturedEvidence>
inspect_manufactured_evidence(
    const Simulation& world,const shipping::ShipmentStatusView& ledger,
    PlayerId authorized_participant,const shipping::ThirdPartyBooking& booking) {
    if(!authorized_participant ||
       (authorized_participant!=booking.source_owner &&
        authorized_participant!=booking.carrier_owner &&
        authorized_participant!=booking.receiving_owner) ||
       !booking.require_dispatch || !booking.quantity_kg ||
       (booking.item_index!=1 && booking.item_index!=2))
        return std::nullopt;
    const auto records=ledger.for_principal(authorized_participant);
    const auto found=std::find_if(records.begin(),records.end(),
        [&](const shipping::ShipmentStatus& item){return item.id==booking.id;});
    if(found==records.end())return std::nullopt;
    const auto& row=*found;
    const auto material=booking.item_index==1?
        std::string("aluminum_panels"):std::string("aluminum_frames");
    const auto code="good/"+material;
    if(row.material!=material ||
       row.source_site!=booking.origin_site ||
       row.destination_site!=booking.destination_site ||
       row.carrier_ship!=booking.carrier_ship ||
       row.source_owner!=booking.source_owner ||
       row.carrier_owner!=booking.carrier_owner ||
       row.receiver_owner!=booking.receiving_owner ||
       row.quantity_kg!=booking.quantity_kg ||
       row.revoked || row.redirected || row.returned ||
       (row.loaded && !row.dispatched) ||
       (row.delivered && !row.loaded) ||
       (row.settled && !row.delivered))
        return std::nullopt;
    const auto* source=world.storage(booking.origin_site);
    const auto* dest=world.storage(booking.destination_site);
    const auto* carrier=world.colony_ship(booking.carrier_ship);
    const auto* source_entity=world.find(booking.origin_site);
    const auto* dest_entity=world.find(booking.destination_site);
    const auto* carrier_entity=world.find(booking.carrier_ship);
    if(!source || !dest || !carrier ||
       !source_entity || !dest_entity || !carrier_entity ||
       source->source_ship!=booking.source_ship ||
       dest->source_ship!=booking.receiving_ship ||
       source_entity->owner_id!=booking.source_owner ||
       dest_entity->owner_id!=booking.receiving_owner ||
       carrier_entity->owner_id!=booking.carrier_owner)
        return std::nullopt;
    const auto carrying=world.ship_inventory_snapshot(
        booking.carrier_ship).amount(code);
    const auto arrived=world.storage_inventory_snapshot(
        booking.destination_site).amount(code);
    if(carrying<0 || arrived<0 ||
       (row.loaded && !row.delivered && carrying!=booking.quantity_kg) ||
       ((!row.loaded || row.delivered) && carrying!=0) ||
       (row.delivered && arrived<booking.quantity_kg))
        return std::nullopt;
    ManufacturedEvidence result{};
    result.id=booking.id;
    result.viewing_principal=authorized_participant;
    result.source_owner=booking.source_owner;
    result.carrier_owner=booking.carrier_owner;
    result.receiver_owner=booking.receiving_owner;
    result.source_site=booking.origin_site;
    result.destination_site=booking.destination_site;
    result.carrier_ship=booking.carrier_ship;
    result.kilograms=booking.quantity_kg;
    result.loaded_receipt=row.loaded;
    result.unload_receipt=row.delivered;
    result.settlement_receipt=row.settled;
    result.original_carrier_held_kg=carrying;
    result.original_destination_stock_kg=arrived;
    result.phase=row.settled?ManufacturedPhase::Settled:
        row.delivered?ManufacturedPhase::PhysicallyDelivered:
        row.loaded?ManufacturedPhase::PhysicallyLoaded:
        row.dispatched?ManufacturedPhase::Dispatched:
                       ManufacturedPhase::AwaitingConsent;
    return result;
}
} // namespace galaxy::logistics05
