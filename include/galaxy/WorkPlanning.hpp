#pragma once

// WORK-01B: capability descriptors and deterministic read-only planning.
// This layer never owns resources, workers, transports or ship inventories.
// Only original Simulation methods may commit real physical effects.
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace galaxy::work {

enum class RequestSchema : std::uint8_t {
    ExistingLocalDelivery=1,
    ExistingFreightLeg=2,
    ExistingShipyardConstruction=3,
    ExistingFreighterBuild=4,
    AuthorizedCustomerContract=5
};
enum class ResultSchema : std::uint8_t {
    LocalDeliveryReceipt=1,
    SealedFreightArrivalReceipt=2,
    OperationalShipyardReceipt=3,
    ManufacturedHullReceipt=4,
    CustomerHullAndCommissionReceipt=5
};
struct Capability {
    WorkTaskKind kind;
    RequestSchema request;
    ResultSchema result;
    std::uint8_t named_workers;
    std::uint8_t native_slots;
    bool delegated_executor;
};
// This is a registry over existing native commands and immutable receipts.
// A false executor is NOT an invitation to invent an alternative work path.
inline constexpr std::array<Capability,5> capabilities{{
    {WorkTaskKind::LocalDelivery,RequestSchema::ExistingLocalDelivery,
        ResultSchema::LocalDeliveryReceipt,1,1,false},
    {WorkTaskKind::IntercolonyFreight,RequestSchema::ExistingFreightLeg,
        ResultSchema::SealedFreightArrivalReceipt,0,1,false},
    {WorkTaskKind::ShipyardConstruction,RequestSchema::ExistingShipyardConstruction,
        ResultSchema::OperationalShipyardReceipt,2,1,false},
    {WorkTaskKind::FreighterBuild,RequestSchema::ExistingFreighterBuild,
        ResultSchema::ManufacturedHullReceipt,2,1,false},
    {WorkTaskKind::CustomerShipOrder,RequestSchema::AuthorizedCustomerContract,
        ResultSchema::CustomerHullAndCommissionReceipt,2,1,true}
}};
constexpr const Capability* find_capability(WorkTaskKind kind) noexcept {
    for(const auto& c:capabilities)if(c.kind==kind)return &c;
    return nullptr;
}

enum class Gate : std::uint8_t {
    Ready=0,Dependency=1,SupplierApproval=2,
    WorkerUnavailable=3,WorkerReserved=4,MissingSource=5,
    UnsupportedExecutor=6
};
struct PlannedDirective {
    std::uint64_t id=0;
    PlayerId owner=0;
    EntityId colony_ship=0;
    std::uint64_t source_order=0;
    std::uint64_t dependency=0;
    std::uint32_t dependency_depth=0;
    // Derived only from durable owner intent and original source stage.
    std::uint32_t priority=0;
    Gate gate=Gate::Ready;
    std::array<std::uint64_t,2> reserved_crew{};
    bool can_execute() const noexcept {return gate==Gate::Ready;}
};
// Every planner call starts from committed canonical records. There is NO
// persistent shadow queue, resource escrow, second workforce or synthetic
// successful receipt. Higher native stage takes precedence, then aging,
// then stable posted-tick / ID ordering for same-stage fairness.
inline std::vector<PlannedDirective> plan(
    const std::map<std::uint64_t,WorkDirective>& directives,
    const std::map<std::uint64_t,ShipyardCustomerOrder>& contracts,
    const std::map<std::uint64_t,NpcRecord>& npcs,
    std::uint64_t tick) {
    std::vector<PlannedDirective> rows;
    rows.reserve(directives.size());
    for(const auto& [id,d]:directives){
        if(d.status!=WorkDirectiveStatus::Active)continue;
        PlannedDirective p{};
        p.id=id;
        p.owner=d.owner;
        p.colony_ship=d.colony_ship;
        p.source_order=d.source_order;
        p.dependency=d.requires_directive;
        // Older work wins within its current physical stage; this aging is
        // finite and cannot overrule genuine higher-stage work.
        const auto age=tick>=d.posted_tick?tick-d.posted_tick:0;
        p.priority=static_cast<std::uint32_t>(std::min<std::uint64_t>(age,999));
        const auto cap=find_capability(d.kind);
        if(!cap || !cap->delegated_executor){
            p.gate=Gate::UnsupportedExecutor;
            rows.push_back(p);continue;
        }
        const auto contract=contracts.find(d.source_order);
        if(contract==contracts.end() ||
           contract->second.customer_owner!=d.owner ||
           contract->second.customer_ship!=d.colony_ship){
            p.gate=Gate::MissingSource;
            rows.push_back(p);continue;
        }
        switch(contract->second.stage){
        case ShipyardCustomerStage::Requested:
            p.priority+=1000;break;
        case ShipyardCustomerStage::Accepted:
            p.priority+=2000;break;
        case ShipyardCustomerStage::Funded:
            p.priority+=3000;break;
        case ShipyardCustomerStage::Delivered:
            p.priority+=4000;break;
        }
        std::uint64_t prerequisite=d.requires_directive;
        std::set<std::uint64_t> visited;
        while(prerequisite){
            if(!visited.insert(prerequisite).second ||
               p.dependency_depth>=directives.size()){
                p.gate=Gate::Dependency;break;
            }
            ++p.dependency_depth;
            const auto it=directives.find(prerequisite);
            if(it==directives.end() || it->second.owner!=d.owner ||
               it->second.colony_ship!=d.colony_ship ||
               it->second.status!=WorkDirectiveStatus::Fulfilled){
                p.gate=Gate::Dependency;break;
            }
            prerequisite=it->second.requires_directive;
        }
        if(p.gate!=Gate::Ready){rows.push_back(p);continue;}
        if(contract->second.stage==ShipyardCustomerStage::Requested)
            p.gate=Gate::SupplierApproval;
        // Only final, near-berth commissioning may reserve two named crew.
        // Funding a future build must not immobilize those NPCs for 12 ticks.
        if(p.gate==Gate::Ready &&
           contract->second.stage==ShipyardCustomerStage::Delivered &&
           d.commissioning_crew[0]){
            for(const auto member:d.commissioning_crew){
                const auto n=npcs.find(member);
                if(!member || n==npcs.end() ||
                   n->second.home_ship!=d.colony_ship ||
                   n->second.location!=d.colony_ship ||
                   n->second.assigned_site!=d.colony_ship ||
                   n->second.role!=NpcRole::Ship ||
                   n->second.transport_id || n->second.mentor_id){
                    p.gate=Gate::WorkerUnavailable;break;
                }
            }
            if(p.gate==Gate::Ready)p.reserved_crew=d.commissioning_crew;
        }
        rows.push_back(p);
    }
    std::sort(rows.begin(),rows.end(),[&](const auto& a,const auto& b){
        if(a.priority!=b.priority)return a.priority>b.priority;
        const auto& ad=directives.at(a.id);
        const auto& bd=directives.at(b.id);
        if(ad.posted_tick!=bd.posted_tick)return ad.posted_tick<bd.posted_tick;
        return a.id<b.id;
    });
    // Non-authoritative per-tick reservations prevent simultaneous selection
    // of one named NPC by two coordinator directives. Original native
    // commissioning performs independent final custody/supply checks.
    std::set<std::uint64_t> claimed;
    for(auto& row:rows){
        if(!row.can_execute() || !row.reserved_crew[0])continue;
        if(claimed.count(row.reserved_crew[0]) ||
           claimed.count(row.reserved_crew[1])){
            row.gate=Gate::WorkerReserved;
            row.reserved_crew={};
        }else{
            claimed.insert(row.reserved_crew[0]);
            claimed.insert(row.reserved_crew[1]);
        }
    }
    return rows;
}
} // namespace galaxy::work
