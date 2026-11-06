#include "galaxy/WorkPlanning.hpp"
#include "galaxy/ColonySafety.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
using namespace galaxy::work;
#define CHECK(x) do { if(!(x)) throw std::runtime_error( \
    std::string("WORK-01B.3 line ")+std::to_string(__LINE__)+": "+#x); } while(false)

static NpcRecord worker(std::uint64_t id,EntityId ship) {
    NpcRecord n{};
    n.id=id;n.home_ship=ship;n.location=ship;
    n.assigned_site=ship;n.role=NpcRole::Ship;
    return n;
}
static ShipyardCustomerOrder contract(
    std::uint64_t id,EntityId buyer,ShipyardCustomerStage stage){
    ShipyardCustomerOrder c{};
    c.id=id;c.yard_order=900;c.build_order=id+20000;
    c.customer_owner=8;c.customer_ship=buyer;
    c.supplier_owner=7;c.supplier_ship=700;c.stage=stage;
    return c;
}
static WorkDirective directive(
    std::uint64_t id,std::uint64_t source,EntityId buyer,
    std::uint64_t posted,std::array<std::uint64_t,2> crew={}){
    WorkDirective d{};
    d.id=id;d.source_order=source;d.colony_ship=buyer;d.owner=8;
    d.kind=WorkTaskKind::CustomerShipOrder;d.posted_tick=posted;
    d.commissioning_crew=crew;
    return d;
}
static const PlannedDirective& get(const std::vector<PlannedDirective>& rows,
                                    std::uint64_t id){
    for(const auto& row:rows)if(row.id==id)return row;
    throw std::runtime_error("missing planned native work");
}
int main(){
    try{
        // Real, canonical one-slip yard and two assigned builders.
        std::map<std::uint64_t,ShipyardConstruction> yards;
        ShipyardConstruction yard{};
        yard.order_id=900;yard.owner=7;yard.colony_ship=700;
        yard.workers={701,702};yard.progress=8;yard.required=8;
        yards.emplace(900,yard);
        std::map<std::uint64_t,FreighterBuildOrder> builds;
        std::map<std::uint64_t,NpcRecord> npcs{
            {701,worker(701,700)},{702,worker(702,700)},
            {801,worker(801,800)},{802,worker(802,800)}
        };
        NativeCapacityContext capacity{&yards,&builds,
            [](std::uint64_t){return true;}};
        // Two hypothetical candidates contend for the same slip.
        // The native seller still refuses the second simultaneous Accepted
        // contract: this isolated planner test exercises defensive contention.
        std::map<std::uint64_t,ShipyardCustomerOrder> contracts{
            {200,contract(200,800,ShipyardCustomerStage::Accepted)},
            {201,contract(201,801,ShipyardCustomerStage::Accepted)}
        };
        std::map<std::uint64_t,WorkDirective> directives{
            {10,directive(10,200,800,1)},
            {11,directive(11,201,801,2)}
        };
        WorkPolicyContext policy{};
        policy.can_fund_contract=[](PlayerId p,std::uint64_t id){
            return p==8 && id==201;
        };
        auto rows=plan(directives,contracts,npcs,40,capacity,policy);
        CHECK(get(rows,10).gate==Gate::FundingNotReady);
        CHECK(get(rows,10).reserved_yard==0);
        CHECK(get(rows,11).gate==Gate::Ready);
        CHECK(get(rows,11).reserved_yard==900);
        CHECK(get(rows,11).reserved_builders[0]==701);
        CHECK(get(rows,10).priority>get(rows,11).priority);
        CHECK(get(rows,10).policy==PolicyClass::NewDiscretionaryFunding);

        // Emergency on buyer 800 pauses ONLY its elective payments.
        // Another owner-approved buyer 801 with real funding capacity
        // can use the supplier's still-free slot.
        policy.can_fund_contract=[](PlayerId,std::uint64_t){return true;};
        policy.critical_life_support=[](EntityId id){return id==800;};
        rows=plan(directives,contracts,npcs,41,capacity,policy);
        CHECK(get(rows,10).gate==Gate::EmergencyHold);
        CHECK(get(rows,10).reserved_yard==0);
        CHECK(get(rows,11).gate==Gate::Ready);
        CHECK(get(rows,11).reserved_yard==900);

        // A paid original build/physical handover cannot be undone by a
        // discretionary scheduling policy; optional crew movement CAN wait.
        contracts.emplace(202,contract(202,800,ShipyardCustomerStage::Funded));
        directives.emplace(12,directive(12,202,800,1));
        contracts.emplace(203,contract(203,800,ShipyardCustomerStage::Delivered));
        directives.emplace(13,directive(13,203,800,1,{801,802}));
        rows=plan(directives,contracts,npcs,42,capacity,policy);
        CHECK(get(rows,12).gate==Gate::Ready);
        CHECK(get(rows,12).policy==PolicyClass::CommittedConstructionOrHandover);
        CHECK(get(rows,13).gate==Gate::EmergencyHold);
        CHECK(get(rows,13).policy==PolicyClass::FinalStaffing);
        CHECK(get(rows,13).reserved_crew[0]==0);

        // With critical conditions cleared, the same durable approved
        // directives become eligible again; nothing was permanently
        // cancelled or secretly escalated into an emergency rescue action.
        policy.critical_life_support=[](EntityId){return false;};
        rows=plan(directives,contracts,npcs,43,capacity,policy);
        CHECK(get(rows,13).gate==Gate::Ready);
        CHECK(get(rows,13).reserved_crew[0]==801);

        // Once both accepted funding requests have waited >=999 ticks,
        // they share the same physical-stage/age band and rotate selection
        // deterministically. Ties cannot starve a later approved request.
        directives.erase(12);directives.erase(13);
        contracts.erase(202);contracts.erase(203);
        rows=plan(directives,contracts,npcs,1004,capacity,policy);
        CHECK(get(rows,10).priority==get(rows,11).priority);
        CHECK(rows.front().id==10);
        CHECK(get(rows,10).gate==Gate::Ready);
        CHECK(get(rows,11).gate==Gate::AssemblySlipBusy);
        rows=plan(directives,contracts,npcs,1005,capacity,policy);
        CHECK(rows.front().id==11);
        CHECK(get(rows,11).gate==Gate::Ready);
        CHECK(get(rows,10).gate==Gate::AssemblySlipBusy);
        // Restoring the same committed tick always recreates exact order.
        const auto replay=plan(directives,contracts,npcs,1005,capacity,policy);
        CHECK(replay.size()==rows.size());
        for(std::size_t i=0;i<rows.size();++i){
            CHECK(replay[i].id==rows[i].id);
            CHECK(replay[i].priority==rows[i].priority);
            CHECK(replay[i].gate==rows[i].gate);
            CHECK(replay[i].reserved_yard==rows[i].reserved_yard);
        }
        // Cancellation releases the slot and does not refund an unrelated
        // real physical payment or alter the original buyer entitlement.
        directives.at(11).status=WorkDirectiveStatus::Cancelled;
        rows=plan(directives,contracts,npcs,1005,capacity,policy);
        CHECK(rows.size()==1);
        CHECK(rows[0].id==10);
        CHECK(rows[0].gate==Gate::Ready);

        // Real existing colony safety code is the sole risk classifier.
        Simulation safe(991);
        const auto stocked=safe.add_colony_ship(8,{0,0},100,6);
        safe.provision_colony_genesis(stocked,200,200,200,200,200);
        const auto safe_risk=colony_safety::assess(safe,stocked,stocked);
        CHECK(safe_risk.risk==colony_safety::Risk::Safe);
        Simulation emergency(992);
        const auto low_stock=emergency.add_colony_ship(8,{0,0},100,200);
        emergency.provision_colony_genesis(low_stock,200,200,200,200,200);
        const auto urgent=colony_safety::assess(
            emergency,low_stock,low_stock);
        CHECK(urgent.owner==8);
        CHECK(urgent.population==200);
        CHECK(urgent.emergency_override);
        CHECK(urgent.risk==colony_safety::Risk::Critical ||
              urgent.risk==colony_safety::Risk::Emergency);

        // No work planner changes actual world stocks, emergency state,
        // NPC count, or committed snapshot bytes.
        const auto unchanged_safe=safe.canonical_state();
        const auto unchanged_urgent=emergency.canonical_state();
        (void)plan(directives,contracts,npcs,1006,capacity,policy);
        CHECK(safe.canonical_state()==unchanged_safe);
        CHECK(emergency.canonical_state()==unchanged_urgent);
        CHECK(npcs.size()==4);
        CHECK(builds.empty());
        CHECK(yards.size()==1);

        std::cout<<"PASS WORK-01B.3: real colony risk provenance, "
            "life-safety elective preemption, committed physical work "
            "continuity, funding readiness skip, fair tie rotation, "
            "cancel/restart determinism and no invented materials or NPCs\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL WORK-01B.3: "<<e.what()<<'\n';
        return 1;
    }
}
