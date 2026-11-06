#pragma once

// WORK-01B: capability descriptors and deterministic read-only planning.
// This layer never owns resources, workers, transports or ship inventories.
// Only original Simulation methods may commit real physical effects.
#include "galaxy/WorkCapacity.hpp"
#include "galaxy/WorkPolicy.hpp"
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
    UnsupportedExecutor=6,YardUnavailable=7,
    AssemblySlipBusy=8,BuilderUnavailable=9,WorkerOnNativeJob=10,
    EmergencyHold=11,FundingNotReady=12
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
    PolicyClass policy=PolicyClass::NewDiscretionaryFunding;
    std::array<std::uint64_t,2> reserved_crew{};
    std::uint64_t reserved_yard=0;
    std::array<std::uint64_t,2> reserved_builders{};
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
    std::uint64_t tick,
    const NativeCapacityContext& capacity={},
    const WorkPolicyContext& policy={}) {
    // Index existing build receipts and assigned construction workers ONCE.
    // This is derived from native sources at the start of a world tick.
    const auto capacity_index=index_native_capacity(capacity);
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
        p.policy=policy_class(contract->second.stage,
            d.commissioning_crew[0]!=0);
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
        if(p.gate==Gate::Ready){
            switch(inspect_policy(policy,d,contract->second)){
            case PolicyHold::None:break;
            case PolicyHold::LifeSupportEmergency:
                p.gate=Gate::EmergencyHold;break;
            case PolicyHold::PhysicalFundingUnavailable:
                p.gate=Gate::FundingNotReady;break;
            }
        }

        // Only the supplier's actual one-slip yard may accept new funding.
        // Occupancy comes from unfinished native FreighterBuildOrder records.
        // The context is required in production; absent context retains
        // compatibility with pure WORK-01B.1 read-model callers.
        if(p.gate==Gate::Ready &&
           contract->second.stage==ShipyardCustomerStage::Accepted &&
           capacity.yards){
            const auto yard=inspect_shipyard(
                contract->second,npcs,capacity,capacity_index);
            switch(yard.status){
            case YardCapacityStatus::MissingOrForeignYard:
            case YardCapacityStatus::NotBuilt:
                p.gate=Gate::YardUnavailable;break;
            case YardCapacityStatus::BusySlip:
                p.gate=Gate::AssemblySlipBusy;break;
            case YardCapacityStatus::UnqualifiedBuilders:
                p.gate=Gate::BuilderUnavailable;break;
            case YardCapacityStatus::Available:
                p.reserved_yard=yard.yard_id;
                p.reserved_builders=yard.approved_builders;
                break;
            }
        }
        // After physical delivery, commission exactly the buyer-approved
        // REAL crew; never auto-recruit other identities or steal yard labor.
        // Do not pre-reserve crew while a 12-tick build is underway.
        if(p.gate==Gate::Ready &&
           contract->second.stage==ShipyardCustomerStage::Delivered &&
           d.commissioning_crew[0]){
            bool already_commissioned=false;
            if(capacity.builds){
                const auto build=capacity.builds->find(
                    contract->second.build_order);
                already_commissioned=build!=capacity.builds->end() &&
                    build->second.commissioned() &&
                    build->second.commissioning_crew==d.commissioning_crew &&
                    build->second.provisions_kg_each==d.provisions_kg_each;
            }
            if(!already_commissioned){
                for(const auto member:d.commissioning_crew){
                    const auto qual=qualify_ship_worker(member,d.colony_ship,
                        npcs,capacity,capacity_index,true);
                    if(qual!=Qualification::Qualified){
                        p.gate=qual==Qualification::ExistingNativeAssignment?
                            Gate::WorkerOnNativeJob:Gate::WorkerUnavailable;
                        break;
                    }
                }
                if(p.gate==Gate::Ready)
                    p.reserved_crew=d.commissioning_crew;
            }
        }
        rows.push_back(p);
    }
    std::sort(rows.begin(),rows.end(),[&](const auto& a,const auto& b){
        if(a.priority!=b.priority)return a.priority>b.priority;
        return a.id<b.id;
    });
    // Stage remains absolute priority. On equal age/physical-stage bands,
    // rotate ties so old saturated requests cannot hold scarce slots
    // forever; all ordering derives from the committed world tick.
    fair_rotate_ties(rows,tick,policy.fairness_quantum_ticks);
    // One selection per physical yard slot and named worker per tick.
    // This selection is EPHEMERAL; the native command rechecks everything
    // before consuming inventory or moving an NPC.
    std::set<std::uint64_t> claimed_workers,claimed_slips;
    for(auto& row:rows){
        if(!row.can_execute())continue;
        const auto& workers=row.reserved_crew[0]?
            row.reserved_crew:row.reserved_builders;
        if(row.reserved_yard && claimed_slips.count(row.reserved_yard)){
            row.gate=Gate::AssemblySlipBusy;
        }else if((workers[0] && claimed_workers.count(workers[0])) ||
                 (workers[1] && claimed_workers.count(workers[1]))){
            row.gate=Gate::WorkerReserved;
        }
        if(!row.can_execute()){
            row.reserved_crew={};
            row.reserved_builders={};
            row.reserved_yard=0;
            continue;
        }
        if(row.reserved_yard)claimed_slips.insert(row.reserved_yard);
        for(const auto worker:workers)if(worker)
            claimed_workers.insert(worker);
    }
    return rows;
}
} // namespace galaxy::work
