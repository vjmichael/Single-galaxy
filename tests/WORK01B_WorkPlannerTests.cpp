#include "galaxy/WorkPlanning.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace galaxy;
using namespace galaxy::work;
#define CHECK(e) do { if(!(e)) throw std::runtime_error(     std::string("WORK-01B line ")+std::to_string(__LINE__)+": "+#e); } while(false)

static WorkDirective directive(std::uint64_t id,std::uint64_t contract,
    std::uint64_t posted,std::array<std::uint64_t,2> crew={},
    std::uint64_t dependency=0){
    WorkDirective d{};
    d.id=id;d.owner=8;d.colony_ship=800;
    d.kind=WorkTaskKind::CustomerShipOrder;
    d.source_order=contract;d.posted_tick=posted;
    d.commissioning_crew=crew;d.requires_directive=dependency;
    return d;
}
static ShipyardCustomerOrder contract(
    std::uint64_t id,ShipyardCustomerStage stage){
    ShipyardCustomerOrder c{};
    c.id=id;c.customer_owner=8;c.customer_ship=800;
    c.supplier_owner=7;c.supplier_ship=700;c.stage=stage;
    return c;
}
static NpcRecord worker(std::uint64_t id){
    NpcRecord n{};
    n.id=id;n.home_ship=800;n.location=800;
    n.assigned_site=800;n.role=NpcRole::Ship;
    return n;
}
static const PlannedDirective& find_row(
    const std::vector<PlannedDirective>& p,std::uint64_t id){
    for(const auto& v:p)if(v.id==id)return v;
    throw std::runtime_error("missing planned work");
}
int main(){
    try{
        CHECK(capabilities.size()==5);
        for(int i=1;i<=5;++i){
            const auto kind=static_cast<WorkTaskKind>(i);
            const auto* cap=find_capability(kind);
            CHECK(cap!=nullptr);
            CHECK(static_cast<int>(cap->request)==i);
            CHECK(static_cast<int>(cap->result)==i);
            CHECK(cap->native_slots==1);
            CHECK(cap->delegated_executor==(i==1 || i==2 || i==5));
        }
        CHECK(find_capability(static_cast<WorkTaskKind>(88))==nullptr);
        CHECK(find_capability(WorkTaskKind::ShipyardConstruction)->named_workers==2);

        std::map<std::uint64_t,WorkDirective> directives{
            {10,directive(10,100,1,{11,12})},
            {20,directive(20,200,5,{12,13})},
            {30,directive(30,300,0,{11,13})},
            {40,directive(40,400,1,{},10)}
        };
        std::map<std::uint64_t,ShipyardCustomerOrder> contracts{
            {100,contract(100,ShipyardCustomerStage::Delivered)},
            {200,contract(200,ShipyardCustomerStage::Delivered)},
            {300,contract(300,ShipyardCustomerStage::Accepted)},
            {400,contract(400,ShipyardCustomerStage::Accepted)}
        };
        std::map<std::uint64_t,NpcRecord> npcs{
            {11,worker(11)},{12,worker(12)},{13,worker(13)}
        };
        const auto before_directives=directives.size();
        const auto before_workers=npcs.size();

        // Already delivered work with the oldest consent wins and claims
        // its two REAL named workers only for the current native tick.
        auto rows=plan(directives,contracts,npcs,20);
        CHECK(rows.size()==4);
        CHECK(rows[0].id==10);
        CHECK(find_row(rows,10).gate==Gate::Ready);
        CHECK(find_row(rows,10).priority>find_row(rows,20).priority);
        CHECK(find_row(rows,10).reserved_crew[0]==11);
        CHECK(find_row(rows,20).gate==Gate::WorkerReserved);
        CHECK(find_row(rows,20).reserved_crew[0]==0);
        CHECK(find_row(rows,30).gate==Gate::Ready);
        CHECK(find_row(rows,30).reserved_crew[0]==0);
        CHECK(find_row(rows,40).gate==Gate::Dependency);
        CHECK(find_row(rows,40).dependency_depth==1);

        // A cancelled directive releases selection, but does not return
        // any physical resources: the planner cannot mutate inventories.
        directives.at(10).status=WorkDirectiveStatus::Cancelled;
        rows=plan(directives,contracts,npcs,21);
        CHECK(rows.size()==3);
        CHECK(find_row(rows,20).gate==Gate::Ready);
        CHECK(find_row(rows,20).reserved_crew[0]==12);
        CHECK(find_row(rows,40).gate==Gate::Dependency);

        // Prior dependency now has original-world fulfillment proof.
        directives.at(10).status=WorkDirectiveStatus::Fulfilled;
        rows=plan(directives,contracts,npcs,22);
        CHECK(find_row(rows,40).gate==Gate::Ready);
        CHECK(find_row(rows,40).dependency_depth==1);

        // Named NPCs cannot be assumed to be free when physically away
        // or carrying an active passenger transport assignment.
        npcs.at(12).transport_id=900;
        rows=plan(directives,contracts,npcs,23);
        CHECK(find_row(rows,20).gate==Gate::WorkerUnavailable);
        npcs.at(12).transport_id=0;
        npcs.at(12).location=701;
        rows=plan(directives,contracts,npcs,23);
        CHECK(find_row(rows,20).gate==Gate::WorkerUnavailable);
        npcs.at(12).location=800;

        // Supplier approval is never inferred, and the planning layer
        // cannot authorize buyer work against a foreign canonical source.
        contracts.at(300).stage=ShipyardCustomerStage::Requested;
        rows=plan(directives,contracts,npcs,24);
        CHECK(find_row(rows,30).gate==Gate::SupplierApproval);
        contracts.at(300).customer_owner=7;
        rows=plan(directives,contracts,npcs,24);
        CHECK(find_row(rows,30).gate==Gate::MissingSource);

        // Explicit typed registry never silently auto-executes other
        // original native systems before their own consent adapters exist.
        auto unsupported=directive(50,500,10);
        unsupported.kind=WorkTaskKind::ShipyardConstruction;
        directives.emplace(50,unsupported);
        rows=plan(directives,contracts,npcs,25);
        CHECK(find_row(rows,50).gate==Gate::UnsupportedExecutor);
        CHECK(directives.size()==before_directives+1);
        CHECK(npcs.size()==before_workers);

        std::cout<<"PASS WORK-01B: five typed capability schemas, stable "
            "stage priority and aging, prerequisite planning, real named-NPC "
            "reservation, cancel release, custody denial, foreign source "
            "denial, and no invented executor or inventory\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL WORK-01B: "<<e.what()<<'\n';
        return 1;
    }
}
