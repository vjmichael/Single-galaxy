#include "galaxy/Persistence.hpp"
#include "galaxy/FleetSelection.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("WORK-01A line ")+std::to_string(__LINE__)+" "+#x);}while(false)

static void fabricate(PersistentWorld& world,PlayerId player,EntityId ship,
                      EntityId rock,std::uint64_t& nonce,int batches){
    auto run=[&](Action action,int quantity,EntityId target=0){
        Command c{};
        c.id=++nonce;c.sequence=c.id;c.execute_tick=world.world().tick()+1;
        c.issuer=player;c.actor=ship;c.action=action;
        c.amount=quantity;c.target=target;
        CHECK(world.submit(c));
        world.step();
        CHECK(world.world().events().back().result==Outcome::Applied);
    };
    for(int i=0;i<batches;++i){
        run(Action::Mine,4,rock);
        run(Action::RefineOre,4);
        run(Action::FabricateModule,2);
    }
}
static const WorkBoardItem& require_board(
        const std::vector<WorkBoardItem>& rows,WorkTaskKind type,
        std::uint64_t id) {
    for(const auto& e:rows)
        if(e.kind==type && e.source_order==id)return e;
    throw std::runtime_error("missing typed native work adapter");
}
static bool invalid_snapshot(const std::string& s){
    try{(void)Simulation::from_snapshot(s);return false;}
    catch(const std::exception&){return true;}
}
int main(){
    const auto db=fs::temp_directory_path()/
        ("single-galaxy-work01a-"+std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    std::string verified;
    EntityId supplier=0,buyer=0,hull=0;
    std::int64_t conserved=0;
    std::array<std::uint64_t,2> builders{},buyer_crew{};
    try{
        {
            Simulation genesis(881);
            supplier=genesis.add_colony_ship(7,{0,0},100,6);
            buyer=genesis.add_colony_ship(8,{0,1000},100,6);
            const auto rock=genesis.add_asteroid({2000,0},120);
            genesis.provision_colony_genesis(supplier,2000,2000,2000,2000,2000);
            genesis.provision_colony_genesis(buyer,2000,2000,2000,2000,2000);
            genesis.enable_individual_workforce();
            genesis.provision_starter_exploration_scanner(supplier);
            genesis.provision_starter_exploration_scanner(buyer);
            genesis.ensure_physical_celestial_systems();
            genesis.ensure_ship_frame_registry();
            std::vector<std::uint64_t> seller_npcs,buyer_npcs;
            for(const auto& [id,n]:genesis.npc_records()){
                if(n.home_ship==supplier)seller_npcs.push_back(id);
                if(n.home_ship==buyer)buyer_npcs.push_back(id);
            }
            CHECK(seller_npcs.size()==6 && buyer_npcs.size()==6);
            builders={seller_npcs[0],seller_npcs[1]};
            buyer_crew={buyer_npcs[2],buyer_npcs[3]};
            const auto population=genesis.npc_records().size();
            conserved=genesis.total_ore();
            PersistentWorld world(db.string(),std::move(genesis));
            std::uint64_t nonce=81000;
            fabricate(world,7,supplier,rock,nonce,2);
            CHECK(world.start_shipyard_construction(7,supplier,91001,builders));
            world.steps(8);
            CHECK(world.world().shipyard_constructions().at(91001).operational());
            fabricate(world,8,buyer,rock,nonce,4);
            CHECK(world.world().colony_ship(buyer)->modules==8);
            CHECK(world.post_shipyard_customer_order(8,buyer,91001,92001,93001));

            // No delegation = no NPC authority to spend buyer materials,
            // even if the seller accepts a valid physical contract.
            CHECK(world.accept_shipyard_customer_order(7,93001));
            const auto before=world.world().canonical_state();
            const auto generation=world.generation();
            bool fault=false;
            try{
                (void)world.delegate_customer_ship_order(
                    8,buyer,94001,93001,buyer_crew,20,0,
                    FailPoint::AfterSnapshotWrite);
            }catch(const std::exception&){fault=true;}
            CHECK(fault);
            CHECK(world.generation()==generation);
            CHECK(world.world().canonical_state()==before);
            CHECK(world.world().work_directives().empty());
            world.step();
            CHECK(world.world().shipyard_customer_orders().at(93001).stage==
                  ShipyardCustomerStage::Accepted);
            CHECK(world.world().colony_ship(buyer)->modules==8);

            // Different player cannot delegate buyer NPCs or money.
            CHECK(!world.delegate_customer_ship_order(
                7,buyer,94001,93001,buyer_crew,20));
            CHECK(!world.delegate_customer_ship_order(
                8,buyer,94001,93001,buyer_crew,20,999));
            CHECK(!world.delegate_customer_ship_order(
                8,buyer,94001,93001,buyer_crew,20,94001));
            CHECK(!world.delegate_customer_ship_order(
                8,buyer,94001,93001,{buyer_crew[0],0},20));
            CHECK(world.delegate_customer_ship_order(
                8,buyer,94001,93001,buyer_crew,20));
            CHECK(!world.delegate_customer_ship_order(
                8,buyer,94001,93001,buyer_crew,20));
            CHECK(!world.cancel_work_directive(7,94001));
            CHECK(world.world().canonical_state().rfind(
                "galaxy-kernel-v94|",0)==0);
            const auto saved=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
            auto bad=saved;
            const auto row_start=bad.find("workdirective|94001|");
            CHECK(row_start!=std::string::npos);
            const auto fake="workdirective|94001|8|"+std::to_string(buyer)+
                "|5|999999|0|"+std::to_string(buyer_crew[0])+"|"+
                std::to_string(buyer_crew[1])+"|20|0|0|"+
                std::to_string(world.world().tick())+"|"+
                std::to_string(world.world().tick())+"|0";
            bad.replace(row_start,bad.find('\n',row_start)-row_start,fake);
            CHECK(invalid_snapshot(bad));

            auto rows=world.world().universal_work_board(8,buyer);
            const auto purchase=require_board(
                rows,WorkTaskKind::CustomerShipOrder,93001);
            CHECK(purchase.delegated_id==94001);
            CHECK(purchase.phase==WorkPhase::Executing);
            CHECK(require_board(world.world().universal_work_board(7,supplier),
                WorkTaskKind::ShipyardConstruction,91001).phase==
                WorkPhase::Completed);
            // Different owner cannot see buyer's private work directives.
            CHECK(world.world().universal_work_board(7,buyer).empty());

            // First committed tick after explicit delegation transfers the
            // buyer's *own* six modules via the existing same-berth payment
            // gate. No supplier modules or external courier are fabricated.
            world.step();
            CHECK(world.world().shipyard_customer_orders().at(93001).stage==
                  ShipyardCustomerStage::Funded);
            CHECK(world.world().colony_ship(buyer)->modules==2);
            CHECK(world.world().colony_ship(supplier)->modules==0);
            CHECK(world.world().freighter_build_orders().at(92001).owner==7);
            CHECK(world.world().work_directives().at(94001).blocked==
                  WorkBlock::NativeConstruction);
            CHECK(world.world().total_ore()==conserved);
            rows=world.world().universal_work_board(7,supplier);
            CHECK(require_board(rows,WorkTaskKind::FreighterBuild,92001).phase==
                  WorkPhase::Executing);
            world.steps(12);
            hull=world.world().freighter_build_orders().at(92001).launched_ship;
            CHECK(hull);
            CHECK(world.world().find(hull)->owner_id==8);
            CHECK(!world.world().colony_ship(hull));
            CHECK(world.world().work_directives().at(94001).status==
                  WorkDirectiveStatus::Active);
            CHECK(world.world().work_directives().at(94001).blocked==
                  WorkBlock::NamedCrewAndSupplies);
            CHECK(world.world().shipyard_customer_orders().at(93001).stage==
                  ShipyardCustomerStage::Delivered);
            // Second tick explicitly commissions using two existing buyer
            // NPCs and real scanner and life-support stock.
            world.step();
            CHECK(world.world().work_directives().at(94001).status==
                  WorkDirectiveStatus::Fulfilled);
            CHECK(!world.cancel_work_directive(8,94001));
            CHECK(world.world().colony_ship(hull)->crew==2);
            CHECK(world.world().colony_ship(buyer)->modules==1);
            CHECK(world.world().life_support(hull)->food_kg==20);
            CHECK(world.world().life_support(hull)->water_kg==20);
            CHECK(world.world().life_support(hull)->oxygen_kg==20);
            CHECK(world.world().npc_records().size()==population);
            for(auto id:buyer_crew)
                CHECK(world.world().npc_records().at(id).home_ship==hull);
            for(auto id:builders)
                CHECK(world.world().npc_records().at(id).home_ship==supplier);
            CHECK(world.world().freighter_build_orders().at(92001).source_ship==
                  supplier);
            FleetSelection fleet(8,buyer);
            CHECK(fleet.is_ready(world.world(),hull));
            CHECK(world.world().total_ore()==conserved);
            CHECK(require_board(world.world().universal_work_board(8,buyer),
                WorkTaskKind::CustomerShipOrder,93001).phase==
                WorkPhase::Completed);

            // Cancel a separate pending delegation: cancellation cannot
            // reverse paid history or fund a second requested hull.
            CHECK(world.post_shipyard_customer_order(
                8,buyer,91001,92002,93002));
            CHECK(world.delegate_customer_ship_order(
                8,buyer,94002,93002,{0,0},0,94001));
            CHECK(world.cancel_work_directive(8,94002));
            CHECK(!world.cancel_work_directive(8,94002));
            world.step();
            CHECK(world.world().shipyard_customer_orders().at(93002).stage==
                  ShipyardCustomerStage::Requested);
            CHECK(!world.world().freighter_build_orders().count(92002));
            CHECK(world.world().work_directives().at(94002).status==
                  WorkDirectiveStatus::Cancelled);
            verified=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(verified).canonical_state()==verified);
        }
        {
            PersistentWorld restored(db.string(),Simulation(881));
            CHECK(restored.world().canonical_state()==verified);
            CHECK(restored.world().total_ore()==conserved);
            CHECK(restored.world().find(hull)->owner_id==8);
            CHECK(restored.world().work_directives().at(94001).status==
                  WorkDirectiveStatus::Fulfilled);
            CHECK(restored.world().work_directives().at(94002).status==
                  WorkDirectiveStatus::Cancelled);
            const auto before=restored.world().canonical_state();
            restored.steps(3);
            CHECK(restored.world().freighter_build_orders().size()==1);
            CHECK(restored.world().colony_ship(hull)->crew==2);
            CHECK(restored.world().total_ore()==conserved);
            CHECK(restored.world().canonical_state()!=before);
        }
        fs::remove(db);
        std::cout<<"PASS WORK-01A: 5 typed native work adapters, consent-bound NPC shipyard automation, two distinct owners, six-module physical funding, one delivered hull, exact named crew commissioning, cancellation, v94 crash rollback and SQLite restart\n";
    }catch(const std::exception& e){
        std::error_code ec;fs::remove(db,ec);
        std::cerr<<"FAIL WORK-01A "<<e.what()<<'\n';return 1;
    }
}
