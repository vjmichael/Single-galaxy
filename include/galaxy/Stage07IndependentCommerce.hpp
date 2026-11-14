#pragma once
// Stage 7 POP-09: original named resident proposes a REAL cross-owner,
 // physically manufactured consignment. All goods, factories, storage,
 // third-party contracts, host authentication, approvals, carrier movement,
 // settlement and legal custody remain solely in the original game.
#include "galaxy/Stage07ResidentEconomy.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace galaxy::stage7 {
struct ResidentManufacturedConsignment {
    PlayerId owner=0;
    EntityId colony=0;
    std::uint64_t resident=0,generation=0,tick=0,world_hash=0;
    shipping::ThirdPartyBooking original_contract{};
};

// Local opinion never invents products, prices, labor, credentials or freight.
// A genuine resident chooses to support a candidate only when the exact
// original physically produced stock still exists and all three original
// ship/warehouse owners are distinct.
inline std::optional<ResidentManufacturedConsignment>
decide_resident_manufactured_consignment(
        const PersistentWorld& world,PlayerId owner,EntityId home,
        std::uint64_t resident,
        const shipping::ThirdPartyBooking& contract) {
    const auto& sim=world.world();
    if(!world.generation() ||
       !authentic_resident(sim,owner,home,resident,home) ||
       contract.source_owner!=owner || contract.source_ship!=home ||
       contract.source_owner==contract.carrier_owner ||
       contract.source_owner==contract.receiving_owner ||
       contract.carrier_owner==contract.receiving_owner ||
       contract.source_ship==contract.carrier_ship ||
       contract.source_ship==contract.receiving_ship ||
       contract.carrier_ship==contract.receiving_ship ||
       contract.id.empty() || contract.id.size()>100 ||
       !contract.factory_return_command || !contract.load_command ||
       !contract.unload_command ||
       contract.quantity_kg<=0 || contract.quantity_kg>1000)
        return std::nullopt;
    const std::string item=contract.item_index==1?"aluminum_panels":
                           contract.item_index==2?"aluminum_frames":"";
    if(item.empty())return std::nullopt;
    const auto* sender=sim.find(contract.source_ship);
    const auto* carrier=sim.find(contract.carrier_ship);
    const auto* receiver=sim.find(contract.receiving_ship);
    const auto* source=sim.storage(contract.origin_site);
    const auto* destination=sim.storage(contract.destination_site);
    if(!sender || !carrier || !receiver || !source || !destination ||
       sender->owner_id!=owner ||
       carrier->owner_id!=contract.carrier_owner ||
       receiver->owner_id!=contract.receiving_owner ||
       receiver->system_id!=contract.destination_system ||
       source->source_ship!=home ||
       destination->source_ship!=contract.receiving_ship ||
       contract.origin_site==contract.destination_site)
        return std::nullopt;
    const auto stock=source->goods_kg.find(item);
    if(stock==source->goods_kg.end() ||
       stock->second<contract.quantity_kg)
        return std::nullopt;
    return ResidentManufacturedConsignment{
        owner,home,resident,world.generation(),sim.tick(),sim.state_hash(),
        contract};
}

// This is an independently owner-approved proposal, *not* an NPC power to
// approve a shipment. ThirdPartyRegistry::book checks the host's authenticated
// current principal, real original production receipt, physical warehouses,
// unique manifest, and original SQLite custody before posting a contract.
// All three owner approvals, real voyage, unload, settlement remain required.
inline bool book_resident_manufactured_consignment(
        PersistentWorld& world,shipping::ThirdPartyRegistry& original_registry,
        const ResidentManufacturedConsignment& intent,
        PlayerId authenticated_owner,bool independent_owner_approval) {
    if(!independent_owner_approval ||
       authenticated_owner!=intent.owner ||
       world.generation()!=intent.generation ||
       world.world().tick()!=intent.tick ||
       world.world().state_hash()!=intent.world_hash)
        return false;
    const auto fresh=decide_resident_manufactured_consignment(
        world,intent.owner,intent.colony,intent.resident,
        intent.original_contract);
    if(!fresh || fresh->generation!=intent.generation)
        return false;
    original_registry.book(intent.original_contract);
    original_registry.verify();
    return true;
}

inline bool resident_manufactured_delivery_verified(
        const PersistentWorld& world,
        const shipping::ThirdPartyRegistry& original_registry,
        const ResidentManufacturedConsignment& intent) {
    if(world.generation()<=intent.generation)return false;
    const auto& c=intent.original_contract;
    const std::string item=c.item_index==1?"aluminum_panels":
                           c.item_index==2?"aluminum_frames":"";
    const auto* destination=world.world().storage(c.destination_site);
    if(item.empty() || !destination ||
       destination->source_ship!=c.receiving_ship ||
       !world.world().has_command_id(c.load_command) ||
       !world.world().has_command_id(c.unload_command))
        return false;
    const auto stock=destination->goods_kg.find(item);
    if(stock==destination->goods_kg.end() ||
       stock->second<c.quantity_kg ||
       world.world().ship_inventory_snapshot(c.carrier_ship).amount(
           "good/"+item)!=0)
        return false;
    // The original independent registry checks the SPECIFIC immutable
    // contract, source/cargo/destination resource_ledger, actual jump,
    // three approvals, physical carrier and anti-duplication. Warehouse
    // quantities alone are never proof that this NPC's sale was delivered.
    original_registry.verify();
    return true;
}
} // namespace galaxy::stage7
