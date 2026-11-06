#include "galaxy/WorkPlanning.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
using namespace galaxy::work;
#define CHECK(x) do { if(!(x)) throw std::runtime_error( \
    std::string("WORK-01B.2 line ")+std::to_string(__LINE__)+": "+#x); } while(false)

static NpcRecord ship_npc(std::uint64_t id,EntityId ship) {
    NpcRecord n{};
    n.id=id;n.home_ship=ship;n.location=ship;
    n.assigned_site=ship;n.role=NpcRole::Ship;
    return n;
}
static ShipyardCustomerOrder customer(std::uint64_t id,
    std::uint64_t yard,ShipyardCustomerStage stage) {
    ShipyardCustomerOrder c{};
    c.id=id;c.customer_owner=8;c.customer_ship=800;
    c.supplier_owner=7;c.supplier_ship=700;
    c.yard_order=yard;c.build_order=id+20000;c.stage=stage;
    return c;
}
static WorkDirective delegation(std::uint64_t id,std::uint64_t source,
    std::uint64_t posted,std::array<std::uint64_t,2> crew={}) {
    WorkDirective d{};
    d.id=id;d.owner=8;d.colony_ship=800;
    d.kind=WorkTaskKind::CustomerShipOrder;
    d.source_order=source;d.posted_tick=posted;
    d.commissioning_crew=crew;
    return d;
}
static const PlannedDirective& row(
    const std::vector<PlannedDirective>& rows,std::uint64_t id) {
    for(const auto& r:rows)if(r.id==id)return r;
    throw std::runtime_error("missing work plan candidate");
}
int main() {
    try{
        std::map<std::uint64_t,NpcRecord> npcs{
            {701,ship_npc(701,700)},{702,ship_npc(702,700)},
            {801,ship_npc(801,800)},{802,ship_npc(802,800)},
            {803,ship_npc(803,800)}
        };
        std::map<std::uint64_t,ShipyardConstruction> yards;
        ShipyardConstruction yard{};
        yard.order_id=900;yard.owner=7;yard.colony_ship=700;
        yard.workers={701,702};yard.progress=8;yard.required=8;
        yards.emplace(900,yard);
        std::map<std::uint64_t,FreighterBuildOrder> builds;
        NativeCapacityContext ctx{&yards,&builds,
            [](std::uint64_t){return true;}};
        const auto contract=customer(100,900,ShipyardCustomerStage::Accepted);
        std::map<std::uint64_t,ShipyardCustomerOrder> contracts{
            {100,contract}
        };
        std::map<std::uint64_t,WorkDirective> directives{
            {10,delegation(10,100,1)}
        };

        auto indexed=index_native_capacity(ctx);
        auto cap=inspect_shipyard(contract,npcs,ctx,indexed);
        CHECK(cap.usable());
        CHECK(cap.physical_slips==1);
        CHECK(cap.occupied_slips==0);
        CHECK(cap.qualified_builders==2);
        CHECK(cap.approved_builders==yard.workers);
        auto p=plan(directives,contracts,npcs,50,ctx);
        CHECK(p.size()==1);
        CHECK(row(p,10).gate==Gate::Ready);
        CHECK(row(p,10).reserved_yard==900);
        CHECK(row(p,10).reserved_builders==yard.workers);
        CHECK(indexed.working_npcs.empty());

        // One physically unfinished hull occupies the ONLY native slip.
        FreighterBuildOrder unfinished{};
        unfinished.order_id=500;unfinished.yard_order=900;
        unfinished.source_ship=700;unfinished.owner=7;
        unfinished.progress=6;unfinished.required=12;
        builds.emplace(500,unfinished);
        indexed=index_native_capacity(ctx);
        cap=inspect_shipyard(contract,npcs,ctx,indexed);
        CHECK(cap.status==YardCapacityStatus::BusySlip);
        CHECK(cap.occupied_slips==1);
        CHECK(indexed.working_npcs.count(701)==1);
        CHECK(indexed.working_npcs.count(702)==1);
        p=plan(directives,contracts,npcs,51,ctx);
        CHECK(row(p,10).gate==Gate::AssemblySlipBusy);
        CHECK(row(p,10).reserved_yard==0);
        // Completion frees the actual slip; never fabricate a second one.
        builds.at(500).launched_ship=10001;
        p=plan(directives,contracts,npcs,52,ctx);
        CHECK(row(p,10).gate==Gate::Ready);
        CHECK(row(p,10).reserved_yard==900);

        // The yard cannot use absent/foreign/offsite/unqualified workers.
        npcs.at(701).role=NpcRole::Farm;
        p=plan(directives,contracts,npcs,53,ctx);
        CHECK(row(p,10).gate==Gate::BuilderUnavailable);
        npcs.at(701).role=NpcRole::Ship;
        npcs.at(702).location=703;
        p=plan(directives,contracts,npcs,54,ctx);
        CHECK(row(p,10).gate==Gate::BuilderUnavailable);
        npcs.at(702).location=700;
        npcs.at(702).transport_id=72;
        p=plan(directives,contracts,npcs,55,ctx);
        CHECK(row(p,10).gate==Gate::BuilderUnavailable);
        npcs.at(702).transport_id=0;

        // Actual medical/shuttle-duty availability comes from the native
        // world's existing duty method, not a synthetic skill counter.
        ctx.native_duty_available=[](std::uint64_t id){return id!=702;};
        p=plan(directives,contracts,npcs,56,ctx);
        CHECK(row(p,10).gate==Gate::BuilderUnavailable);
        CHECK(qualify_ship_worker(702,700,npcs,ctx,index_native_capacity(ctx),
              false)==Qualification::NativeDutyUnavailable);
        ctx.native_duty_available=[](std::uint64_t){return true;};

        yards.at(900).progress=7;
        p=plan(directives,contracts,npcs,57,ctx);
        CHECK(row(p,10).gate==Gate::YardUnavailable);
        yards.at(900).progress=8;
        yards.at(900).owner=33;
        p=plan(directives,contracts,npcs,58,ctx);
        CHECK(row(p,10).gate==Gate::YardUnavailable);
        yards.at(900).owner=7;

        // Even a malformed pair of simultaneously accepted requests
        // cannot both receive coordinator reservation for the same slip.
        contracts.emplace(101,customer(101,900,
            ShipyardCustomerStage::Accepted));
        directives.emplace(11,delegation(11,101,2));
        p=plan(directives,contracts,npcs,59,ctx);
        CHECK(row(p,10).gate==Gate::Ready);
        CHECK(row(p,11).gate==Gate::AssemblySlipBusy);
        CHECK(row(p,11).reserved_builders[0]==0);
        // Completing/cancelling higher-priority work releases the selection.
        directives.at(10).status=WorkDirectiveStatus::Cancelled;
        p=plan(directives,contracts,npcs,60,ctx);
        CHECK(row(p,11).gate==Gate::Ready);

        // Existing active construction or manufacturing crews are not
        // silently taken away from the BUYER for commissioning.
        ShipyardConstruction buyer_yard{};
        buyer_yard.order_id=901;buyer_yard.owner=8;
        buyer_yard.colony_ship=800;buyer_yard.workers={801,802};
        buyer_yard.progress=3;buyer_yard.required=8;
        yards.emplace(901,buyer_yard);
        contracts.emplace(102,customer(102,900,
            ShipyardCustomerStage::Delivered));
        directives.emplace(12,delegation(12,102,3,{801,802}));
        p=plan(directives,contracts,npcs,61,ctx);
        CHECK(row(p,12).gate==Gate::WorkerOnNativeJob);
        CHECK(row(p,12).reserved_crew[0]==0);
        yards.at(901).progress=8;
        p=plan(directives,contracts,npcs,62,ctx);
        CHECK(row(p,12).gate==Gate::Ready);
        CHECK(row(p,12).reserved_crew[0]==801);

        // Origin/custody, role, residence and mentorship are qualifications.
        npcs.at(801).mentor_id=55;
        p=plan(directives,contracts,npcs,63,ctx);
        CHECK(row(p,12).gate==Gate::WorkerUnavailable);
        npcs.at(801).mentor_id=0;
        npcs.at(801).residence_kind=transport::LocationKind::PlanetarySite;
        p=plan(directives,contracts,npcs,64,ctx);
        CHECK(row(p,12).gate==Gate::WorkerUnavailable);
        npcs.at(801).residence_kind=transport::LocationKind::ColonyShip;
        npcs.at(801).home_ship=700;
        p=plan(directives,contracts,npcs,65,ctx);
        CHECK(row(p,12).gate==Gate::WorkerUnavailable);
        npcs.at(801).home_ship=800;

        // A completed original commissioning receipt requires no new
        // crew reservation, allowing restored/replayed completion.
        FreighterBuildOrder commissioned{};
        commissioned.order_id=20102;commissioned.yard_order=900;
        commissioned.source_ship=700;commissioned.owner=7;
        commissioned.launched_ship=99999;commissioned.commissioned_tick=62;
        commissioned.commissioning_crew={801,802};
        commissioned.provisions_kg_each=20;
        builds.emplace(20102,commissioned);
        directives.at(12).provisions_kg_each=20;
        p=plan(directives,contracts,npcs,66,ctx);
        CHECK(row(p,12).gate==Gate::Ready);
        CHECK(row(p,12).reserved_crew[0]==0);

        // Pure planning never mutates any original work order or NPC.
        const auto unchanged_crew=npcs.at(801).home_ship;
        const auto unchanged_yard=yards.at(900).progress;
        const auto unchanged_builds=builds.size();
        const auto first=plan(directives,contracts,npcs,67,ctx);
        const auto again=plan(directives,contracts,npcs,67,ctx);
        CHECK(first.size()==again.size());
        for(std::size_t i=0;i<first.size();++i){
            CHECK(first[i].id==again[i].id);
            CHECK(first[i].gate==again[i].gate);
            CHECK(first[i].reserved_yard==again[i].reserved_yard);
            CHECK(first[i].reserved_crew==again[i].reserved_crew);
        }
        CHECK(npcs.at(801).home_ship==unchanged_crew);
        CHECK(yards.at(900).progress==unchanged_yard);
        CHECK(builds.size()==unchanged_builds);

        // Integration proof: the native genesis duty check is available
        // without new NPC identities or a separate workforce database.
        Simulation world(981);
        const auto colony=world.add_colony_ship(19,{0,0},100,4);
        world.enable_individual_workforce();
        CHECK(world.npc_records().size()==4);
        const auto native_id=world.npc_records().begin()->first;
        CHECK(world.npc_available_for_duty(native_id));
        NativeCapacityContext native_ctx{};
        native_ctx.native_duty_available=[&](std::uint64_t id){
            return world.npc_available_for_duty(id);
        };
        const CapacityIndex empty{};
        CHECK(qualify_ship_worker(native_id,colony,
              world.npc_records(),native_ctx,empty,true)==
              Qualification::Qualified);

        std::cout<<"PASS WORK-01B.2: canonical one-slip occupancy, two real "
            "yard builders, duty/medical hook, native job double-book denial, "
            "supplier ownership, cancellation release, replay and no new inventory\n";
    }catch(const std::exception& e){
        std::cerr<<"FAIL WORK-01B.2: "<<e.what()<<'\n';
        return 1;
    }
}
