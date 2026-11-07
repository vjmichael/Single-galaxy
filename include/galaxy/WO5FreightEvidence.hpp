#pragma once
// WO5-B: observational verification of a REAL original intercolony shipment.
// The existing Simulation, original load/unload journal and celestial receipts
// remain the sole physical truth. No second mutable shipment balance/status.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>

namespace galaxy::logistics05 {
struct OriginalFreightEvidence {
    std::uint64_t original_order_id=0;
    PlayerId owner=0;
    EntityId origin_ship=0,carrier_ship=0,destination_ship=0;
    ShuttleCargo cargo=ShuttleCargo::Food;
    std::int64_t kilograms=0;
    LocalDeliveryStage physical_pickup_stage=LocalDeliveryStage::Posted;
    IntercolonyFreightStage original_stage=IntercolonyFreightStage::AwaitingPickup;
    std::uint64_t native_loaded_tick=0,original_arrival_receipt=0;
    std::int64_t physically_carried_kg=0;
    bool actual_arrival_proved=false;
};
inline std::optional<OriginalFreightEvidence> inspect_original_freight_evidence(
    const Simulation& world,PlayerId owner,std::uint64_t order_id) {
    const auto freight=world.intercolony_freight().find(order_id);
    const auto pickup=world.local_delivery_board().find(order_id);
    if(!owner || owner==Simulation::kServerAuthority || !order_id ||
       freight==world.intercolony_freight().end() ||
       pickup==world.local_delivery_board().end())
        return std::nullopt;
    const auto& f=freight->second;
    const auto& p=pickup->second;
    if(f.owner!=owner || p.owner!=owner ||
       p.id!=order_id || f.order_id!=order_id ||
       f.source_ship!=p.board_ship ||
       f.cargo!=p.cargo || f.kilograms!=p.kilograms ||
       !f.origin_body || f.kilograms<=0 || f.kilograms>1000 ||
       f.source_ship==f.carrier_ship ||
       f.source_ship==f.destination_ship ||
       f.carrier_ship==f.destination_ship)
        return std::nullopt;
    for(const auto ship:{f.source_ship,f.carrier_ship,f.destination_ship}){
        const auto* e=world.find(ship);
        if(!e || e->owner_id!=owner ||
           e->type!=EntityType::Ship || !world.colony_ship(ship))
            return std::nullopt;
    }
    if(f.cargo!=ShuttleCargo::Water && f.cargo!=ShuttleCargo::Food &&
       f.cargo!=ShuttleCargo::Nutrients)return std::nullopt;
    OriginalFreightEvidence view{};
    view.original_order_id=order_id;view.owner=owner;
    view.origin_ship=f.source_ship;view.carrier_ship=f.carrier_ship;
    view.destination_ship=f.destination_ship;view.cargo=f.cargo;
    view.kilograms=f.kilograms;view.physical_pickup_stage=p.stage;
    view.original_stage=f.stage;
    view.native_loaded_tick=f.loaded_tick;
    view.original_arrival_receipt=f.arrival_receipt;
    const auto code=f.cargo==ShuttleCargo::Water?"freight_water":
        f.cargo==ShuttleCargo::Food?"freight_food":"freight_nutrients";
    const auto carrier=world.colony_ship(f.carrier_ship);
    const auto held=carrier->goods_kg.find(code);
    const auto present=held==carrier->goods_kg.end()?0:held->second;
    if(present<0)return std::nullopt;
    view.physically_carried_kg=present;
    const auto manifest=world.intercolony_transport_manifest(order_id);
    if(!manifest || !manifest->valid() ||
       manifest->vehicle!=f.carrier_ship || manifest->owner!=owner)
        return std::nullopt;
    switch(f.stage) {
    case IntercolonyFreightStage::AwaitingPickup:
        // A plan is not a physically loaded vehicle or a remote delivery.
        if(f.loaded_tick || f.arrival_receipt || present ||
           p.stage==LocalDeliveryStage::Cancelled)return std::nullopt;
        return view;
    case IntercolonyFreightStage::Loaded: {
        if(!f.loaded_tick || f.loaded_tick>world.tick() ||
           f.arrival_receipt || p.stage!=LocalDeliveryStage::Complete ||
           present!=f.kilograms)
            return std::nullopt;
        const auto kind=f.cargo==ShuttleCargo::Water?
            transport::ResourceGood::Water:
            f.cargo==ShuttleCargo::Food?
            transport::ResourceGood::Food:
            transport::ResourceGood::Nutrients;
        if(!transport::contains_good(*manifest,transport::GoodKind::Resource,
                static_cast<std::uint8_t>(kind),
                static_cast<std::uint64_t>(f.kilograms)))
            return std::nullopt;
        return view;
    }
    case IntercolonyFreightStage::Delivered: {
        if(!f.loaded_tick || !f.arrival_receipt ||
           p.stage!=LocalDeliveryStage::Complete || present ||
           !manifest->contents.empty())
            return std::nullopt;
        const auto arrival=world.completed_celestial_transits().find(
            f.arrival_receipt);
        if(arrival==world.completed_celestial_transits().end() ||
           arrival->second.owner!=owner ||
           arrival->second.ship!=f.carrier_ship ||
           arrival->second.completed_tick<f.loaded_tick)
            return std::nullopt;
        view.actual_arrival_proved=true;
        return view;
    }
    }
    return std::nullopt;
}
} // namespace galaxy::logistics05
