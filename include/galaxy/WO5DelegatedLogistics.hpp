#pragma once
// WO5-D: owner-approved contract/delegation facade over ORIGINAL persistent
// LocalDeliveryOrder, IntercolonyFreightLeg, WorkDirective, physical pilot,
// transport manifest and native completed arrival. NO new state/scheduler,
// free NPC, currency, synthetic carrier, shadow reservation or credit.
#include "galaxy/WO5FreightEvidence.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace galaxy::logistics05 {

struct DelegationPolicy {
    // The user may grant autonomy over already-posted physical transport.
    // These switches never create orders or permission for a new shipment.
    bool owner_approved=false;
    bool permit_native_pilot_selection=true;
    bool permit_existing_carrier_autodelivery=true;
};
struct DelegatedContractOffer {
    PlayerId owner=0;
    EntityId origin_ship=0;
    std::uint64_t native_order_id=0;
    std::uint64_t pickup_directive=0,freight_directive=0;
    EntityId real_carrier=0,real_receiver=0;
    PlanetarySiteKey site{};
    ShuttleCargo cargo=ShuttleCargo::Nutrients;
    std::int64_t kilograms=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
    DelegationPolicy policy{};
    std::uint64_t currently_assigned_pilot=0;
    std::uint64_t eligible_native_pilots=0;
    bool pickup_already_delegated=false,freight_already_delegated=false;
};

inline bool matches_native_directive(const WorkDirective& d,
    const DelegatedContractOffer& offer,WorkTaskKind kind,
    std::uint64_t dependency) {
    return d.owner==offer.owner && d.colony_ship==offer.origin_ship &&
           d.kind==kind && d.source_order==offer.native_order_id &&
           d.requires_directive==dependency &&
           d.status!=WorkDirectiveStatus::Cancelled;
}

// Original native simulation preflight can be repeated after any restart;
// the stable original directive identities prove whether its first half was
// already committed. No assumptions about long-running flight progress.
inline std::optional<DelegatedContractOffer> inspect_native_contract(
    const Simulation& world,PlayerId owner,EntityId origin_ship,
    std::uint64_t order_id,std::uint64_t pickup_directive,
    std::uint64_t freight_directive,DelegationPolicy policy) {
    if(!policy.owner_approved || !policy.permit_native_pilot_selection ||
       !policy.permit_existing_carrier_autodelivery ||
       !owner || owner==Simulation::kServerAuthority ||
       !origin_ship || !order_id || order_id>=(std::uint64_t{1}<<50) ||
       !pickup_directive || !freight_directive ||
       pickup_directive>=freight_directive ||
       freight_directive>=(std::uint64_t{1}<<50))
        return std::nullopt;
    const auto pickup=world.local_delivery_board().find(order_id);
    const auto freight=world.intercolony_freight().find(order_id);
    const auto* original=world.find(origin_ship);
    if(!original || original->type!=EntityType::Ship ||
       original->owner_id!=owner ||
       !world.colony_ship(origin_ship) || !world.life_support(origin_ship) ||
       pickup==world.local_delivery_board().end() ||
       freight==world.intercolony_freight().end())
        return std::nullopt;
    const auto& p=pickup->second;
    const auto& f=freight->second;
    if(p.owner!=owner || p.board_ship!=origin_ship ||
       p.kilograms<=0 || p.kilograms>1000 ||
       p.stage==LocalDeliveryStage::Cancelled ||
       f.owner!=owner || f.source_ship!=origin_ship ||
       f.order_id!=order_id || f.kilograms!=p.kilograms ||
       f.cargo!=p.cargo || !f.carrier_ship || !f.destination_ship ||
       f.carrier_ship==origin_ship || f.destination_ship==origin_ship ||
       f.carrier_ship==f.destination_ship ||
       f.stage==IntercolonyFreightStage::Delivered)
        return std::nullopt;
    // No authorization to use an imagined shuttle, crew or carrier.
    const auto site=world.planetary_sites().find(p.site);
    const auto sh=world.planetary_shuttles().find(p.site);
    const auto* carrier=world.find(f.carrier_ship);
    const auto* receiver=world.find(f.destination_ship);
    if(site==world.planetary_sites().end() ||
       sh==world.planetary_shuttles().end() ||
       site->second.owner!=owner ||
       site->second.source_ship!=origin_ship ||
       site->second.spaceport_phase!=3 ||
       sh->second.owner!=owner || sh->second.source_ship!=origin_ship ||
       !carrier || !receiver ||
       carrier->owner_id!=owner || receiver->owner_id!=owner ||
       !world.colony_ship(f.carrier_ship) ||
       !world.colony_ship(f.destination_ship) ||
       !world.life_support(f.carrier_ship) ||
       !world.life_support(f.destination_ship) ||
       !world.colony_ship(f.carrier_ship)->crew ||
       !carrier->fitting.exploration_scanner())
        return std::nullopt;
    DelegatedContractOffer offer{};
    offer.owner=owner;offer.origin_ship=origin_ship;
    offer.native_order_id=order_id;
    offer.pickup_directive=pickup_directive;
    offer.freight_directive=freight_directive;
    offer.real_carrier=f.carrier_ship;offer.real_receiver=f.destination_ship;
    offer.site=p.site;offer.cargo=p.cargo;offer.kilograms=p.kilograms;
    offer.observed_tick=world.tick();offer.observed_state_hash=world.state_hash();
    offer.policy=policy;
    offer.currently_assigned_pilot=sh->second.pilot_npc;
    // This is a read-only visible count of original *available* named crew,
    // not a promise that any will survive or accept the next shift.
    for(const auto& [npc,person]:world.npc_records())
        if(person.home_ship==origin_ship &&
           person.location==origin_ship &&
           person.assigned_site==origin_ship &&
           person.role==NpcRole::Ship &&
           world.npc_available_for_duty(npc))
            ++offer.eligible_native_pilots;
    if(!offer.currently_assigned_pilot &&
       !offer.eligible_native_pilots &&
       p.stage==LocalDeliveryStage::Posted)return std::nullopt;
    const auto& directives=world.work_directives();
    const auto first=directives.find(pickup_directive);
    const auto second=directives.find(freight_directive);
    if(first!=directives.end()){
        if(!matches_native_directive(first->second,offer,
               WorkTaskKind::LocalDelivery,0))return std::nullopt;
        offer.pickup_already_delegated=true;
    }else if(p.stage==LocalDeliveryStage::Complete){
        // Native does not permit creating an already-completed pickup
        // directive; only a previously posted one may resume here.
        return std::nullopt;
    }
    if(second!=directives.end()){
        if(!offer.pickup_already_delegated ||
           !matches_native_directive(second->second,offer,
               WorkTaskKind::IntercolonyFreight,pickup_directive))
            return std::nullopt;
        offer.freight_already_delegated=true;
    }
    // Test precisely the original validation and duplicate-source rules on
    // a throwaway copy. Client proposals cannot bypass native admission.
    auto preview=world;
    if(!offer.pickup_already_delegated &&
       !preview.delegate_transport_work_order(
          owner,origin_ship,pickup_directive,WorkTaskKind::LocalDelivery,
          order_id))
        return std::nullopt;
    if(!offer.freight_already_delegated &&
       !preview.delegate_transport_work_order(
          owner,origin_ship,freight_directive,WorkTaskKind::IntercolonyFreight,
          order_id,pickup_directive))
        return std::nullopt;
    return offer;
}
enum class DelegationResult : std::uint8_t {
    Refused=0,FirstDirectiveCommitted=1,AllOriginalDirectivesCommitted=2
};
struct DelegationReceipt {
    DelegationResult status=DelegationResult::Refused;
    std::uint64_t pickup_directive=0,freight_directive=0;
    std::uint64_t original_generation=0,world_hash=0;
};

// Two original SQLite transactions, NOT a falsely claimed atomic pair.
// If the second write crashes, the first is durable. Caller re-observes
// original source and retries SAME two IDs; native duplicate prevention
// and verified dependency make the continuation idempotent.
inline DelegationReceipt commit_native_contract(
    PersistentWorld& persistent,const DelegatedContractOffer& offered,
    bool trusted_host_authenticated_owner_approval,
    FailPoint second_directive_fault=FailPoint::None) {
    DelegationReceipt receipt{};
    receipt.pickup_directive=offered.pickup_directive;
    receipt.freight_directive=offered.freight_directive;
    if(!trusted_host_authenticated_owner_approval ||
       !offered.policy.owner_approved ||
       offered.observed_tick!=persistent.world().tick() ||
       offered.observed_state_hash!=persistent.world().state_hash())
        return receipt;
    const auto fresh=inspect_native_contract(
        persistent.world(),offered.owner,offered.origin_ship,
        offered.native_order_id,offered.pickup_directive,
        offered.freight_directive,offered.policy);
    if(!fresh || fresh->real_carrier!=offered.real_carrier ||
       fresh->real_receiver!=offered.real_receiver ||
       fresh->site!=offered.site ||
       fresh->cargo!=offered.cargo ||
       fresh->kilograms!=offered.kilograms)
        return receipt;
    if(!fresh->pickup_already_delegated){
        if(!persistent.delegate_transport_work_order(
             offered.owner,offered.origin_ship,offered.pickup_directive,
             WorkTaskKind::LocalDelivery,offered.native_order_id))
            return receipt;
    }
    receipt.status=DelegationResult::FirstDirectiveCommitted;
    receipt.original_generation=persistent.generation();
    receipt.world_hash=persistent.world().state_hash();
    const auto after=inspect_native_contract(
        persistent.world(),offered.owner,offered.origin_ship,
        offered.native_order_id,offered.pickup_directive,
        offered.freight_directive,offered.policy);
    if(!after)return receipt; // live state changed between original commits
    if(!after->freight_already_delegated &&
       !persistent.delegate_transport_work_order(
           offered.owner,offered.origin_ship,offered.freight_directive,
           WorkTaskKind::IntercolonyFreight,offered.native_order_id,
           offered.pickup_directive,second_directive_fault))
        return receipt;
    receipt.status=DelegationResult::AllOriginalDirectivesCommitted;
    receipt.original_generation=persistent.generation();
    receipt.world_hash=persistent.world().state_hash();
    return receipt;
}
struct DelegatedContractStatus {
    PlayerId owner=0;
    std::uint64_t order_id=0,pickup_directive=0,freight_directive=0;
    WorkDirectiveStatus pickup_status=WorkDirectiveStatus::Cancelled;
    WorkDirectiveStatus freight_status=WorkDirectiveStatus::Cancelled;
    WorkBlock pickup_block=WorkBlock::None,freight_block=WorkBlock::None;
    LocalDeliveryStage native_pickup=LocalDeliveryStage::Posted;
    IntercolonyFreightStage native_freight=IntercolonyFreightStage::AwaitingPickup;
    std::uint64_t original_pilot=0,original_arrival_receipt=0;
    bool real_delivery_confirmed=false;
};
inline std::optional<DelegatedContractStatus> inspect_native_contract_status(
    const Simulation& world,PlayerId owner,std::uint64_t order_id,
    std::uint64_t pickup_directive,std::uint64_t freight_directive){
    const auto p=world.local_delivery_board().find(order_id);
    const auto f=world.intercolony_freight().find(order_id);
    const auto a=world.work_directives().find(pickup_directive);
    const auto b=world.work_directives().find(freight_directive);
    if(!owner || owner==Simulation::kServerAuthority ||
       p==world.local_delivery_board().end() ||
       f==world.intercolony_freight().end() ||
       a==world.work_directives().end() ||
       b==world.work_directives().end() ||
       p->second.owner!=owner || f->second.owner!=owner ||
       a->second.owner!=owner || b->second.owner!=owner ||
       a->second.kind!=WorkTaskKind::LocalDelivery ||
       b->second.kind!=WorkTaskKind::IntercolonyFreight ||
       a->second.source_order!=order_id ||
       b->second.source_order!=order_id ||
       a->second.colony_ship!=p->second.board_ship ||
       b->second.colony_ship!=p->second.board_ship ||
       b->second.requires_directive!=pickup_directive ||
       f->second.source_ship!=p->second.board_ship ||
       f->second.kilograms!=p->second.kilograms ||
       f->second.cargo!=p->second.cargo)return std::nullopt;
    DelegatedContractStatus out{};
    out.owner=owner;out.order_id=order_id;
    out.pickup_directive=pickup_directive;
    out.freight_directive=freight_directive;
    out.pickup_status=a->second.status;
    out.freight_status=b->second.status;
    out.pickup_block=a->second.blocked;
    out.freight_block=b->second.blocked;
    out.native_pickup=p->second.stage;
    out.native_freight=f->second.stage;
    out.original_pilot=p->second.assigned_pilot;
    out.original_arrival_receipt=f->second.arrival_receipt;
    out.real_delivery_confirmed=
        f->second.stage==IntercolonyFreightStage::Delivered &&
        f->second.arrival_receipt!=0 &&
        b->second.status==WorkDirectiveStatus::Fulfilled &&
        inspect_original_freight_evidence(world,owner,order_id)
            .has_value();
    return out;
}
// Revocation cancels *future original NPC delegation* only. It never
// reverses a physical launch or completed cargo movement, and does NOT
// silently cancel the underlying local/second-leg freight contract.
inline bool revoke_native_delegation(
    PersistentWorld& persistent,PlayerId owner,
    std::uint64_t pickup_directive,std::uint64_t freight_directive,
    bool trusted_host_authenticated_owner_approval,
    FailPoint fault=FailPoint::None){
    if(!trusted_host_authenticated_owner_approval ||
       !owner || owner==Simulation::kServerAuthority ||
       !pickup_directive || !freight_directive ||
       pickup_directive>=freight_directive)return false;
    const auto& directives=persistent.world().work_directives();
    const auto a=directives.find(pickup_directive);
    const auto b=directives.find(freight_directive);
    if(a==directives.end() || b==directives.end() ||
       a->second.owner!=owner || b->second.owner!=owner ||
       a->second.kind!=WorkTaskKind::LocalDelivery ||
       b->second.kind!=WorkTaskKind::IntercolonyFreight ||
       a->second.source_order!=b->second.source_order ||
       b->second.requires_directive!=pickup_directive ||
       a->second.colony_ship!=b->second.colony_ship)return false;
    bool changed=false;
    // Cancel the dependent remote action FIRST, so an interrupted
    // operation cannot leave it enabled after pickup is revoked.
    if(b->second.status==WorkDirectiveStatus::Active){
        if(!persistent.cancel_work_directive(owner,freight_directive,fault))
            return false;
        changed=true;
    }
    const auto& now=persistent.world().work_directives();
    if(now.at(pickup_directive).status==WorkDirectiveStatus::Active){
        if(!persistent.cancel_work_directive(owner,pickup_directive))
            return false;
        changed=true;
    }
    return changed;
}

// WO5-D native flight continuation for a PHYSICALLY loaded sealed cargo
// carrier, a true NPC aboard that hull, and the original receiver's
// celestial anchor. Autonomous navigation is never a free spacecraft or
// instant transfer. The separate original WorkDirective will only close
// after the actual completed celestial receipt and physical delivery.
struct NpcFreightFlightOffer {
    PlayerId owner=0;
    std::uint64_t original_order_id=0;
    EntityId carrier_ship=0,destination_ship=0;
    std::uint64_t destination_body=0;
    std::uint64_t native_flight_order=0,native_transit_request=0;
    std::uint64_t native_pilot_id=0;
    std::uint64_t observed_tick=0,observed_state_hash=0;
};
inline std::optional<NpcFreightFlightOffer> inspect_native_npc_freight_flight(
    const Simulation& world,PlayerId owner,std::uint64_t order_id,
    std::uint64_t flight_order,std::uint64_t transit_request,
    std::uint64_t named_npc_pilot=0){
    if(!owner || owner==Simulation::kServerAuthority ||
       !order_id || !flight_order || !transit_request ||
       flight_order>=(std::uint64_t{1}<<50) ||
       transit_request>=(std::uint64_t{1}<<50) ||
       flight_order==transit_request)
        return std::nullopt;
    const auto leg=world.intercolony_freight().find(order_id);
    if(leg==world.intercolony_freight().end() ||
       leg->second.owner!=owner ||
       leg->second.stage!=IntercolonyFreightStage::Loaded ||
       leg->second.arrival_receipt ||
       leg->second.carrier_ship==leg->second.destination_ship)
        return std::nullopt;
    const auto& f=leg->second;
    const auto destination=world.ship_frames().find(f.destination_ship);
    if(destination==world.ship_frames().end() ||
       !destination->second.anchor_body)
        return std::nullopt;
    const auto proof=inspect_original_freight_evidence(world,owner,order_id);
    if(!proof || proof->original_stage!=IntercolonyFreightStage::Loaded ||
       proof->physically_carried_kg!=f.kilograms ||
       proof->actual_arrival_proved)return std::nullopt;
    // Native owner, NPC duty, physical presence, transit admission,
    // scanner, life-support, route, duplicate-ID and fleet claims are all
    // checked by the exact original flight scheduler on a read-only copy.
    auto preview=world;
    if(!preview.queue_npc_flight(
       owner,f.carrier_ship,named_npc_pilot,
       destination->second.anchor_body,flight_order,transit_request,
       NpcFlightPurpose::Freight))
        return std::nullopt;
    const auto selected=preview.npc_flight_orders().find(flight_order);
    if(selected==preview.npc_flight_orders().end() ||
       !selected->second.pilot_npc)return std::nullopt;
    NpcFreightFlightOffer offer{};
    offer.owner=owner;offer.original_order_id=order_id;
    offer.carrier_ship=f.carrier_ship;
    offer.destination_ship=f.destination_ship;
    offer.destination_body=destination->second.anchor_body;
    offer.native_flight_order=flight_order;
    offer.native_transit_request=transit_request;
    offer.native_pilot_id=selected->second.pilot_npc;
    offer.observed_tick=world.tick();
    offer.observed_state_hash=world.state_hash();
    return offer;
}
inline bool commit_native_npc_freight_flight(
    PersistentWorld& persistent,const NpcFreightFlightOffer& offer,
    bool trusted_host_authenticated_owner_approval,
    FailPoint fail=FailPoint::None){
    if(!trusted_host_authenticated_owner_approval ||
       offer.observed_tick!=persistent.world().tick() ||
       offer.observed_state_hash!=persistent.world().state_hash())
        return false;
    const auto current=inspect_native_npc_freight_flight(
        persistent.world(),offer.owner,offer.original_order_id,
        offer.native_flight_order,offer.native_transit_request,
        offer.native_pilot_id);
    if(!current || current->carrier_ship!=offer.carrier_ship ||
       current->destination_ship!=offer.destination_ship ||
       current->destination_body!=offer.destination_body ||
       current->native_pilot_id!=offer.native_pilot_id)
        return false;
    return persistent.queue_npc_flight(
        offer.owner,offer.carrier_ship,offer.native_pilot_id,
        offer.destination_body,offer.native_flight_order,
        offer.native_transit_request,NpcFlightPurpose::Freight,fail);
}
} // namespace galaxy::logistics05
