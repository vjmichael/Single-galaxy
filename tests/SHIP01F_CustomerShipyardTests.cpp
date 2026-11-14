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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("SHIP-01F line ")+std::to_string(__LINE__)+" "+#x);}while(false)

static void fabricate(PersistentWorld& world,PlayerId player,EntityId ship,
                      EntityId rock,std::uint64_t& nonce,int batches) {
    auto run=[&](Action action,int quantity,EntityId target=0) {
        Command cmd{};
        cmd.id=++nonce;cmd.sequence=cmd.id;
        cmd.execute_tick=world.world().tick()+1;
        cmd.issuer=player;cmd.actor=ship;
        cmd.action=action;cmd.amount=quantity;cmd.target=target;
        CHECK(world.submit(cmd));
        world.step();
        CHECK(world.world().events().back().result==Outcome::Applied);
    };
    for(int i=0;i<batches;++i) {
        run(Action::Mine,4,rock);
        run(Action::RefineOre,4);
        run(Action::FabricateModule,2);
    }
}

int main() {
    const auto db=fs::temp_directory_path()/
        ("single-galaxy-ship01f-"+std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    std::string committed;
    EntityId supplier=0,buyer=0,manufactured=0;
    std::array<std::uint64_t,2> staff{},buyer_crew{};
    std::int64_t conserved=0;
    try{
        {
            Simulation genesis(881);
            supplier=genesis.add_colony_ship(7,{0,0},100,6);
            buyer=genesis.add_colony_ship(8,{0,1000},100,6);
            const auto rock=genesis.add_asteroid({2000,0},100);
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
            staff={seller_npcs[0],seller_npcs[1]};
            buyer_crew={buyer_npcs[2],buyer_npcs[3]};
            conserved=genesis.total_ore();
            const auto population=genesis.npc_records().size();
            PersistentWorld world(db.string(),std::move(genesis));
            std::uint64_t serial=13000;

            // A real supplier must first construct the four-module yard
            // using its OWN two existing NPC workers, not buyer identities.
            fabricate(world,7,supplier,rock,serial,2);
            CHECK(world.start_shipyard_construction(7,supplier,91001,staff));
            world.steps(8);
            CHECK(world.world().shipyard_constructions().at(91001).operational());
            // The supplier also has six ordinary, independently fabricated
            // modules. A customer payment must not consume the supplier's
            // own stock or allow that private stock to bypass the accepted
            // customer assembly reservation.
            fabricate(world,7,supplier,rock,serial,3);
            CHECK(world.world().colony_ship(supplier)->modules==6);

            // Buyer manufactures six modules for this purchase plus one
            // future scanner; no free payment or fabricated crew.
            fabricate(world,8,buyer,rock,serial,4);
            CHECK(world.world().colony_ship(buyer)->modules==8);
            CHECK(world.world().colony_ship(supplier)->modules==6);
            CHECK(!world.post_shipyard_customer_order(7,buyer,91001,92001,93001));
            CHECK(world.post_shipyard_customer_order(8,buyer,91001,92001,93001));
            CHECK(!world.post_shipyard_customer_order(8,buyer,91001,92001,93001));
            CHECK(!world.post_shipyard_customer_order(8,buyer,91001,92001,93002));
            CHECK(world.world().shipyard_customer_orders().at(93001).stage==
                  ShipyardCustomerStage::Requested);
            CHECK(!world.accept_shipyard_customer_order(8,93001));
            CHECK(!world.fund_shipyard_customer_order(8,93001));
            CHECK(world.accept_shipyard_customer_order(7,93001));
            CHECK(!world.accept_shipyard_customer_order(7,93001));
            // The supplier has six spare units but may NOT bypass the
            // customer's accepted order with a competing private hull.
            CHECK(!world.queue_shipyard_freighter(7,91001,92002));
            CHECK(world.world().total_ore()==conserved);

            // Only the authorized customer can pay; the provider's
            // construction consumes six physically transferred modules.
            CHECK(!world.fund_shipyard_customer_order(7,93001));
            const auto before_fund=world.world().canonical_state();
            const auto generation=world.generation();
            bool failed=false;
            try{
                (void)world.fund_shipyard_customer_order(
                    8,93001,FailPoint::AfterSnapshotWrite);
            }catch(const std::exception&){failed=true;}
            CHECK(failed);
            CHECK(world.generation()==generation);
            CHECK(world.world().canonical_state()==before_fund);
            CHECK(world.world().colony_ship(buyer)->modules==8);
            CHECK(world.world().colony_ship(supplier)->modules==6);
            CHECK(world.fund_shipyard_customer_order(8,93001));
            CHECK(!world.fund_shipyard_customer_order(8,93001));
            CHECK(world.world().colony_ship(buyer)->modules==2);
            CHECK(world.world().colony_ship(supplier)->modules==6);
            CHECK(world.world().total_ore()==conserved);
            CHECK(world.world().freighter_build_orders().at(92001).owner==7);
            CHECK(world.world().freighter_build_orders().at(92001).source_ship==supplier);
            CHECK(world.world().shipyard_customer_orders().at(93001).stage==
                  ShipyardCustomerStage::Funded);
            CHECK(world.world().canonical_state().rfind("galaxy-kernel-v93|",0)==0);
            const auto in_progress=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(in_progress).canonical_state()==in_progress);

            // Twelve native simulation ticks: actual material becomes ONE
            // regular physical uncrewed Ship. Supplier still owns it until
            // the buyer physically claims it.
            world.steps(12);
            manufactured=world.world().freighter_build_orders().at(92001).launched_ship;
            CHECK(manufactured && manufactured!=buyer && manufactured!=supplier);
            CHECK(world.world().find(manufactured)->owner_id==7);
            CHECK(world.world().find(manufactured)->manufactured_order_id==92001);
            CHECK(!world.world().colony_ship(manufactured));
            CHECK(!world.world().life_support(manufactured));
            CHECK(!world.commission_shipyard_freighter(8,92001,buyer_crew,20));
            CHECK(!world.deliver_shipyard_customer_order(7,93001));
            const auto before_claim=world.world().canonical_state();
            const auto claim_generation=world.generation();
            failed=false;
            try{
                (void)world.deliver_shipyard_customer_order(
                    8,93001,FailPoint::AfterSnapshotWrite);
            }catch(const std::exception&){failed=true;}
            CHECK(failed);
            CHECK(world.generation()==claim_generation);
            CHECK(world.world().canonical_state()==before_claim);
            CHECK(world.world().find(manufactured)->owner_id==7);
            CHECK(world.deliver_shipyard_customer_order(8,93001));
            CHECK(!world.deliver_shipyard_customer_order(8,93001));
            CHECK(world.world().find(manufactured)->owner_id==8);
            CHECK(world.world().shipyard_customer_orders().at(93001).delivered_ship==
                  manufactured);
            CHECK(!world.world().colony_ship(manufactured));
            CHECK(!world.commission_shipyard_freighter(7,92001,staff,20));

            // Buyer, not the seller, supplies the remaining scanner module,
            // names its own crew, and transfers finite food/water/oxygen.
            CHECK(world.commission_shipyard_freighter(8,92001,buyer_crew,20));
            CHECK(!world.commission_shipyard_freighter(8,92001,buyer_crew,20));
            CHECK(world.world().colony_ship(buyer)->modules==1);
            CHECK(world.world().colony_ship(manufactured)->crew==2);
            CHECK(world.world().life_support(manufactured)->food_kg==20);
            CHECK(world.world().life_support(manufactured)->water_kg==20);
            CHECK(world.world().life_support(manufactured)->oxygen_kg==20);
            CHECK(world.world().find(manufactured)->fitting.exploration_scanner());
            CHECK(world.world().npc_records().size()==population);
            for(const auto id:buyer_crew){
                const auto& n=world.world().npc_records().at(id);
                CHECK(n.home_ship==manufactured && n.location==manufactured);
            }
            for(const auto id:staff)
                CHECK(world.world().npc_records().at(id).home_ship==supplier);
            FleetSelection fleet(8,buyer);
            CHECK(fleet.is_ready(world.world(),manufactured));
            CHECK(fleet.select(world.world(),manufactured));
            CHECK(fleet.piloted()==manufactured && fleet.home()==buyer);
            CHECK(world.world().freighter_build_orders().at(92001).source_ship==supplier);
            CHECK(world.world().total_ore()==conserved);
            committed=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(committed).canonical_state()==committed);
            // Tampering with a purported delivery identity is refused by
            // cross-record v93 custody validation.
            auto forged=committed;
            const auto start=forged.find("yardcustomer|93001|");
            CHECK(start!=std::string::npos);
            const auto end=forged.find('\n',start);
            CHECK(end!=std::string::npos);
            const auto row=forged.substr(start,end-start);
            const auto separator=row.rfind('|');
            CHECK(separator!=std::string::npos);
            forged.replace(start+separator+1,end-(start+separator+1),"999999");
            bool rejected=false;
            try{(void)Simulation::from_snapshot(forged);}
            catch(const std::exception&){rejected=true;}
            CHECK(rejected);
        }
        {
            PersistentWorld restored(db.string(),Simulation(881));
            CHECK(restored.world().canonical_state()==committed);
            CHECK(restored.world().shipyard_customer_orders().at(93001).delivered_ship==
                  manufactured);
            CHECK(restored.world().find(manufactured)->owner_id==8);
            CHECK(restored.world().freighter_build_orders().at(92001).source_ship==supplier);
            CHECK(restored.world().total_ore()==conserved);
            FleetSelection fleet(8,buyer);
            CHECK(fleet.is_ready(restored.world(),manufactured));
        }
        fs::remove(db);
        std::cout<<"PASS SHIP-01F: two real owners, six physical modules, 12-tick seller build, uncrewed delivery, buyer NPC commissioning, SQLite recovery and v93 custody rejection\n";
    }catch(const std::exception& e){
        std::error_code ec;fs::remove(db,ec);
        std::cerr<<"FAIL SHIP-01F "<<e.what()<<'\n';
        return 1;
    }
}
