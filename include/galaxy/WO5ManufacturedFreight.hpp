#pragma once
// WO5-C: adapt a RECIPIENT-DECLARED WO4 material need to the original
// v56/v57 factory-to-warehouse shipping host. No second cargo ledger,
// demand scheduler, money, ownership change, or invented manufacturing.
//
// A WO4 one-owner ProposedMaterialFreight is deliberately NOT sufficient
// authority for this three-INDEPENDENT-owner path. The receiving owner
// declares a MaterialNeed; the source chooses an already physically
// returned factory lot; a trusted host admits booking. Separate native
// source/carrier/receiver approvals and original voyage/receipt still apply.
#include "galaxy/WO4GoodsRegional.hpp"
#include "galaxy/CanonicalShippingHost.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace galaxy::logistics05 {

struct ManufacturedConsignmentOffer {
    shipping::ThirdPartyBooking native{};
    economy04::MaterialNeed declared_need{};
    PlayerId declared_receiver=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    std::int64_t observed_source_kg=0,observed_destination_free_liters=0;
    // Production return proof is NOT certified by this world-only preflight.
    // CanonicalShippingHost -> ProductionShippingHost -> the original SQLite
    // sg_v53_world_return receipt verifies it when booking is committed.
    bool factory_provenance_requires_native_host_verification=true;
};

inline bool canonical_consignment_id(const std::string& name) {
    if(name.empty() || name.size()>100)return false;
    for(const unsigned char c:name)
        if(!(c>='a'&&c<='z') && !(c>='A'&&c<='Z') &&
           !(c>='0'&&c<='9') && c!='-' && c!='_')return false;
    return true;
}

// A recipient declares a real need. The trusted host has authenticated
// that request to declared_receiver. This yields a PROPOSAL, not an
// approval, booking, reserved stock or physical shipment.
inline std::optional<ManufacturedConsignmentOffer>
inspect_manufactured_consignment(
    const Simulation& world,const economy04::MaterialNeed& recipient_need,
    PlayerId declared_receiver,bool trusted_host_verified_recipient_request,
    EntityId source_ship,EntityId carrier_ship,EntityId receiving_ship,
    EntityId origin_site,PlayerId source_owner,PlayerId carrier_owner,
    std::uint64_t original_factory_return_command,
    std::uint64_t native_load_command,std::uint64_t native_unload_command,
    const std::string& consignment_id,
    std::int64_t proposed_kg) {
    using economy04::NativeMaterial;
    using economy04::Priority;
    if(!trusted_host_verified_recipient_request ||
       !declared_receiver || declared_receiver==Simulation::kServerAuthority ||
       !source_owner || !carrier_owner ||
       source_owner==Simulation::kServerAuthority ||
       carrier_owner==Simulation::kServerAuthority ||
       source_owner==carrier_owner || source_owner==declared_receiver ||
       carrier_owner==declared_receiver ||
       !source_ship || !carrier_ship || !receiving_ship ||
       source_ship==carrier_ship || source_ship==receiving_ship ||
       carrier_ship==receiving_ship ||
       !origin_site || origin_site==recipient_need.destination ||
       !original_factory_return_command || !native_load_command ||
       !native_unload_command ||
       native_load_command==native_unload_command ||
       original_factory_return_command==native_load_command ||
       original_factory_return_command==native_unload_command ||
       !canonical_consignment_id(consignment_id) ||
       proposed_kg<=0 || proposed_kg>1000 ||
       proposed_kg>recipient_need.requested_kg ||
       recipient_need.requested_kg<=0 ||
       recipient_need.requested_kg>100000000 ||
       (recipient_need.purpose!=Priority::Maintenance &&
        recipient_need.purpose!=Priority::Production &&
        recipient_need.purpose!=Priority::Discretionary) ||
       (recipient_need.material!=NativeMaterial::Panels &&
        recipient_need.material!=NativeMaterial::Frames))
        return std::nullopt;
    const auto* origin=world.find(origin_site);
    const auto* destination=world.find(recipient_need.destination);
    const auto* source=world.find(source_ship);
    const auto* carrier=world.find(carrier_ship);
    const auto* receiver=world.find(receiving_ship);
    const auto* from=world.storage(origin_site);
    const auto* into=world.storage(recipient_need.destination);
    if(!origin || !destination || !source || !carrier || !receiver ||
       !from || !into ||
       origin->owner_id!=source_owner ||
       destination->owner_id!=declared_receiver ||
       source->owner_id!=source_owner ||
       carrier->owner_id!=carrier_owner ||
       receiver->owner_id!=declared_receiver ||
       source->type!=EntityType::Ship ||
       carrier->type!=EntityType::Ship ||
       receiver->type!=EntityType::Ship ||
       !world.colony_ship(source_ship) ||
       !world.colony_ship(carrier_ship) ||
       !world.colony_ship(receiving_ship) ||
       !world.built_outposts().count(origin_site) ||
       !world.built_outposts().count(recipient_need.destination) ||
       from->source_ship!=source_ship || into->source_ship!=receiving_ship ||
       origin->system_id!=carrier->system_id ||
       destination->system_id!=receiver->system_id ||
       destination->system_id==origin->system_id ||
       !destination->system_id || destination->system_id>1000000)
        return std::nullopt;
    const int native_index=recipient_need.material==NativeMaterial::Panels?1:2;
    const std::string native_key=native_index==1?
        "aluminum_panels":"aluminum_frames";
    const std::string cargo_key="good/"+native_key;
    const auto stocked=from->goods_kg.find(native_key);
    if(stocked==from->goods_kg.end() || stocked->second<proposed_kg)
        return std::nullopt;
    // Inspect real volume limits; native booking/physical transfer will
    // recheck all proof, custody, reservations and current location.
    const auto& catalogue=inventory::Catalogue::standard();
    if(!catalogue.contains(cargo_key))return std::nullopt;
    const auto liters=catalogue.liters_per_unit(cargo_key);
    if(liters<=0 || proposed_kg>
       std::numeric_limits<std::int64_t>::max()/liters)
        return std::nullopt;
    const auto size=proposed_kg*liters;
    const auto carrier_hold=world.ship_inventory_snapshot(carrier_ship);
    const auto receiver_bin=world.storage_inventory_snapshot(
        recipient_need.destination);
    if(carrier_hold.amount(cargo_key)!=0 ||
       carrier_hold.free_liters()<size ||
       receiver_bin.free_liters()<size)
        return std::nullopt;
    ManufacturedConsignmentOffer offer{};
    offer.declared_need=recipient_need;
    offer.declared_receiver=declared_receiver;
    offer.observed_tick=world.tick();
    offer.observed_state_hash=world.state_hash();
    offer.observed_source_kg=stocked->second;
    offer.observed_destination_free_liters=receiver_bin.free_liters();
    offer.native.id=consignment_id;
    offer.native.source_ship=source_ship;
    offer.native.carrier_ship=carrier_ship;
    offer.native.receiving_ship=receiving_ship;
    offer.native.origin_site=origin_site;
    offer.native.destination_site=recipient_need.destination;
    offer.native.source_owner=source_owner;
    offer.native.carrier_owner=carrier_owner;
    offer.native.receiving_owner=declared_receiver;
    offer.native.destination_system=destination->system_id;
    offer.native.item_index=native_index;
    offer.native.quantity_kg=proposed_kg;
    offer.native.factory_return_command=original_factory_return_command;
    offer.native.load_command=native_load_command;
    offer.native.unload_command=native_unload_command;
    offer.native.require_dispatch=true; // three grants then carrier dispatch
    return offer;
}

// This is a TRUSTED-HOST adapter. The authenticated source owner's session
// is passed to the existing CanonicalShippingHost; it validates real v53
// manufacture proof + region/owner authority + native immutable contract.
// No completion is claimed: the carrier still needs three distinct durable
// approvals, dispatch, real cargo loading, navigation, unloading and
// carrier/receiver settlement witnesses.
inline bool book_manufactured_consignment(
    shipping::CanonicalShippingHost& native_host,
    PersistentWorld& original_world,
    const ManufacturedConsignmentOffer& proposal,
    PlayerId authenticated_source_session,
    bool trusted_host_verified_recipient_request) {
    if(!trusted_host_verified_recipient_request ||
       authenticated_source_session!=proposal.native.source_owner ||
       !proposal.native.require_dispatch ||
       proposal.declared_receiver!=proposal.native.receiving_owner ||
       proposal.observed_tick!=original_world.world().tick() ||
       proposal.observed_state_hash!=original_world.world().state_hash())
        return false;
    const auto live=inspect_manufactured_consignment(
        original_world.world(),proposal.declared_need,
        proposal.declared_receiver,true,
        proposal.native.source_ship,proposal.native.carrier_ship,
        proposal.native.receiving_ship,proposal.native.origin_site,
        proposal.native.source_owner,proposal.native.carrier_owner,
        proposal.native.factory_return_command,proposal.native.load_command,
        proposal.native.unload_command,proposal.native.id,
        proposal.native.quantity_kg);
    if(!live || live->native.destination_system!=proposal.native.destination_system ||
       live->observed_source_kg!=proposal.observed_source_kg ||
       live->observed_destination_free_liters!=
           proposal.observed_destination_free_liters)
        return false;
    // Do not catch or launder native failures. Invalid production receipts,
    // region handoffs, duplicate commands or source authentication MUST
    // propagate as refusal from the trusted original host.
    native_host.book(live->native,authenticated_source_session);
    return true;
}
} // namespace galaxy::logistics05
